/*
 * This file contains the procedures for the handling of select
 *
 * Created for Linux based loosely upon Mathius Lattner's minix
 * patches by Peter MacDonald. Heavily edited by Linus.
 */
/*
 * 本文件含有处理select()系统调用的过程。
 *
 * 这是Peter MacDonald基于Mathius Lattner提供给MINIX系统的补丁程序修改而成。
 */

#include <linux/fs.h>		/* 文件系统头文件。定义文件表结构（file,buffer_head,m_inode等） */
#include <linux/kernel.h>	/* 内核头文件。含有一些内核常用函数的原型定义 */
#include <linux/tty.h>		/* tty头文件，定义了有关tty_io，串行通信方面的参数、常数 */
#include <linux/sched.h>	/* 调度程序头文件。定义了任务结构task_struct、任务0的数据等 */

#include <asm/segment.h>	/* 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数 */
#include <asm/system.h>		/* 系统头文件。定义了设置或修改描述符/中断门等的嵌入式汇编宏 */

#include <sys/stat.h>		/* 文件状态头文件。含有文件或文件系统状态结构stat{}和常量 */
#include <sys/types.h>		/* 类型头文件。定义了基本的系统数据类型 */
#include <string.h>			/* 字符串头文件。主要定义了一些有关字符串操作的嵌入函数。 */
#include <const.h>			/* 常数符号头文件。目前仅定义i节点中i_mode字段的各标志位 */
#include <errno.h>			/* 错误号头文件。包含系统中各种出错号。 */
#include <sys/time.h>
#include <signal.h>			/* 信号头文件。定义信号符号常量，信号结构以及信号操作函数原型 */

/*
 * Ok, Peter made a complicated, but straightforward multiple_wait() function.
 * I have rewritten this, taking some shortcuts: This code may not be easy to
 * follow, but it should be free of race-conditions, and it's practical. If you
 * understand what I'm doing here, then you understand how the linux sleep/wakeup
 * mechanism works.
 *
 * Two very simple procedures, add_wait() and free_wait() make all the work. We
 * have to have interrupts disabled throughout the select, but that's not really
 * such a loss: sleeping automatically frees interrupts when we aren't in this
 * task.
 */
/*
 * OK，Peter编制了复杂但很直观的多个_wait()函数。我对这些函数进行了改写，以使之更简洁：这些代码
 * 可能不容易看懂，但是其中应该不会存在竞争条件问题，并且很实际。如果你能理解这里编制的代码，那么
 * 就说明你已经理解Linux中睡眠/唤醒的工作机制。
 *
 * 两个很简单的过程，add_wait()和free_wait()执行了主要操作。在整个select处理过程中我们不得不禁
 * 止中断。但是这样做并不会带来太多的损失：因为当我们不在执行本任务时睡眠状态会自动释放中断(即其他
 * 任务会使用自己EFLAGS中的中断标志)。
 */

typedef struct {
	struct task_struct * old_task;
	struct task_struct ** wait_address;
} wait_entry;

typedef struct {
	int nr;
	wait_entry entry[NR_OPEN * 3];	/* 等待项类数组 */
} select_table;

/**
 * 等待队列添加入等待表
 * 把未准备好描述符的等待队列指针加入等待表wait_table中
 * @param[in]	*wait_address	与描述符相关的等待队列头指针。如tty读缓冲队列secondary的
 *								等待队列头指针是proc_list
 * @param[in]	p				do_select()中定义的等待表结构指针
 * @retval		void
 */
static void add_wait(struct task_struct ** wait_address, select_table * p)
{
	int i;

	/*
	 * 首先检查描述符是否有对应的等待队列，若无则返回。然后在等待表中搜索参数指定的等待队列指针是否已经在等待表中设置
	 * 过，若设置过也立刻返回。这个检查主要是针对管道文件描述符。例如若一个管道在等待可以进行读操作，那么其他必定可以立刻
	 * 进行写操作
	 */
	if (!wait_address) {
		return;
	}
	for (i = 0; i < p->nr; i++) {
		if (p->entry[i].wait_address == wait_address) {
			return;
		}
	}
	/*
	 * 然后我们把描述符对应的等待队列的头指针保存在等待表wait_table中，同时让等待表项的old_task字段指向等待队列头指针
	 * 所指向的任务（若无则为NULL），再让等待队列头指针指向当前任务。最后把等待表有效项计数值nr增1（之前被初始化为0）
	 */
	p->entry[p->nr].wait_address = wait_address;
	p->entry[p->nr].old_task = * wait_address;
	*wait_address = current;
	p->nr ++;
}


/**
 * 唤醒所有等待项上的进程
 * 清空等待表
 * 本函数在do_select()函数中睡眠后被唤醒返回时被调用，用于唤醒等待表中处于各个等待队列上的其他
 * 任务，它与kernel/sched.c中sleep_on()函数的后半部分代码几乎完全相同，请参考对sleep_on()函数的
 * 说明。
 * @param[in]	p		等待表结构指针
 * @return		void
 */
static void free_wait(select_table * p)
{
	int i;
	struct task_struct ** tpp;

	/*
	 * 如果等待表中各项（共nr个有效项）记录的等待队列头指针表明还有其他后来添加进的等待任务（例如其他进程调用sleep_on()函数二睡眠在等待
	 * 队列上），则此时等待队列头指针指向的不是当前进程，那么我们就需要先唤醒这些任务。操作方法是将等待队列头所指任务先置为就绪状态（state=0）
	 * 并把自己设置为不可中断等待状态，即自己要等待这些后续进队列的任务被唤醒而执行时来唤醒本任务。所以要重新执行调度程序
	 */
	for (i = 0; i < p->nr ; i++) {
		tpp = p->entry[i].wait_address;
		while (*tpp && *tpp != current) {
			(*tpp)->state = 0;
			current->state = TASK_UNINTERRUPTIBLE;
			schedule();
		}
		/*
		 * 执行到这里，说明等待表当前处理项中的等待队列头指针字段wait_address指向当前任务。若它为空，则表明调度有问题，于是
		 * 显示警告信息。然后我们让等待队列头指针指向在我们前面进入队列的任务。若此时该头指针缺失指向一个任务而不是NULL，则说明
		 * 队列中还有任务（*tpp不为空），于是将该任务设置成就绪状态，唤醒之。最后把等待表的有效表项计数字段nr置零，清空该等待表
		 */
		if (!*tpp) {
			printk("free_wait: NULL");
		}
		if ((*tpp = p->entry[i].old_task)) {
			(**tpp).state = 0;
		}
	}
	p->nr = 0;
}


/**
 * 根据i节点取得tty结构
 * 根据文件i节点判断文件是不是字符终端设备文件
 * @param[in]	inode	i节点
 * @retval		若是则返回其tty结构指针，否则返回NULL
 */
static struct tty_struct * get_tty(struct m_inode * inode)
{
	int major, minor;

	/*
	 * 如果不是字符设备文件则返回NULL。如果主设备号不是5（控制终端号）或4，则也返回NULL。如果主设备号是5，那么进程
	 * 的tty字段值即是其终端设备号，否则它等待字符设备号的次设备号。如果终端设备号小于0，表示进程没有控制终端或没有
	 * 使用终端，于是返回NULL。否则返回对应的tty结构指针
	 */
	if (!S_ISCHR(inode->i_mode)) {
		return NULL;
	}
	if ((major = MAJOR(inode->i_zone[0])) != 5 && major != 4) {
		return NULL;
	}
	if (major == 5) {
		minor = current->tty;
	} else {
		minor = MINOR(inode->i_zone[0]);
	}
	if (minor < 0) {
		return NULL;
	}
	return TTY_TABLE(minor);
}

/*
 * The check_XX functions check out a file. We know it's either
 * a pipe, a character device or a fifo (fifo's not implemented)
 */
/*
 * check_XX函数用于检查一个文件。我们知道该文件要么是管道文件、要么是字符设备文件，或者要么是
 * 一个FIFO（FIFO）还未实现。
 */

/**
 * 检查读文件操作是否准备好，即终端读缓冲队列secondary是否有字符可读，或者管道文件是否不空。
 * @param[in]	wait	等待表指针
 * @param[in]	inode	文件i节点指针
 * @retval		若描述符可进行读操作则返回1，否则返回0
 */
static int check_in(select_table * wait, struct m_inode * inode)
{
	struct tty_struct * tty;

	/*
	 * 首先根据文件i节点调用get_tty()检测文件是否是一个tty终端（字符）设备文件。如果是则检查该终端读缓冲队列secondary中是否有字符
	 * 可供读取，若有则返回1.若此时secondary为空则把当前任务添加到secondary的等待队列proc_list上并返回0。如果是管道文件，则判断
	 * 目前管道中是否有字符可读，若有则返回1，若没有（管道空）则把当前任务添加到管道i节点的等待队列上并返回0。注意，PIPE_EMPTY()宏
	 * 使用管道当前头尾指针位置来判断管道是否空。管道i节点的i_zone[0]和i_zone[1]字段分别存放着当前的头尾指针
	 */
	if ((tty = get_tty(inode))) {
		if (!EMPTY(tty->secondary)) {
			return 1;
		} else {
			add_wait(&tty->secondary->proc_list, wait);
		}
	} else if (inode->i_pipe) {
		if (!PIPE_EMPTY(*inode)) {
			return 1;
		} else {
			add_wait(&inode->i_wait, wait);
		}
	}
	return 0;
}

/**
 * 检查文件写操作是否准备好，即终端写缓冲队列write_q中是否还有空闲位置可写，或者此时管道文件
 * 是否不满。
 * @param[in]	wait	等待表指针
 * @param[in]	inode	文件i节点指针
 * @retval		若描述符可进行写操作则返回1，否则返回0
 */
static int check_out(select_table * wait, struct m_inode * inode)
{
	struct tty_struct * tty;

	/*
	 * 首先根据文件i节点调用get_tty()检测文件是否是一个tty终端（字符）设备文件。如果是则检查该终端写缓冲队列write_q中是否有空间可写入，
	 * 若有则返回1，若没有空空间则把当前任务添加到write_q的等待队列proc_list上并返回0。如果是管道文件则判断目管道中是否有空闲空间可写入
	 * 字符，若有则返回1，若没有（管道满）则把当前任务添加到管道i节点的等待队列上并返回0
	 */
	if ((tty = get_tty(inode))) {
		if (!FULL(tty->write_q)) {
			return 1;
		} else {
			add_wait(&tty->write_q->proc_list, wait);
		}
	} else if (inode->i_pipe) {
		if (!PIPE_FULL(*inode)) {
			return 1;
		} else {
			add_wait(&inode->i_wait, wait);
		}
	}
	return 0;
}


/**
 * 检查文件是否处于异常状态。对于终端设备文件，目前内核总是返回0。对于管道文件，如果此时两个管
 * 道描述符中有一个或都已被关闭，则返回1，否则就把当前任务添加到管道i节点的等待队列上并返回0。
 * 返回0。
 * @param[in]	wait	等待表指针
 * @param[in]	inode	文件i节点指针
 * 若出现异常条件则返回1，否则返回0
 */
static int check_ex(select_table * wait, struct m_inode * inode)
{
	struct tty_struct * tty;

	if ((tty = get_tty(inode))) {
		if (!FULL(tty->write_q)) {
			return 0;
		} else {
			return 0;
		}
	} else if (inode->i_pipe) {
		if (inode->i_count < 2) {
			return 1;
		} else {
			add_wait(&inode->i_wait, wait);
		}
	}
	return 0;
}


/**
 * do_select()是内核执行select()系统调用的实际处理函数。该函数首先检查描述符集中各个描述符的有
 * 效性，然后分别调用相关描述符集描述符检查函数check_XX()对每个描述符进行检查，同时统计描述符
 * 集中当前已经准备好的描述符个数。若有任何一个描述符已经准备好，本函数就会立刻返回，否则进程
 * 就会在本函数中进入睡眠状态，并在过了超时时间或者由于某个描述符所在等待队列上的进程被唤醒而
 * 使本进程继续运行。
 */
int do_select(fd_set in, fd_set out, fd_set ex,
	fd_set *inp, fd_set *outp, fd_set *exp)
{
	int count;		/* 已准备好的描述符个数计数值 */
	select_table wait_table;	/* 等待表结构 */
	int i;
	fd_set mask;

	/*
	 * 首先把3个描述符进行或操作，在mask中得到描述符比特位屏蔽码。然后循环判断当前进程各个描述符是否有效并且包含在描述符集内。
	 * 在循环中，每判断完一个描述符就会把mask右移1位，因此根据mask的最低有效比特位我们就可以判断相应描述符是否在用户给的描述符
	 * 集中。有效的描述符应该是一个管道文件描述符，或者是一个字符设备文件描述符，或者是一个FIFO描述符，其余类型的都作为无效描述符
	 * 而返回EBADF错误
	 */
	mask = in | out | ex;
	for (i = 0 ; i < NR_OPEN ; i++,mask >>= 1) {
		if (!(mask & 1)) {		/* 若不在描述符集中则继续判断下一个 */
			continue;
		}
		if (!current->filp[i]) {	/* 若该文件未打开，则返回描述符错 */
			return -EBADF;
		}
		if (!current->filp[i]->f_inode) {	/* 若文件i节点指针空，则返回错误号 */
			return -EBADF;
		}
		if (current->filp[i]->f_inode->i_pipe) {	/* 若是管道文件描述符，则有效 */
			continue;
		}
		if (S_ISCHR(current->filp[i]->f_inode->i_mode)) {	/* 字符设备文件有效 */
			continue;
		}
		if (S_ISFIFO(current->filp[i]->f_inode->i_mode)) {	/* FIFO也有效 */
			continue;
		}
		return -EBADF;		/* 其余都作为无效描述符而返回 */
	}
	/*
	 * 下面开始循环检查3个描述符集中的各个描述符是否准备好（可以操作）。此时mask用作前正在处理描述符的屏蔽码。循环中的3个函数
	 * check_in()、check_out()和check_ex()分别用来判断描述符是否已经准备好读入、写出和出现异常。若一个描述符已经准备好，则
	 * 相关描述符集中设置对应比特位，并且把已准备好的描述符个数计算值count增1
	 */
repeat:
	wait_table.nr = 0;
	*inp = *outp = *exp = 0;
	count = 0;
	mask = 1;
	for (i = 0 ; i < NR_OPEN ; i++, mask += mask) {
		/*
		 * 如果此时判断的描述符在读操作描述符集中，并且该描述符已经准备好可以进行读操作，则把该描述符在描述符集in中对应比特位置为1，
		 * 同时把已准备好描述符个数计算值count增1
		 */
		if (mask & in)
			if (check_in(&wait_table,current->filp[i]->f_inode)) {
				*inp |= mask;	/* 描述符集中设置对应比特位 */
				count++;	/* 已准备好描述符个数计数 */
			}
		/*
		 * 如果此时判断的描述符是在写操作描述符集中，并且该描述符已经准备好可以进行写操作，则把该描述符在描述符集out中对应比特位置为1，
		 * 同时把已准备好描述符个数计算值count增1
		 */
		if (mask & out)
			if (check_out(&wait_table,current->filp[i]->f_inode)) {
				*outp |= mask;
				count++;
			}
		/*
		 * 如果此时判断的描述符是在异常描述符集中，并且该描述符已经有异常出现，则把该描述符在描述符集ex中对应比特位置为1，
		 * 同时把已准备好描述符个数计算值count增1
		 */
		if (mask & ex)
			if (check_ex(&wait_table,current->filp[i]->f_inode)) {
				*exp |= mask;
				count++;
			}
	}
	/*
	 * 在对进程所有描述符判断处理过后，若没有发现有准备好的描述符（count==0），并且此时进程没有收到任何非阻塞信号，并且此时有等待着的描述符
	 * 或者等待时间还没超时，那么我们就把当前进程状态设置成可中断睡眠状态，然后指向调度函数去执行其他任务。当内核又一次调度执行本任务时就会调用
	 * free_wait()唤醒相关等待队列上本任务前后的任务，然后跳转到repeat标号处再次重新检测是否有我们关心的（描述符集中的）描述符已准备好
	 */
	if (!(current->signal & ~current->blocked) &&
	    (wait_table.nr || current->timeout) && !count) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		free_wait(&wait_table);		/* 本任务被唤醒返回后从这里开始执行 */
		goto repeat;
	}
	/*
	 * 如果此时count不等于0，或者接收到了信号，或者等待时间到并且没有需要等待的描述符，那么我们就调用free_wait()唤醒等待队列上的任务，然后
	 * 返回已准备好的描述符个数值
	 */
	free_wait(&wait_table);
	return count;
}

/*
 * Note that we cannot return -ERESTARTSYS, as we change our input
 * parameters. Sad, but there you are. We could do some tweaking in
 * the library function ...
 */
/*
 * 注意我们不能返回-ERESTARTSYS，因为我们会在select运行过程中改变输入参数值(*timeout)。很不幸，
 * 但你也只能接受这个事实。不过我们可以在库函数中做些处理...
 */
/**
 * select 系统调用
 * 该函数中的代码主要负责进行select功能操作前后的参数复制和转换工作。select主要的工作由
 * do_select()函数来完成。sys_select()会首先根据参数传递来的缓冲区指针从用户数据空间把select()
 * 函数调用的参数分解复制到内核空间，然后设置需要等待的超时时间值timeout，接着调用do_select()执
 * 行select功能，返回后就把处理结果再复制回用户空间中。
 * @param[in]	buffer		指向用户数据区中select()函数的第1个参数处
 * @retval		如果返回值小于0，表示执行时出现错误；
 * 				如果返回值等于0，表示在规定等待时间内没有描述符准备好操作；
 *				如果返回值大于0,则表示已准备好的描述符数量。
 */
int sys_select( unsigned long *buffer )
{
/* Perform the select(nd, in, out, ex, tv) system call. */
/* 执行select(nd, in, out, ex, tv)系统调用 */
	/* 首先定义几个局部变量，用于把指针参数传递来的select()函数参数分解开来 */
	int i;
	fd_set res_in, in = 0, *inp;		/* 读操作描述符集 */
	fd_set res_out, out = 0, *outp;		/* 写操作描述符集 */
	fd_set res_ex, ex = 0, *exp;		/* 异常条件描述符集 */
	fd_set mask;						/* 处理的描述符数值范围（nd）屏蔽码 */
	struct timeval *tvp;				/* 等待时间结构指针 */
	unsigned long timeout;

	/*
	 * 然后从用户数据区把参数分别隔离和复制到局部指针变量中，并根据描述符集指针是否有效分别取得3个描述符集in（读）、
	 * out（写）和ex（异常）。其中mask也是一个描述符集变量，根据3个描述符集中最大描述符数值+1（即第1个参数nd的值），
	 * 它被设置成用户程序关心的所有描述符的屏蔽码。例如，若nd=4，则mask=0b00001111（共32比特）
	 */
	mask = ~((~0) << get_fs_long(buffer++));
	inp = (fd_set *) get_fs_long(buffer++);
	outp = (fd_set *) get_fs_long(buffer++);
	exp = (fd_set *) get_fs_long(buffer++);
	tvp = (struct timeval *) get_fs_long(buffer);

	if (inp) {		/* 若指针有效，则取读操作描述符集 */
		in = mask & get_fs_long(inp);
	}
	if (outp) {		/* 若指针有效，则取写操作描述符集 */
		out = mask & get_fs_long(outp);
	}
	if (exp) {		/* 若指针有效，则取异常描述符集 */
		ex = mask & get_fs_long(exp);
	}
	/*
	 * 接下来我们尝试从时间结构中取出等待（睡眠）时间值timeout。首先把timeout初始化成最大（无限）值，然后从用户数据空间取得该时间
	 * 结构中设置的时间值，经转换和加上系统当前滴答值jiffies，最后得到需要等待的时间滴答数值timeout。我们用此值来设置当前进程应该
	 * 等待的延时。另外，tv_usec字段是微秒值，把它除以1000000后得到对应秒数，再成祎系统每秒滴答数HZ，即把tv_usec转换成滴答值
	 */
	timeout = 0xffffffff;
	if (tvp) {
		timeout = get_fs_long((unsigned long *)&tvp->tv_usec) / (1000000 / HZ);
		timeout += get_fs_long((unsigned long *)&tvp->tv_sec) * HZ;
		timeout += jiffies;
	}
	current->timeout = timeout;			/* 设置当前进程应该延时的滴答值 */
	/*
	 * select()函数的主要工作在do_select()中完成。在调用函数之后的代码用于把处理结构复制到用户数据区中，返回给用户。为了避免出现
	 * 竞争条件，在调用do_select()前需要禁止中断，并在该函数返回后再开启中断。
	 * 如果在do_select()返回之后进程的等待延时字段timeout还大于当前系统计时滴答值jiffies，说明在超时之前已经有描述符准备好，于是
	 * 这里我们先记下到超市还剩余的时间值，随后我们会把这个值返回给用户。如果进程的等待延时字段timeout已经小于或等于当前系统jiffies，
	 * 表示do_select()可能是用于超时而返回，因此把剩余时间值设置为0
	 */
	cli();		/* 禁止响应中断 */
	i = do_select(in, out, ex, &res_in, &res_out, &res_ex);
	if (current->timeout > jiffies) {
		timeout = current->timeout - jiffies;
	} else {
		timeout = 0;
	}
	sti();		/* 开启中断响应 */
	/*
	 * 接下来我们把进程的超时字段清零。如果do_select()返回的已准备号描述符个数小于0，表示执行出错，于是返回这个错误号。然后我们把处理
	 * 过的描述符集内容和延迟时间结构内容写回到用户数据缓冲空间。在写时间结构内容时还需要先将滴答时间单位表示的剩余延迟时间转换成秒和微秒值
	 */
	current->timeout = 0;
	if (i < 0)
		return i;
	if (inp) {
		verify_area(inp, 4);
		put_fs_long(res_in, inp);		/* 可读描述符 集*/
	}
	if (outp) {
		verify_area(outp, 4);
		put_fs_long(res_out, outp);		/* 可写描述符集 */
	}
	if (exp) {
		verify_area(exp, 4);
		put_fs_long(res_ex, exp);		/* 出现异常条件描述符集 */
	}
	if (tvp) {
		verify_area(tvp, sizeof(*tvp));
		put_fs_long(timeout/HZ, (unsigned long *) &tvp->tv_sec);	/* 秒 */
		timeout %= HZ;
		timeout *= (1000000/HZ);
		put_fs_long(timeout, (unsigned long *) &tvp->tv_usec);	/* 微秒 */
	}
	/*
	 * 如果此时并没有已准备好的描述符，并且收到了某个非阻塞信号，则返回被中断错误号。否则返回已准备号的描述符个数值
	 */
	if (!i && (current->signal & ~current->blocked)) {
		return -EINTR;
	}
	return i;
}
