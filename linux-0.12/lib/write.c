/*
 *  linux/lib/write.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>     /* Linux标准头文件。定义了各种符号常数和类型，并声明了各种函数。若定义了__LIBRARY__，则含有系统调用号和内嵌汇编syscal0()等 */

/** 
 * 写文件 系统调用
 * int write(int fd, const char * buf, off_t count)
 * @param[in]	fd		文件描述符
 * @param[in]	buf		写缓冲指针
 * @param[in]	count	写字节数
 * @retval		成功时返回写入的字节数(0表示写入0字节)，出错返回-1，并且设置出错号
 * 
 */
_syscall3(int, write, int, fd, const char *, buf, off_t, count)
