/*
 *  linux/kernel/chr_drv/tty_ioctl.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>											/* 错误号头文件。包含系统中各种出错号 */
#include <termios.h>										/* 终端输入输出函数头文件。主要定义控制异步通信口的终端接口 */

#include <linux/sched.h>									/* 调度程序头文件。定义了任务结构task_struct、任务0数据等 */
#include <linux/kernel.h>									/* 内核头文件。含有一些内核常用函数的原型定义 */
#include <linux/tty.h>										/* tty头文件，定义有关tty_io、串行通信方面参数、常数 */

#include <asm/io.h>											/* io头文件。定义硬件端口输入/输出宏汇编语句 */
#include <asm/segment.h>									/* 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数 */
#include <asm/system.h>										/* 系统头文件。定义设置或修改描述符/中断门等嵌入式汇编宏 */

/*
 * 以下代码行给出了两个外部函数函数原型和一个串行端口使用的波特率因子数组。第一个函数用于根据进程组号pgrp取得进程组所属的会话号，定义在kernel/exit.c。第二个
 * 函数用于向使用指定tty中断的进程组中所有进程发送信号，并定义在chr_drv/tty_io.c。波特率因子数组（或称为除数数组）给出了波特率与波特率因子的对应关系。例如，
 * 波特率是2400bps时，对应的因子是48（0x30）；9600bps的因子是12（0x1c）。
 */
extern int session_of_pgrp(int pgrp);
extern int tty_signal(int sig, struct tty_struct *tty);

static unsigned short quotient[] = {
	0, 2304, 1536, 1047, 857,
	768, 576, 384, 192, 96,
	64, 48, 24, 12, 6, 3
};

/*
 * 修改传输波特率
 * 参数：tty - 终端对应的tty数据结构
 * 在除数锁存标志DLAB置位情况下，通过串口1的端口0x3f8和0x3f9向UART分别写入波特率因子低字节和高字节。写完后再复制DLAB位。对于串口2，这两个端口分别是0x2f8和0x2f9
 */
static void change_speed(struct tty_struct * tty)
{
	unsigned short port,quot;

	/*
	 * 函数首先检查参数tty指定的中断是否是串行终端，若不是则退出。对于串口终端的tty结构，其读队列的data字段存放着串行端口的基地址（0x3f8或0x2f8），而一般控制台终端的tty结构
	 * 的read_q.data字段值为0.然后从终端termios结构的控制模式标志集中取得已设置的波特率索引号，并据此从波特率因子数组quotient[]中取得对应的波特率因子值quot。
	 * CBAUD是控制模式标志集中波特率位屏蔽码
	 */
	if (!(port = tty->read_q->data))
		return;
	quot = quotient[tty->termios.c_cflag & CBAUD];
	/*
	 * 接着把波特率因子quot写入串行端口对应UART芯片的波特率因子锁存器中。在写之前我们先要把线路控制寄存器LCR的除数锁存访问比特位DLAB（位7）置1。然后把16位的波特率
	 * 因子低高字节分别写入端口0x3f8、0x3f9（分别对应波特率因子低、高字节锁存器）。最后再复位LCR的DLAB标志位
	 */
	cli();
	outb_p(0x80,port+3);		/* set DLAB */	/* 首先设置除数锁定标志DLAB */
	outb_p(quot & 0xff,port);	/* LS of divisor */	/* 输出因子低字节 */
	outb_p(quot >> 8,port+1);	/* MS of divisor */	/* 输出因子高字节 */
	outb(0x03,port+3);		/* reset DLAB */	/* 复位DLAB */
	sti();
}

/*
 * 刷新tty缓冲队列。
 * 参数：queue - 指定的缓冲队列指针
 * 令缓冲队列的头指针等于尾指针，从而达到清空缓冲区的目的
 */
static void flush(struct tty_queue * queue)
{
	cli();
	queue->head = queue->tail;
	sti();
}

/* 等待字符发送出去 */
static void wait_until_sent(struct tty_struct * tty)
{
	/* do nothing - not implemented */	/* 什么都没做 - 还未实现 */
}

/* 发送BREAK控制符 */
static void send_break(struct tty_struct * tty)
{
	/* do nothing - not implemented */	/* 什么都没做 - 还未实现 */
}

/*
 * 取终端termios结构信息
 * 参数：tty - 指定终端的tty结构指针；termios - 存放termios结构的用户缓冲区
 */
static int get_termios(struct tty_struct * tty, struct termios * termios)
{
	int i;

	/* 首先验证用户缓冲区指针所指内存区容量是否足够，如不够则分配内存。然后复制指定终端的termios结构信息到用户缓冲区中。最后返回0 */
	verify_area(termios, sizeof (*termios));							/* kernel/fork.c */
	for (i=0 ; i< (sizeof (*termios)) ; i++)
		put_fs_byte( ((char *)&tty->termios)[i] , i+(char *)termios );
	return 0;
}

/*
 * 设置终端termios结构信息
 * 参数：tty - 指定终端的tty结构指针；termios - 用户数据区termios结构指针。
 */
static int set_termios(struct tty_struct * tty, struct termios * termios,
			int channel)
{
	int i, retsig;

	/* If we try to set the state of terminal and we're not in the
	   foreground, send a SIGTTOU.  If the signal is blocked or
	   ignored, go ahead and perform the operation.  POSIX 7.2) */
	/* 如果试图设置终端的状态但此时终端不在前台，那么我们就需要发送一个SIGTTOU信号。如果该信号被进程屏蔽或忽略了，就直接执行本次操作。 POSIX7.2 */
	/*
	 * 如果当前进程使用的tty终端的进程组号与进程的进程组号不同，即当前进程终端不在前台，表示当前进程试图修改不受控制的终端的termios结构。因此根据POSIX标准的要求这里需要发送SIGTTOU
	 * 信号让使用这个终端的进行先暂时停止执行，以让我们先修改termios结构。如果发送信号函数tty_signal()返回值是ERESTARTSYS或EINTR，则等一会再执行本次操作。
	 */
	if ((current->tty == channel) && (tty->pgrp != current->pgrp)) {
		retsig = tty_signal(SIGTTOU, tty);
		if (retsig == -ERESTARTSYS || retsig == -EINTR)
			return retsig;
	}
	/*
	 * 接着把用户数据区中termios结构信息复制到指定终端tty结构的termios结构中。因为用户有可能已经修改了终端串行口传输波特率，所以这里再根据termios结构中的控制模式标志c_cflag中
	 * 的波特率信息修改串行UART芯片内的传输波特率。最后返回0
	 */
	for (i=0 ; i< (sizeof (*termios)) ; i++)
		((char *)&tty->termios)[i]=get_fs_byte(i+(char *)termios);
	change_speed(tty);
	return 0;
}

/*
 * 读去termios结构中的信息
 * 参数：tty - 指定终端的tty结构指针；termios - 保存termios结构信息的用户缓冲区
 */
static int get_termio(struct tty_struct * tty, struct termio * termio)
{
	int i;
	struct termio tmp_termio;

	/*
	 * 首先验证用户的缓冲区指针所指内存区容量是否足够，如不够则分配内促。然后将termios结构的信息复制到临时termio结构中。这两个结构基本相同，但输入、输出、控制和本地
	 * 标志集的数据类型不同。前者的是long，后者的是short。因此先复制到临时termio结构中目的是为了进行数据类型转换。最后逐字节地把临时termio结构中的信息复制到用户termio结构缓冲区中。并返回0
	 */
	verify_area(termio, sizeof (*termio));
	tmp_termio.c_iflag = tty->termios.c_iflag;
	tmp_termio.c_oflag = tty->termios.c_oflag;
	tmp_termio.c_cflag = tty->termios.c_cflag;
	tmp_termio.c_lflag = tty->termios.c_lflag;
	tmp_termio.c_line = tty->termios.c_line;
	for(i=0 ; i < NCC ; i++)
		tmp_termio.c_cc[i] = tty->termios.c_cc[i];
	for (i=0 ; i< (sizeof (*termio)) ; i++)
		put_fs_byte( ((char *)&tmp_termio)[i] , i+(char *)termio );
	return 0;
}

/*
 * This only works as the 386 is low-byt-first
 */
/* 下面termio设置函数仅适用于低字节在前的386CPU */
/*
 * 设置终端termio结构信息
 * 参数：tty - 指定终端的tty结构指针；termio - 用户数据区中termios结构
 * 该函数用于将用户缓冲区termio的信息复制到终端的termios结构中，并返回0
 */
static int set_termio(struct tty_struct * tty, struct termio * termio,
			int channel)
{
	int i, retsig;
	struct termio tmp_termio;

	/*
	 * 与set_termios()一样，如果进程使用的终端的进程组号与进程的进程组号不同，即当前进程终端不在前台，表示当前进程试图修改不受控制的终端的termios结构。因此根据POSIX标准
	 * 的要求这里需要发送SIGTTOU信号让使用这个终端的进程先暂时停止执行，以让我们先修改termios结构。如果发送信号函数tty_signal()返回值是ERESTARTSYS或EINTR，则等一会
	 * 再执行本次操作
	 */
	if ((current->tty == channel) && (tty->pgrp != current->pgrp)) {
		retsig = tty_signal(SIGTTOU, tty);
		if (retsig == -ERESTARTSYS || retsig == -EINTR)
			return retsig;
	}
	/*
	 * 接着复制用户数据区中termio结构信息到临时termio结构中。然后再将termio结构的信息复制到tty的termio结构中。这样做的目的是为了对其中模式标志集的类型进行转换，即
	 * 从termio的短整数类型转换成termios的长整数类型。但两种结构的c_line和c_cc[]字段是完全相同的
	 */
	for (i=0 ; i< (sizeof (*termio)) ; i++)
		((char *)&tmp_termio)[i]=get_fs_byte(i+(char *)termio);
	*(unsigned short *)&tty->termios.c_iflag = tmp_termio.c_iflag;
	*(unsigned short *)&tty->termios.c_oflag = tmp_termio.c_oflag;
	*(unsigned short *)&tty->termios.c_cflag = tmp_termio.c_cflag;
	*(unsigned short *)&tty->termios.c_lflag = tmp_termio.c_lflag;
	tty->termios.c_line = tmp_termio.c_line;
	for(i=0 ; i < NCC ; i++)
		tty->termios.c_cc[i] = tmp_termio.c_cc[i];
	/* 最后因为用户有可能已修改了终端串行口传输波特率，所以这里再根据termios结构中的控制模式标志c_cflag中的波特率信息修改串行UART芯片内的传输波特率，并返回0 */
	change_speed(tty);
	return 0;
}

/*
 * tty终端设备输入输出控制函数
 * 参数：dev - 设备号；cmd - ioctl命令；arg - 操作参数指针
 * 该函数首先根据参数给出的设备号找出对应终端的tty结构，然后根据控制命令cmd分别进行处理
 */
int tty_ioctl(int dev, int cmd, int arg)
{
	struct tty_struct * tty;
	int	pgrp;

	/*
	 * 首先根据设备号取得tty子设备号，从而取得终端的tty结构。若主设备号是5（控制终端），则进程的tty字段即是tty子设备号。此时如果进程的tty子设备号是负数，表明该进程没有控制终端，
	 * 即不能发出该iotcl调用，于是显示出错信息并停机。如果主设备号不是5而是4，我们就可以从设备号中取出子设备号。子设备号可以试0（控制欲终端）、1（串口1终端）、2（串口2终端）
	 */
	if (MAJOR(dev) == 5) {
		dev=current->tty;
		if (dev<0)
			panic("tty_ioctl: dev<0");
	} else
		dev=MINOR(dev);
	/*
	 * 然后根据子设备号和tty表，我们可以取得对应子设备号的tty结构。然后再根据参数提供的ioctl命令cmd进行分别处理。后半部分用于根据子设备号dev在tty_table[]表中选择对应的tty结构
	 * 如果dev=0，表示正在使用前台终端，因此直接使用终端号fg_console作为tty_table[]项索引取tty结构。如果dev大于0，那么就要分两种情况考虑：1.dev是虚拟终端号；2.dev是串行终端
	 * 好或伪终端号。对于虚拟终端，其tty结构在tty_table[]中索引项是dev-1（0-63）。对于其他类型终端，则它们的tty结构索引项就是dev。例如，若dev=64，表示是一个串行终端1，则其tty
	 * 结构就是tty_table[dev]。如果dev=1，则对应终端的tty结构是tty_table[0]。参加tty_io.c程序
	 */
	tty = tty_table + (dev ? ((dev < 64)? dev-1:dev) : fg_console);
	/*
	 * TCGETS：取相应终端termios结构信息。此时参数arg是用户缓冲区指针
	 * TCSETSF：在设置termios结构信息之前，需要先等待输出队列中所有数据处理完毕，并且刷新（清空）输入队列。再接着执行下面设置终端termios结构的操作
	 * TCSETSW：在设置终端termios的信息之前，需要先等待输出队列中所有数据处理完毕（耗尽）。对于修改参数会影响输出的情况，就需要使用这种形式
	 * TCSETS：设置终端termios结构信息。此时arg是保存termios结构的缓冲区指针
	 * TCGETA：取相应终端termio结构中的信息。此时参数arg是用户缓冲区指针
	 * TCSETAF：在设置termio结构信息之前，需要先等待输出队列中所有数据处理完毕，并且刷新（清空）输入队列。再接着执行下面设置终端termio结构的操作
	 * TCSETAW：在设置终端termios信息之前，需要先等待输出队列中所有数据处理完毕（耗尽）。对于修改参数会影响输出的情况，就需要使用这种形式
	 * TCSETA：设置终端termio结构信息。此时参数arg是保存termio结构的用户缓冲区指针
	 * TCSBRK：如果参数arg值是0，则等待输出队列处理完毕，并发送一个break
	 */
	switch (cmd) {
		case TCGETS:
			return get_termios(tty,(struct termios *) arg);
		case TCSETSF:
			flush(tty->read_q); /* fallthrough */	/* 接着继续执行 */
		case TCSETSW:
			wait_until_sent(tty); /* fallthrough */
		case TCSETS:
			return set_termios(tty,(struct termios *) arg, dev);
		case TCGETA:
			return get_termio(tty,(struct termio *) arg);
		case TCSETAF:
			flush(tty->read_q); /* fallthrough */	/* 接着继续执行 */
		case TCSETAW:
			wait_until_sent(tty); /* fallthrough */
		case TCSETA:
			return set_termio(tty,(struct termio *) arg, dev);
		case TCSBRK:
			if (!arg) {
				wait_until_sent(tty);
				send_break(tty);
			}
			return 0;
		/*
		 * 开始/停止流控制。如果参数arg是TCOOFF（Terminal Control Output OFF），则挂起输出；如果是TCOON，则恢复挂起的输出。在挂起或恢复输出同时需要把写队列中的字符输出，
		 * 以加快用户交互响应速度。如果arg是TCIOOF（Terminal Control Input ON），则挂起输入；如果是TCION，则重新开启挂起的输入
		 */
		case TCXONC:
			switch (arg) {
			case TCOOFF:
				tty->stopped = 1;							/* 停止终端输出 */
				tty->write(tty);							/* 写缓冲队列输出 */
				return 0;
			case TCOON:
				tty->stopped = 0;							/* 恢复终端输出 */
				tty->write(tty);
				return 0;
			/*
			 * 如果参数arg是TCIOFF，表示要求终端停止输入，于是我们往终端写队列中放入STOP字符。当终端收到该字符时就会暂停输入。如果参数是TCION，表示要发送一个START字符，让终端恢复传输
			 * STOP_CHAR(tty)定义为（（tty）->termios.c_cc[VSTOP]），即取终端termios结构控制字符组中对应项值。若内核定义了_POSIX_VDISABLE（\0），那么当某一项值等于_POSIX_VDISABLE
			 * 的值时，表示禁止使用相应的特殊字符。因此这里直接判断该值是否为0来确定要不要把停止控制字符放入终端写队列中。以下同。
			 */
			case TCIOFF:
				if (STOP_CHAR(tty))
					PUTCH(STOP_CHAR(tty),tty->write_q);
				return 0;
			case TCION:
				if (START_CHAR(tty))
					PUTCH(START_CHAR(tty),tty->write_q);
				return 0;
			}
			return -EINVAL; /* not implemented */
		/* 刷新已写输出但还没有发送、或已收但还没有读的数据。如果参数arg是0，则刷新（清空）输入队列；如果是1，则刷新输出队列；如果是2，则刷新输入和输出队列 */
		case TCFLSH:
			if (arg==0)
				flush(tty->read_q);
			else if (arg==1)
				flush(tty->write_q);
			else if (arg==2) {
				flush(tty->read_q);
				flush(tty->write_q);
			} else
				return -EINVAL;
			return 0;
		/* 设置终端串行线路专用模式 */
		case TIOCEXCL:
			return -EINVAL; /* not implemented */ /* 未实现 */
		/* 复位终端串行线路专用模式 */
		case TIOCNXCL:
			return -EINVAL; /* not implemented */
		/* 设置tty为控制终端。（TIOCSCTTY - 不要控制终端） */
		case TIOCSCTTY:
			return -EINVAL; /* set controlling term NI */ /* 未实现 */
		/* 读取终端进程组号（即读取前台进程组号）。首先验证用户缓冲区长度，然后复制终端tty的pgrp字段到用户缓冲区。此时参数args是用户缓冲区指针 */
		case TIOCGPGRP:						/* 实现库函数tcgetpgrp() */
			verify_area((void *) arg,4);
			put_fs_long(tty->pgrp,(unsigned long *) arg);
			return 0;
		/*
		 * 设置终端进程组号pgrp（即设置前台进程组号）。此时参数arg是用户缓冲区中进程组号pgrp的指针。执行该命令的前提条件是进程必须有控制终端。如果当前进程没有控制终端，
		 * 或者dev不是其控制终端，或者控制终端现在的确是正在处理的终端dev，但进程的会话号与该终端dev的会话号不同，则返回无终端错误信息。
		 * 然后我们就从用户缓冲区中取得欲设置的进程组号，并对该组号的有效性进行验证。如果组号pgrp小于0，则返回无效组号错误信息；如果pgrp的会话号与当前进程的不同，则返回
		 * 许可错误信息。否则我们可以设终端的进程组号为pgrp。此时pgrp成为前台进程组
		 */
		case TIOCSPGRP:										/* 实现库函数tcsetpgrp() */
			if ((current->tty < 0) ||
			    (current->tty != dev) ||
			    (tty->session != current->session))
				return -ENOTTY;
			pgrp=get_fs_long((unsigned long *) arg);		/* 从用户缓冲中取 */
			if (pgrp < 0)
				return -EINVAL;
			if (session_of_pgrp(pgrp) != current->session)
				return -EPERM;
			tty->pgrp = pgrp;			
			return 0;
		/*
		 * TIOCOUTQ：返回输出队列中还未送出的字符数。首先验证用户缓冲区长度，然后复制队列中字符数给用户。此时参数arg是用户缓冲区指针
		 * TIOCINQ：返回输入队列中还未读取的字符数。首先验证用户缓冲区长度，然后复制队列中字符数给用户。此时参数arg是用户缓冲区指针
		 */
		case TIOCOUTQ:
			verify_area((void *) arg,4);
			put_fs_long(CHARS(tty->write_q),(unsigned long *) arg);
			return 0;
		case TIOCINQ:
			verify_area((void *) arg,4);
			put_fs_long(CHARS(tty->secondary),
				(unsigned long *) arg);
			return 0;
		/*
		 * TIOCSTI：模拟终端输入操作。该命令以一个指向字符的指针作为参数，并假设该字符是在终端上键入的。用户必须在控制终端上具有超级用户权限或具有读许可权限
		 * TIOCGWINSZ：读取终端设备窗口大小信息（参见termios.h中的winsize结构）
		 * TIOCSWINSZ：读取中断设备窗口大小信息（参见winsize结构）
		 */
		case TIOCSTI:
			return -EINVAL; /* not implemented */	/* 未实现 */
		case TIOCGWINSZ:
			return -EINVAL; /* not implemented */
		case TIOCSWINSZ:
			return -EINVAL; /* not implemented */
		/*
		 * TIOCMGET：返回MODEM状态控制引线的当前状态比特位标志集（见termios.h）
		 * TIOCMBIS：设置单个MODEM状态控制引线的状态（true或false）
		 * TIOCMBIC：复位单个MODEM状态控制引线的状态。
		 * TIOCMSET：设置MODEM状态引线的状态。若某比特位置位，则MODEM对应的状态引线将为有效
		 */
		case TIOCMGET:
			return -EINVAL; /* not implemented */
		case TIOCMBIS:
			return -EINVAL; /* not implemented */
		case TIOCMBIC:
			return -EINVAL; /* not implemented */
		case TIOCMSET:
			return -EINVAL; /* not implemented */
		/*
		 * TIOCGSOFTCAR：读取软件载波检测标志（1-开启；0-关闭）
		 * TIOCSSOFTCAR：设置软件载波检查标志（1-开启；0-关闭）
		 */
		case TIOCGSOFTCAR:
			return -EINVAL; /* not implemented */
		case TIOCSSOFTCAR:
			return -EINVAL; /* not implemented */
		default:
			return -EINVAL;
	}
}
