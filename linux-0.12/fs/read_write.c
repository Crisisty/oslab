/*
 *  linux/fs/read_write.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <sys/stat.h>		/* 文件状态头文件。含有文件或文件系统状态结构stat{}和常量 */
#include <errno.h>			/* 错误号头文件。包含系统中各种出错号。 */
#include <sys/types.h>		/* 类型头文件。定义了基本的系统数据类型 */

#include <linux/kernel.h>	/* 内核头文件。含有一些内核常用函数的原型定义 */
#include <linux/sched.h>	/* 调度程序头文件。定义了任务结构task_struct、任务0的数据等 */
#include <asm/segment.h>	/* 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数 */

#include <unistd.h>		/* import SEEK_SET,SEEK_CUR,SEEK_END */

extern int rw_char(int rw,int dev, char * buf, int count, off_t * pos);		/* 字符设备读写函数，fs/char_dev.c */
extern int read_pipe(struct m_inode * inode, char * buf, int count);	/* 读管道操作函数，fs/pipe.c */
extern int write_pipe(struct m_inode * inode, char * buf, int count);	/* 写管道操作函数，fs/pipe.c */
extern int block_read(int dev, off_t * pos, char * buf, int count);		/* 块设备读操作函数，fs/block_dev.c */
extern int block_write(int dev, off_t * pos, char * buf, int count);	/* 块设备写操作函数，fs/block_dev.c */
extern int file_read(struct m_inode * inode, struct file * filp, char * buf, int count);	/* 读文件操作函数，fs/file_dev.c */
extern int file_write(struct m_inode * inode, struct file * filp, char * buf, int count);	/* 写文件操作函数，fs/file_dev.c */

/**
 * 重定位文件读写指针 系统调用
 * @param[in]	fd		文件句柄
 * @param[in]	offset	文件读写指针偏移值
 * @param[in]	origin	偏移的起始位置，可有三种选择：SEEK_SET、SEEK_CUR、SEEK_END
 * @retval		成功返回读写偏移值，失败返回失败码
 */
int sys_lseek(unsigned int fd, off_t offset, int origin)
{
	struct file * file;
	int tmp;

	/*
	 * 首先判断函数提供的参数有效性，如果文件句柄值大于程序最多打开文件数NR_OPEN(20)，或者该句柄文件结构指针为空，
	 * 或者对应文件结构的i节点字段为空，或者指定设备文件指针是不可定位的，则返回出错码并退出。如果文件对应的i节点是管道
	 * 节点，则返回出错码退出。因为管道头尾指针不可随意移动
	 */
	if (fd >= NR_OPEN || !(file = current->filp[fd]) || !(file->f_inode)
	   || !IS_SEEKABLE(MAJOR(file->f_inode->i_dev))) {
		return -EBADF;
	}
	if (file->f_inode->i_pipe) { /* 管道不能操作读写指针 */
		return -ESPIPE;
	}
	/*
	 * 然后根据设置的定位标志，分别重新定位文件读写指针。
	 * origin=SEEK_SET，表示要求以文件起始处作为原点设置文件读写指针。若偏移值小于零，则出错返回出错码，否则
	 * 设置文件读写指针等于offset
	 */
	/* SEEK_CUR，SEEK_END分支中对相加值判断，既可过滤offset为负数且绝对值比文件长度大的情况，
	 又可以过滤相加超过文件所能支持的最大值(off_t数据类型溢出的情况) */
	switch (origin) {
		case SEEK_SET:	/* 从文件开始处 */
			if (offset < 0) {
				return -EINVAL;
			}
			file->f_pos = offset;
			break;
		case SEEK_CUR:	/* 从当前读写位置 */
			if (file->f_pos + offset < 0) {
				return -EINVAL;
			}
			file->f_pos += offset;
			break;
		case SEEK_END:	/* 从文件尾处 */
			if ((tmp = file->f_inode->i_size + offset) < 0) {
				return -EINVAL;
			}
			file->f_pos = tmp;
			break;
		default:
			return -EINVAL;
	}
	return file->f_pos;		/* 最后返回重定位后的文件读写指针值 */
}

/* TODO: 为什么只对读写管道操作判断是否有权限？ */

/**
 * 读文件 系统调用
 * @param[in]	fd		文件句柄
 * @param[in]	buf		缓冲区
 * @param[in]	count	欲读字节数
 * @retval		成功返回读取的长度，失败返回错误码
 */
int sys_read(unsigned int fd, char * buf, int count)
{
	struct file * file;
	struct m_inode * inode;

	/*
	 * 函数首先对参数有效性进行判断。如果文件句柄值大于程序最多打开文件数NR_OPEN，或者需要读取的字节计数值
	 * 小于0，或者该句柄的文件结构指针为空，则返回出错码并退出。若需读取的字节数count等于0，则返回0退出
	 */
	if (fd >= NR_OPEN || count < 0 || !(file = current->filp[fd])) {
		return -EINVAL;
	}
	if (!count) {
		return 0;
	}
	/*
	 * 然后验证存放数据的缓冲区内存限制。并取文件的i节点，用于根据该i节点的属性，分别调用相应的读操作函数，
	 * 若是读管道文件模式，则进行读管道操作，若成功则返回读取的字节数，否则返回出错码，退出。如果是字符型文件，
	 * 则进行读字符设备操作，并返回读取的字节数。如果是块设备文件，则执行块设备读操作，并返回读取的字节数
	 */
	verify_area(buf, count); 		/* 验证存放数据的缓冲区内存限制 */
	/* 根据文件类型执行相应的读操作 */
	inode = file->f_inode;
	if (inode->i_pipe) { 			/* 管道文件 */
		return (file->f_mode & 1) ? read_pipe(inode, buf, count) : -EIO;
	}
	if (S_ISCHR(inode->i_mode)) { 	/* 字符设备 */
		return rw_char(READ, inode->i_zone[0], buf, count, &file->f_pos);
	}
	if (S_ISBLK(inode->i_mode)) { 	/* 块设备 */
		return block_read(inode->i_zone[0], &file->f_pos, buf, count);
	}
	/*
	 * 如果是目录文件或者是常规文件，则首先验证读取字节数count的有效性，并进行调整。若读取字节数加上文件当前
	 * 读写指针值大于文件长度，则重新设置读取字节数为文件长度值减去当前读写指针值。若读取数等于0，则返回0退出。
	 * 然后执行文件读操作，返回读取得字节数并退出
	 */
	/* 目录文件或常规文件 */
	if (S_ISDIR(inode->i_mode) || S_ISREG(inode->i_mode)) {
		if (count+file->f_pos > inode->i_size) {
			count = inode->i_size - file->f_pos;
		}
		if (count <= 0) {
			return 0;
		}
		return file_read(inode, file, buf, count);
	}
	/* 如果执行到这，说明无法判断文件类型 */
	printk("(Read)inode->i_mode=%06o\n\r", inode->i_mode);
	return -EINVAL;
}


/**
 * 写文件 系统调用
 * @param[in]	fd		文件句柄
 * @param[in]	buf		用户缓冲区
 * @param[in]	count	欲写字节数
 * @retval		成功返回写入的长度，失败返回错误码
 */
int sys_write(unsigned int fd, char * buf, int count)
{
	struct file * file;
	struct m_inode * inode;
	
	/*
	 * 函数首先对参数有效性进行判断。如果文件句柄值大于程序最多打开文件数NR_OPEN，或者需要读取的字节计数值
	 * 小于0，或者该句柄的文件结构指针为空，则返回出错码并退出。若需读取的字节数count等于0，则返回0退出
	 */
	if (fd >= NR_OPEN || count < 0 || !(file = current->filp[fd])) {
		return -EINVAL;
	}
	if (!count) {
		return 0;
	}

	/*
	 * 然后取文件的i节点，并根据该i节点的属性分别调用相应的写操作函数。若是管道文件，并且是写管道文件模式，
	 * 则进行写管道操作，若成功则返回写入的字节数，否则返回出错码退出；如果是字符设备文件，则进行写字符设备操作，
	 * 返回写入的字符数退出；如果是块设备文件，则进行块设备写操作，并返回写入的字符数退出；若是常规文件，则执行
	 * 文件写操作，并返回写入的字节数，退出
	 */
	/* 根据文件类型执行相应的写操作 */
	inode = file->f_inode;
	if (inode->i_pipe) { 			/* 管道 */
		/* file->f_mode & 2 即是否有写的权限 */
		return (file->f_mode & 2) ? write_pipe(inode, buf, count) : -EIO;
	}
	if (S_ISCHR(inode->i_mode)) { 	/* 字符设备 */
		return rw_char(WRITE, inode->i_zone[0], buf, count, &file->f_pos);
	}
	if (S_ISBLK(inode->i_mode)) { 	/* 块设备 */
		return block_write(inode->i_zone[0], &file->f_pos, buf, count);
	}
	if (S_ISREG(inode->i_mode)) { 	/* 文件 */
		return file_write(inode, file, buf, count);
	}
	/* 如果执行到这，说明无法判断文件类型 */
	printk("(Write)inode->i_mode=%06o\n\r", inode->i_mode);
	return -EINVAL;
}
