/*
 *  linux/fs/stat.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>			/* 错误号头文件。包含系统中各种出错号。 */
#include <sys/stat.h>		/* 文件状态头文件。含有文件或文件系统状态结构stat{}和常量 */

#include <linux/fs.h>		/* 文件系统头文件。定义文件表结构（file,buffer_head,m_inode等） */
#include <linux/sched.h>	/* 调度程序头文件。定义了任务结构task_struct、任务0的数据等 */
#include <linux/kernel.h>	/* 内核头文件。含有一些内核常用函数的原型定义 */
#include <asm/segment.h>	/* 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数 */

/**
 * 复制文件状态信息
 * 利用i节点获取文件状态信息
 * @param[in]		inode		文件i节点
 * @param[in/out]	statbuf		用户数据空间中stat文件状态结构指针，用于存放取得的状态信息
 * @retval			void
 */
static void cp_stat(struct m_inode * inode, struct stat * statbuf)
{
	struct stat tmp;
	int i;

	/*
	 * 首先验证（或分配）存放数据的内存空间。然后临时复制相应节点上的信息。最后将这些状态信息复制到用户缓冲区中
	 */
	verify_area(statbuf, sizeof (struct stat));
	tmp.st_dev		= inode->i_dev;		/* 文件所在设备号 */
	tmp.st_ino		= inode->i_num;		/* 文件i节点号 */
	tmp.st_mode		= inode->i_mode;	/* 文件属性 */
	tmp.st_nlink	= inode->i_nlinks;	/* 文件连接数 */
	tmp.st_uid		= inode->i_uid;		/* 文件的用户ID */
	tmp.st_gid		= inode->i_gid;		/* 文件的组ID */
	tmp.st_rdev		= inode->i_zone[0];	/* 设备号（若是特殊字符文件或块设备文件） */
	tmp.st_size		= inode->i_size;	/* 文件字节长度（如果文件是常规文件） */
	tmp.st_atime	= inode->i_atime;	/* 最后访问时间 */
	tmp.st_mtime	= inode->i_mtime;	/* 最后修改时间 */
	tmp.st_ctime	= inode->i_ctime;	/* 最后i节点修改时间 */

	for (i = 0; i < sizeof(tmp); i++) {
		put_fs_byte(((char *) &tmp)[i], i + (char *) statbuf);
	}
}


/**
 * 获取文件状态
 * 根据给定的文件名获取相关文件状态信息。
 * @param[in]		filename	指定的文件名
 * @param[in/out]	statbuf		存放状态信息的缓冲区指针
 * @retval			成功返回0，出错返回出错码
 */
int sys_stat(char * filename, struct stat * statbuf)
{
	struct m_inode * inode;
	/*
	 * 首先根据文件名找出对应的i节点，然后将i节点上的文件状态信息复制到用户缓冲区中，并放回i节点
	 */
	if (!(inode = namei(filename))) {
		return -ENOENT;
	}
	cp_stat(inode, statbuf);
	iput(inode);
	return 0;
}


/**
 * 获取文件状态（符号链接） 系统调用
 * 根据给定的文件名获取相关文件状态信息。文件路径名中有符号链接文件名，则取符号文件的状态。
 * 不跟随连接查目标文件
 * @param[in]		filename	指定的文件名
 * @param[in/out]	statbuf		存放状态信息的缓冲区指针
 * @retval			成功返回0，出错返回出错码
 */
int sys_lstat(char * filename, struct stat * statbuf)
{
	struct m_inode * inode;

	/*
	 * 首先根据文件名找出对应的i节点，然后将i节点上的文件状态信息复制到用户缓冲区中，并放回i节点
	 */
	if (!(inode = lnamei(filename))) {		/* 取指定路径名i节点，不跟随符号链接 */
		return -ENOENT;
	}
	cp_stat(inode,statbuf);
	iput(inode);
	return 0;
}

/**
 * 获取文件状态（使用句柄）
 * 根据给定的文件句柄获取相关文件状态信息。
 * @param[in]		fd			指定文件的句柄
 * @param[in/out]	statbuf		存放状态信息的缓冲区指针
 * @retval			成功返回0，出错返回出错码
 */
int sys_fstat(unsigned int fd, struct stat * statbuf)
{
	struct file * f;
	struct m_inode * inode;

	/*
	 * 首先取文件句柄对应的文件结构，然后从中得到文件的i节点。然后将i节点上的文件状态信息复制到用户换冲区中。
	 * 如果文件句柄值大于一个程序最多打开文件数NR_OPEN，或者该句柄的文件结构指针为空，或者对应文件结构的i节点
	 * 字段为空，则出错，返回出错码并退出
	 */
	if (fd >= NR_OPEN || !(f = current->filp[fd]) || !(inode = f->f_inode)) {
		return -EBADF;
	}
	cp_stat(inode,statbuf);
	return 0;
}

/**
 * 符号链接文件 系统调用
 * 该调用读取符号链接文件的内容（即该符号链接所指向文件的路径名字符串），并放到指定长度的用户缓
 * 冲区中。若缓冲区太小，就会截断符号链接的内容。
 * @param[in]		path	符号链接文件路径名
 * @param[in/out]	buf		用户缓冲区
 * @param[in]		bufsiz	缓冲区长度
 * @retval			成功则返回放入缓冲区中的字符数；若失败则返回出错码
*/
int sys_readlink(const char * path, char * buf, int bufsiz)
{
	struct m_inode * inode;
	struct buffer_head * bh;
	int i;
	char c;

	/*
	 * 首先检查和验证函数参数的有效性，并对其进行调整，用户缓冲区长度bufsi必须是1-1023之间。然后取得符号链接文件名的i节点
	 * 并读取该文件的第1块数据内容。之后放回i节点
	 */
	if (bufsiz <= 0) {
		return -EBADF;
	}
	if (bufsiz > 1023) {
		bufsiz = 1023;
	}
	verify_area(buf, bufsiz);
	if (!(inode = lnamei(path))) {
		return -ENOENT;
	}
	if (inode->i_zone[0]) {
		bh = bread(inode->i_dev, inode->i_zone[0]);
	} else {
		bh = NULL;
	}
	iput(inode);
	/* 
	 * 如果成功读取文件数据内容，则从内容中复制最多bufsi个字符到用户缓冲区中。不复制NULL字符。最后释放缓冲块，并返回
	 * 复制的字节数
	 */
	if (!bh) {
		return 0;
	}
	i = 0;
	while (i < bufsiz && (c = bh->b_data[i])) {
		i++;
		put_fs_byte(c,buf++);
	}
	brelse(bh);
	return i;
}
