/*
 *  linux/fs/block_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>			/* 错误号头文件。包含系统中各种出错号。 */

#include <linux/sched.h>	/* 调度程序头文件。定义了任务结构task_struct、任务0的数据，还有一些有关描述符参数设置和获取的嵌入式汇编函数宏语句 */
#include <linux/kernel.h>	/* 内核头文件。含有一些内核常用函数的原型定义 */
#include <asm/segment.h>	/* 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数 */
#include <asm/system.h>		/* 系统头文件。定义设置或修改描述符/中断门等的嵌入式汇编宏 */

/*
 * 设备数据块总数指针数组。其每个指针项指向指定住设备号的总块数数组hd_sizes[]。该总块数数组每一项对应字设备号确定
 * 的一个子设备上所拥有的数据块总数（1块大小=1KB）
 */
extern int *blk_size[];

/**
 * 数据块写函数
 * 向指定设备（实际先写到高速缓冲中）从给定偏移处写入指定长度数据
 * 对于内核来说，写操作是向高速缓冲区中写入数据。什么时候数据最终写入设备是由高速缓冲管理程序
 * 决定并处理的。另外，因为快设备是以块为单位进行读写，因此对于写开始位置不处于块起始处是，需要
 * 先将开始字节所在的整个块读出，然后将需要写的数据从写开始处填写满该块，再将完整的一块数据写盘（即交由高速缓冲程序去处理）
 * @param[in]	dev		设备号
 * @param[in]	pos		设备文件中偏移量指针
 * @param[in]	buf		用户空间中缓冲区地址
 * @param[in]	count	要写的字节数
 * @retval		成功返回已写入字节数，若没有写入任何字节或出错则返回出错号
 */
int block_write(int dev, long * pos, char * buf, int count)
{
	/*
	 * 首先由文件中位置pos换算成开始读写盘块的块号block，并求出需写第1个字节在该块中的偏移位置offset
	 */
	int block = *pos >> BLOCK_SIZE_BITS;		/* pos所在文件数据块号 */
	int offset = *pos & (BLOCK_SIZE - 1);		/* pos在数据块中偏移值 */
	int chars;
	int written = 0;
	int size;
	struct buffer_head * bh;
	register char * p;							/* 局部寄存器变量，被存放在寄存器中 */

	/*
	 * 在写一个块设备文件时，要求写的总数据块数当然不能超过指定设备上容许的最大数据块总数。因此这里首先取出指定
	 * 设备的块总数size来比较和限制函数参数给定的写入数据长度。如果系统中没有对设备指定长度，就使用默认长度0x7fffffff（2G个块）
	 */
	if (blk_size[MAJOR(dev)]) {
		size = blk_size[MAJOR(dev)][MINOR(dev)];
	} else {
		size = 0x7fffffff; /* 没有对设备指定长度，就使用默认长度2G个块 */
	}
	/* 
	 * 然后针对要写入的字节数count，循环执行以下操作，直到数据全部写入。在循环执行过程中，若当前写入数据的块号已大于或等于指定设备的总块数，
	 * 则返回已写字节数并退出。然后再计算再当前处理的数据块中可写入的字节数。如果需要写入的字节数填不满一块，那么就只需写count字节。如果正好
	 * 要写1块数据内容，则直接申请1块高速缓冲块，并放入用户数据即可。否则就需要读入被写入部分数据的数据块，并预读下两块。然后将块号递增1，为下次
	 * 操作做好准备。若缓冲块操作失败，则返回已写字节数，若没有写入任何字节，则返回出错号（负数）
	 */
	while (count > 0) {
		if (block >= size) {
			return written ? written : -EIO;
		}
		chars = BLOCK_SIZE - offset; /* 本块可写入的字节数 */
		if (chars > count) {
			chars = count;
		}
		if (chars == BLOCK_SIZE) {
			bh = getblk(dev, block);
		} else {
			bh = breada(dev, block, block+1, block+2, -1);
		}
		block ++;
		if (!bh) {
			return written ? written : -EIO;
		}
		/*
		 * 接着先把指针p指向读出数据的缓冲块中开始写入数据的位置处。若最后一次循环写入的数据不足一块，则需从块开始处填写（修复）所需的字节。
		 * 因此这里需预先设置offset为零。此后将文件中便宜指针pos前移此次要写的字节数chars，并累加这些要写的字节数到统计值written中。再把
		 * 还需要写入的计数值count减去此次要写的字节数chars。然后我们从用户缓冲区复制chars个字节到p指向的高速缓冲块中开始写入的位置处。复制
		 * 完后就设置该缓冲块已修改标志，并释放该缓冲块（也即该缓冲区引用计数递减1）
		 */
		p = offset + bh->b_data;
		offset = 0;
		*pos += chars;
		written += chars;					/* 累计写入字节数 */
		count -= chars;
		while (chars-- > 0) {
			*(p++) = get_fs_byte(buf++);
		}
		bh->b_dirt = 1;
		brelse(bh);
	}
	return written;						/* 返回已写入的字节数，正常退出 */
}


/**
 * 数据块读函数
 * 从指定设备和位置处读入指定长度数据到用户缓冲区中
 * @param[in]	dev		设备号
 * @param[in]	pos		设备文件中领衔量指针
 * @param[in]	buf		用户空间中缓冲区地址
 * @param[in]	count	要传送的字节数
 * @retval		返回已读入字节数。若没有读入任何字节或出错，则返回出错号
 */
int block_read(int dev, unsigned long * pos, char * buf, int count)
{
	int block = *pos >> BLOCK_SIZE_BITS;
	int offset = *pos & (BLOCK_SIZE-1);
	int chars;
	int size;
	int read = 0;
	struct buffer_head * bh;
	register char * p;			/* 局部寄存器变量，被存放在寄存器中 */

	/*
	 * 在读一个块设备文件时，要求写的总数据块数当然不能超过指定设备上容许的最大数据块总数。因此这里首先取出指定
	 * 设备的块总数size来比较和限制函数参数给定的写入数据长度。如果系统中没有对设备指定长度，就使用默认长度0x7fffffff（2G个块）
	 */
	if (blk_size[MAJOR(dev)]) {
		size = blk_size[MAJOR(dev)][MINOR(dev)];
	} else {
		size = 0x7fffffff;
	}
	/* 
	 * 然后针对要写入的字节数count，循环执行以下操作，直到数据全部写入。在循环执行过程中，若当前写入数据的块号已大于或等于指定设备的总块数，
	 * 则返回已写字节数并退出。然后再计算再当前处理的数据块中可写入的字节数。如果需要写入的字节数填不满一块，那么就只需写count字节。然后调用
	 * 读块函数breada()读入需要的数据块，并预读下两块数据。若读操作出错，则返回已读字节数。如果没有读入任何字节，则返回出错号，然后将块号递增1，
	 * 为下次操作做准备
	 */
	while (count>0) {
		if (block >= size) {
			return read ? read : -EIO;
		}
		chars = BLOCK_SIZE - offset;
		if (chars > count) {
			chars = count;
		}
		if (!(bh = breada(dev, block, block+1, block+2, -1))) {
			return read ? read : -EIO;
		}
		block++;
		/*
		 * 接着先把指针p指向读出数据的缓冲块中开始读入数据的位置处。若最后一次循环读操作的数据不足一块，则需从块开始处读取所需的字节。
		 * 因此这里需预先设置offset为零。此后将文件中便宜指针pos前移此次要读的字节数chars，并累加这些要读的字节数到统计值read中。再把
		 * 还需要读的计数值count减去此次要写的字节数chars。然后我们从高速缓冲块中p指向的开始读的位置处复制chars个字符到用户缓冲区中，
		 * 同时把用户缓冲区指针前移。本次复制完成后就释放该缓冲块
		 */
		p = offset + bh->b_data;
		offset = 0;
		*pos += chars;
		read += chars;
		count -= chars;
		while (chars-->0) {
			put_fs_byte(*(p++), buf++);
		}
		brelse(bh);
	}
	return read;
}
