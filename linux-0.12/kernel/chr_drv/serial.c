/*
 *  linux/kernel/serial.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	serial.c
 *
 * This module implements the rs232 io functions
 *	void rs_write(struct tty_struct * queue);
 *	void rs_init(void);
 * and all interrupts pertaining to serial IO.
 */
/*
 * serial.c
 * 该程序用于实现rs232的输入输出函数
 * void rs_write(struct tty_struct * queue);
 * void rs_init(void);
 * 以及与串行IO有关的所有中断处理程序
 */

#include <linux/tty.h>						/* tty头文件，定义了有关tty_io，串行通信方面的参数、常数 */
#include <linux/sched.h>					/* 调度程序头文件，定义了任务结构task_struct、任务0数据等 */
#include <asm/system.h>						/* 系统头文件。定义设置或修改描述符/中断门等的嵌入式汇编宏 */
#include <asm/io.h>							/* io头文件。定义硬件端口输入/输出宏汇编语句 */

#define WAKEUP_CHARS (TTY_BUF_SIZE/4)		/* 当写队列中含有WAKEUP_CHARS个字符时就开始发送 */

extern void rs1_interrupt(void);			/* 串行口1的中断处理程序 */
extern void rs2_interrupt(void);			/* 串行口2的中断处理程序 */

/*
 * 初始化串行端口
 * 设置指定串行端口的传输波特率（2400bps）并允许除了写保持寄存器空以外的所有中断源。另外，在输出2字节的波特率因子时，须首先设置线路控制寄存器的DLAB位（位7）
 * 参数：port是串行端口基地址，串口1 - 0x3F8；串口2 - 0x2F8
 */
static void init(int port)
{
	outb_p(0x80,port+3);	/* set DLAB of line control reg */
	outb_p(0x30,port);	/* LS of divisor (48 -> 2400 bps */
	outb_p(0x00,port+1);	/* MS of divisor */
	outb_p(0x03,port+3);	/* reset DLAB */
	outb_p(0x0b,port+4);	/* set DTR,RTS, OUT_2 */
	outb_p(0x0d,port+1);	/* enable all intrs but writes */
	(void)inb(port);	/* read data port to reset things (?) */
}

/*
 * 初始化串行中断程序和串行接口
 * 中断描述符表IDT中的门描述符设置宏set_intr_gate()在include/asm/system.h中实现
 */
void rs_init(void)
{
	/* 下面两句用于设置两个串行口的中断门描述符。rs1_interrupt是串口1的中断处理过程指针。串口1使用的中断是int 0x24，串口2的是int 0x23。 */
	set_intr_gate(0x24,rs1_interrupt);		/* 设置串行口1的中断门向量（IRQ4信号） */
	set_intr_gate(0x23,rs2_interrupt);		/* 设置串行口2的中断门向量（IRQ3信号） */
	init(tty_table[64].read_q->data);		/* 初始化串行口1（.data是端口基地址） */
	init(tty_table[65].read_q->data);		/* 初始化串行口2 */
	outb(inb_p(0x21)&0xE7,0x21);			/* 允许主8259A响应IRQ3、IRQ4中断请求 */
}

/*
 * This routine gets called when tty_write has put something into
 * the write_queue. It must check wheter the queue is empty, and
 * set the interrupt register accordingly
 *
 *	void _rs_write(struct tty_struct * tty);
 */
/* 在tty_write()已将数据放入输出（写）队列时会调用下面的子程序。在该子程序中必须首先检查写队列是否为空，然后设置相应中断寄存器 */
/*
 * 串行数据发送输出
 * 该函数实际上只是开启发送保持寄存器已空中断标志。此后当发送保持寄存器空时，UART就会产生中断请求。而在该串行中断处理过程中，程序会取出些队列尾指针处的字符，并输出到发送
 * 保持寄存器中。一旦UART把该字符发送了出去，发送保持寄存器又会变空而引发中断请求。于是只要写队列中还有字符，系统就会重复这个处理过程，把字符一个一个地发送出去。当写队列中
 * 所有字符都发送了出去，写队列变空了，中断处理程序就会把中断允许寄存器中的发送保持寄存器中断允许标志复位掉，从而再次禁止发送保持寄存器空引发中断请求。此次“循环”发送操作也随之结束
 */
void rs_write(struct tty_struct * tty)
{
	/*
	 * 如果写队列不空，则首先从0x3f9（或0x2f9）读取中断允许寄存器内容，添上发送保持寄存器中断允许标志（位1）后，再写回该寄存器。这样，当发送保持寄存器空时UART就能够因期望获得
	 * 欲发送的字符而引发中断。write_q.data中是串行端口基地址
	 */
	cli();
	if (!EMPTY(tty->write_q)) {
		outb(inb_p(tty->write_q->data+1)|0x02,tty->write_q->data+1);
	}
	sti();
}
