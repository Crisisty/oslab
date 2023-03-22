/*
 *  linux/fs/fcntl.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>			/* 字符串头文件。主要定义了一些有关字符串操作的嵌入函数。 */
#include <errno.h>			/* 错误号头文件。包含系统中各种出错号。 */
#include <linux/sched.h>	/* 调度程序头文件。定义了任务结构task_struct、任务0的数据等 */
#include <linux/kernel.h>	/* 内核头文件。含有一些内核常用函数的原型定义 */
#include <asm/segment.h>	/* 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数 */

#include <fcntl.h>			 /* 文件控制头文件。文件及其描述符的操作控制常数符号的定义。*/
#include <sys/stat.h>		/* 文件状态头文件。含有文件或文件系统状态结构stat{}和常量 */

extern int sys_close(int fd);		/* 关闭文件系统调用 */

/**
 * 复制文件句柄(文件描述符)
 * @param[in]	fd		欲复制的文件句柄
 * @param[in]	arg		指定新文件句柄的最小数值
 * @retval		成功返回新文件句柄，失败返回出错码
 */
static int dupfd(unsigned int fd, unsigned int arg)
{
	/*
	 * 首先检查函数参数的有效性。如果文件句柄值大于一个程序最多打开文件数NR_OPEN，或者该句柄的文件结构不存在，则返回
	 * 出错码并退出。如果指定的新句柄值arg大于最多打开文件数，也返回出错码并退出。注意，实际上文件句柄就是进程文件结构
	 * 指针数组项索引号
	 */
	if (fd >= NR_OPEN || !current->filp[fd]) {
		return -EBADF; /* 文件句柄错误 */
	}
	if (arg >= NR_OPEN) {
		return -EINVAL; /* 参数非法 */
	}
	/*
	 * 然后在当前进程的文件结构指针数组中寻找索引号等于或大于arg，但还没有使用的项。若找到的新句柄值arg大于最多打开文件数
	 * （即没有空闲项），则返回出错码并退出
	 */
	/* 找到一个比arg大的最小的未使用的句柄值 */
	while (arg < NR_OPEN) {
		if (current->filp[arg]) {
			arg++;
		} else {
			break;
		}
	}
	if (arg >= NR_OPEN) {
		return -EMFILE;	/* 打开文件太多 */
	}
	/*
	 * 否则针对找到的空闲项（句柄），在执行时关闭标志位图close_on_exec中复位该句柄位。即在运行exec()类函数时，不会关闭
	 * 用dup()创建的句柄。并令该文件结构指针等于原句柄fd的指针，并且将文件引用数增1。最后返回新的文件句柄arg
	 */
	current->close_on_exec &= ~(1<<arg);
	(current->filp[arg] = current->filp[fd])->f_count++;
	return arg;
}

/**
 * 复制文件句柄
 * @param[in]	oldfd	欲复制的指定文件句柄
 * @param[in]	newfd	新文件句柄值(如果newfd已打开，则首先关闭之)
 * @retval		新文件句柄或出错码
 */
int sys_dup2(unsigned int oldfd, unsigned int newfd)
{
	sys_close(newfd);		/* 若句柄newfd已经打开，则首先关闭之 */
	return dupfd(oldfd, newfd);		/* 复制并返回新句柄 */
}

/**
 * 复制文件句柄
 * 复制指定文件句柄oldfd，新句柄的值是当前最小的未用句柄值
 * @param[in]	fildes	欲复制的指定文件句柄
 * @retval		新文件句柄(当前最小的未用句柄值)或出错码
 */
int sys_dup(unsigned int fildes)
{
	return dupfd(fildes, 0);
}


/**
 * 文件控制
 * @param[in]	fd		文件句柄
 * @param[in]	cmd		控制命令
 * @param[in]	arg		针对不同的命令有不同的含义
 *						1. F_DUPFD，arg是新文件句可取的最小值
 *						2. F_SETFD，arg是要设置的文件句柄标志
 *						3. F_SETFL，arg是新的文件操作和访问模式
 *						4. F_GETLK、F_SETLK和F_SETLKW，arg是指向flock结构的指针（为
 *						实现文件上锁功能）
 * @retval	若出错，则所有操作都返回 -1;
 *			若成功，那么
 *			1. F_DUPFD，返回新文件句柄
 *			2. F_GETFD，返回文件句柄的当前执行时关闭标志close_on_exec
 *			3. F_GETFL，返回文件操作和访问标志。
 */
int sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg)
{	
	struct file * filp;

	/*
	 * 首先给出的文件句柄的有效性。然后根据不同命令cmd进行分别处理。如果文件句柄值大于一个进程最多打开文件数NR_OPEN,
	 * 或者该句柄的文件结构指针为空，则返回出错码并退出
	 */
	if (fd >= NR_OPEN || !(filp = current->filp[fd])) {
		return -EBADF;
	}
	switch (cmd) {
		case F_DUPFD: /* 复制句柄，返回新文件句柄 */
			return dupfd(fd, arg);
		case F_GETFD: /* 获取文件句柄标志，返回文件句柄的当前执行时关闭标志 */
			return (current->close_on_exec>>fd)&1;
		case F_SETFD: /* 设置文件句柄标志，arg=1设置关闭标志为1，arg=0设置关闭标志为0 */
			if (arg & 1) {
				current->close_on_exec |= (1<<fd);
			} else {
				current->close_on_exec &= ~(1<<fd);
			}
			return 0;
		case F_GETFL: /* 获取文件状态标志和访问模式flag，返回文件操作和访问标志 */
			return filp->f_flags;
		case F_SETFL: /* 设置文件状态标志和访问模式flag */
			filp->f_flags &= ~(O_APPEND | O_NONBLOCK);
			filp->f_flags |= arg & (O_APPEND | O_NONBLOCK);
			return 0;
		case F_GETLK:	case F_SETLK:	case F_SETLKW: /* 未实现 */
			return -1;
		default:
			return -1;
	}
}
