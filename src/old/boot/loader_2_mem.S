%include "boot.inc"
section loader vstart=LOADER_BASE_ADDR
	
	LOADER_STACK_TOP equ LOADER_BASE_ADDR	; 一会为保护模式 esp 初始化

	jmp near loader_start 	; near 强调这里是3字节
	db 0 					
	dd 0					; 凑八字节舒服点, 0xb08 就是 mem_bytes 地址，0xc00 是loader_start
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

   	total_mem_bytes:
   		dd 0		 
							
	gdt_ptr:				; 构造 48 位 GDTR
		dw GDT_LIMIT
		dd GDT_BASE
	
  	ards_buf:	; 人工对齐: _mem_bytes 4+gdt_p 6+ards_buf 236+ards_nr 2+jmp3+5= 256 byte
  		times 236 db 0
   	ards_nr:	; 用于记录ards结构体数量
   		dw 0		      
 

loader_start: 
 
	; ----- int 15h eax = 0000E820h ,edx = 534D4150h ('SMAP') 获取内存布局  -----
	; 填好输入需要的 reg
   	xor ebx, ebx		 	;第一次调用时，ebx值要为0
   	mov edx, 0x534d4150	    ;edx只赋值一次，循环体中不会改变
   	mov di,  ards_buf	  	;ards结构缓冲区

	.e820_mem_get_loop:	  	;循环获取每个ARDS内存范围描述结构
		mov eax, 0x0000e820 ;执行int 0x15后,eax值变为0x534d4150,所以每次执行int前都要更新为子功能号 
	 	mov ecx, 20		  	;ARDS地址范围描述符结构大小是20字节
	   	int 0x15
	    jc  .e820_failed_so_try_e801   
	   						;若cf位为1则跳转 0xe801 子功能
	    add di,   cx		;使di增加20字节指向缓冲区中新的ARDS结构位置
	    inc word [ards_nr]  ;记录ARDS数量
	    cmp ebx,  0		    ;若ebx为0且cf不为1,这说明ards全部返回，当前已是最后一个
	    jnz .e820_mem_get_loop
 
	; 在所有ards结构中，找出(base_add_low + length_low)的最大值，即内存的容量。
   	mov cx, [ards_nr]	    ;遍历每一个ARDS结构体,循环次数是ARDS的数量
    mov ebx, ards_buf 
    xor edx, edx		    ;edx为最大的内存容量,在此先清0
	
	.find_max_mem_area: 	;无须判断type是否为1,最大的内存块一定是可被使用
		mov eax, [ebx]	  	;base_add_low
		add eax, [ebx+8] 	;length_low
		add ebx, 20		 	;指向缓冲区中下一个ARDS结构
		cmp edx, eax	   	;冒泡排序，找出最大,edx寄存器始终是最大的内存容量
		jge .next_ards
		mov edx, eax	 	;edx为总内存大小
	
	.next_ards:
   		loop .find_max_mem_area
   		jmp  .mem_get_ok


	; ----- int 15h ax = E801h 获取内存大小,最大支持4G -----
	; 返回后, ax cx 值一样,以KB为单位,bx dx值一样,以64KB为单位
	; 在ax和cx寄存器中为低16M,在bx和dx寄存器中为16MB到4G。
	.e820_failed_so_try_e801:
    mov ax, 0xe801
    int 0x15 				;若当前e801方法失败,就尝试0x88方法
    jc  .e801_failed_so_try88   

	; 1 先算出低15M的内存,ax和cx中是以KB为单位的内存数量,将其转换为以byte为单位
    mov cx,  0x400	     	;cx和ax值一样,cx用做乘数
    mul cx 
    shl edx, 16
    and eax, 0x0000FFFF
    or  edx, eax
    add edx, 0x100000 		;ax只是15MB,故要加1MB
    mov esi, edx	    	;先把低15MB的内存容量存入esi寄存器备份

	; 2 再将16MB以上的内存转换为byte为单位,寄存器bx和dx中是以64KB为单位的内存数量
    xor eax, eax
    mov ax,  bx		
    mov ecx, 0x10000		;0x10000十进制为64KB
    mul ecx					;32位乘法,默认的被乘数是eax,积为64位,高32位存入edx,低32位存入eax.
    add esi, eax			;由于此方法只能测出4G以内的内存,故32位eax足够了,edx肯定为0,只加eax便可
    mov edx, esi			;edx为总内存大小
    jmp .mem_get_ok


	; ----- int 15h ah = 0x88 获取内存大小,只能获取64M之内 ----- 
   	; 返回后,ax存入的是以kb为单位的内存容量
	.e801_failed_so_try88: 
    mov ah,  0x88
    int 0x15
    jc  .error_hlt
    and eax, 0x0000FFFF
      
    ;16位乘法，被乘数是ax,积为32位.积的高16位在dx中，积的低16位在ax中
    mov cx,  0x400     		;0x400等于1024,将ax中的内存容量换为以byte为单位
    mul cx
    shl edx, 16	     		;把dx移到高16位
    or  edx, eax	     	;把积的低16位组合到edx,为32位的积
    add edx, 0x100000  		;0x88子功能只会返回1MB以上的内存,故实际内存大小要加上1MB


	.mem_get_ok:

   		mov [total_mem_bytes], edx	 ;将内存换为byte单位后存入total_mem_bytes处。
  
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
				

   	.error_hlt:		      ;出错则挂起
	   hlt
 

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
 
