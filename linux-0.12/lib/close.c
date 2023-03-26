/*
 *  linux/lib/close.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>     /* Linux标准头文件。定义了各种符号常数和类型，并声明了各种函数。若定义了__LIBRARY__，则含有系统调用号和内嵌汇编syscal0()等 */

/** 
 * 关闭文件
 * 下面该宏对应函数原型：int close(int fd)。它直接调用系统中断int 0x80，参数是__NR_close。其中fd是文件描述符
 * @param[in]	fd		要关闭的文件描述符
 * @retval		成功返回0，失败返回-1。
 */
_syscall1(int, close, int, fd)
