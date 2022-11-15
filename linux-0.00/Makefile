include Rules.make
# Makefile for the simple example kernel.

# -0: 使用16bit代码段　
# -a: 开启与GNU as，ld部分兼容性选项
AS86	= $(QUIET_CC)as86 -0 -a

# -0: 产生具有16bit魔数的头结构
LD86	= $(QUIET_LINK)ld86 -0

# -s(去除): 输出文件中省略所有的符号信息
# -x: 删除所有局部符号
# -M: 用于输出符号表
# -e startup_32(新增): 指定入口
# -Ttext 0(新增): 使`startup_32`标号对应的地址为`0x0`
LDFLAGS	= -M -x -Ttext 0 -e startup_32

all:	Image

Image: boot system
	dd bs=32 if=boot of=Image skip=1
	dd bs=512 if=system of=Image skip=8 seek=1
	sync

disk: Image
	dd bs=8192 if=Image of=/dev/fd0
	sync;sync;sync

head.o: head.s

system: head.o
	$(LD) $(LDFLAGS) head.o -o system > System.map

boot:   boot.s
	$(AS86) -o boot.o boot.s
	$(LD86) -s -o boot boot.o

clean:
	rm -f Image System.map core boot *.o system