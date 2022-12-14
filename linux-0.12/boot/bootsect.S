! SYS_SIZE is the number of clicks (16 bytes) to be loaded.
! 0x3000 is 0x30000 bytes = 196kB, more than enough for current
! versions of linux
; SYS_SIZE是要加载的系统模块长度的节数（每节有16b）。0x3000节就等
; 于0x30000bytes=192KB，对于当前的版本空间已足够了。

; 该头文件里定义了内核用到的一些常数符号和Linus自己使用的默认硬盘默认参数块。
#include <linux/config.h>

SYSSIZE = DEF_SYSSIZE 	; 系统模块大小为0x3000节

!
!	bootsect.s (C) 1991 Linus Torvalds
!	modified by Drew Eckhardt
!
! bootsect.s is loaded at 0x7c00 by the bios-startup routines, and moves
! iself out of the way to address 0x90000, and jumps there.
!
! It then loads 'setup' directly after itself (0x90200), and the system
! at 0x10000, using BIOS interrupts.
!
! NOTE! currently system is at most 8*65536 bytes long. This should be no
! problem, even in the future. I want to keep it simple. This 512 kB
! kernel size should be enough, especially as this doesn't contain the
! buffer cache as in minix
!
! The loader has been made as simple as possible, and continuos
! read errors will result in a unbreakable loop. Reboot by hand. It
! loads pretty fast by getting whole sectors at a time whenever possible.

; 以下是前面这些文字的翻译：
; 	bootsect.s (C) 1991 Linus Torvalds 版权所有
;	Drew Eckhardt 修改
;
; bootsect.s被BIOS启动子程序加载至0x7c00处，并将自己移到了地址0x90000处，并跳转至那里。
;
; 它然后使用BIOS中断将setup直接加载到自己的后面(0x90200)，并将system加载到地址0x10000处。
;
; 注意! 目前的内核系统最大长度限制为512KB字节，即使是在将来这也应该没有问题的。我想让它保持简
; 单明了。这样512KB的最大内核长度应该足够了，尤其是这里没有像MINIX中一样包含缓冲区。
;
; 加载程序已经做得够简单了，而且持续的读操作出错将导致死循环。就只能手工重启。
; 只要能够一次读取所有的扇区，加载过程可以做的很快的。ENDSEG

.globl begtext, begdata, begbss, endtext, enddata, endbss
.text
begtext:
.data
begdata:
.bss
begbss:
.text

SETUPLEN = 4					! nr of setup-sectors
                                ; setup 占用的磁盘扇区数
BOOTSEG  = 0x07c0				! original address of boot-sector
                                ; bootsect 代码所在的原地址（被BIOS子程序加载至此处）
INITSEG  = DEF_INITSEG			! we move boot here - out of the way
                                ; bootsect将要移动到的目的段位置，为了避开系统模块占用处
SETUPSEG = DEF_SETUPSEG			! setup starts here
                                ; setup程序代码的段位置
SYSSEG   = DEF_SYSSEG			! system loaded at 0x10000 (65536).
                                ; system模块将被加载到0x10000
ENDSEG   = SYSSEG + SYSSIZE		! where to stop loading
                                ; 停止加载的段地址

! ROOT_DEV & SWAP_DEV are now written by "build".
; 根文件系统设备号ROOT_DEV和交换设备号SWAP_DEV现在由tools目录下的build程序写入。

ROOT_DEV = 0
SWAP_DEV = 0

entry start 	; 告知链接程序，程序从start标号处开始执行
start:
;;;;; 1. 将自身(bootsect)从0x7c00移动到0x90000处，共256字(512字节) ;;;;;;;;;;;;;;;;;;
    mov	ax,#BOOTSEG
    mov	ds,ax
    mov	ax,#INITSEG
    mov	es,ax
    mov	cx,#256
    sub	si,si       ; 源地址 ds:si = 0x07c0:0x0000
    sub	di,di       ; 目标地址 es:di = 0x9000:0x0000
    rep
    movw            ; 此处结束后，代码已经成功移动到0x90000
    jmpi go,INITSEG ; 段间跳转(Jump Intersegment)，跳转到INITSEG:go(段地址:段内偏移)处。

;;;;; 从go处开始，CPU在已移动到0x90000处继续执行 ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; 以下几行代码设置了几个段寄存器，包括栈寄存器ss和sp。
go:	mov	ax,cs
    mov	dx,#0xfef4	! arbitrary value >>512 - disk parm size
                    ; 栈指针要远大于512字节偏移(即0x90200)处都可以；一般setup程序大概占用4个
                    ; 扇区，所以sp要大于(0x90200+0x200*4+堆栈大小)。
                    ; 这里sp被设置成了0x9ff00-12(参数表长度)，即sp = 0xfef4(0xff00-0xa)。
    mov	ds,ax
    mov	es,ax
    ;push   ax		; 临时保存段值(0x9000)，供后面使用。

    mov	ss,ax		! put stack at 0x9ff00 - 12.
    mov	sp,dx
/*
 *	Many BIOS's default disk parameter tables will not
 *	recognize multi-sector reads beyond the maximum sector number
 *	specified in the default diskette parameter tables - this may
 *	mean 7 sectors in some cases.
 *
 *	Since single sector reads are slow and out of the question,
 *	we must take care of this by creating new parameter tables
 *	(for the first disk) in RAM.  We will set the maximum sector
 *	count to 18 - the most we will encounter on an HD 1.44.
 *
 *	High doesn't hurt.  Low does.
 *
 *	Segments are as follows: ds=es=ss=cs - INITSEG,
 *		fs = 0, gs = parameter table segment
 */
/*
 * 对于多扇区读操作所读的扇区数超过默认磁盘参数表中指定的最大扇区数时，很多BIOS
 * 将不能进行正确识别。在某些情况下是7个扇区。
 *
 * 由于单扇区读操作太慢，不予考虑。我们必须通过在内存中重新创建新的参数表(为第1个驱动器)
 * 来解决这个问题。我们将把其中最大扇区数设置为18，即在1.44MB磁盘上会碰到的最大值。
 *
 * 数值大不会出问题，但太小就不行了。
 *
 * 段寄存器将被设置成：ds = es = ss = cs 都为INITSEG(0x9000),
 * fs = 0, gs = 参数表所在段值。(fs,gs 为辅助寄存器)
 */

;;;;; 修改软驱参数表 ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; BIOS中断0x1e的中断向量值是软驱参数表地址。该向量值位于内存0x1e*4=0x78处。这段代码首先从内
; 存0x0000:0x0078处复制原软驱参数表到0x9000:0xfef4处，然后修改表中的每磁道最大扇区数为18。

    push	#0          ; 置段寄存器 fs = 0
    pop		fs          ; fs:bx 指向存有软驱参数表地址处（指针的指针）
    mov	bx,	#0x78		; fs:bx is parameter table address

    seg fs              ; seg fs只影响接下来的一条语句，表示下一条语句的操作数在fs所指的段中
    lgs	si,(bx)			! gs:si is source       ; 将fs:bx赋值给gs:si 0x0000:0x0078
    mov	di,dx			! es:di is destination  ;                   0x9000:0xfef4
    mov	cx,#6			! copy 12 bytes
    cld                 ; 清方向标志。复制时指针递增

    rep                 ; 复制12字节的软驱动参数表到 0x9000:#0xfef4 处
    seg gs
    movw

    mov	di,dx           ; es:di 指向新表，然后修改表中偏移4处的最大扇区数
    movb 4(di),*18		! patch sector count ;修改新表的最大扇区数为18

    seg fs 				; 让中断向量0x1e的值指向新表
    mov	(bx),di
    seg fs
    mov	2(bx),es

    mov ax,cs           ; pop ax	! ax = 0x9000
    mov	fs,ax           ; 设置 fs = gs = 0x9000，恢复原段值
    mov	gs,ax

    xor	ah,ah			! reset FDC ; 复位软盘控制器，让其采用新参数。
    xor	dl,dl 			! dl = 0    ; 第1个软驱
    int 0x13

! load the setup-sectors directly after the bootblock.
! Note that 'es' is already set up.
; 在bootsect程序块后紧跟着加载setup模块的代码数据。
; 在移动代码时，es的值已被设置好。

;;;;; 2. 加载setup模块到0x90200开始处，共读4个扇区 ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; 如果读出错，显示磁盘上出错扇区位置，则复位驱动器，并重试，没有退路。
; INT 0x13 读扇区使用调用参数设置如下：
; ah = 0x02 - 读磁盘扇区到内存；al = 需要读出的扇区数量；
; ch = 磁道（柱面）号的低8位；   cl = 开始扇区（位 0-5），磁道号高2位（位6-7）
; dh = 磁头号；				  dl = 驱动器号（如果是硬盘则位7要置位）
; es:bx -> 指向数据缓冲区； 如果出错则CF标志置位，ah中是出错码 
load_setup:
    xor	dx, dx					! drive 0, head 0
    mov	cx,#0x0002				! sector 2, track 0
    mov	bx,#0x0200				! address = 512, in INITSEG
    mov	ax,#0x0200+SETUPLEN		! service 2, nr of sectors
    int	0x13					! read it
    jnc	ok_load_setup 			! ok - continue ;jnc - jump not cf

    push	ax			! dump error code
    call	print_nl    ; 屏幕光标回车
    mov	bp, sp          ; ss:bp 指向欲显示的字（word）
    call	print_hex   ; 显示十六进制值
    pop	ax

    xor	dl, dl			! reset FDC ;复位磁盘控制器，重试。
    xor	ah, ah
    int	0x13
    j	load_setup 		; j - jmp

ok_load_setup:

! Get disk drive parameters, specifically nr of sectors/track
; 这段代码利用BIOS INT 0x13 功能8来取磁盘驱动器的参数。实际是取每磁道扇区数，并保存在位置sectors处。取磁盘驱动器参数INT 0x13 调用格式和返回信息如下：
; ah = 0x08 dl = 驱动器号（如果是硬盘则要置位7为1）
; 返回信息：
; 如果出错则CF置位，并且ah = 状态码。
; ah = 0， al = 0，bl = 驱动器类型（AT/PS2）
; ch = 最大磁道号的低8位，cl = 每磁道最大扇区数（位0-5），最大磁道号高2位（位6-7）
; dh = 最大磁头数，dl = 驱动器数量
; es:di -> 软驱磁盘参数表
    xor	dl,dl
    mov	ah,#0x08        ! AH=8 is get drive parameters
    int	0x13
    xor	ch,ch
; 下面指令表示下一条语句的操作数在cs段寄存器所指的段中，它只影响其下一条语句。实际上，由于本程序代码和数据都被设置处于同一段中，即段寄存器cs和ds、es的值相同，因此本程序中此处可以不使用该指令
    seg cs
; 下句保存每磁道扇区数。对于软盘来说（dl=0），其最大磁道号不会超过256，ch已经足够表示它，因此cl的位6-7肯定为0。又 xor ch,ch 命令已置ch = 0，因此此时cx中是每磁道扇区数
    mov	sectors,cx      ; 保存每磁道扇区数。
    mov	ax,#INITSEG
    mov	es,ax           ; 上面取磁盘参数中断改了es的值，这里需要改回来

! Print some inane message
; 下面利用BIOS INT 0x10 功能0x03和0x13来显示信息：“'Loading'+回车+换行'”，显示包括回车和换行控制字符在内共9个字符
; BIOS中断0x10功能号ah = 0x03，读光标位置。
; 输入：bh = 页号
; 返回：ch = 扫描开始线；cl = 扫描结束线；dh = 行号（0x00顶端）；dl = 列号（0x00最左边）
; BIOS中断0x10功能好ah = 0x03，读光标位置。
; 输入：al = 放置光标的方式及规定属性。0x01-表示使用bl中的属性值，光标停在字符串结尾处。
; bh = 显示页面好；bl = 字符属性；dh = 行号；dl = 列号。cx = 显示的字符串字符数
; es:bp 此寄存器对指向要显示的字符串起始位置处
    mov	ah,#0x03        ! read cursor pos
    xor	bh,bh           ; 首先读光标位置。返回光标位置值在dx中
    int	0x10            ; dh - 行（0 - 24）；dl - 列（0 - 79）

    mov	cx,#9           ; 共显示9个字符。
    mov	bx,#0x0007      ! page 0, attribute 7 (normal)
    mov	bp,#msg1        ; es:bp 指向要显示的字符串
    mov	ax,#0x1301      ! write string, move cursor
    int	0x10            ; 写字符串并移动光标到串结尾处
! ok, we've written the message, now

! we want to load the system (at 0x10000)
;;;;; 3. 将system模块加载到0x10000(64K)处 ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    mov	ax,#SYSSEG
    mov	es,ax			! segment of 0x010000
    call	read_it 	; 读磁盘上system模块，es为输入参数
    call	kill_motor 	; 关闭驱动器马达，这样就可以知道驱动器的状态了
    call	print_nl    ; 光标回车换行

! After that we check which root-device to use. If the device is
! defined (!= 0), nothing is done and the given device is used.
! Otherwise, either /dev/PS0 (2,28) or /dev/at0 (2,8), depending
! on the number of sectors that the BIOS reports currently.
; 此后，我们检查要使用哪个根文件系统设备。如果已经指定了设备(!=0)就直接使用给定
; 的设备。否则就需要根据BIOS报告的每磁道扇区数来确定到底使用/dev/PS0(2,28)
; 还是/dev/at0(2,8)。
; 在Linux中，软驱的主设备号是2，次设备号 = type << 2 + nr，
;       type    软驱的类型（2->1.2M或7->1.44M等）。
;       nr      0-3分别对应软驱A、B、C或D；
; type是软驱的类型（2->1.2MB 或 7 -> 1.44MB等）。因为7*4 + 0 = 28，所以/dev/PS（2,28）指的是 1.44MB A驱动器，其设备号是0x021c 同理/dev/at0（2,8）指的是 1.2MB A驱动器，其设备号是0x0208
; 下面root_dev定义在引导扇区508,509字节处，指根文件系统所在设备号。0x306指第2个硬盘第1个分区。这里默认为0x306是因为当时Linus开发Linux系统时是在第2个硬盘第1个分区中存放根文件系统。这个值需要根据你自己根文件系统所在硬盘和分区进行修改。例如，如果你的根文件系统在第1个硬盘第1个分区上，那么该值应该为0x0301，即（0x01,0x03）。如果根文件系统是在第2个 1.44MB软盘上，那么该值应该为0x021D，即（0x1D,0x02）。当编译内核时，你可以在Makefile文件中另行指定你自己的值。内核映像文件Image的创建程序tools/build会使用你指定的值来设置你的根文件系统所在设备号
    seg cs
    mov	ax,root_dev     ; 取508,509字节处的根设备号并判断是否已被定义
    or	ax,ax
    jne	root_defined
 ; 下面语句取上面mov sectors,cx指令保存的每磁道扇区数sectors来判断磁盘类型。如果sectors=15则说明是 1.2MB的驱动器；如果sectors=18，则是 1.44MB软驱。因为是可引导驱动器，所以肯定是A驱。
    seg cs              ; 取出sectors的值(每磁道扇区数)
    mov	bx,sectors      
    mov	ax,#0x0208      ! /dev/PS0 - 1.2Mb
    cmp	bx,#15          ; sectors=15则说明是1.2MB的软驱
    je	root_defined    ; 如果等于，则ax中就是引导驱动器的设备号
    mov	ax,#0x021c      ! /dev/PS0 - 1.44Mb
    cmp	bx,#18          ; sectors=18则说明是1.44MB的软驱
    je	root_defined
undef_root:             ; 如果都不一样，则死循环（死机）
    jmp undef_root
root_defined:
    seg cs
    mov	root_dev,ax     ; 将检查过的设备号保存到root_dev中

! after that (everyting loaded), we jump to
! the setup-routine loaded directly after
! the bootblock:

;;;;; 4. 到此，所有程序都加载完毕，我们就跳转到setup程序去 ;;;;;;;;;;;;;;;;;;;;;;;;;
; 下面段见跳转指令（Jump Intersegment）。跳转到 0x90200:0000 （setup.s程序开始处）去执行
    jmpi	0,SETUPSEG

;;;;; bootsect.S程序到此就结束了 ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; 下面是几个子程序:
; read_it 用于读取磁盘上的system模块
; kill_motor 用于关闭软驱电动机
; 还有一些屏幕显示子程序

! This routine loads the system at address 0x10000, making sure
! no 64kB boundaries are crossed. We try to load it as fast as
! possible, loading whole tracks whenever we can.
!
! in:	es - starting address segment (normally 0x1000)
!
; 该子程序将系统模块system加载到内存地址0x10000处，并确定没有跨越64KB的内存边界。我们试图尽快地进行加载，只要可能，就每次加载整条磁道的数据。输入 es: - 开始内存地址段值（通常是0x1000）
; 下面伪操作符.word定义一个2字节目标。相当于C语言程序中定义的变量和所占内存空间大小。常数值'1+SETUPLEN'表示开始时已经读进1个引导扇区和setup程序所占的扇区数SETUPLEN
sread:	.word 1+SETUPLEN	! sectors read of current track
                            ; 当前磁道的已读扇区数（bootsect + setup）
head:	.word 0				! current head  ;当前磁头号
track:	.word 0				! current track ;当前磁道号

read_it:
; 首先测试输入的段值。必须位于内存地址64KB边界处，否则进入死循环。bx寄存器用于当前段内存放数据的开始位置。test ax,#0x0fff 指令中test是以比特位逻辑与两个操作数。若两个操作数对应的比特位都为1，则结果值的对应比特位为1，否则为0.该操作结果只影响标志（零标志ZF等）。例如若AX=0x1000，那么test指令的执行结果是（0x1000 & 0x0fff）= 0x0000，于是ZF标志置位。此时即下一条指令jne条件不成立
    mov ax,es
    test ax,#0x0fff
die:
    jne die			! es must be at 64kB boundary   ; es值必须位于64KB地址边界。
    xor bx,bx		! bx is starting address within segment     ; bx为段内偏移
rp_read:
; 接着判断是否已经读入全部数据。比较当前所读段是否就是系统数据末端所处的段（#ENDSEG），如果不是就跳转至下面ok1_read标号处继续读数据。否则退出子程序返回
    mov ax,es
    cmp ax,#ENDSEG		! have we loaded all yet? ; 是否已经加载了全部数据？
    jb ok1_read
    ret
ok1_read:
    ; 计算和验证当前磁道需要读取的扇区数，放在ax寄存器中。根据当前磁道还未读取的扇区数以及段内数据字节开始偏移位置，计算如果全部读取这些未读扇区，所读总字节数是否会超过64KB段长度的限制。若会超过，则根据此次最多能读入的字节数（64KB-段内偏移位置），反算出此次需要读取的扇区数
    seg cs
    mov ax,sectors		; 取每磁道扇区数
	sub ax,sread		; 减去当前磁道已读扇区数
	mov cx,ax			; cx = ax = 当前磁道未读扇区数
	shl cx,#9			; cx = cx * 512字节 + 段内当前偏移值（bx）
	add cx,bx			; = 此次读操作后，段内共读入的字节数
	jnc ok2_read		; 若没有超过64KB字节，则跳转至ok2_read处执行
    je ok2_read
; 若加上此次将读磁道上所有未读扇区是会超过64KB，则计算此时最多能读入的字节数：（64KB-段内读偏移位置），再转换成需读取的扇区数。其中0减某数就是取该数64KB的补值
    xor ax,ax
    sub ax,bx
    shr ax,#9
ok2_read:
; 读当前磁道上指定开始扇区（cl）和需读扇区数（al）的数据到 es:bx 开始处。然后统计当前磁道上已经读取的扇区数并与磁道最大扇区数sectors作比较。如果小于sectors说明当前磁道上还有扇区未读。于是跳转到ok3_read处继续操作
    call read_track ; 读当前磁道上指定扇区和需读扇区数的数据
    mov cx,ax       ; cx = 该次操作已读取的扇区数。
    add ax,sread    ; 加上当前磁道上已经读取的扇区数
    seg cs
    cmp ax,sectors  ; 若当前磁道上还有扇区未读，则跳转到ok3_read处
    jne ok3_read
; 若该磁道的当前磁头面所有扇区已经读取，则读该磁道的下一磁头面（1号磁头）上的数据。如果已经完成，则去读下一磁道
    mov ax,#1
    sub ax,head     ; 判断当前磁头号
    jne ok4_read    ; 如果是0磁头，则再去读1磁头面上的扇区数据
    inc track       ; 否则去读下一磁道
ok4_read:
    mov head,ax     ; 保存当前磁头号
    xor ax,ax       ; 清当前磁道已读扇区数
ok3_read:
; 如果当前磁道上还有未读扇区，则首先保存当前磁道已读扇区数，然后调整存放数据处的开始位置。若小于64KB边界值，则跳转到rp_read处，继续读数据
    mov sread,ax    ; 保存当前磁道已读扇区数
    shl cx,#9       ; 上次已读取扇区数*512字节
    add bx,cx       ; 调整当前段内数据开始位置
    jnc rp_read
; 否则说明已读取64KB数据。此时调整当前段，为读下一段数据做准备
    mov ax,es
    add ah,#0x10    ; 将段基址调整为指向下一个64KB内存开始处
    mov es,ax
    xor bx,bx       ; 清段内数据开始偏移值
    jmp rp_read     ; 跳转至rp_read处，继续读取数据

; 读当前磁道上指定开始扇区和需读扇区数的数据到es:bx开始处。参见load_setup代码块对BIOS磁道读中断int 0x13，ah=2的说明。
; al - 需读扇区数；es:bx - 缓冲区开始位置
read_track:
; 首先调用BIOS中断 0x10，功能 0x0e （以电传方式写字符），光标前移一位置。显示一个'.'
    pusha			; 压入所有寄存器(push all)
    pusha           ; 为调用显示中断压入所有寄存器值
    mov	ax, #0xe2e 	! loading... message 2e = .
    mov	bx, #7      ; 字符前景色属性
    int	0x10
    popa
    ; 开始进行磁道扇区读操作
    mov dx,track		; 取当前磁道号。
	mov cx,sread		; 取当前磁道上已读扇区数
	inc cx				; cl = 开始读扇区
	mov ch,dl			; ch = 当前磁道号
	mov dx,head			; 取当前磁道号。
	mov dh,dl			; dh = 磁头号，dl = 驱动器号（为0表示当前A驱动器）
	and dx,#0x0100		; 磁头号不大于1
	mov ah,#2			; ah = 2，读磁盘扇区功能号

    push	dx      ! save for error dump
    push	cx      ; 保留出错情况
    push	bx
    push	ax

    int 0x13
    jc bad_rt       ; 若出错，则跳转至bad_rt
    add	sp, #8      ; 若没有出错，丢弃为出错情况保存的信息
    popa
    ret

; 读磁盘操作出错。则先显示出错信息，然后执行驱动器复位操作（磁盘中断功能号0），再跳转到read_track处重试
bad_rt:
    push	ax          ! save error code
    call	print_all   ! ah = error, al = read

    xor ah,ah
    xor dl,dl
    int 0x13

    add	sp, #10         ; 丢弃为出错情况保存的信息
    popa
    jmp read_track

/*
 *	print_all is for debugging purposes.
 *	It will print out all of the registers.  The assumption is that this is
 *	called from a routine, with a stack frame like
 *	dx
 *	cx
 *	bx
 *	ax
 *	error
 *	ret <- sp
 *
*/
; print_all 用于调试目的，它会显示所有寄存器的内容。前提是从一个子程序中调用。栈帧结构如上所示
; 若标志寄存器的CF=0，则不显示寄存器名称。
print_all:
    mov	cx, #5          ! error code + 4 registers  ; 显示值个数
    mov	bp, sp          ; 保存当前栈指针sp.

print_loop:
    push	cx          ! save count left   ; 保存需要显示的剩余个数
    call	print_nl    ! nl for readability    ; 为可读性先让光标回车换行
    jae		no_reg      ! see if register name is needed    ; 若FLAGS的标志CF=0则不显示寄存器名，于是跳转

    ; 下面几行用于显示寄存器号，例如："AX:", 0x45 - E
    mov	ax, #0xe05 + 0x41 - 1 ; ah = 功能号(0x0e)；al = 字符(0x05 + 0x41 - 1)
    sub	al, cl
    int	0x10

    mov	al, #0x58       ! X     ; 显示字符'X'
    int	0x10

    mov	al, #0x3a       ! :     ; 显示字符':'
    int	0x10
; 显示寄存器bp所指栈中的内容。开始时bp指向返回地址
no_reg:
    add	bp, #2          ! next register     ; 栈中下一个位置
    call	print_hex   ! print it          ; 以十六进制显示
    pop	cx
    loop	print_loop
    ret

; 显示回车换行，调用BIOS中断0x10，以电传方式显示回车换行
print_nl:
    mov	ax, #0xe0d      ! CR
    int		0x10
    mov	al, #0xa        ! LF
    int 	0x10
    ret

/*
 *	print_hex is for debugging purposes, and prints the word
 *	pointed to by ss:bp in hexadecmial.
*/
/* 子程序print_hex用于调试目的.它使用十六进制在屏幕上显示出ss:bp指向的字 */
; 调用BIOS中断0x10，以电传方式和4个十六进制数显示 ss:bp 指向的字
print_hex:
    mov	cx, #4      ! 4 hex digits          ; 要显示4个十六进制数字
    mov	dx, (bp)    ! load word into dx     ; 显示值放入dx中

; 先显示高字节，因此需要把dx中值左旋4位，此时高4位在dx的低4位中
print_digit:
    rol	dx, #4      ! rotate so that lowest 4 bits are used ;左旋4位
    mov	ah, #0xe    ; 中断功能号
    mov	al, dl      ! mask off so we have only next nibble
    and	al, #0xf    ; 放入al中并只取低4比特（1个值）
; 加上'0'的ASCII码值0x30，把显示值转换成基于数字'0'的字符。若此时al值超过0x39，表示欲显示值超过数字9，因此需要使用'A'-'F'来表示
    add	al, #0x30   ! convert to 0 based digit, '0'
    cmp	al, #0x39   ! check for overflow ; 大于9的处理，转换成A-F
    jbe	good_digit
    add	al, #0x41 - 0x30 - 0xa  ! 'A' - '0' - 0xa

good_digit:
    int	0x10
    loop	print_digit     ; cx--。若cx>0则去显示下一个值
    ret

/*
 * This procedure turns off the floppy drive motor, so
 * that we enter the kernel in a known state, and
 * don't have to worry about it later.					'
 */
; 这个子程序用于关闭软驱的马达，这样我们进入内核后它处于已知状态，以后也就无须担心它了。
; 下面mov dx,#0x3f2 指令上的值0x3f2是软盘控制器的一个端口，被称为数字输出寄存器（DOR）端口。它是一个8位的寄存器，其位7-位4分别用于控制4个软驱（D-A）的启动和关闭。位3-位2用于允许/禁止DMA和中断请求以及启动/复位软盘控制器FDC。位1-位0用于选择选择操作的软驱。xor al, al 指令在al中设置并输出的0值，就是用于选择A驱动器，关闭FDC，禁止DMA和中断请求，关闭马达。有关软驱控制卡编程的详细信息参见kernel/blk_drv/floppy.c程序后面的说明
kill_motor:
    push dx
    mov dx,#0x3f2       ; 软驱控制卡的数字输出寄存器端口，只写
    xor al, al			; A驱动器，关闭FDC，禁止DMA和中断请求，关闭马达
	outb				; 将al中的内容输出到dx指定的端口去
    pop dx
    ret

sectors:
    .word 0             ; 存放当前启动软盘每磁道的扇区数

msg1:                   ; 开机调用BIOS中断显示的信息。共9个字符
    .byte 13,10         ; 回车、换行的ASCII码
    .ascii "Loading"

; 表示下面语句从地址506（0x1FA）开始，所以root_dev在启动扇区的第508开始的2个字节中
.org 506
; swap_dev在第506开始的2个字节中，root_dev在第508开始的2个字节中
swap_dev:
    .word SWAP_DEV      ; 这里存放交换系统所在设备号（init/main.c中会用）
root_dev:
    .word ROOT_DEV      ; 这里存放根文件系统所在设备号（init/main.c中会用）

; 下面0xAA55是启动盘具有有效引导扇区的标志。供BIOS中的程序加载引导扇区时识别使用。它必须位于引导扇区的最后两个字节中
boot_flag:
    .word 0xAA55

.text
endtext:
.data
enddata:
.bss
endbss:
