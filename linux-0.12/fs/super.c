/*
 *  linux/fs/super.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * super.c contains code to handle the super-block tables.
 */
/*
 * super.c 程序中含有处理超级块表的代码。
 */
#include <linux/config.h>		/* 内核配置头文件。定义键盘语言和硬盘类型（HD_TYPE）选项 */
#include <linux/sched.h>		/* 调度程序头文件。定义了任务结构task_struct、任务0的数据，还有一些有关描述符参数设置和获取的嵌入式汇编函数宏语句 */
#include <linux/kernel.h>		/* 内核头文件。含有一些内核常用函数的原型定义 */
#include <asm/system.h>			/* 系统头文件。定义了设置或修改描述符/中断门等的嵌入式汇编宏 */

#include <errno.h>				/* 错误号头文件。包含系统中各种出错号。 */
#include <sys/stat.h>			/* 文件状态头文件。含有文件或文件系统状态结构stat{}和常量*/

int sync_dev(int dev);			/* 同步高速缓冲到设备上 */
void wait_for_keypress(void);	/* 等待击键 */

/* set_bit uses setb, as gas doesn't recognize setc */
/* set_bit()使用了setb指令，因为汇编编译器gas不能识别指令setc */

/**
 * 测试指定位偏移处的位的值，并返回该原位值
 * 指令bt用于对位进行测试(Bit Test)。它会把地址addr(%3)和位偏移量bitnr(%2)指定的位的值放入
 * 进位标志CF中。指令setb用于根据进位标志CF设置操作数%al。如果CF=1，则%al=1，否则%al=0。
 * @param[in]	bitnr	位偏移值
 * @param[in]	addr	测试位操作的起始地址
 * @retval		原位值
 */
#define set_bit(bitnr, addr) ({ 											\
	register int __res; 													\
	__asm__("bt %2, %3; setb %%al"											\
			:"=a" (__res)													\
			:"a" (0),"r" (bitnr),"m" (*(addr))); 							\
	__res; })

struct super_block super_block[NR_SUPER];	/* 超级块结构表数组 */
/* this is initialized in init/main.c */ 	/* ROOT_DEV已在init/main.c中被初始化 */
int ROOT_DEV = 0;	/* 根文件系统设备号 */

/* 以下3个函数(lock_super()，free_super()和wait_on_super())的作用与inode.c文件中头3个函
 数的作用相同，只是这里操作的对象换成了超级块 */

/**
 * 锁定超级块
 * 如果超级块已被锁定，则将当前任务置为不可中断的等待状态，并添加到该超级块等待队列s_wait中，
 * 直到该超级块解锁并明确地唤醒本任务。然后对其上锁。
 * 这三个函数（lock_super()、free_super()和wait_on_super()）的作用与inode.c程序文件中
 * 头3个函数的作用雷同，只是这里操作的对象换成了超级块
 * @param[in]	sb		超级块指针
 * @retval		void
 */
static void lock_super(struct super_block *sb)
{
	cli();							/* 关中断 */
	while (sb->s_lock) {			/* 如果该超级块已经上锁，则睡眠等待 */
		sleep_on(&(sb->s_wait));
	}
	sb->s_lock = 1;					/* 给该超级块加锁（置锁定标志） */
	sti();							/* 开中断 */
}

/**
 * 解锁指定超级块
 * 复位超级块的锁定标志，并明确地唤醒等待在此超级块等待队列s_wait上所有进程
 * 如果使用ulock_super这个名称则可能更妥帖
 * @param[in]	sb		超级块指针
 * @retval		void
 */
static void free_super(struct super_block * sb)
{
	cli();
	sb->s_lock = 0;					/* 复位锁定标志 */
	wake_up(&(sb->s_wait));			/* 唤醒等待该超级块的进程 */
	sti();
}

/**
 * 睡眠等待超级块解锁
 * 如果超级块已被锁定，则将当前任务置为不可中断的等待状态，并添加到该超级块的等地队列
 * s_wait中。直到该超级块解锁并明确地唤醒本任务
 * @param[in]	sb		超级块指针
 * @retval		void
 */
static void wait_on_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock) {
		sleep_on(&(sb->s_wait));	/* 如果超级块已经上锁，则睡眠等待 */
	}
	sti();
}

/**
 * 取指定设备dev的超级块结构体指针
 * 在超级块表(数组)中搜索指定设备dev的超级块结构信息。若找到则返回超级块的指针，否则返回空指针。
 * @param[in]	dev		设备号
 * @retval		超级块指针
 */
struct super_block * get_super(int dev)
{
	struct super_block *s;			/* s是超级块数据结构指针 */
	/*
	 * 首先判断参数所给出设备的有效性。若设备号为0则返回空指针。然后让s指向超级块数组起始处，
	 * 开始搜索整个超级块数组，以寻找指定设备dev的超级块。s = 0 + super_block 等同于
	 * s = super_block、s = &super_block[0]
	 */
	/* 若设备号为0，则返回空指针 */
	if (!dev) {
		return NULL;
	}
	s = 0 + super_block;
	while (s < NR_SUPER + super_block) {
		/*
		 * 如果当前搜索项是指定设备的超级块，即该超级块的设备号字段值与函数参数指定的相同，
		 * 则先等待该超级块解锁（若已被其他进程上锁的话）。在等待期间，该超级块项有可能被
		 * 其他设备使用，因此等待返回之后需要再判断一次是否是指定设备的超级块。如果是则返回
		 * 该超级块的指针。否则就重新对超级块再搜索一遍，因此此时s需要又指向超级块数组开始处
		 */
		if (s->s_dev == dev) {
			wait_on_super(s);
			/* 在等待期间，该超级块项有可能被其他设备使用，因此等待返回后需再判断一次 */
			if (s->s_dev == dev) {
				return s;
			}
			s = 0 + super_block;
		} else {
			/* 如果当前搜索项不是，则检查下一项。如果没有找到指定的超级块，则返回空指针 */
			s++;
		}
	}
	return NULL;
}

/**
 * 释放指定设备dev的超级块
 * 释放设备所使用的超级块数组项(置s_dev = 0)，并释放该设备i节点位图和逻辑块位图所占用的高速缓
 * 冲块。如果超级块对应的文件系统是根文件系统，或者其某个i节点上已经安装了其他的文件系统，则不能
 * 释放该超级块。
 * @param[in]	dev		设备号
 * @retval		void
 */
void put_super(int dev)
{
	struct super_block * sb;
	int i;
	/*
	 * 首先判断参数的有效性和合法性。如果指定设备是根文件系统设备，则显示警告信息“根系统盘改变，准备生死决战吧”
	 * 并返回。然后在超级块表中寻找指定设备号的文件系统超级块。如果该超级块指明该文件系统所安装到的i节点还没有
	 * 被处理过，则显示警告信息并返回。在文件系统卸载（umount）操作中，s_imount会先被置成null以后才会调用
	 * 本函数
	 */
	/* 根文件系统设备的超级块不能被释放 */
	if (dev == ROOT_DEV) {
		printk("root diskette changed: prepare for armageddon\n\r");
		return;
	}
	if (!(sb = get_super(dev))) {
		return;
	}
	/* 超级块对应的文件系统的某个i节点上已经安装了其他的文件系统，则不能释放 */
	if (sb->s_imount) {
		printk("Mounted disk changed - tssk, tssk\n\r");
		return;
	}
	/*
	 * 然后找到指定设备的超级块之后，我们先锁定该超级块，再置该超级块对应的设备号字段s_dev为0，也即释放该设备上的
	 * 文件系统超级块。然后释放该超级块占用的其他内核资源，即释放该设备上文件系统i节点位图和逻辑块位图在缓冲区中所占
	 * 用的缓冲块。下面常数符号I_MAP_SLOTS和Z_MAP_SLOTS均等于8，用于分别指明i节点位图和逻辑块位图占用的磁盘逻辑
	 * 块数。注意，若这些缓冲块内容被修改过，则需要同步操作才能把缓冲块中的数据写入设备中。函数最后对该超级块解锁
	 * 并返回
	 */
	lock_super(sb);
	sb->s_dev = 0;	/* 置超级块空闲 */
	/* 释放该设备上文件系统i节点位图和逻辑位图在缓冲区中所占用的缓冲块 */
	for(i = 0; i < I_MAP_SLOTS; i++) {
		brelse(sb->s_imap[i]);
	}
	for(i = 0; i < Z_MAP_SLOTS; i++) {
		brelse(sb->s_zmap[i]);
	}
	free_super(sb);
	return;
}

/**
 * 读取指定设备的超级块
 * 如果指定设备dev上的文件系统超级块已经在超级块表中，则直接返回该超级块项的指针。否则就从设备
 * dev上读取超级块到缓冲块中，并复制到超级块表中，并返回超级块指针。
 * @param[in]	dev		设备号
 * @retval		超级块指针
 */
static struct super_block * read_super(int dev)
{
	struct super_block * s;
	struct buffer_head * bh;
	int i, block;

	/* 
	 * 首先判断参数的有效性，然后检查该设备是否已更换过盘片（也即是否是软盘设备）。如果更换过盘。
	 * 则高速缓冲区有关该设备的所有缓冲块均失效，需要进行失效处理，即释放原来加载的文件系统
	 */
	if (!dev) {
		return NULL;
	}
	/* 检查软盘是否更换 */
	check_disk_change(dev);

	/* 
	 * 如果设备的超级块已经在超级块表中，则直接返回该超级块的指针。否则就在超级块数组中找出一个空闲
	 * 项（也即字段s_dev=0的项）。如果数组已经占满则返回空指针
	 */
	if ((s = get_super(dev))) {
		return s;
	}
	/* 在超级块数组中找到空项用于要读取的超级块 */
	for (s = 0 + super_block ;; s++) {
		if (s >= NR_SUPER + super_block) {
			return NULL;
		}
		if (!s->s_dev) {
			break;
		}
	}
	/*
	 * 在超级块数组中找到空闲项之后，就将该超级块项用于指定设备dev上的文件系统。于是对该超级块结构中字段
	 * 进行部分初始化处理
	 */
	s->s_dev = dev;		/* 用于dev设备上的文件系统 */
	s->s_isup = NULL;
	s->s_imount = NULL;
	s->s_time = 0;
	s->s_rd_only = 0;
	s->s_dirt = 0;
	/*
	 * 然后锁定该超级块，并从设备上读取超级块信息到bh指向的缓冲块中。超级块位于设备的第2个逻辑块（1号块）中，
	 * （第1个是引导盘块）。如果读超级块操作失败，则释放上面选定的超级块数组中的项（即置s_dev=0），并解锁
	 * 该项，返回空指针退出。否则就将读取的超级块信息从缓冲块数据区复制到超级块数组相应项结构中，并释放存放
	 * 读取信息的高级缓冲块
	 */
	/* 从设备上读取超级块信息到bh指向的缓冲块中，再从缓冲块复制到超级块数组中 */
	lock_super(s);
	if (!(bh = bread(dev, 1))) {
		s->s_dev = 0;
		free_super(s);
		return NULL;
	}
	*((struct d_super_block *) s) = *((struct d_super_block *) bh->b_data);
	brelse(bh);

	/*
	 * 现在我们从设备dev上得到了文件系统的超级块，于是开始检查该超级块的有效性，并从设备上读取i节点位图和逻辑
	 * 块位图等信息。如果所读取的超级块的文件系统魔数字段不对，说明设备上不是正确的文件系统，因此同上面一样，释放
	 * 上面选定的超级块数组中的项，并解锁该项，返回空指针退出
	 */
	if (s->s_magic != SUPER_MAGIC) { /* linux0.12只支持MINIX文件系统1.0，魔数为0x137f */
		s->s_dev = 0;
		free_super(s);
		return NULL;
	}
	/*
	 * 下面开始读取设备上i节点位图和逻辑块位图数据。首先初始化内存超级块结构中位图空间。然后从设备上读取i节点位图和
	 * 逻辑块位图信息，并存放在超级块对应字段中。i节点位图保存在设备上2号块开始的逻辑块中，共占用s_imap_blocks个块。
	 * 逻辑位图在i节点位图所在块的后续块中，共占用s_zmap_blocks个块
	 */
	/* 读取设备上i节点位图和逻辑块位图数据 */
	for (i = 0; i < I_MAP_SLOTS; i++) {		/* 初始化i节点位图和逻辑块位图 */
		s->s_imap[i] = NULL;
	}
	for (i = 0; i < Z_MAP_SLOTS; i++) {
		s->s_zmap[i] = NULL;
	}
	block = 2; /* 0为引导块，1为超级块，2～x为i节点位图，(x+1)~y为逻辑块位图 */
	for (i = 0 ; i < s->s_imap_blocks ; i++) {	/* 读取设备中i节点位图 */
		if ((s->s_imap[i] = bread(dev, block))) {
			block++;
		} else {
			break;
		}
	}
	for (i = 0 ; i < s->s_zmap_blocks ; i++) {	/* 读取设备中逻辑块位图 */
		if ((s->s_zmap[i] = bread(dev, block))) {
			block++;
		} else {
			break;
		}
	}
	/* 如果读出的位图个数不等于位图应该占有的逻辑块数，说明文件系统位图信息有问题，超级块初始
	 化失败，则释放所有资源 */
	/*
	 * 即释放i节点位图和逻辑块位图占用的高速缓冲块、释放上面选定的超级块数组项、解锁该超级块项，并返回空指针退出
	 */
	if (block != 2 + s->s_imap_blocks + s->s_zmap_blocks) {
		for(i = 0; i < I_MAP_SLOTS; i++) {		/* 释放位图占用的高速缓冲块 */
			brelse(s->s_imap[i]);
		}
		for(i = 0; i < Z_MAP_SLOTS; i++) {
			brelse(s->s_zmap[i]);
		}
		s->s_dev = 0;		/* 释放选定的超级块数组项 */
		free_super(s);		/* 解锁该超级块项 */
		return NULL;
	}
	/* 0号i节点和0号逻辑块不可用 */
	s->s_imap[0]->b_data[0] |= 1;
	s->s_zmap[0]->b_data[0] |= 1;
	free_super(s);
	return s;
}

/**
 * 卸载文件系统
 * @param[in]	dev_name	文件系统所在设备的设备文件名
 * @retval		成功返回0，失败返回出错码
 */
int sys_umount(char * dev_name)
{
	struct m_inode * inode;
	struct super_block * sb;
	int dev;

	if (!(inode = namei(dev_name))) {
		return -ENOENT;
	}
	dev = inode->i_zone[0];	/* 对于设备文件，i_zone[0]存有设备号 */
	if (!S_ISBLK(inode->i_mode)) { /* 文件系统应该在块设备上 */
		iput(inode);
		return -ENOTBLK;
	}
	iput(inode);
	if (dev == ROOT_DEV) {	/* 根文件系统不能被卸载 */
		return -EBUSY;
	}
	/* 在超级块表中没有找到该设备的超级块，或者已找到但是该设备上文件系统没有安装过 */
	if (!(sb = get_super(dev)) || !(sb->s_imount)) {
		return -ENOENT;
	}
	/* 如果超级块所指明被安装到的i节点并没有置位其安装标志i_mount，则显示警告信息 */
	if (!sb->s_imount->i_mount) {
		printk("Mounted inode has i_mount=0\n");
	}
	for (inode = inode_table + 0 ; inode < inode_table + NR_INODE ; inode++) {
		/* 有进程在使用该设备上的文件，则返回忙出错码 */
		if (inode->i_dev == dev && inode->i_count) {
			return -EBUSY;
		}
	}
	/* 开始卸载操作 */
	sb->s_imount->i_mount = 0; /* 复位被安装到的i节点的安装标志，释放该i节点 */
	iput(sb->s_imount);
	sb->s_imount = NULL;
	iput(sb->s_isup);	/* 设备文件系统的根i节点，接着置超级块中被安装系统根i节点指针为空 */
	sb->s_isup = NULL;
	/* 释放该设备上的超级块以及位图占用的高速缓冲块，同步高速缓冲到设备 */
	put_super(dev);
	sync_dev(dev);
	return 0;
}

/**
 * 安装文件系统 
 * @param[in]	dev_name	设备文件名
 * @param[in]	dir_name	安装到的目录名
 * @param[in]	rw_flag		被安装文件系统的可读写标志
 * @retval		成功返回0，失败返回出错号
 */
int sys_mount(char *dev_name, char *dir_name, int rw_flag)
{
	struct m_inode *dev_i, *dir_i;
	struct super_block * sb;
	int dev;

	/* 检查设备名是否有效 */
	if (!(dev_i = namei(dev_name))) {
		return -ENOENT;
	}
	dev = dev_i->i_zone[0];	/* 对于设备文件，i_zone[0]存有设备号 */
	if (!S_ISBLK(dev_i->i_mode)) {	/* 文件系统应该在块设备上 */
		iput(dev_i);
		return -EPERM;
	}
	iput(dev_i);

	/* 检查一下文件系统安装到的目录名是否有效 */
	if (!(dir_i = namei(dir_name))) {
		return -ENOENT;
	}
	/* 如果该i节点的引用计数不为1（仅在这里引用），或者该i节点的节点号是根文件系统的节点号 */
	if (dir_i->i_count != 1 || dir_i->i_num == ROOT_INO) {
		iput(dir_i);
		return -EBUSY;
	}
	if (!S_ISDIR(dir_i->i_mode)) {	/* 安装点需要是一个目录名 */
		iput(dir_i);
		return -EPERM;
	}
	/* 读取要安装文件系统的超级块信息 */
	if (!(sb = read_super(dev))) {
		iput(dir_i);
		return -EBUSY;
	}
	if (sb->s_imount) { /* 被安装的文件系统已经安装在其他地方 */
		iput(dir_i);
		return -EBUSY;
	}
	if (dir_i->i_mount) { /* 将要安装到的i节点已经安装了文件系统 */
		iput(dir_i);
		return -EPERM;
	}
	/* 设置被安装文件系统超级块的“被安装到i节点”字段指向安装到的目录名的i节点 */
	sb->s_imount = dir_i;
	dir_i->i_mount = 1;
	dir_i->i_dirt = 1;	/* NOTE! we don't iput(dir_i) */
						/* 注意！这里没有用iput(dir_i) */
	return 0;			/* we do that in umount */
						/* 这将在umount内操作 */
}

/**
 * 安装根文件系统
 * 函数首先初始化文件表数组file_table[]和超级块表(数组)，然后读取根文件系统超级块，并取得文
 * 件系统根i节点。最后统计并显示出根文件系统上的可用资源(空闲块数和空闲i节点数0。该函数会在系
 * 统开机进行初始化设置时(sys_setup())调用(blk_drv/hd.c) 
 * @retval		void
 */
void mount_root(void)
{
	int i, free;
	struct super_block * p;
	struct m_inode * mi;

	/* 若磁盘i节点结构不是32字节，则出错停机 */
	if (32 != sizeof (struct d_inode)) {
		panic("bad i-node size");
	}
	/* 初始化系统中的文件表数组 */
	for(i = 0; i < NR_FILE; i++) {
		file_table[i].f_count = 0;
	}
	if (MAJOR(ROOT_DEV) == 2) {		/* 根文件系统所在设备是软盘，提示插入根文件系统盘 */
		printk("Insert root floppy and press ENTER\n\r");
		wait_for_keypress();
	}
	/* 初始化超级块表 */
	for(p = &super_block[0]; p < &super_block[NR_SUPER]; p++) {
		p->s_dev = 0;
		p->s_lock = 0;
		p->s_wait = NULL;
	}
	/* 做好以上"份外"的初始化工作之后，我们开始安装根文件系统 */
	/* 从根设备上读取文件系统超级块，并取得文件系统的根i节点(1号节点)在内存i节点表中的指针 */
	if (!(p = read_super(ROOT_DEV))) {
		panic("Unable to mount root");
	}
	if (!(mi = iget(ROOT_DEV, ROOT_INO))) {	/* 在fs.h中ROOT_INO定义为1 */
		panic("Unable to read root i-node");
	}
	mi->i_count += 3 ;	/* NOTE! it is logically used 4 times, not 1 */
                        /* 注意！从逻辑上讲，它已被引用了4次，而不是1次 */
	p->s_isup = p->s_imount = mi; /* 置被安装文件系统i节点和被安装到i节点字段为该i节点 */
	current->pwd = mi;	/* 设置当前进程的当前工作目录和根目录i节点 */
	current->root = mi;
	/* 对根文件系统上的资源作统计工作 */
	/* 统计该设备上空闲块数 */
	free = 0;
	i = p->s_nzones;
	while (-- i >= 0) {
		if (!set_bit(i & 8191, p->s_zmap[i >> 13]->b_data)) {
			free++;
		}
	}
	printk("%d/%d free blocks\n\r", free, p->s_nzones);
	/* 统计设备上空闲i节点数 */
	free = 0;
	i = p->s_ninodes + 1;
	while (-- i >= 0) {
		if (!set_bit(i & 8191, p->s_imap[i >> 13]->b_data)) {
			free++;
		}
	}
	printk("%d/%d free inodes\n\r", free, p->s_ninodes);
}
