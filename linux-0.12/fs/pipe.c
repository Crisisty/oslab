/*
 *  linux/fs/pipe.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <signal.h>			/* 信号头文件。定义信号符号常量，信号结构以及信号操作函数原型 */
#include <errno.h>			/* 错误号头文件。包含系统中各种出错号。 */
#include <termios.h>		/* 终端输入输出函数头文件。主要定义控制异步通信口的终端接口 */

#include <linux/sched.h>	/* 调度程序头文件。定义了任务结构task_struct、任务0的数据等 */
#include <linux/mm.h>	/* for get_free_page */	/* 使用其中的get_free_page */
#include <asm/segment.h>	/* 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数 */
#include <linux/kernel.h>	/* 内核头文件。含有一些内核常用函数的原型定义 */

/**
 * 读管道
 * @param[in]		inode	管道对应的i节点
 * @param[in/out]	buf		用户数据缓冲区指针
 * @param[in]		count	欲读取的字节数
 * @retval			成功返回读取长度，失败返回错误码
 */
int read_pipe(struct m_inode * inode, char * buf, int count)
{
	int chars, size, read = 0;

	/*
	 * 如果需要读取的字节计数count大于0，我们就循环执行以下操作。在循环读操作过程中，若当前管道中没有数据（size=0）
	 * 则唤醒等待该节点的进程，这通常是写管道进程。如果已没有写管道者，即i节点引用计数值小于2，则返回已读字节数退出。
	 * 如果当前收到有非阻塞信号，则立刻返回已读取字节数退出；若还没有收到任何数据，则返回重新启动系统调用号退出。否则就
	 * 让进程在该管道上睡眠，用以等待消息的到来。宏PIPE_SIZE定义在include/linux/fs.h中。关于“重新启动系统调用”，
	 * 参见kernel/signal.c程序
	 */
	while (count > 0) {
		while (!(size = PIPE_SIZE(*inode))) {		/* 取管道中数据长度值 */
			wake_up(& PIPE_WRITE_WAIT(*inode));
			/* 没有写进程，立即返回 */
			if (inode->i_count != 2) { /* are there any writers? */
				return read;
			}
			/* 没有阻塞信号，立即返回 */
			if (current->signal & ~current->blocked) {
				return read ? read : -ERESTARTSYS;
			}
			interruptible_sleep_on(& PIPE_READ_WAIT(*inode));
		}
		/*
		 * 此时说明管道（缓冲区）中有数据。于是我们取管道尾指针到缓冲区末端的字节数chars。如果其大于还需要读取的
		 * 字节数count，则令其等于count。如果chars大于当前管道中含有数据的长度size，则令其等于size。然后把需读
		 * 字节数count减去此次可读的字节数chars，并累加已读字节数read
		 */
		/* chars表示当次循环可读取的字节数 */
		chars = PAGE_SIZE - PIPE_TAIL(*inode);
		if (chars > count) {
			chars = count;
		}
		if (chars > size) {
			chars = size;
		}
		count -= chars;
		read += chars;
		/*
		 * 再令size指向管道尾指针处，并调整当前管道尾指针（前移chars字节）。若尾指针超过管道末端则绕回。然后
		 * 将管道中的数据复制到用户缓冲区中。对于管道i节点，其i_size字段中是管道缓冲块指针
		 */
		size = PIPE_TAIL(*inode);
		PIPE_TAIL(*inode) += chars;
		PIPE_TAIL(*inode) &= (PAGE_SIZE-1);
		while (chars-->0) {
			put_fs_byte(((char *)inode->i_size)[size++], buf++);
		}
	}
	/* 当此次读管道操作结束，则唤醒等待该管道的进程，并返回读取的字节数 */
	wake_up(& PIPE_WRITE_WAIT(*inode));
	return read;
}


/**
 * 写管道
 * @param[in]	inode		管道对应的i节点
 * @param[in]	buf			数据缓冲区指针
 * @param[in]	count		将写入管道的字节数
 * @retval		成功返回写入的长度，失败返回-1
 */
int write_pipe(struct m_inode * inode, char * buf, int count)
{
	int chars, size, written = 0;

	/*
	 * 如果要写入的字节数count还大于0，那么我们就循环执行以下操作。在循环操作过程中，如果当前管道中已经满了
	 * （空闲空间size=0），则唤醒等待该管道的进程，通知唤醒的是读管道进程。如果已没有读管道者，即i节点引用计数
	 * 值小于2，则向当前进程发送SIGPIPE信号，并返回已写入的字节数退出；若写入0字节，则返回-1.否则让当前进程
	 * 在该管道上睡眠，以等待读管道进程来读取数据，从而让管道腾出空间。宏PIPE_SIZE()、PIPE_HEAD()等定义在
	 * 文件include/linux/fs.h中
	 */
	while (count > 0) {
		while (!(size = (PAGE_SIZE-1) - PIPE_SIZE(*inode))) {
			wake_up(& PIPE_READ_WAIT(*inode));
			/* 没有读进程，发出SIGPIPE信号并立即返回 */
			if (inode->i_count != 2) { /* no readers */
				current->signal |= (1<<(SIGPIPE-1));
				return written ? written : -1;
			}
			sleep_on(& PIPE_WRITE_WAIT(*inode));
		}
		/*
		 * 程序执行到这里表示管道缓冲区中有可写空间size，于是我们取管道头指针到缓冲区末端空间字节数chars。写管道
		 * 操作是从管道头指针处开始写的。如果chars大于需要写入的字节数count，则令其等于count，如果chars大于当前
		 * 管道中空闲空间长度size，则令其等于size。然后把需要写入字节数count减去此次可写入的字节数chars，并把
		 * 写入字节数累加到written中
		 */
		/* chars表示当次循环可写入的字节数 */
		chars = PAGE_SIZE - PIPE_HEAD(*inode);
		if (chars > count) {
			chars = count;
		}
		if (chars > size) {
			chars = size;
		}
		count -= chars;
		written += chars;
		/* 
		 * 再令size指向管道数据头指针处，并调整当前管道数据头指针（前移chars字节）。若头指针超过管道末端则绕回。
		 * 然后从用户缓冲区复制chars个字节到管道头指针开始处。对于管道i节点，其i_size字段中是管道缓冲块指针
		 */
		size = PIPE_HEAD(*inode);
		PIPE_HEAD(*inode) += chars;
		PIPE_HEAD(*inode) &= (PAGE_SIZE-1);
		while (chars-->0) {
			((char *)inode->i_size)[size++] = get_fs_byte(buf++);
		}
	}
	/* 当此次写管道操作结束，则唤醒等待该管道的进程，并返回已写入的字节数，退出 */
	wake_up(& PIPE_READ_WAIT(*inode));
	return written;
}


/**
 * 创建管道
 * 在fildes所指的数组中创建一对指向一管道i节点的句柄
 * @param[in/out]	fildes		文件句柄数组：fildes[0]用于读管道，fildes[1]用于写管道
 * @retval		成功返回0，出错返回-1
 */
int sys_pipe(unsigned long * fildes)
{
	struct m_inode * inode;
	struct file * f[2];		/* 文件结构数组 */
	int fd[2];				/* 文件句柄数组 */
	int i, j;

	/* 从系统文件表中取出两个空闲项 */
	/* 
	 * (引用计数字段为0的项），并分别设置引用计数为1。若只有1个空闲项，则释放该项（引用计数复位）。若
	 * 没有找到两个空闲项，则返回-1
	 */
	j = 0;
	for(i = 0; j < 2 && i < NR_FILE; i ++) {
		if (!file_table[i].f_count) {
			(f[j++] = i + file_table)->f_count++;
		}
	}
	if (j == 1) {
		f[0]->f_count = 0;
	}
	if (j < 2) {
		return -1;
	}
	/*
	 * 针对上面取得的两个文件表结构项，分别分配一文件句柄，并使用进程文件结构指针数组的两项分别执行这两个
	 * 文件结构。而文件句柄即是该数组的索引号，类似地，如果只有一个空闲文件句柄，则释放该句柄（置空相应数据项）
	 * 如果没有找到两个空闲句柄，则释放上面获取的两个文件结构项（复位引用计数值），并返回-1
	 */
	/* 在当前进程的文件结构指针数组中分配两个文件句柄，用于上面取出的文件表结构项 */
	j = 0;
	for(i = 0; j < 2 && i < NR_OPEN; i ++) {
		if (!current->filp[i]) {
			current->filp[ fd[j] = i ] = f[j];
			j++;
		}
	}
	if (j == 1) {
		current->filp[fd[0]] = NULL;
	}
	if (j < 2) {
		f[0]->f_count = f[1]->f_count = 0;
		return -1;
	}
	/*
	 * 然后利用函数get_pipe_inode()申请一个管道使用的i节点，并为管道分配一页内存作为缓冲区，如果不成功，
	 * 则释放两个文件句柄和文件结构，并返回-1
	 */
	/* 申请一个管道使用的i节点 */
	if (!(inode = get_pipe_inode())) {
		current->filp[fd[0]] = current->filp[fd[1]] = NULL;
		f[0]->f_count = f[1]->f_count = 0;
		return -1;
	}
	/*
	 * 如果管道i节点申请成功，则对两个文件结构进行初始化操作，让它们指向同一管道i节点，并把读写指针置零。
	 * 第1个文件结构的文件模式置为读，第2个文件结构模式置为写。最后将文件句柄数组复制到对应的用户空间数组中，
	 * 成功返回0，退出
	 */
	/* 初始化 */
	f[0]->f_inode = f[1]->f_inode = inode;
	f[0]->f_pos = f[1]->f_pos = 0;
	f[0]->f_mode = 1;		/* read */
	f[1]->f_mode = 2;		/* write */

	put_fs_long(fd[0], 0+fildes);
	put_fs_long(fd[1], 1+fildes);
	return 0;
}

/**
 * 管道io控制函数
 * @param[in]	pino	管道i节点指针
 * @param[in]	cmd		控制命令
 * @param[in]	arg		参数
 * @retval		返回0表示执行成功，否则返回出错码
 */
int pipe_ioctl(struct m_inode *pino, int cmd, int arg)
{
	/*
	 * 如果命令是取管道中当前可读数据长度，则把管道数据长度值填入用户参数指定的位置处，并返回0，
	 * 否则返回无效命令错误码
	 */
	switch (cmd) {
		/* 取管道中当前可读数据的长度 */
		case FIONREAD:
			verify_area((void *) arg, 4);
			put_fs_long(PIPE_SIZE(*pino), (unsigned long *) arg);
			return 0;
		default:
			return -EINVAL;
	}
}
