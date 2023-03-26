/*
 *  linux/lib/setsid.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>     /* Linux标准头文件。定义了各种符号常数和类型，并声明了各种函数。若定义了__LIBRARY__，则含有系统调用号和内嵌汇编syscal0()等 */

/** 
 * 创建一个会话并设置进程组号
 * pid_t setsid()
 * @retval	调用进程的会话标识符(session ID)
 */
_syscall0(pid_t, setsid)
