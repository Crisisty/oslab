/*
 *  linux/lib/dup.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>     /* Linux标准头文件。定义了各种符号常数和类型，并声明了各种函数。若定义了__LIBRARY__，则含有系统调用号和内嵌汇编syscal0()等 */

/** 
 * 复制文件描述符
 * 下面该宏对应函数原型：int dup(int fd)。直接调用了系统中断 int 0x80，参数是__NR_dup。其中fd是文件描述符
 * @param[in]	fd		需要复制的文件描述符
 * @retval		成功返回新文件句柄，失败返回出错码
 */
_syscall1(int, dup, int, fd)
