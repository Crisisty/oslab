/*
 *  linux/fs/file_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>				/* 错误号头文件。包含系统中各种出错号。 */
#include <fcntl.h>				/* 文件控制头文件。文件及其描述符的操作控制常数符号的定义。*/

#include <linux/sched.h>		/* 调度程序头文件。定义了任务结构task_struct、任务0的数据等 */
#include <linux/kernel.h>		/* 内核头文件。含有一些内核常用函数的原型定义 */
#include <asm/segment.h>		/* 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数 */

#define MIN(a,b) (((a)<(b))?(a):(b))		/* 取a,b中的最小值 */
#define MAX(a,b) (((a)>(b))?(a):(b))		/* 取a,b中的最大值 */

/**
 * 文件读函数
 * 根据i节点和文件结构，读取文件中数据。
 * @param[in]	*inode	i节点
 * @param[in]	*filp	
 * @param[in]	buf		指定用户空间中缓冲区的位置
 * @param[in]	count	需要读取的字节数
 * @retval		实际读取的字节数，或出错号(小于0)
*/
int file_read(struct m_inode * inode, struct file * filp, char * buf, int count)
{
	int left, chars, nr;
	struct buffer_head * bh;

	/*
	 * 首先判断参数的有效性。若需要读取的字节计数count小于等于零，则返回0。若还需要读取的字节数不等于0，就循环
	 * 执行下面操作，直到数据全部读出或遇到问题。在读循环操作过程中，我们根据i节点和文件表结构信息，并利用bmap()
	 * 得到包含文件当前读写位置的数据库在设备上对应的逻辑块号nr。若nr不为0，则从设备上读取该逻辑块。如果读操作失败
	 * 则退出循环。若nr为0，表示指定的数据库不存在，置缓冲块指针为NULL。（filp->f_pos）/BLOCK_SIZE用于计算出
	 * 文件当前指针所在数据块号
	 */
	if ((left = count) <= 0) {
		return 0;
	}
	while (left) {
		if ((nr = bmap(inode, (filp->f_pos)/BLOCK_SIZE))) {
			if (!(bh = bread(inode->i_dev, nr))) {
				break;
			}
		} else {
			bh = NULL;
		}
		/*
		 * 接着我们计算文件读写指针在数据块中的偏移值nr，则在该数据块中我们希望读取的字节数为（BLOCK_SIZE - nr）
		 * 然后和现在还需读取的字节数left作比较，其中小值即为本次操作需读取的字节数chars。如果（BLOCK_SIZE - nr）
		 * > left，则说明该块是需要读取的最后一块数据，反之则还需要读取下一块数据。之后调整读写文件指针。指针前移此次
		 * 将读取的字节数chars。剩余字节计数left相应减去chars
		 */
		nr = filp->f_pos % BLOCK_SIZE;
		chars = MIN( BLOCK_SIZE-nr, left );
		filp->f_pos += chars;
		left -= chars;
		/*
		 * 若上面从设备上读取到了数据，则将p指向缓冲块中开始读取数据的位置，并且赋值chars字节到用户缓冲区buf中。否则
		 * 往用户缓冲区中填入chars个0值字节
		 */
		if (bh) {
			char * p = nr + bh->b_data;
			while (chars-- > 0) {
				put_fs_byte(*(p++), buf++);
			}
			brelse(bh);
		} else {
			while (chars-- > 0) {
				put_fs_byte(0, buf++);
			}
		}
	}
	/*
	 * 修改该i节点的访问时间为当前时间，返回读取的字节数。若读取字节数为0，则返回出错号。CURRENT_TIME是定义在
	 * include/linux/sched.h的宏，用于计算UNIX时间。即从1970年1月1日0时0分0秒开始，到当前的时间，单位是秒
	 */
	inode->i_atime = CURRENT_TIME;
	return (count-left) ? (count-left) : -ERROR;
}

/**
 * 文件写函数
 * 根据i节点和文件结构信息，将用户数据写入文件中。
 * @param[in]	*inode		i节点指针
 * @param[in]	*filp		文件结构指针
 * @param[in]	buf			指定用户态中缓冲区的位置
 * @param[in]	count		需要写入的字节数
 * @retval		成功返回实际写入的字节数，失败返回出错号(小于0)
 */
int file_write(struct m_inode * inode, struct file * filp, char * buf, int count)
{
	off_t pos;
	int block, c;
	struct buffer_head * bh;
	char * p;
	int i = 0;

/*
 * ok, append may not work when many processes are writing at the same time
 * but so what. That way leads to madness anyway.
 */
 /* ok，许多多线程同时写时，append操作可能不行，但那又怎样。不管怎样那样做会导致混乱一团 */
	/*
	 * 首先确定数据写入文件的位置。如果是要向文件后添加数据，则将文件读写指针移到文件尾部。否则
	 * 就将在文件当前读写指针处写入
	 */
	if (filp->f_flags & O_APPEND) {	/* 指定以追加方式，则将pos置文件尾 */
		pos = inode->i_size;
	} else {
		pos = filp->f_pos;
	}
	/*
	 * 然后在已写入字节数i（刚开始时为0）小于指定写入字节数count时，循环执行以下操作。在循环操作过程中，
	 * 我们先取文件数据块号（pos/BLOCK_SIZE）在设备上对应的逻辑块号block。如果对应的逻辑块不存在就创建一块。
	 * 如果得到的逻辑块号=0，则表示创建失败，于是退出循环。否则我们根据该逻辑块号读取设备上的相应逻辑块，若出错
	 * 也退出循环
	 */
	while (i < count) {
		/* 获取逻辑块号 */
		if (!(block = create_block(inode, pos/BLOCK_SIZE))) {
			break;
		}
		/* 读取指定数据块 */
		if (!(bh = bread(inode->i_dev, block))) {
			break;
		}
		/*
		 * 此时缓冲块指针bh正指向刚读入的文件数据块。现在再求出文件当前读写指针在该数据块中的偏移值c,
		 * 并将指针p指向缓冲块中开始写入数据的位置，并置该缓冲块已修改标志。对于块中当前指针，从开始读写
		 * 位置到块末共可写入c=(BLOCK_SIZE-c)个字节。若c大于剩余还需写入的字节数（count-i），则此次
		 * 只需再写入c=(count-i)个字节即可
		 */
		c = pos % BLOCK_SIZE;
		p = c + bh->b_data;
		bh->b_dirt = 1;
		c = BLOCK_SIZE - c;
		if (c > count - i) {
			c = count - i;
		}
		/*
		 * 在写入数据之前，我们先预先设置好下一次循环操作要读写文件中的位置。因此我们把pos指针前移此次需写入的字节数。
		 * 如果此时pos位置值超过了文件当前长度，则修改i节点中文件长度字段，并置i节点已修改标志。然后把此次要写入的字节数
		 * c累加到已写入字节计数值i中，供判断使用。接着从用户缓冲区buf中复制c个字节到高速缓存块块中p指向的开始位置处，
		 * 复制完后就释放该缓冲块
		 */
		pos += c;
		if (pos > inode->i_size) {
			inode->i_size = pos;
			inode->i_dirt = 1;
		}
		i += c;
		while (c-- > 0) {
			*(p++) = get_fs_byte(buf++);
		}
		brelse(bh);
	}
	/*
	 * 当数据已经全部写入文件或者在写操作过程中发生问题时就会退出循环。此时我们更改文件修改时间为当前时间，并调整
	 * 文件读写指针。如果此次操作不是在文件尾添加数据，则把文件读写指针调整到当前读写位置pos处，并更改文件i节点
	 * 的修改时间为当前时间。最后返回写入的字节数，若写入字节数为0，则返回出错号-1
	 */
	inode->i_mtime = CURRENT_TIME;
	if (!(filp->f_flags & O_APPEND)) {
		filp->f_pos = pos;
		inode->i_ctime = CURRENT_TIME;
	}
	return (i ? i : -1);
}
