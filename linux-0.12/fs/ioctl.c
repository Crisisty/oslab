/*
 *  linux/fs/ioctl.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>		/* 字符串头文件。主要定义了一些有关字符串操作的嵌入函数。 */
#include <errno.h>		/* 错误号头文件。包含系统中各种出错号。 */
#include <sys/stat.h>	/* 文件状态头文件。含有文件或文件系统状态结构stat{}和常量 */

#include <linux/sched.h>	/* 调度程序头文件。定义了任务结构task_struct、任务0的数据等 */

extern int tty_ioctl(int dev, int cmd, int arg);
extern int pipe_ioctl(struct m_inode *pino, int cmd, int arg);

/* 定义输入输出控制(ioctl)函数指针类型 */
typedef int (*ioctl_ptr)(int dev,int cmd,int arg);

/* 取系统中设备种数的宏 */
#define NRDEVS ((sizeof (ioctl_table))/(sizeof (ioctl_ptr)))

/* ioctl操作函数指针表 */
static ioctl_ptr ioctl_table[]={
	NULL,		/* nodev */
	NULL,		/* /dev/mem */
	NULL,		/* /dev/fd */
	NULL,		/* /dev/hd */
	tty_ioctl,	/* /dev/ttyx */
	tty_ioctl,	/* /dev/tty */
	NULL,		/* /dev/lp */
	NULL};		/* named pipes */
	
/**
 * 输入输出控制
 * 该函数首先判断参数给出的文件描述符是否有效。然后根据对应i节点中文件属性判断文件类型，并根据
 * 具体文件类型调用相关的处理函数。
 * @param[in]	fd		文件描述符
 * @param[in]	cmd		命令码
 * @param[in]	arg		参数
 * @retval		成功返回0，失败返回出错码
 */
int sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{	
	struct file * filp;
	int dev, mode;

	/*
	 * 首先判断给出的文件描述符的有效性。如果文件描述符超出可打开的文件数，或者对应描述符的文件结构指针为空，则返回出错码并退出。
	 * 如果文件结构对应的是管道i节点，则我们需要根据进程是否有权操作该管道确定是否执行管道IO控制操作。若有权执行则调用pipe_ioctl()，
	 * 否则返回无效文件错误码
	 */
	if (fd >= NR_OPEN || !(filp = current->filp[fd])) {
		return -EBADF;
	}
	if (filp->f_inode->i_pipe) {
		return (filp->f_mode&1) ? pipe_ioctl(filp->f_inode,cmd,arg) : -EBADF;
	}
	/*
	 * 对于其他类型文件，取对应文件的属性，并据此判断文件的类型。如果该文件既不是字符设备文件，也不是块设备文件，则返回出错码退出。
	 * 若是字符或块设备文件，则从文件的i节点中取设备号。如果设备号大于系统现有的设备数，则返回出错码
	 */
	mode = filp->f_inode->i_mode;
	if (!S_ISCHR(mode) && !S_ISBLK(mode)) {
		return -EINVAL;
	}
	dev = filp->f_inode->i_zone[0];		/* 对于设备类文件，此处存有设备号 */
	if (MAJOR(dev) >= NRDEVS) {
		return -ENODEV;
	}
	/*
	 * 然后根据IO控制表ioctl_table查得对应的设备的ioctl函数指针，并调用该函数。如果设备在ioctl函数指针表中没有对应函数，
	 * 则返回出错码
	 */
	if (!ioctl_table[MAJOR(dev)]) {
		return -ENOTTY;
	}
	return ioctl_table[MAJOR(dev)](dev,cmd,arg);
}
