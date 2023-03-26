/*
 *  linux/lib/execve.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>     /* Linux标准头文件。定义了各种符号常数和类型，并声明了各种函数。若定义了__LIBRARY__，则含有系统调用号和内嵌汇编syscal0()等 */

/** 
 * 加载并执行子进程(其他程序)
 * int execve(const char *file, char **argv, char **envp)
 * 直接调用了系统中断int 0x80，参数是__NR_execve
 * @param[in]	file	被执行程序文件名
 * @param[in]	argv	命令行参数指针数组
 * @param[in]	envp	环境变量指针数组
 * @retval      成功不返回；失败设置出错号，并返回-1
 */
_syscall3(int, execve, const char *, file, char **, argv, char **, envp)
