%include "boot.inc"
section loader vstart=LOADER_BASE_ADDR
	
	LOADER_STACK_TOP equ LOADER_BASE_ADDR	; 一会为保护模式 esp 初始化

	jmp loader_start
							; GDT 只是一片内存区域，里面每 8 字节一个表项——段描述符，起始地址：
	GDT_BASE:				; 第 0 个描述符不可用，写入8字节 0，这8字节写入方式任意，这里用的俩 dd
		dd 0x00000000
		dd 0x00000000
							; 我们在 boot.inc 里事先定义了三个段描述符：		
	CODE_DESC:				; 代码段描述符 
		dd 0x0000FFFF 		; 段基址 15～0，段界限 15～0
		dd DESC_CODE_HIGH4	; 界限拼接后：20位1 访问共 4G
	
	DATA_STACK_DESC: 		; 数据段栈段描述符
		dd 0x0000FFFF 
		dd DESC_DATA_HIGH4

	VIDEO_DESC:    			; 显存段描述符
							; 现在只做文本模式输出，为方便显存操作，显存段不采用平坦模型。
							; 直接把段基址置为文本模式的起始地址 0xb8000
							; 段大小：0xbffff-0xb8000=0x7fff 段粒度：4k 段界限：0x7fff/4k=7
		dd 0x80000007		
		dd DESC_VIDEO_HIGH4	; 此时 DPL0, so?

	GDT_SIZE 		equ $ - GDT_BASE
	GDT_LIMIT 		equ GDT_SIZE - 1
	
	times 60 dq 0 			; 预留 60 个描述符的 slot

	SELECTOR_CODE 	equ (0x0001 << 3) + TI_GDT + RPL0 	; 相当于（CODE_DESC - GDT_BASE)/8 + TI_GDT + RPL0
	SELECTOR_DATA 	equ (0x0002 << 3) + TI_GDT + RPL0 	; 同上
	SELECTOR_VIDEO 	equ (0x0003 << 3) + TI_GDT + RPL0	; 同上
							; 构造 48 位 GDTR
	gdt_ptr:
		dw GDT_LIMIT
		dd GDT_BASE

	loadermsg:
		db 'LOADER IN REAL MODE'

	loader_start: 
		mov sp, LOADER_BASE_ADDR
		mov bp, loadermsg			; ES:BP = 字符串地址
		mov cx, 19					; str len
		mov ax, 0x1301				; see mbr_1.S
		mov bx, 0x2			
		mov dx, 0x100				; write at second line, 0 col 
		
		int 0x10 

		; -------------------- 准备进入保护模式 --------------------
   		; 打开 A20
		; 加载 GDT
		; CR0 的 PE 置 1
   							; === A20 ===
   		in 	al,   0x92
   		or 	al,   0000_0010B
   		out 0x92, al
   							; === GDT === 
   		lgdt [gdt_ptr]
   							; CR0 -> PE
   		mov eax, cr0
   		or 	eax, 0x00000001
   		mov cr0, eax
   		; ----- 至此已进入保护模式！----- 以上16位指令，下面这句也是

   		jmp dword SELECTOR_CODE:p_mode_start	
   			; 现在还是16位保护模式下，为使用32位reg，dword使机器码前加 0x66反转操作数
			; 32位CPU在实模式下段描述符缓冲寄存器(DR)中存的是 左移4位后的段基址(共20位)，其余位为0
   			; 由于流水线的工作方式，指令间重叠，上一行在执行时，本行已经送上流水线了
   			; 但DR内容还是实模式下的内容，在保护模式里解读出 D 位为0，表示操作数16位，流水线上指令全按照16位操作数译码。
			; 如果把本行去掉，则声明[bits 32]后的代码开始出错（CPU按16位格式译32位代码）
   			; 综上：
   			; 1、需要马上更新DR，即往相应段寄存器中加载选择子
   			; 2、需要清空流水线，避免分支预测的影响（回顾 4.4.4 cpu优化策略）
   			; 而本行代码 jmp 自带清空流水线；
   			; 由于已进入保护模式，CPU认为 SELECTOR_CODE 是选择子而不是段基址
   			; D位0表示16位操作数，而当前操作数大小就是16位，且 p_mode_start 并未超过16位
   			; 所以用 dword 并没有发挥功效（但应该这么用）
				

	[bits 32]
	p_mode_start:
							; 用选择子初始化各段寄存器
		mov ax, SELECTOR_DATA
		mov ds, ax
		mov es, ax
		mov ss, ax
		mov esp,LOADER_STACK_TOP
		mov ax, SELECTOR_VIDEO
		mov gs, ax

		mov byte [gs:480], 'P'	
							; 一行默认80字符，默认25行，一个字符两位
							; 未传入属性值时默认黑底白字
	 
		jmp $
 
