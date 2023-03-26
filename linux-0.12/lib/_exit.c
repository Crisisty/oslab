/*
 *  linux/lib/_exit.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__		/* 定义一个符号常量 */
#include <unistd.h>		/* Linux标准头文件。定义了各种符号常数和类型，并声明了各种函数。若定义了__LIBRARY__，则含有系统调用号和内嵌汇编syscal0()等 */

/** 
 * 内核使用的程序(退出)终止函数
 * 直接调用系统终端int80，功能号__NR_exit
 * 函数名前的关键字volatile用于告诉编译器gcc，该函数不会返回。这样可让gcc产生更好一些代码，更重要
 * 的是使用这个关键字可以避免产生某些假告警信息
 * @param[in]   exit_code	退出码 
 * @retval  	void
 */ 
volatile void _exit(int exit_code)
{
	__asm__("int $0x80"::"a" (__NR_exit), "b" (exit_code));
}
