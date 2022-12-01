/*
 *  linux/kernel/fork.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also system_call.s), and some misc functions ('verify_area').
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */
/*
 * 'fork.c'中含有系统调用'fork'的辅助子程序，以及一些其他函数('verify_area')。一旦你了解了
 * fork，就会发现它非常简单的，但内存管理却有些难度。
 */

#include <errno.h>          /* 错误号头文件。包含系统中各种出错号。*/

#include <linux/sched.h>    /* 调度程序头文件。定义了任务结构task_struct、任务0数据等 */
#include <linux/kernel.h>   /* 内核头文件。含有一些内核常用函数的原型定义 */
#include <asm/segment.h>    /* 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数 */
#include <asm/system.h>     /* 系统头文件。定义了设置或修改描述符/中断门等的嵌入式汇编宏 */

/* 写页面验证。若页面不可写，则复制页面（mm/memory.c） */
extern void write_verify(unsigned long address);

/* 最新进程号，其值会由get_empty_process()生成，会不断增加，无上限；系统同时容纳的最多任务
 数有上限（NR_TASKS = 64） */
static long last_pid = 0;	

/*
 * 对于80386 CPU，在执行特权级0代码时不会理会用户空间中的页面是否页保护的。因此在执行内核代码时用户空间中数据页面保护标志不起作用，写时复制机制也就失去了作用
 * verify_area()函数正是用于解决这个问题。但对应80486或后来的CPU，其控制寄存器CR0中有一个写保护标志WP（位16），内核可以通过设置该标志来禁止特权级0的代码向
 * 用户空间只读页面执行写数据，从而486以上CPU可以通过设置改标志来达到使用本函数同样的目的
 * 该函数对当前进程逻辑地址从addr到addr+size这一段范围执行写操作前的检测操作。由于检测试以页面为单位进行操作，因此程序首先需要找出addr所在页面开始地址start
 * 然后start加上进程数据段基址，使这个start变换成CPU 4G线性空间中的地址，最后循环调用write_verify()对指定大小的内存空间进行写前验证。若页面是只读的，则执行
 * 共享检验和复制页面操作（写时复制）
 */
/**
 * 进程空间区域的写前验证
 * 对于80386 CPU，在执行内核代码时用户空间中的R/W标志起不了作用，写时复制机制失效了。所以
 * 我们得手动做这个写前验证。
 * @param[in]		addr	需要写验证的逻辑地址起始位置
 * @param[in]		size	需要写验证的长度（单位为字节）
 * @retval			void
 */
void verify_area(void * addr, int size)
{
    unsigned long start;

    /*
     * 首先将起始地址start调整为其所在页面开始位置，同事相应地调整验证区域大小size。下句中的start&0xfff用来获得指定其实位置addr在页面中的偏移值，原验证范围size
     * 加上这个偏移值即扩展成以页面起始位置开始的范围值。因此start &= 0xfffff000行上也需要把验证开始位置start调整成页面边界值
     */
    start = (unsigned long) addr;
    size += start & 0xfff;
    start &= 0xfffff000;                /* 此时start是当前进程空间中的逻辑地址 */
    /* 下面加上进程数据段基址，就把start变成CPU整个线性地址空间中的地址位置。然后循环进行写页面验证。若页面不可写，则复制页面 */
    start += get_base(current->ldt[2]);
    while (size > 0) {
        size -= 4096;
        write_verify(start);
        start += 4096;
    }
}

/**
 * 复制内存页表
 * 该函数为新任务在线性地址空间中设置代码段和数据段基址，限长，并复制页表。由于Linux系统采用写时复制
 * (copy on write)技术，因此这里仅为新进程设置自己的页目录表项和页表项，而没有实际为新进程分配物理内
 * 存页面。此时新进程与其父进程共享所有内存页面。
 * @param[in]		nr		新任务号
 * @param[in]		p		新任务的数据结构指针
 * @retval			成功返回0，失败返回出错号
*/
static int copy_mem(int nr, struct task_struct * p)
{
    unsigned long old_data_base, new_data_base, data_limit;
    unsigned long old_code_base, new_code_base, code_limit;

    /*
     * 首先取当前进程局部描述符表中代码描述符和数据段描述符项中的段限长（字节数）。0x0f是代码段选择符；0x17是数据段选择符。然后取当前进程代码段和数据段在线性地址
     * 空间中的基地址。由于Linux0.12内核还不支持代码和数据段分立的情况，因此这里需要检查代码段和数据段基址是否都相同，并且要求数据段的长度至少不小于代码段的长度，
     * 否则内核显示出错信息，并停止运行
     */
    code_limit = get_limit(0x0f);
    data_limit = get_limit(0x17);
    old_code_base = get_base(current->ldt[1]);
    old_data_base = get_base(current->ldt[2]);
    if (old_data_base != old_code_base) {
        panic("We don't support separate I&D");
    }
    if (data_limit < code_limit) {
        panic("Bad data_limit");
    }
    /*
     * 然后设置新建进程在线性地址空间中的基地址（等于64MB*其任务号），并用该值设置新进程LDT中段描述符中的基地址字段值。接着设置新进程的页目录表项和页表项。
     * 正常情况下copy_page_tables()返回0。否则表示出错，则是否刚申请的页表项
     */
    new_data_base = new_code_base = nr * TASK_SIZE;
    p->start_code = new_code_base;
    set_base(p->ldt[1], new_code_base);
    set_base(p->ldt[2], new_data_base);
    if (copy_page_tables(old_data_base, new_data_base, data_limit)) {
        free_page_tables(new_data_base, data_limit);
        return -ENOMEM;
    }
    return 0;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in it's entirety.
 */
/*
 * OK，下面是主要的fork子程序。它复制系统进程信息(task[n])，并且设置必要的寄存器。它还整个地复制数据段(也是
 * 代码段)。
 */

/**
 * 复制进程
 * sys_call.s中sys_fork会首先调用find_empty_process会更新last_pid，然后压入一些参数，再调用copy_process。
 * @param[in]	nr,ebp,edi,esi,gs               find_empty_process分配的任务数组项号nr，调用copy_process之前
 *                                              入栈的gs，esi，edi，ebp
 * @param[in]   none                            sys_fork函数入栈的返回地址
 * @param[in]	ebx,ecx,edx,orig_eax,fs,es,ds   system_call时入栈的段寄存器ds，es，fs和edx，ecx，ebx
 * @param[in]	eip,cs,eflags,esp,ss            CPU执行中断指令压入的用户栈地址ss和esp，标志eflags和返回地址cs和eip
 * @return      成功返回最新的PID，失败返回错误号
 */
int copy_process(int nr, long ebp, long edi, long esi, long gs, long none,
        long ebx, long ecx, long edx, long orig_eax, 
        long fs, long es, long ds,
        long eip, long cs, long eflags, long esp, long ss)
{
    struct task_struct *p;
    int i;
    struct file *f;
    /* 为新任务数据结构分配内存 */
    /* 
     * 如果分配出错，则返回出错码并退出。然后将新任务结构指针放入任务数组的nr项中。其中nr为任务号，它又前面find_empty_process()返回。接着把当前进程任务结构内容复制
     * 到刚申请到的内存页面p开始处
     */
    p = (struct task_struct *) get_free_page();
    if (!p) {
        return -EAGAIN;
    }
    task[nr] = p;
    *p = *current;	    /* NOTE! this doesn't copy the supervisor stack */  /* 注意，这样做不会复制超级用户堆栈（只复制进程结构） */

    /* 对复制来的进程结构内容进行一些修改。先将新进程的状态置为不可中断等待状态，以防止内核调度其执行 */
    /* 
     * 然后设置新进程的进程号pid，并初始化进程运行时间片值等于其priority值（一般为15个滴答）。接着复位新进程的信号位图、报警定时值、会话（session）领导标志leader、进程及
     * 其子进程在内核和用户态运行时间统计值，还设置进程开始运行的系统时间start_time
     */
    p->state = TASK_UNINTERRUPTIBLE;
    p->pid = last_pid;                  /* 新进程号。也由find_empty_process()得到 */
    p->counter = p->priority;           /* 运行时间片值（滴答数） */
    p->signal = 0;                      /* 信号位图 */
    p->alarm = 0;                       /* 报警定时值（滴答数） */
    p->leader = 0;		/* process leadership doesn't inherit */    /* 进程的领导权是不能继承的 */
    p->utime = p->stime = 0;            /* 用户态时间和核心态运行时间 */
    p->cutime = p->cstime = 0;          /* 子进程用户态和和心态运行时间 */
    p->start_time = jiffies;            /* 进程开始运行时间（当前时间滴答数） */

    /* 修改任务状态段TSS内容 */
    /*
     * 由于系统给任务结构p分配了1页新内存，所以（PAGE_SIZE +（long）p）让esp0正好指向该页顶端。ss0:esp0用作程序在内核态执行时的栈。另外，每个任务在GDT表中
     * 都有两个段描述符，一个是任务的TSS段描述符，另一个是任务的LDT表段描述符。 p->tss.ldt = _LDT(nr)语句就是把GDT的本地任务LDT段描述符的选择符保存到本
     * 任务的TSS段中。当执行任务切换时，CPU会自动从TSS中把LDT段描述符的选择符加载到ldtr寄存器中。
     */
    p->tss.back_link = 0;
    p->tss.esp0 = PAGE_SIZE + (long) p; /* (PAGE_SIZE + (long) p)让esp0正好指向该页顶端 任务内核态栈指针*/
    p->tss.ss0 = 0x10;                  /* 内核态栈的段选择符（与内核数据段相同） */
    p->tss.eip = eip;                   /* 指令代码指针 */
    p->tss.eflags = eflags;             /* 标志寄存器 */
    p->tss.eax = 0;                     /* 这是当fork()返回时新进程会返回0的原因所在 */
    p->tss.ecx = ecx;
    p->tss.edx = edx;
    p->tss.ebx = ebx;
    p->tss.esp = esp;
    p->tss.ebp = ebp;
    p->tss.esi = esi;
    p->tss.edi = edi;
    p->tss.es = es & 0xffff;            /* 段寄存器仅16位有效 */
    p->tss.cs = cs & 0xffff;
    p->tss.ss = ss & 0xffff;
    p->tss.ds = ds & 0xffff;
    p->tss.fs = fs & 0xffff;
    p->tss.gs = gs & 0xffff;
    p->tss.ldt = _LDT(nr);              /* 任务LDT描述符的选择符（LDT描述符在GDT中） */
    p->tss.trace_bitmap = 0x80000000;   /* 高16位有效 */
    /* 当前任务使用了协处理器，就保存其上下文 */
    /*
     * 指令CLTS用于清除控制寄存器CR0中的任务已经交换（TS）标志。每当发生任务切换，CPU都会设置该标志。该标志用于管理数学协处理器：如果该标志置位，那么每个ESC指令
     * 都会被捕获（异常7）。如果协处理器存在标志MP也同时置位的话，那么WAIT指令也会捕获。因此，如果任务切换发生在一个ESC指令开始执行之后，则协处理器中的内容就可能
     * 需要再执行新的ESC指令之前保存起来。捕获处理句柄会保存协处理器的内容并复位TS标志。指令fnsave用于把协处理器的所有状态保存到目的操作数指定的内存区域中（tss.i387）
     */
    if (last_task_used_math == current) {
        __asm__("clts ; fnsave %0 ; frstor %0"::"m" (p->tss.i387));
    }
    /* 复制父进程的内存页表，没有分配物理内存，共享父进程内存 */
    /* 在线性地址空间中设置新任务代码段和数据段描述符中的基地址和限长，并复制页表。如果出错（返回值不是0），则复位任务数组中相应项并释放为该新任务分配的用于任务结构的内存页 */
    if (copy_mem(nr,p)) {       /* 返回不为0表示出错 */
        task[nr] = NULL;
        free_page((long) p);
        return -EAGAIN;
    }
    /* 修改打开的文件，当前工作目录，根目录，执行文件，被加载库文件的使用数 */
    /* 
     * 因为新创建的子进程与父进程共享打开着的文件，所以父进程若有打开着的文件，则需将对应文件的打开次数增1，同样道理，
     * 也需要把当前进程（父进程）的pwd、rot和executable这些i节点的引用次数都增1 
     */
    for (i = 0; i < NR_OPEN; i++) {
        if ((f = p->filp[i])) {
            f->f_count ++;
        }
    }
    if (current->pwd) {
        current->pwd->i_count++;
    }
    if (current->root) {
        current->root->i_count++;
    }
    if (current->executable) {
        current->executable->i_count++;
    }
    if (current->library) {
        current->library->i_count++;
    }

    /* 在GDT表中设置任务状态段描述符TSS和局部表描述符LDT */
    /*
     * 这两个段的限长均被设置成104字节。然后设置进程之间的关系链表指针，即把新进程插入到当前进程的子进程链表中。把新进程的父进程设置为当前进程，把新进程的最新子进程指针p_cptr
     * 和年轻兄弟进程指针p_ysptr置空。接着让新进程的老兄进程指针p_osptr设置等于负进程的最新子进程指针。若当前进程却是还有其他子进程，则让比邻老兄进程的最年轻进程指针p_ysptr指向
     * 新进程。最后把当前进程的最新子进程指针指向这个新进程。然后把新进程设置成就绪状态。最后返回新进程号。另外，set_tss_desc()和set_ldt_desc()定义在include/asm/system.h文件中
     * “gdt+(nr<<1)+FIRST_TSS_ENTRY"是任务nr的TSS描述符项在全局表中的地址，因为每个任务占用GDT表中2项，因此上式中要包括’(nr<<1)‘。请注意，在任务切换时，任务寄存器TR会由CPU自动加载
     */
    set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY, &(p->tss));
    set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY, &(p->ldt));

    /* 设置子进程的进程指针 */
    p->p_pptr = current;            /* 设置新进程的父进程指针 */
    p->p_cptr = 0;                  /* 复位新进程的最新子进程指针 */
    p->p_ysptr = 0;                 /* 复位新进程的比邻年轻兄弟进程指针 */
    p->p_osptr = current->p_cptr;   /* 设置新进程的比邻老兄兄弟进程指针 */
    if (p->p_osptr) {               /* 若新进程有老兄兄弟进程，则将其年轻进程兄弟指针指向新进程 */
        p->p_osptr->p_ysptr = p;
    }
    current->p_cptr = p;            /* 让当前进程最新子进程指针指向新进程 */

    p->state = TASK_RUNNING;	/* do this last, just in case */

    return last_pid;
}

/**
 * 取得不重复的进程号last_pid 函数返回在任务数组中的任务号（数组项）
 * 由sys_fork调用为新进程取得不重复的进程号last_pid，并返回第一个空闲的任务结构数组索引号
 * @param[in]   void
 * @retval      成功返回在任务数组中的任务号(数组项)，失败返回错误号
 */
int find_empty_process(void)
{
    int i;

    /*
     * 首先获取新的进程号。如果last_pid增1后超出进程号的正数表示范围，则重新从1开始使用pid号。然后在任务数组中搜索刚设置的pid号是否已经被任何任务使用。
     * 如果是则跳转到函数开始处重新获得一个pid号。接着在任务数组中为新任务寻找一个空闲项，并返回项号。last_pid是一个全局变量，不用返回。如果此时任务数组
     * 中64个项已经被全部占用，则返回出错码
     */
    repeat:
        if ((++last_pid) < 0) {
            last_pid = 1;
        }
        for(i = 0; i < NR_TASKS; i++) {
            if (task[i] && ((task[i]->pid == last_pid) ||
                        (task[i]->pgrp == last_pid))) {
                goto repeat;
            }
        }
    for(i = 1; i < NR_TASKS; i++) {        /* 任务0项呗排除在外 */
        if (!task[i]) {
            return i;
        }
    }
    return -EAGAIN;
}
