!
!	setup.s		(C) 1991 Linus Torvalds
!
! setup.s is responsible for getting the system data from the BIOS,
! and putting them into the appropriate places in system memory.
! both setup.s and system has been loaded by the bootblock.
!
! This code asks the bios for memory/disk/other parameters, and
! puts them in a "safe" place: 0x90000-0x901FF, ie where the
! boot-block used to be. It is then up to the protected mode
! system to read them from there before the area is overwritten
! for buffer-blocks.
!
;  setup.s负责从BIOS中获取系统数据，并将这些数据放到系统内存的适当地方。
; 此时setup.s和system已经由bootsect引导块加载到内存中。
;
; 这段代码询问bios有关内存/磁盘/其它参数，并将这些参数放到一个“安全的”地
; 方：0x90000 - 0x901FF，也即原来bootsect代码块曾经在的地方，然后在被缓冲
; 块覆盖掉之前由保护模式的system读取。
!

! NOTE! These had better be the same as in bootsect.s!
; 以下这些参数最好和bootsect.s中的相同！
#include <linux/config.h>
; config.h中定义了DEF_INITSEG = 0x9000，DEF_SYSSEG = 0x1000，DEF_SETUPSEG = 0x9020。
INITSEG  = DEF_INITSEG	! we move boot here - out of the way	; 原来bootsect所处的段
SYSSEG   = DEF_SYSSEG	! system loaded at 0x10000 (65536).		; system在0x10000处
SETUPSEG = DEF_SETUPSEG	! this is the current segment			; system在0x10000处

.globl begtext, begdata, begbss, endtext, enddata, endbss
.text
begtext:
.data
begdata:
.bss
begbss:
.text

entry start
start:

! ok, the read went well so we get current cursor position and save it for
! posterity.
; ok，整个读磁盘过程都正常，现在将光标位置保存以备今后使用。
; 下句将ds置成INITSEG（0x9000）。这已经在bootsect程序中设置过，但是现在是setup程序，Linus觉得需要再重新设置一下
	mov	ax,#INITSEG		! this is done in bootsect already, but...
						; 这已经在bootsect程序中设置过ds，但是...
	mov	ds,ax

;;;;; 1.获取系统初始化所需要的参数 ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; 内存地址	 ; 字节 ; 内容				 ;
; 0x90000	; 2	;   光标位置			;
; 0x90002	; 2	;	扩展内存大小 		 ;
; 0x90004	; 2	;	显示页面			;
; 0x90006	; 1 ;	显示模式			;
; 0x90007	; 1 ;	字符列数			;
; 0x90008	; 2	;	??				   ;
; 0x9000A	; 1	;	安装的显示内存		 ;
; 0x9000B	; 1	;	显示状态(彩色/单色)	 ;
; 0x9000C	; 2	;	显示卡特性参数		 ;
; 0x9000E	; 1	;	屏幕当前行值		 ;
; 0x9000F	; 1	;	屏幕当前列值		 ;
; ...								   ;
; 0x90080	; 16;	第1个硬盘的参数表	  ;
; 0x90090	; 16;	第2个硬盘的参数表	  ;
; 0x901FC	; 2 ;	根文件系统所在的设备号（bootsec.s中设置）;

! Get memory size (extended mem, kB) 
;;; 取扩展内存的大小值（KB）。利用BIOS中断0x15功能号ah=0x88取系统所含扩展内存大小并保存在内存0x90002处。返回：ax = 从0x100000（1M）处开始的扩展内存大小（KB）。若出错则CF置位，ax = 出错码
	mov	ah,#0x88
	int	0x15
	mov	[2],ax		; 将扩展内存数值在0x90002处（1个字）

! check for EGA/VGA and some config parameters 
;;; 检查显示方式（EGA/VGA）并取参数。调用BIOS中断0x10功能号0x12（视频子系统配置）取EBA配置信息。ah = 0x12，bl = 0x10 - 取EGA配置信息。
;;; 返回：bh = 显示状态（0x00 - 色彩模式，I/O端口=0x3dX；0x01 - 单色模式，I/O端口 = 0x3bX）。
;;; bl = 安装的显示内存（0x00 - 64k；0x01 - 128k；0x02 - 192k；0x03 = 256k。）
;;; cx = 显示卡特性参数（参加程序后对BIOS视频中断0x10的说明）
	mov	ah,#0x12
	mov	bl,#0x10
	int	0x10
	mov	[8],ax				; 0x90008 = ??
	mov	[10],bx				; 0x9000A = 安装的显示内存；0x9000B=显示状态（彩/单色）
	mov	[12],cx				; 0x9000C = 显示卡特性参数
; 检测屏幕当前行列值。若显示卡是VGA卡时则请求用户选择显示行列值，并保存到0x9000E处。
	mov	ax,#0x5019			; 在ax中预置屏幕默认行列值（ah = 80列；al = 25行）
	cmp	bl,#0x10			; 若中断返回bl值为0x10，则表示不是VGA显示卡，跳转
	je	novga
	call	chsvga			; 检测显示卡厂家和类型，修改显示行列值
novga:	
	mov	[14],ax				; 保存屏幕当前行列值（0x9000E，0x9000F）
;;; 使用 BIOS中断0x10功能0x03取屏幕当前光标位置，并保存在内存 0x90000 处（2 字节）
;;; 控制台初始化程序 console.c 会到此处读取该值。BIOS中断0x10功能号ah = 0x03，读光标位置。
;;; 输入：bh = 页号
;;; 返回：ch = 扫描开始线；cl = 扫描结束线；dh = 行号(0x00 顶端)；dl = 列号(0x00 最左边)。
	mov	ah,#0x03	! read cursor pos
	xor	bh,bh
	int	0x10		! save it in known place, con_init fetches
	mov	[0],dx		! it from 0x90000.
	
! Get video-card data:
;;; 取显示卡当前显示模式
;;; 调用 BIOS 中断 0x10，功能号 ah = 0x0f
;;; 返回：ah = 字符列数；al = 显示模式；bh = 当前显示页
;;; 0x90004(1 字)存放当前页；0x90006 存放显示模式；0x90007 存放字符列数。
	mov	ah,#0x0f
	int	0x10
	mov	[4],bx		! bh = display page
	mov	[6],ax		! al = video mode, ah = window width

;;; 复制两个硬盘的参数表：
;;; 第 1 个硬盘参数表的首地址竟然是中断 0x41 的中断向量值！而第 2 个硬盘参数表紧接在第 1 个
;;; 表的后面，中断 0x46 的向量向量值也指向第 2 个硬盘的参数表首址。表的长度是 16 个字节。
;;; 下面两段程序分别复制 ROM BIOS 中有关两个硬盘参数表到：0x90080 处存放第 1 个硬盘的表，
;;; 0x90090 处存放第 2 个硬盘的表
! Get hd0 data
	mov	ax,#0x0000
	mov	ds,ax
	lds	si,[4*0x41]			; 取中断向量0x41的值，即hd0参数表的地址--ds:si，这里是把内存地址4 * 0x41（= 0x104）处保存的4个字节读出。这4字节即是硬盘参数表所处位置的段和偏移值
	mov	ax,#INITSEG
	mov	es,ax
	mov	di,#0x0080			; 传输目的地址: 0x9000:0x0080 --es:di
	mov	cx,#0x10			; 共传输 16 字节。
	rep
	movsb

! Get hd1 data
	mov	ax,#0x0000
	mov	ds,ax
	lds	si,[4*0x46]			; 取中断向量0x46的值，即hd1参数表的地址--ds:si
	mov	ax,#INITSEG
	mov	es,ax
	mov	di,#0x0090			; 传输目的地址: 0x9000:0x0090 --es:di
	mov	cx,#0x10
	rep
	movsb

! Check that there IS a hd1 :-)
;;; 检查系统是否存在第2个硬盘，如果不存在则第2个表清零。
;;; 利用 BIOS 中断调用 0x13 的取盘类型功能，功能号 ah = 0x15；
;;; 输入：dl = 驱动器号（0x8X 是硬盘：0x80 指第 1 个硬盘，0x81 第 2 个硬盘）
;;; 输出：ah = 类型码；00 - 没有这个盘，CF 置位；01 - 是软驱，没有 change-line 支持；
;;; 				 02 - 是软驱(或其他可移动设备)，有 change-line 支持； 03 - 是硬盘。
	mov	ax,#0x01500
	mov	dl,#0x81
	int	0x13
	jc	no_disk1
	cmp	ah,#3
	je	is_disk1		; 是硬盘吗?(类型 = 3?).
no_disk1:
	mov	ax,#INITSEG		; 第 2 个硬盘不存在，则对第 2 个硬盘表清零。
	mov	es,ax
	mov	di,#0x0090
	mov	cx,#0x10
	mov	ax,#0x00
	rep
	stosb
is_disk1:

! now we want to move to protected mode ...
; 这里开始，我们将要开始进入保护模式

	cli			! no interrupts allowed !		; 这里开始不允许任何中断

! first we move the system to it's rightful place
;;;;; 2. 把整个system模块移动到0x00000位置 ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;  bootsect 引导程序会把 system 模块读入到内存 0x10000（64KB）开始的位置。由于当时假设system模块最大长度不会超过0x80000（512KB），即其末端不会超过内存地址0x90000，所以bootsect 会把自己移动到0x90000开始的地方，并把setup加载到它的后面。下面这段程序的用途是再把整个system模块移动到0x00000位置，即把从0x10000到0x8ffff的内存数据块（512KB）整块地向内存低端移动了0x10000（64KB）字节
	mov	ax,#0x0000
	cld					! 'direction' = 0, movs moves forward
do_move:
	mov	es,ax			! destination segment ; es:di是目的地址(初始为0x0:0x0)
	add	ax,#0x1000
	cmp	ax,#0x9000		; 已经把最后一段(从0x8000段开始的64KB)代码移动完.
	jz	end_move 		; 判断是否移动完成.是，则跳转
	mov	ds,ax			! source segment		; ds:si 是源地址(初始为 0x1000:0x0)
	sub	di,di
	sub	si,si
	mov cx,#0x8000 		; 移动0x8000个字（64KB字节）
	rep
	movsw
	jmp	do_move

! then we load the segment descriptors
;;;;; 3. 此后，我们加载段描述符表（临时gdt，ldt表） ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; 从这里开始会遇到 32 位保护模式的操作。在进入保护模式中运行之前，我们需要首先设置好需要使用的段描述符表。这里需要设置全局描述符表GDT和中断描述符表IDT。
; 指令LIDT用于加载中断描述符表寄存器。它的操作数（idt_48）有6字节。前2字节(字节0-1）是描述符表的字节长度值；后4字节（字节2-5）是描述符表的32位线性基地址。中断描述符表中的每一个8字节表项指出发生中断时需要调用的代码信息。与中断向量有些相似，但要包含更多的信息。
; LGDT指令用于加载全局描述符表寄存器，其操作数格式与LIDT指令的相同。全局描述符表中的每个描述符项（8字节）描述了保护模式下数据段和代码段（块）的信息。 其中包括段的最大长度限制（16 位）、段的线性地址基址（32位）、段的特权级、段是否在内存、读写许可权以及其他一些保护模式运行的标志。
end_move:
	mov	ax,#SETUPSEG	! right, forgot this at first. didn't work :-)
	mov	ds,ax			; ds 指向本程序(setup)段。
	lidt	idt_48		! load idt with 0,0					 ; 加载IDT寄存器
	lgdt	gdt_48		! load gdt with whatever appropriate ; 加载GDT寄存器

! that was painless, now we enable A20
;;;;; 为了能访问1MB以上的内存，需要开启A20地址线 ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

	call	empty_8042	; 测试8042状态寄存器，等待输入缓冲器空。只有当输入缓冲器为空时才可以对其执行写命令
	mov	al,#0xD1		! command write ; 0xD1 - 写数据到P2口
	out	#0x64,al 		; 8042的P2端口。P2端口位1用于A20线的选通
	call	empty_8042	; 等待输入缓冲器空，看命令是否被接受
	mov	al,#0xDF		! A20 on	; 选通 A20 地址线的参数
	out	#0x60,al		; 数据要写到0x60口
	call	empty_8042	; 若此时输入缓冲器为空，则表示A20线已经选通。

! well, that went ok, I hope. Now we have to reprogram the interrupts :-(
! we put them right after the intel-reserved hardware interrupts, at
! int 0x20-0x2F. There they won't mess up anything. Sadly IBM really
! messed this up with the original PC, and they haven't been able to
! rectify it afterwards. Thus the bios puts interrupts at 0x08-0x0f,
! which is used for the internal hardware interrupts as well. We just
! have to reprogram the 8259's, and it isn't fun.
; 希望以上一切正常。现在我们必须重新对中断进行编程 :-(
; 我们将它们放在正好处于intel保留的硬件中断后面，在int 0x20 - 0x2F。
; 在那里它们不会引起冲突。不幸的是IBM在原PC机中搞糟了，以后也没有纠正过来。
; PC机的BIOS将中断放在了0x08-0x0f，这些中断也被用于内部硬件中断。
; 所以我们就必须重新对8259中断控制器进行编程，这一点都没意思。
;
; 8259 芯片主片端口是0x20-0x21，从片端口是0xA0-0xA1。输出值0x11表示初始化命令开始，它是ICW1命令字，表示边沿触发、多片8259级连、最后要发送ICW4命令字。

;;;;; 以下是重新对8259A芯片进行编程，不是需要理解的重点，不做具体介绍 ;;;;;;;;;;;;;;;;;
	mov	al,#0x11		! initialization sequence
	out	#0x20,al		! send it to 8259A-1		; 发送到 8259A 主芯片。
; 指令起延时作用，0xeb是直接近跳转指令的操作码，带1个字节的相对位移值。因此跳转范围是-127到127。CPU通过把这个相对位移值加到EIP寄存器中就形成一个新的有效地址。执行时所花费的CPU时钟周期数是7至10个。0x00eb表示跳转位移值是0的一条指令，因此还是直接执行下一条指令。这两条指令共可提供14--20个CPU时钟周期的延迟时间。因为在as86中没有表示相应指令的助记符，因此Linus在一些汇编程序中就直接使用机器码来表示这种指令。另外，每个空操作指令NOP的时钟周期数是3个，因此若要达到相同的延迟效果就需要6至7个NOP指令。
	.word	0x00eb,0x00eb		! jmp $+2, jmp $+2 	; $ 表示当前指令的地址，
	out	#0xA0,al		! and to 8259A-2			; 再发送到 8259A 从芯片。
	.word	0x00eb,0x00eb
; Linux系统硬件中断号被设置成从0x20开始。
	mov	al,#0x20		! start of hardware int's (0x20)
	out	#0x21,al		; 送主芯片ICW2命令字，设置起始中断号，要送奇端口。
	.word	0x00eb,0x00eb
	mov	al,#0x28		! start of hardware int's 2 (0x28)
	out	#0xA1,al		; 送从芯片ICW2命令字，从芯片的起始中断号。
	.word	0x00eb,0x00eb
	mov	al,#0x04		! 8259-1 is master
	out	#0x21,al		; 送主芯片ICW3命令字，主芯片的IR2连从芯片INT。
	.word	0x00eb,0x00eb
	mov	al,#0x02		! 8259-2 is slave
	out	#0xA1,al		; 送从芯片ICW3命令字，表示从芯片的INT连到主芯片的IR2引脚上。
	.word	0x00eb,0x00eb
	mov	al,#0x01		! 8086 mode for both
	out	#0x21,al		; 送主芯片ICW4命令字。8086模式；普通EOI、非缓冲方式，需发送指令来复位。初始化结束，芯片就绪。
	.word	0x00eb,0x00eb
	out	#0xA1,al		; 送从芯片ICW4命令字，内容同上。
	.word	0x00eb,0x00eb
	mov	al,#0xFF		! mask off all interrupts for now
	out	#0x21,al		; 屏蔽主芯片所有中断请求。
	.word	0x00eb,0x00eb
	out	#0xA1,al		; 屏蔽从芯片所有中断请求。

! well, that certainly wasn't fun :-(. Hopefully it works, and we don't
! need no steenking BIOS anyway (except for the initial loading :-).
! The BIOS-routine wants lots of unnecessary data, and it's less
! "interesting" anyway. This is how REAL programmers do it.
!
! Well, now's the time to actually move into protected mode. To make
! things as simple as possible, we do no register set-up or anything,
! we let the gnu-compiled 32-bit programs do that. We just jump to
! absolute address 0x00000, in 32-bit protected mode.
; 哼，上面这段当然没劲:-(，希望这样能工作，而且我们也不再需要乏味的BIOS了（除了
; 初始的加载:-)。BIOS子程序要求很多不必要的数据，而且它一点都没趣。那是“真正”的
; 程序员所做的事。
; 
; 好了，现在是真正开始进入保护模式的时候了。为了把事情做得尽量简单，我们并不对
; 寄存器内容进行任何设置。我们让 gnu 编译的 32 位程序去处理这些事。在进入32位
; 护模式时我们仅是简单地跳转到绝对地址 0x00000 处。
; 
; 进入保护模式时，我们只需要简单地跳转到绝对地址0x00000处就好。 
; 这里设置进入32位保护模式运行。首先加载机器状态字(lmsw - Load Machine Status 
; Word)，也称控制寄存器CR0，将其位0置1将导致CPU工作在保护模式，并且运行在特权级 0 中，即
; 当前特权级CPL=0。此时段寄存器仍然指向与实地址模式中相同的线性地址处（在实地址模式下
; 线性地址与物理内存地址相同）。在设置该比特位后，随后一条指令必须是一条段间跳转指令以
; 用于刷新CPU当前指令队列。因为CPU是在执行一条指令之前就已从内存读取该指令并对其进行
; 解码。然而在进入保护模式以后那些属于实模式的预先取得的指令信息就变得不再有效。而一条
; 段间跳转指令就会刷新 CPU 的当前指令队列，即丢弃这些无效信息。
; 注：在Intel公司的手册上建议80386或以上CPU应该使用指令“mov cr0,ax”切换到保护模式。lmsw指令仅用于兼容以前的286CPU。

;;;;; 4. 开启保护模式 ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	mov	ax,#0x0001	! protected mode (PE) bit	; 保护模式比特位(PE)。
	lmsw	ax		! This is it!				; 将CR0的位0置1开启保护模式
	jmpi	0,8		! jmp offset 0 of segment 8 (cs)	; 跳转至cs段偏移0处。
	; 偏移地址是0，因为system模块已被移动到0x00000处了。
	; 段值8则是保护模式下的段选择符。
	; 段选择符长度为16位（2字节）：
	; 	位0-1	- 表示请求的特权级0-3 ，linux0.12只用到两级：0级（系统级）和3级（用户级）
	;	位2		- 用于选择全局描述符表(0)还是局部描述符表(1)
	; 	位3-15	- 描述符表项的索引
	; 段选择符8(0000,0000,0000,1000)表示请求特权级0、使用全局描述符表gdt中的第2个段描述符项（该项指出代码
	; 的基地址是0），因此这里的跳转指令就会去执行system中的代码。

;;;;;;;;;;;;;;;; setup程序到这就结束了 ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

! This routine checks that the keyboard command queue is empty
! No timeout is used - if this hangs there is something wrong with
! the machine, and we probably couldn't proceed anyway.
; 下面这个子程序检查键盘命令队列是否为空。这里不使用超时方法 -
; 如果这里死机，则说明 PC 机有问题，我们就没有办法再处理下去了。
;
; 只有当输入缓冲器为空时（键盘控制器状态寄存器位1 = 0）才可以对其执行写命令。
empty_8042:
	.word	0x00eb,0x00eb
	in	al,#0x64		! 8042 status port		; 读AT键盘控制器状态寄存器
	test	al,#2		! is input buffer full? ; 测试位1,输入缓冲器满?
	jnz	empty_8042		! yes - loop
	ret

! Routine trying to recognize type of SVGA-board present (if any)
! and if it recognize one gives the choices of resolution it offers.
! If one is found the resolution chosen is given by al,ah (rows,cols).

;;;;; 以下代码牵涉到众多显卡端口信息，比较复杂。但跟内核运行关系不大，所以可以跳过不看 ;;;
chsvga:	cld
	push	ds			; 保存ds值。将在之后弹出。
	push	cs			; 把默认数据段设置成和代码段同一个段。
	pop	ds
	mov 	ax,#0xc000
	mov	es,ax			; es指向0xc000段。此处是VGA卡上的ROM BIOS区。
	lea	si,msg1			; ds:si指向msg1字符串。
	call	prtstr		; 显示以NULL结尾的msg1字符串。
; 首先请注意,按键按下产生的扫描码称为接通码（make code)，释放一个按下的按键产生的扫描码
; 称为断开码（break code）。下面这段程序读取键盘控制其输出缓冲器中的扫描码或命令。如果
; 收到的扫描码是比0x82小的接通码，那么因为0x82是最小的断开码值，所以小于0x82表示还没
; 有按键松开。如果扫描码大于0xe0，表示收到的扫描码是扩展扫描码的前缀码。如果收到的是断
; 开码0x9c，则表示用户按下/松开了回车键，于是程序跳转去检查系统是否具有或支持SVGA模式。
; 否则就把 AX 设置为默认行列值并返回。
nokey:	
	in	al,#0x60		; 读取键盘控制器缓冲中的扫描码。
	cmp	al,#0x82		; 与最小断开码0x82比较。
	jb	nokey			; 若小于0x82，表示还没有按键松开。
	cmp	al,#0xe0
	ja	nokey			; 若大于0xe0，表示收到的是扩展扫描码前缀。
	cmp	al,#0x9c		; 若断开码是0x9c，表示用户按下/松开了回车键
	je	svga			; 于是程序跳转去检查系统是否具有SVGA模式。
	mov	ax,#0x5019		; 否则设置默认行列值AL=25行、AH=80列。
	pop	ds
	ret
; 下面根据 VGA 显示卡上的 ROM BIOS 指定位置处的特征数据串或者支持的特别功能来判断机器上
; 安装的是什么牌子的显示卡。本程序共支持 10 种显示卡的扩展功能。注意，此时程序已经在chsvga代码块中mov es,ax
; 把 es 指向 VGA 卡上 ROM BIOS 所在的段 0xc000。
;
; 首先判断是不是ATI显示卡。我们把ds:si指向idati代码块中ATI显示卡特征数据串，并把es:si指
; 向VGA BIOS中指定位置（偏移 0x31）处。该特征串共有9个字符（"761295520"），我们来循环
; 比较这个特征串。如果相同则表示机器中的VGA卡是ATI牌子的，于是让 ds:si 指向该显示卡可
; 以设置的行列模式值dscati，让di指向ATI卡可设置的行列个数和模式，并跳转到标号selmod处进一步进行设置。
svga:	lea 	si,idati		! Check ATI 'clues'		; 检查判断ATI显示卡的数据。
	mov	di,#0x31				; 特征串从0xc000:0x0031开始。
	mov 	cx,#0x09			; 特征串有9个字节。
	repe
	cmpsb						; 如果9个字节都相同，表示系统中有一块ATI牌显示卡。
	jne	noati					; 若特征串不同则表示不是ATI显示卡。跳转继续检测卡。
; 现在确定了显示卡的牌子是ATI。于是si指向ATI显示卡可选行列值表dscati
; di指向扩展模式个数和扩展模式号列表moati，然后跳转到selmod处继续处理。
	lea	si,dscati										; 把dscati的有效地址放入si。
	lea	di,moati
	lea	cx,selmod
	jmp	cx
; 现在来判断是不是Ahead牌子的显示卡。首先向EGA/VGA图形索引寄存器0x3ce写入想访问的
; 主允许寄存器索引号0x0f，同时向0x3cf端口（此时对应主允许寄存器）写入开启扩展寄存器
; 标志值0x20。然后通过0x3cf端口读取主允许寄存器值，以检查是否可以设置开启扩展寄存器
; 标志。如果可以则说明是Ahead牌子的显示卡。注意word输出时 al--端口n，ah--端口n+1。
noati:	mov	ax,#0x200f		! Check Ahead 'clues'
	mov	dx,#0x3ce			; 数据端口指向主允许寄存器（0x0f--0x3ce端口）
	out	dx,ax				; 并设置开启扩展寄存器标志（0x0f--0x3cf端口）
	inc	dx					; 然后再读取该寄存器，检查该标志是否被设置上。
	in	al,dx
	cmp	al,#0x20			; 如果读取值是0x20，则表示是Ahead A显示卡。
	je	isahed				; 如果读取值是0x21，则表示是Ahead B显示卡。
	cmp	al,#0x21			; 否则说明不是Ahead显示卡，于是跳转继续检测其余卡。
	jne	noahed
; 现在确定了显示卡的牌子是Ahead。于是si指向Ahead显示卡可选行列值表dscahead，
; di指向扩展模式个数和扩展模式号列表moahead，然后跳转到selmod处继续处理。
isahed:	lea	si,dscahead
	lea	di,moahead
	lea	cx,selmod
	jmp	cx
; 现在来检查是不是Chips & Tech生产的显示卡。通过端口0x3c3（0x94或0x46e8）设置VGA允许
; 寄存器的进入设置模式标志（位 4），然后从端口0x104读取显示卡芯片集标识值。如果该标识值是0xA5，则说明是Chips & Tech生产的显示卡。
noahed:	mov	dx,#0x3c3		! Check Chips & Tech. 'clues'
	in	al,dx				; 从0x3c3端口读取VGA允许寄存器值，添加上进入设置模式
	or	al,#0x10			; 标志（位 4）后再写回。
	out	dx,al
	mov	dx,#0x104			; 在设置模式时从全局标识端口0x104读取显示卡芯片标识值
	in	al,dx				; 并暂时存放在bl寄存器中。
	mov	bl,al
	mov	dx,#0x3c3			; 然后把0x3c3端口中的进入设置模式标志复位。
	in	al,dx
	and	al,#0xef
	out	dx,al
	cmp	bl,[idcandt]		; 再把 bl 中标识值与位于idcandt处的Chips & Tech的标识值0xA5作比较。
	jne	nocant				; 如果不同则跳转比较下一种显卡。
; 现在确定了该显示卡的牌子。于是si指向该品牌显示卡的可选行列值表dsccandt，di指向
; 扩展模式个数和扩展模式号列表mocandt，然后跳转到selmod处继续进行设置模式操作。
	lea	si,dsccandt
	lea	di,mocandt
	lea	cx,selmod
	jmp	cx
; 现在检查是不是Cirrus显示卡。方法是使用CRT控制器索引号0x1f寄存器的内容来尝试禁止扩展
; 功能。该寄存器被称为鹰标（Eagle ID）寄存器，将其值高低半字节交换一下后写入端口0x3c4索
; 引的 6 号（定序/扩展）寄存器应该会禁止Cirrus显示卡的扩展功能。如果不会则说明不是Cirrus
; 显示卡。因为从端口0x3d4索引的0x1f鹰标寄存器中读取的内容是鹰标值与0x0c索引号对应的显
; 存起始地址高字节寄存器内容异或操作之后的值，因此在读0x1f中内容之前我们需要先把显存起始
; 高字节寄存器内容保存后清零，并在检查后恢复之。另外，将没有交换过的Eagle ID值写到0x3c4
; 端口索引的 6 号定序/扩展寄存器会重新开启扩展功能。
nocant:	mov	dx,#0x3d4		! Check Cirrus 'clues'
	mov	al,#0x0c			; 首先向CRT控制寄存器的索引寄存器端口0x3d4写入要访问
	out	dx,al				; 的寄存器索引号 0x0c（对应显存起始地址高字节寄存器），
	inc	dx					; 然后从 0x3d5 端口读入显存起始地址高字节并暂存在 bl 中，
	in	al,dx				; 再把显存起始地址高字节寄存器清零。
	mov	bl,al
	xor	al,al
	out	dx,al
	dec	dx					; 接着向0x3d4端口输出索引0x1f，指出我们要在0x3d5端口
	mov	al,#0x1f			; 访问读取“Eagle ID”寄存器内容。
	out	dx,al
	inc	dx
	in	al,dx				; 从0x3d5端口读取“Eagle ID”寄存器值，并暂存在bh中。
	mov	bh,al				; 然后把该值高低4比特互换位置存放到cl中。再左移8位
	xor	ah,ah				; 后放入ch中，而cl中放入数值6。
	shl	al,#4
	mov	cx,ax
	mov	al,bh
	shr	al,#4
	add	cx,ax
	shl	cx,#8
	add	cx,#6				; 最后把cx值存放入ax中。此时ah中是换位后的“Eagle
	mov	ax,cx				; ID”值，al中是索引号6，对应定序/扩展寄存器。把ah
	mov	dx,#0x3c4			; 写到0x3c4端口索引的定序/扩展寄存器应该会导致Cirrus
	out	dx,ax				; 显示卡禁止扩展功能。
	inc	dx
	in	al,dx				; 如果扩展功能真的被禁止，那么此时读入的值应该为0。
	and	al,al				; 如果不为0则表示不是Cirrus显示卡，跳转继续检查其他卡。
	jnz	nocirr
	mov	al,bh				; 是Cirrus显示卡，则利用mov bh,al保存在bh中的“Eagle
	out	dx,al				; ID”原值再重新开启Cirrus卡扩展功能。此时读取的返回
	in	al,dx				; 值应该为1。若不是，则仍然说明不是Cirrus显示卡。
	cmp	al,#0x01
	jne	nocirr
; 现在知道该显示卡是Cirrus牌。于是首先调用rst3d4子程序恢复CRT控制器的显示起始
; 地址高字节寄存器内容，然后让si指向该品牌显示卡的可选行列值表dsccurrus，di指向扩展模式
; 个数和扩展模式号列表mocirrus，然后跳转到selmod处继续设置显示操作。
	call	rst3d4			; 恢复CRT控制器的显示起始地址高字节寄存器内容。
	lea	si,dsccirrus
	lea	di,mocirrus
	lea	cx,selmod
	jmp	cx
; 该子程序利用保存在bl中的值恢复CRT控制器的显示起始地址高字节寄存器内容。
rst3d4:	mov	dx,#0x3d4
	mov	al,bl
	xor	ah,ah
	shl	ax,#8
	add	ax,#0x0c
	out	dx,ax				; 注意，这是word输出！！ al--0x3d4，ah--0x3d5。
	ret	
; 现在检查系统中是不是Everex显示卡。方法是利用中断int 0x10功能0x70（ax =0x7000，
; bx=0x0000）调用Everex的扩展视频BIOS功能。对于Everes类型显示卡，该中断调用应该
; 会返回模拟状态，即有以下返回信息：
; al = 0x70，若是基于Trident的Everex显示卡；
; cl = 显示器类型：00-单色；01-CGA；02-EGA；03-数字多频；04-PS/2；05-IBM 8514；06-SVGA。
; ch = 属性：位7-6：00-256K，01-512K，10-1MB，11-2MB；位4-开启VGA保护；位 0-6845 模拟。
; dx = 板卡型号：位15-4：板类型标识号；位3-0：板修正标识号。0x2360-Ultragraphics II；0x6200-Vision VGA；0x6730-EVGA；0x6780-Viewpoint。
; di = 用BCD码表示的视频BIOS版本号。
nocirr:	call	rst3d4			! Check Everex 'clues'
	mov	ax,#0x7000				; 设置ax = 0x7000, bx=0x0000，调用int 0x10。
	xor	bx,bx
	int	0x10
	cmp	al,#0x70				; 对于Everes显示卡，al中应该返回值0x70。
	jne	noevrx
	shr	dx,#4					; 忽律板修正号（位3-0）。
	cmp	dx,#0x678				; 板类型号是0x678表示是一块Trident显示卡，则跳转。
	je	istrid
	cmp	dx,#0x236				; 板类型号是0x236表示是一块Trident显示卡，则跳转。
	je	istrid
; 现在确定了该显示卡的牌子。于是si指向该品牌显示卡的可选行列值表dsceverex，di指向扩展模式个数和扩展模式号列表moeverex，然后跳转到selmod处继续进行设置操作。
	lea	si,dsceverex
	lea	di,moeverex
	lea	cx,selmod
	jmp	cx
istrid:	lea	cx,ev2tri			; 是Trident类型的Everex显示卡，则跳转到ev2tri处理。
	jmp	cx
; 现在检查是不是Genoa显示卡。方式是检查其视频BIOS中的特征数字串（0x77、0x00、0x66、
; 0x99）。注意，此时es已经被设置成指向VGA卡上ROM BIOS所在的段0xc000。
noevrx:	lea	si,idgenoa		! Check Genoa 'clues'
	xor 	ax,ax			; 让ds:si指向特征数字串。
	seg es
	mov	al,[0x37]			; 取VGA卡上BIOS中0x37处的指针（它指向特征串）。
	mov	di,ax				; 因此此时es:di指向特征数字串开始处。
	mov	cx,#0x04
	dec	si
	dec	di
l1:	inc	si					; 然后循环比较这4个字节的特征数字串。
	inc	di
	mov	al,(si)
	seg es
	and	al,(di)
	cmp	al,(si)
	loope 	l1
	cmp	cx,#0x00			; 如果特征数字串完全相同，则表示是Genoa显示卡，
	jne	nogen				; 否则跳转去检查其他类型的显示卡。
; 现在确定了该显示卡的牌子。于是si指向该品牌显示卡的可选行列值表dscgenoa，di指向扩展模式个数和扩展模式号列表mogenoa，然后跳转到selmod处继续进行设置操作。
	lea	si,dscgenoa
	lea	di,mogenoa
	lea	cx,selmod
	jmp	cx
; 现在检查是不是Paradise显示卡。同样是采用比较显示卡上BIOS中特征串（“VGA=”）的方式。
nogen:	lea	si,idparadise		! Check Paradise 'clues'
	mov	di,#0x7d				; es:di指向VGA ROM BIOS的0xc000:0x007d处，该处应该有
	mov	cx,#0x04				; 4 个字符“VGA=”。
	repe
	cmpsb
	jne	nopara					; 若有不同的字符，表示不是Paradise显示卡，于是跳转。
	lea	si,dscparadise			; 否则让si指向Paradise显示卡的可选行列值表，让di指
	lea	di,moparadise			; 向扩展模式个数和模式号列表。然后跳转到selmod处去选
	lea	cx,selmod				; 择想要使用的显示模式。
	jmp	cx
; 现在检查是不是Trident（TVGA）显示卡。TVGA显示卡扩充的模式控制寄存器1（0x3c4 端口索引
; 的 0x0e）的位3--0 是64K内存页面个数值。这个字段值有一个特性：当写入时，我们需要首先把
; 值与0x02进行异或操作后再写入；当读取该值时则不需要执行异或操作，即异或前的值应该与写
; 入后再读取的值相同。下面代码就利用这个特性来检查是不是Trident显示卡。
nopara:	mov	dx,#0x3c4		! Check Trident 'clues'
	mov	al,#0x0e			; 首先在端口0x3c4输出索引号0x0e，索引模式控制寄存器1。
	out	dx,al				; 然后从0x3c5数据端口读入该寄存器原值，并暂存在ah中。
	inc	dx
	in	al,dx
	xchg	ah,al
	mov	al,#0x00			; 然后我们向该寄存器写入0x00，再读取其值--al。
	out	dx,al				; 写入0x00就相当于“原值”0x02异或0x02后的写入值，
	in	al,dx				; 因此若是Trident显示卡，则此后读入的值应该是0x02。
	xchg	al,ah			; 交换后，al=原模式控制寄存器1的值，ah=最后读取的值。
; 注释: “真奇怪...书中并没有要求这样操作，但是这对我的 Trident 显示卡起作用。如果不这样做，屏幕就会变模糊...
; 这几行附带有英文注释的语句执行如下操作：
; 如果 bl 中原模式控制寄存器1的位1在置位状态的话就将其复位，否则就将位1置位。
; 实际上这几条语句就是对原模式控制寄存器 1 的值执行异或 0x02 的操作，然后用结果值去设置（恢复）原寄存器值。
	mov	bl,al		! Strange thing ... in the book this wasn't 				'
	and	bl,#0x02	! necessary but it worked on my card which
	jz	setb2		! is a trident. Without it the screen goes
	and	al,#0xfd	! blurred ...
	jmp	clrb2		!
setb2:	or	al,#0x02	!
clrb2:	out	dx,al
	and	ah,#0x0f	; 取in	al,dx行最后读入值的页面个数字段（位3--0），如果
	cmp	ah,#0x02	; 该字段值等于0x02，则表示是Trident显示卡。
	jne	notrid
; 现在可以确定是Trident显示卡。于是si指向该品牌显示卡的可选行列值表dsctrident，
; di指向扩展模式个数和扩展模式号列表motrident，然后跳转到selmod处继续设置操作。
ev2tri:	lea	si,dsctrident
	lea	di,motrident
	lea	cx,selmod
	jmp	cx
; 现在检查是不是Tseng显示卡（ET4000AX或ET4000/W32类）。方法是对0x3cd端口对应的段
; 选择（Segment Select）寄存器执行读写操作。该寄存器高4位（位7--4）是要进行读操作的
; 64KB段号（Bank number），低4位（位 3--0）是指定要写的段号。如果指定段选择寄存器的
; 值是0x55（表示读、写第6个 64KB 段），那么对于Tseng显示卡来说，把该值写入寄存器后再读出应该还是0x55。
notrid:	mov	dx,#0x3cd		! Check Tseng 'clues'
	in	al,dx			! Could things be this simple ! :-)
	mov	bl,al			; 先从0x3cd端口读取段选择寄存器原值，并保存在bl中。
	mov	al,#0x55		; 然后我们向该寄存器中写入0x55。再读入并放在ah中。
	out	dx,al
	in	al,dx
	mov	ah,al
	mov	al,bl			; 接着恢复该寄存器的原值。
	out	dx,al
	cmp	ah,#0x55		; 如果读取的就是我们写入的值，则表明是Tseng显示卡。
 	jne	notsen
; 现在可以确定是Tseng显示卡。于是si指向该品牌显示卡的可选行列值表dsctseng，
; di指向扩展模式个数和扩展模式号列表motseng，然后跳转到selmod处继续设置操作。
	lea	si,dsctseng		; 于是让si指向Tseng显示卡的可选行列值的列表，让di
	lea	di,motseng		; 指向对应扩展模式个数和模式号列表，然后跳转到selmod
	lea	cx,selmod		; 去执行模式选择操作。
	jmp	cx
; 下面检查是不是Video7显示卡。端口0x3c2是混合输出寄存器写端口，而0x3cc是混合输出寄存
; 器读端口。该寄存器的位0是单色/彩色标志。如果为0则表示是单色，否则是彩色。判断是不是
; Video7显示卡的方式是利用这种显示卡的CRT控制扩展标识寄存器（索引号是0x1f）。该寄存器
; 的值实际上就是显存起始地址高字节寄存器（索引号0x0c）的内容和0xea进行异或操作后的值。
; 因此我们只要向显存起始地址高字节寄存器中写入一个特定值，然后从标识寄存器中读取标识值进行判断即可。
;
; 通过对以上显示卡和这里Video7显示卡的检查分析，我们可知检查过程通常分为三个基本步骤。
; 首先读取并保存测试需要用到的寄存器原值，然后使用特定测试值进行写入和读出操作，最后恢
; 复原寄存器值并对检查结果作出判断。
notsen:	mov	dx,#0x3cc		! Check Video7 'clues'
	in	al,dx
	mov	dx,#0x3b4			; 先设置dx为单色显示CRT控制索引寄存器端口号0x3b4。
	and	al,#0x01			; 如果混合输出寄存器的位0等于0（单色）则直接跳转，
	jz	even7				; 否则dx设置为彩色显示CRT控制索引寄存器端口号0x3d4。
	mov	dx,#0x3d4
even7:	mov	al,#0x0c		; 设置寄存器索引号为0x0c，对应显存起始地址高字节寄存器。
	out	dx,al
	inc	dx
	in	al,dx				; 读取显示内存起始地址高字节寄存器内容，并保存在bl中。
	mov	bl,al
	mov	al,#0x55			; 然后在显存起始地址高字节寄存器中写入值0x55，再读取出来。
	out	dx,al
	in	al,dx
	dec	dx					; 然后通过CRTC索引寄存器端口0x3b4或0x3d4选择索引号是
	mov	al,#0x1f			; 0x1f的Video7显示卡标识寄存器。该寄存器内容实际上就是
	out	dx,al				; 显存起始地址高字节和0xea进行异或操作后的结果值。
	inc	dx
	in	al,dx				; 读取Video7显示卡标识寄存器值，并保存在bh中。
	mov	bh,al
	dec	dx					; 然后再选择显存起始地址高字节寄存器，恢复其原值。
	mov	al,#0x0c
	out	dx,al
	inc	dx
	mov	al,bl
	out	dx,al
	mov	al,#0x55			; 随后我们来验证“Video7 显示卡标识寄存器值就是显存起始
	xor	al,#0xea			; 地址高字节和0xea进行异或操作后的结果值”。因此0x55
	cmp	al,bh				; 和0xea进行异或操作的结果就应该等于标识寄存器的测试值。
	jne	novid7				; 若不是Video7显示卡，则设置默认显示行列值。
; 现在可以确定是Video7显示卡。于是si指向该品牌显示卡的可选行列值表 dscvideo7，
; di指向扩展模式个数和扩展模式号列表movideo7，然后继续进行模式设置操作。
	lea	si,dscvideo7
	lea	di,movideo7
; 下面根据上述代码判断出的显示卡类型以及取得的相关扩展模式信息（si指向的行列值列表；di
; 指向扩展模式个数和模式号列表），提示用户选择可用的显示模式，并设置成相应显示模式。最后
; 子程序返回系统当前设置的屏幕行列值（ah = 列数；al=行数）。例如，如果系统中是ATI显示卡，
; 那么屏幕上会显示以下信息：
; Mode: COLSxROWS:
; 0. 132 x 25
; 1. 132 x 44
; Choose mode by pressing the corresponding number.
; 
; 这段程序首先在屏幕上显示NULL结尾的字符串信息“Mode: COLSxROWS:”。
selmod:	push	si
	lea	si,msg2
	call	prtstr
	xor	cx,cx
	mov	cl,(di)			; 此时cl中是检查出的显示卡的扩展模式个数。
	pop	si
	push	si
	push	cx
; 然后在每一行上显示出当前显示卡可选择的扩展模式行列值，供用户选用。
tbl:	pop	bx			; bx = 显示卡的扩展模式总个数
	push	bx
	mov	al,bl
	sub	al,cl
	call	dprnt		; 以十进制格式显示al中的值。
	call	spcing		; 显示一个点再空4个空格。
	lodsw				; 在ax中加载si指向的行列值，随后si指向下一个word值。
	xchg	al,ah		; 交换位置后al = 列数。
	call	dprnt		; 显示列数；
	xchg	ah,al		; 此时al中是行数值。
	push	ax
	mov	al,#0x78		; 显示一个小“x”，即乘号。
	call	prnt1
	pop	ax				; 此时al中是行数值。
	call	dprnt		; 显示行数。
	call	docr		; 回车换行。
	loop	tbl			; 再显示下一个行列值。cx中扩展模式计数值递减1。
; 在扩展模式行列值都显示之后，显示“Choose mode by pressing the corresponding number.”。
	pop	cx				; cl中是显示卡扩展模式总个数值。
	call	docr
	lea	si,msg3			; 显示“请按相应数字键来选择模式。”
	call	prtstr
; 然后从键盘口读取用户按键的扫描码，根据该扫描码确定用户选择的行列值模式号，并利用ROM 
; BIOS的显示中断int 0x10功能0x00来设置相应的显示模式。
; add cl,#0x80 的“模式个数值+0x80”是所按数字键-1的断开扫描码。对于0--9数字键，它们的断开
; 扫描码分别是：0 - 0x8B；1 - 0x82；2 - 0x83；3 - 0x84；4 - 0x85；
; 			  5 - 0x86；6 - 0x87；7 - 0x88；8 - 0x89；9 - 0x8A。
; 因此，如果读取的键盘断开扫描码小于0x82就表示不是数字键；如果扫描码等于0x8B则表示用户按下数字 0 键。
	pop	si				; 弹出原行列值指针（指向显示卡行列值表开始处）。
	add	cl,#0x80		; cl + 0x80 = 对应“数字键-1”的断开扫描码。
nonum:	in	al,#0x60	! Quick and dirty...
	cmp	al,#0x82		; 若键盘断开扫描码小于0x82则表示不是数字键，忽律该键。
	jb	nonum
	cmp	al,#0x8b		; 若键盘断开扫描码等于0x8b，表示按下了数字键0。
	je	zero
	cmp	al,cl			; 若扫描码大于扩展模式个数值对应的最大扫描码值，表示
	ja	nonum			; 键入的值超过范围或不是数字键的断开扫描码。否则表示
	jmp	nozero			; 用户按下并松开了一个非 0 数字按键。
; 下面把断开扫描码转换成对应的数字按键值，然后利用该值从模式个数和模式号列表中选择对应的
; 的模式号。接着调用机器ROM BIOS中断int 0x10功能0把屏幕设置成模式号指定的模式。最后再
; 利用模式号从显示卡行列值表中选择并在ax中返回对应的行列值。
zero:	sub	al,#0x0a	; al = 0x8b - 0x0a = 0x81。
nozero:	sub	al,#0x80	; 再减去0x80就可以得到用户选择了第几个模式。
	dec	al				; 从0起计数。
	xor	ah,ah			; int 0x10显示功能号=0（设置显示模式）。int 0x10显示功能号=0（设置显示模式）。
	add	di,ax
	inc	di				; di指向对应的模式号（跳过第1个模式个数字节值）。
	push	ax
	mov	al,(di)			; 取模式号--al中，并调用系统BIOS显示中断功能0。
	int 	0x10
	pop	ax
	shl	ax,#1			; 模式号乘2，转换成为行列值表中对应值的指针。
	add	si,ax
	lodsw				; 取对应行列值到ax中（ah = 列数，al = 行数）。
	pop	ds				; 恢复保存的ds原值。在ax中返回当前显示行列值。
	ret
; 若都不是上面检测地显示卡，我们就只好采用默认地80*25的标准行列值。
novid7:	pop	ds	! Here could be code to support standard 80x50,80x30
	mov	ax,#0x5019	
	ret

! Routine that 'tabs' to next col.
; 光标移动到下一制表位的子程序。
; 显示一个点字符'.'和 4 个空格。
spcing:	mov	al,#0x2e	; 显示一个点字符'.'。
	call	prnt1
	mov	al,#0x20
	call	prnt1	
	mov	al,#0x20
	call	prnt1	
	mov	al,#0x20
	call	prnt1	
	mov	al,#0x20
	call	prnt1
	ret	

! Routine to print asciiz-string at DS:SI
; 显示位于DS:SI处以NULL（0x00）结尾的字符串。
prtstr:	lodsb
	and	al,al
	jz	fin
	call	prnt1		; 显示al中的一个字符。
	jmp	prtstr
fin:	ret

! Routine to print a decimal value on screen, the value to be
! printed is put in al (i.e 0-255). 
; 显示十进制数字的子程序。显示值放在寄存器al中（0--255）。
dprnt:	push	ax
	push	cx
	mov	ah,#0x00		
	mov	cl,#0x0a
	idiv	cl
	cmp	al,#0x09
	jbe	lt100
	call	dprnt
	jmp	skip10
lt100:	add	al,#0x30
	call	prnt1
skip10:	mov	al,ah
	add	al,#0x30
	call	prnt1	
	pop	cx
	pop	ax
	ret

! Part of above routine, this one just prints ascii al
; 上面子程序的一部分。显示al中的一个字符。
; 该子程序使用中断0x10的0x0E功能，以电传方式在屏幕上写一个字符。光标会自动移到下一个
; 位置处。如果写完一行光标就会移动到下一行开始处。如果已经写完一屏最后一行，则整个屏幕
; 会向上滚动一行。字符0x07（BEL）、0x08（BS）、0x0A(LF)和 0x0D（CR）被作为命令不会显示。
; 输入：AL -- 欲写字符；BH -- 显示页号；BL -- 前景显示色（图形方式时）。
prnt1:	push	ax
	push	cx
	mov	bh,#0x00		; 显示页面。
	mov	cx,#0x01
	mov	ah,#0x0e
	int	0x10
	pop	cx
	pop	ax
	ret

! Prints <CR> + <LF>	; 显示回车+换行。

docr:	push	ax
	push	cx
	mov	bh,#0x00
	mov	ah,#0x0e
	mov	al,#0x0a
	mov	cx,#0x01
	int	0x10
	mov	al,#0x0d
	int	0x10
	pop	cx
	pop	ax
	ret	
;;;;; 跟显示卡相关代码至此结束 ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;; 全局描述符表（临时，每个描述符项长8个字节）
gdt:
	.word	0,0,0,0		! dummy	;第1个描述符，不用

	; 系统代码段描述符，在GDT表的偏移量是0x08。它是内核代码段选择符的值。
	.word	0x07FF		! 8Mb - limit=2047 (0--2047，因此是 2048*4096=8Mb)
	.word	0x0000		! base address=0
	.word	0x9A00		! code read/exec		; 代码段为只读，可执行
	.word	0x00C0		! granularity=4096, 386 ; 颗粒度4K，32位
	; 系统数据段描述符，在 GDT 表中这里的偏移量是 0x10。它是内核数据段选择符的值。
	.word	0x07FF		! 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000		! base address=0
	.word	0x9200		! data read/write		; 数据段为可读可写
	.word	0x00C0		! granularity=4096, 386	; 颗粒度4K，32位

; 加载中断描述符表寄存器idtr的指令lidt要求的6字节操作数。前2字节是IDT表的限长，后4字节是idt表在线性地址空间中的32位基地址。
; 注：CPU要求在进入保护模式之前需设置idt表，因此这里先设置一个长度为0的空表。
idt_48:
	.word	0			! idt limit=0	; idt的限长
	.word	0,0			! idt base=0L	; idt表在线性地址空间中的32位基地址

; 加载全局描述符表寄存器gdtr的指令lgdt要求的6字节操作数。前2字节是gdt表的限
; 长，后4字节是gdt表的线性基地址。这里全局表长度设置为2KB（0x7ff即可），因为每8
; 字节组成一个段描述符项，所以表中共可有256项。4字节的线性基地址为0x0009<<16 +
; 0x0200 + gdt，即0x90200 + gdt。(符号gdt是全局表在本程序段中的偏移地址)
gdt_48:
	.word	0x800		! gdt limit=2048, 256 GDT entries 	
						; 表限长2k
	.word	512+gdt,0x9	! gdt base = 0X9xxxx 
						; （线性地址空间）基地址：0x90200 + gdt

;;;;; 以下跟显示卡相关（可以不看） ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
msg1:		.ascii	"Press <RETURN> to see SVGA-modes available or any other key to continue."
		db	0x0d, 0x0a, 0x0a, 0x00
msg2:		.ascii	"Mode:  COLSxROWS:"
		db	0x0d, 0x0a, 0x0a, 0x00
msg3:		.ascii	"Choose mode by pressing the corresponding number."
		db	0x0d, 0x0a, 0x00

; 下面是4个显示卡的特征数据串。		
idati:		.ascii	"761295520"
idcandt:	.byte	0xa5					; 标号idcandt意思是ID of Chip AND Tech.
idgenoa:	.byte	0x77, 0x00, 0x66, 0x99
idparadise:	.ascii	"VGA="

; 下面是各种显示卡可使用的扩展模式个数和对应的模式号列表。其中每一行第1个字节是模式个
; 数值，随后的一些值是中断0x10功能0（AH=0）可使用的模式号。例如从moati可知，对于ATI
; 牌子的显示卡，除了标准模式以外还可使用两种扩展模式：0x23 和 0x33。
! Manufacturer:	  Numofmodes:	Mode:
; 厂家：			模式数量：		模式列表：
moati:		.byte	0x02,	0x23, 0x33 
moahead:	.byte	0x05,	0x22, 0x23, 0x24, 0x2f, 0x34
mocandt:	.byte	0x02,	0x60, 0x61
mocirrus:	.byte	0x04,	0x1f, 0x20, 0x22, 0x31
moeverex:	.byte	0x0a,	0x03, 0x04, 0x07, 0x08, 0x0a, 0x0b, 0x16, 0x18, 0x21, 0x40
mogenoa:	.byte	0x0a,	0x58, 0x5a, 0x60, 0x61, 0x62, 0x63, 0x64, 0x72, 0x74, 0x78
moparadise:	.byte	0x02,	0x55, 0x54
motrident:	.byte	0x07,	0x50, 0x51, 0x52, 0x57, 0x58, 0x59, 0x5a
motseng:	.byte	0x05,	0x26, 0x2a, 0x23, 0x24, 0x22
movideo7:	.byte	0x06,	0x40, 0x43, 0x44, 0x41, 0x42, 0x45

; 下面是各种牌子VGA显示卡可使用的模式对应的列、行值列表。例如dscati行表示ATI显示卡两
; 种扩展模式的列、行值分别是 132 x 25、 132 x 44。
!			msb = Cols lsb = Rows:
;			高字节 = 列数 低字节 = 行数
dscati:		.word	0x8419, 0x842c							; ATI卡可设置列、行值。
dscahead:	.word	0x842c, 0x8419, 0x841c, 0xa032, 0x5042	; Ahead 卡可设置值。
dsccandt:	.word	0x8419, 0x8432
dsccirrus:	.word	0x8419, 0x842c, 0x841e, 0x6425
dsceverex:	.word	0x5022, 0x503c, 0x642b, 0x644b, 0x8419, 0x842c, 0x501e, 0x641b, 0xa040, 0x841e
dscgenoa:	.word	0x5020, 0x642a, 0x8419, 0x841d, 0x8420, 0x842c, 0x843c, 0x503c, 0x5042, 0x644b
dscparadise:	.word	0x8419, 0x842b
dsctrident:	.word 	0x501e, 0x502b, 0x503c, 0x8419, 0x841e, 0x842b, 0x843c
dsctseng:	.word	0x503c, 0x6428, 0x8419, 0x841c, 0x842c
dscvideo7:	.word	0x502b, 0x503c, 0x643c, 0x8419, 0x842c, 0x841c
;;;;;; 显示卡相关 到这结束 ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

.text
endtext:
.data
enddata:
.bss
endbss:
