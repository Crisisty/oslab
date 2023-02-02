/*
 * linux/kernel/math/error.c
 *
 * (C) 1991 Linus Torvalds
 */

#include <signal.h>										/* 信号头文件。定义信号符号常量，信号结构及信号操作函数原型 */

#include <linux/sched.h>								/* 调度程序头文件。定义了任务结构task_struct、任务0数据等 */

/*
 * 协处理器错误中断int 16调用的处理函数
 * 下面代码用于处理协处理器发出的出错信号。它让80387清除状态字中所有异常标志位和忙位。若进程上次使用过协处理器，则设置协处理器出错信号。返回后将跳转到系统调用
 * 中断代码的中断返回处ref_from_sys_call处继续执行
 */
void math_error(void)
{
	__asm__("fnclex");									/* 清除状态字中所有异常标志位和忙位 */
	if (last_task_used_math)							/* 若用过协处理器，则设置其出错信号 */
		last_task_used_math->signal |= 1<<(SIGFPE-1);
}
