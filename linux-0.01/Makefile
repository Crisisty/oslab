include Rules.make

RAMDISK = #-DRAMRISK=512

AS86 = $(QUIET_CC)as86 -0 -a

LD86 = $(QUIET_LINK)ld86 -0

LDFLAGS = -M -x -Ttest 0 -e startup_32

CC += $(RAMDISK)

CFLAGS = -g -Wall -O -fstrength-reduce -fomit-frame-pointer -fno-builtin

CPP += -Iinclude

ROOT_DEV = #/dev/hd6
SWAP_DEV = #/dev/hd2

.c.s:
	$(CC) $(CFLAGS) \
	-nostdinc -Iinclude -S -o $*.s $<
.s.o:
	$(AS) -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) \
	-nostdinc -Iinclude -c -o $*.o $<

all: Image

Image: boot/bootsect boot/setup tools/build
	tools/build boot/bootsect boot/setup none $(ROOT_DEV) > Image
	sync

disk: Image
	dd bs=1 if=bootsect of=Image skip=32
# dd bs=8192 if=Image of=/dev/PS0

tools/build: tools/build.c
	$(CC) $(CFLAGS) \
	-o tools/build tools/build.c

tools/system:

boot/setup: boot/setup.s
	$(AS86) -o boot/setup.o boot/setup.s
	$(LD86) -s -o boot/setup boot/setup.o

boot/setup.s: boot/setup.S
	$(CPP) -traditional boot/setup.S -o boot/setup.s

boot/bootsect.s: boot/bootsect.S 
	$(CPP) -traditional boot/bootsect.S -o boot/bootsect.s

boot/bootsect: boot/bootsect.s
	$(AS86) -o boot/bootsect.o boot/bootsect.s
	$(LD86) -s -o boot/bootsect boot/bootsect.o

clean:
	rm -f Image tmp_make core boot/bootsect boot/setup \
		boot/bootsect.s boot/setup.s
	rm -f tools/build boot/*.o