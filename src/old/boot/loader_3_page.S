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
   	jmp .mem_get_ok


	; ----- int 15h ax = E801h 获取内存大小,最大支持4G -----
	; 返回后, ax cx 值一样,以KB为单位,bx dx值一样,以64KB为单位
	; 在ax和cx寄存器中为低16M,在bx和dx寄存器中为16MB到4G。
	.e820_failed_so_try_e801:
    	mov ax, 0xe801
    	int 0x15 			;若当前e801方法失败,就尝试0x88方法
 	jc  .e801_failed_so_try88   

		; 1 先算出低15M的内存,ax和cx中是以KB为单位的内存数量,将其转换为以byte为单位
    	mov cx,  0x400	  	;cx和ax值一样,cx用做乘数
    	mul cx 
    	shl edx, 16
    	and eax, 0x0000FFFF
    	or  edx, eax
    	add edx, 0x100000  	;ax只是15MB,故要加1MB
    	mov esi, edx		;先把低15MB的内存容量存入esi寄存器备份

		; 2 再将16MB以上的内存转换为byte为单位,寄存器bx和dx中是以64KB为单位的内存数量
    	xor eax, eax
    	mov ax,  bx		
    	mov ecx, 0x10000	;0x10000十进制为64KB
    	mul ecx				;32位乘法,默认的被乘数是eax,积为64位,高32位存入edx,低32位存入eax.
    	add esi, eax		;由于此方法只能测出4G以内的内存,故32位eax足够了,edx肯定为0,只加eax便可
    	mov edx, esi		;edx为总内存大小
  	jmp .mem_get_ok


	; ----- int 15h ah = 0x88 获取内存大小,只能获取64M之内 ----- 
   	; 返回后,ax存入的是以kb为单位的内存容量
	.e801_failed_so_try88: 
    	mov ah,  0x88
    	int 0x15
    jc  .error_hlt
    	and eax, 0x0000FFFF
    	mov cx,  0x400    	;0x400等于1024,将ax中的内存容量换为以byte为单位
    	mul cx 				;16位mul，ax*cx,积32位，高16 -> dx, 低16 -> ax
    	shl edx, 16	    	;把dx移到高16位
    	or  edx, eax		;把积的低16位组合到edx,为32位的积
    	add edx, 0x100000  	;0x88子功能只会返回1MB以上的内存,故实际内存大小要加上1MB
 
	.mem_get_ok:
   							;将内存换为byte单位后存入total_mem_bytes处。
   		mov [total_mem_bytes], edx	 

		; -------------------- 准备进入保护模式 --------------------
   		; 打开 A20
		; 加载 GDT
		; CR0 的 PE 置 1
   							; === A20 ===
   		in 	al,   0x92
   		or 	al,   0000_0010B
   		out 0x92, al
   							; 把16+32位加载到GDTR
   		lgdt [gdt_ptr]
   							; CR0 -> PE
   		mov eax, cr0
   		or 	eax, 0x00000001
   		mov cr0, eax

   		mov byte [gs:160], 'P'
   		mov byte [gs:162], '_'
   		mov byte [gs:164], 'M'
   		mov byte [gs:166], 'O'
   		mov byte [gs:168], 'D'
   		mov byte [gs:170], 'E'

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
				

   	.error_hlt:		      	;出错则挂起
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

	call setup_page


	; 为重启加载 GDT 做准备。因为页表中会将内核放到 3GB 以上的地址，我们也把 GDT 放在内核的地址空间，
	; 通过 sgdt 指令将 GDTR dump 到 gdt_ptr 处，待条件成熟时，用新地址重新从 gdt_ptr加载GDT
    sgdt [gdt_ptr]	     
 
	; 不能让用户进程直接控制显存。故显存段的段基址也要改为 3GB 以上。显存段描述符中段基址 + 0xc0000000
	mov ebx,  [gdt_ptr + 2]  				; 取出 GDT_BASE
	or  dword [ebx + 0x18 + 4], 0xc0000000  ; 视频段是第3个段描述符,每个描述符是8字节，+ 0x18
				; 段描述符高四字节的高八位 是段基址的31~24位，之前只访问1M内，此8位肯定全0，可以直接or

	; GDT_BASE += 0xc0000000 不是必须的，如果分页后不重复加载 GDT 的话，也可以不修改 GDT_BASE
	add dword [gdt_ptr + 2], 0xc0000000

	add esp, 0xc0000000        ; 将栈指针同样映射到内核地址

	; 把页目录表地址赋给cr3
	mov eax, PAGE_DIR_TABLE_POS
	mov cr3, eax

	; 打开 cr0 pg位
	mov eax, cr0
	or  eax, 0x80000000
	mov cr0, eax

	; 在开启分页后,用gdt新的地址重新加载
	lgdt [gdt_ptr]             ; 重新加载

	mov byte [gs:160], 'V'     ;视频段段基址已经被更新,用字符v表示virtual addr

	jmp $


;------------- 创建内核的 页目录表、页表 ---------------
setup_page:
;--------------------------------------------------

	mov ecx, 4096 		; 把页目录表占用的4KB逐字节清0，loop 4096次
    mov esi, 0
	.clear_page_dir:
	    mov byte [PAGE_DIR_TABLE_POS + esi], 0
	    inc esi
	loop .clear_page_dir

	; 开始创建页目录项 PDE
	.create_pde:				     
	    mov eax, PAGE_DIR_TABLE_POS
	    add eax, 0x1000 			; 第一个页表的位置 0x101000
	    mov ebx, eax				; 为 .create_pte 备份

		; 现在程序运行的是loader(1MB内)，因第0个页目录项代表的页表表示空间是 0～4MB(我们是这么设计的布局)
		; loader里访问的线性地址是 0～1M，第0个PDT代表的PT必须映射到0～1MB.
		; 又因为我们把OS放在虚拟地址的 3G～4G，即第 1024*3/4=768～1024 的目录项代表的页表，
		; 第768对应的4MB为 0xc0000000~0xc03fffff，OS的‘第一页’映射的也是物理内存低端1M
		; 因此页目录项0和0xc00=3*1024都存第一个页表的地址: PDT_ADDR + 0x1000 
		; 为把地址映射为内核地址做准备
	    or eax, PG_US_U | PG_RW_W | PG_P		; User/Write/Present，查布局 111b 作页目录项的属性，
	    mov [PAGE_DIR_TABLE_POS + 0x0], eax     ; 0号目录项, 写入第一个页表的位置 0x101000 + 属性 0x7
	    mov [PAGE_DIR_TABLE_POS + 0xc00], eax   ; 768号目录项,0xc00以上的目录项用于内核空间,
						     					; 映射后 0xc0000000~0xffffffff 的1G为内核
						     					; 0x0~0xbfffffff 共3G属于用户进程
		; 为什么使用属性 PG_US_U 而不是 PG_US_S ？ 
		; 此时若使用 PG_US_S 也没问题，但将来会实现 init 进程，它位于内核地址空间
		; 但它是用户级程序，会在特权级3 下执行。需要授权它访问内核空间，因此使用属性 PG_US_U
	    sub eax, 0x1000
	    mov [PAGE_DIR_TABLE_POS + 4092], eax	; 使最后一个目录项指向 页目录表地址
	    										; 为了将来能够动态操作页表
	; 下面创建页表项(PTE)

	; 第0个PDE对应的PT：
	mov ecx, 256				     	; 1M低端内存 / 每页大小4k = 256页即可
	mov esi, 0
	mov edx, PG_US_U | PG_RW_W | PG_P 	; 属性 111b, User/Write/Present
	.create_pte:						; 创建Page Table Entry
    	mov [ebx+esi*4], edx			; ebx 已在上面赋值为0x101000,第一个页表的地址 	
    	add  edx, 4096					; 通过这种分配，低端1MB的虚拟地址=物理地址
    	inc  esi
    loop .create_pte

    ; 因为1023号已经指向页目录表本身了，内核空间实际大小为 1GB-4MB，255个内核PDE
	; 本项目的内核只用了1MB，用一个PDE都绰绰有余。但为了实现内核共享，必须填满255个
    mov eax, PAGE_DIR_TABLE_POS
    add eax, 0x2000 		     		; 此时eax为第二个页表的位置
    or  eax, PG_US_U | PG_RW_W | PG_P  	; 属性 U/W/P
    mov ebx, PAGE_DIR_TABLE_POS
    mov ecx, 254			     ; 去掉刚才创建过的768号，1023号，循环254次
    mov esi, 769
	.create_kernel_pde:
    	mov [ebx+esi*4], eax
   		inc  esi
   		add  eax, 0x1000
   	loop .create_kernel_pde
    ret

