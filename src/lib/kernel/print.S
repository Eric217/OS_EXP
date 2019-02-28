TI_GDT         equ  0
RPL0           equ  0
SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL0     ; 视频段选择子


section .data 
put_int_buffer:   			; 定义8字节缓冲区用于数字到字符的转换
	dq 0   


[bits 32]
section .text 

;-------------------------- put_str ---------------------------------
; 通过put_char来打印的字符串
; 栈中参数为要打印的字符串（以0字符结尾）
global put_str
put_str:
;--------------------------------------------------------------------
	push ebx 				; 备份用到的寄存器
	push ecx
	xor  ecx,  ecx	 		; 清空 
	mov  ebx, [esp + 12]  	; 从栈中得到待打印的字符串首地址，返回地址4+2*4 e-reg 
.go_on:
	mov  cl,  [ebx]
	cmp  cl,   0			; 如果处理到了字符串尾,跳到结束处返回
jz  .str_over
	push ecx		    	; 为put_char函数传递参数
	call put_char 
	add  esp, 4		        ; 回收栈空间
	inc  ebx		        ; 使ebx指向下一个字符(+1 Byte)
jmp .go_on

.str_over:
	pop ecx
	pop ebx
	ret 

;-------------------------- put_char -------------------------------
; 把栈中的1个字符写入光标所在处, 更新光标
global put_char
put_char:
;-------------------------------------------------------------------   
 	; (1) 备份寄存器
    pushad         			; pushad = push all double，该指令压入所有双字长(32-bit)的寄存器，
                			; 这里共8个，入栈顺序 EAX->ECX->EDX->EBX->ESP->EBP->ESI->EDI 

	; loader 中已将 gs 赋正确的选择子，程序进入保护模式就继承了0特权级，内核也在0特权级下工作。
	; 但有用户进程之后，用户进程用 iretd 返回执行，CPU 在执行 iretd 指令时会做特权检查：
	; 它检查 DS ES FS GS 等非代码段、栈段寄存器的内容（选择子）：
	; 如果有指向的段描述符的 DPL高于从 iretd 返回后新的 CPL, CPU 就会将该段寄存器赋值为 0
	; CPU 是不会让低特权级程序有访问高特权级资源的机会的，段寄存器置0了,不访问的话，一切正常
	; 一旦用到该段寄存器，由于选择子16位全0，取第0个段描述符抛异常。因此每次打印都重新赋gs值
	mov ax, SELECTOR_VIDEO 	; 为保险起见 
	mov gs, ax              ; 不能直接把立即数送入段寄存器

	; (2) 获取当前光标位置 
	; 高8位
	mov dx, 0x03d4 			; 索引寄存器
	mov al, 0x0e 			; 用于提供光标位置的高8位
	out dx, al     			; 写入索引
	mov dx, 0x03d5 			; 通过读写数据端口0x3d5来获得或设置光标位置 
	in  al, dx	  			; 读出光标位置的高8位
	mov ah, al     			; 只接受 al，这里复制到 ah

	; 低8位
	mov dx, 0x03d4
	mov al, 0x0f      
	out dx, al         
	mov dx, 0x03d5 			; 低8位端口号
	in  al, dx    			; 读出

	mov bx, ax	   			; 习惯用bx存基址，将光标存入bx

	; (3) 在栈中获取待打印的字符，对控制/可见字符跳转处理
	mov ecx, [esp + 36]	 	; pushad 压入 4*8 + 主调函数的返回地址 4
	cmp cl,  0xd			; CR carriage return 是0x0d，LF line feed 是0x0a
jz  .is_carriage_return  	; 这里 回车符 或 换行符 都按 回车换行 CRLF 处理 
	cmp cl,  0xa
jz  .is_line_feed

	cmp cl,  0x8		  	; BS backspace 是8
jz  .is_backspace

jmp .put_other	    


.is_backspace:		       
	; 光标移向前一个显存位置；原处写为空格或空字符 
	dec bx 					; 被删字符位置
	shl bx, 1 				; 乘二为字符偏移量
	mov byte [gs:bx], 0		; 将待删除的字符填0或空格皆可
	inc bx 					; 属性位
	mov byte [gs:bx], 7 	; 黑底白字
	shr bx, 1 				; 除以二为当前光标应该在的位置 
jmp .set_cursor


.put_other:
	shl bx, 1				; 光标值乘2，字节
	mov [gs:bx], cl			; ascii字符本身
	inc bx
	mov byte [gs:bx], 0x07 	; 字符属性
	shr bx, 1				; 恢复原光标值
	inc bx				  	; 下一个光标值
	cmp bx, 2000		   	; 若光标值小于2000,表示未写到显存的最后,则去设置新的光标值
jl  .set_cursor			
					  		; 若超出屏幕字符数大小(2000)则滚屏：回车换行
.is_line_feed:				; 换行符LF(\n)
.is_carriage_return:		; 回车符CR(\r) 都处理为 CRLF
	xor dx, dx				; dx 被除数的高16位, 清0 
	mov ax, bx				; ax 被除数的低16位 
	mov si, 80				; 除以80，余数在dx中
	div si				   
	sub bx, dx			 	; 光标值减去余数，当前行首坐标
.is_carriage_return_end: 	; 上面处理 \r 结束
    add bx, 80 				; 新行坐标
    cmp bx, 2000 			; 若光标值小于2000,表示未写到显存的最后,则去设置新的光标值
.is_line_feed_end:			 
jl  .set_cursor

	; (4) 滚屏 屏幕行范围是 0~24,滚屏的原理是将屏幕的 1~24 行搬运到 0~23 行,再将第24行用空格填充（项目采用）
	; 另一种方案：显存32KB可存2000字符，显卡提供了两个寄存器设置显存中显示到屏幕的字符的起始地址base，过高回卷，上面同时需要光标base+2000时滚屏等多处修改。
.roll_screen:				; 若超出屏幕大小，开始滚屏
	cld  
	mov ecx, 960			; 一共有2000-80=1920个字符要搬运,共1920*2=3840字节 共搬3840/4=960次4字节
	mov esi, 0xb80a0		; 第1行行首
	mov edi, 0xb8000		; 第0行行首
	rep movsd				; 重复拷贝过程

 	; 将最后一行填充为空白
	mov ebx, 3840		 	; 最后一行 第一个字节偏移 1920 * 2
	mov ecx, 80           	; 一行80字符 loop 80次
	.cls:
		mov word [gs:ebx], 0x0700 	 
		add ebx, 2
	loop .cls 
	mov bx, 1920			; 将光标值重置为1920,最后一行的首字符 
	
	; (5) 更新光标 
.set_cursor:   	 			; 位置在 bx 中		   
	
	; 高8位 
	mov dx, 0x03d4			; 索引寄存器
	mov al, 0x0e			; 用于提供光标位置的高8位
	out dx, al
	mov dx, 0x03d5			; 通过读写数据端口 0x3d5 设置光标位置 
	mov al, bh
	out dx, al

	; 低8位 
	mov dx, 0x03d4
	mov al, 0x0f
	out dx, al
	mov dx, 0x03d5 
	mov al, bl
	out dx, al

.put_char_done: 
	popad
	ret

;------------------------ put_int --------------------------
; 栈中参数为待打印的数字(32-bit)
; 在屏幕上打印16进制数字，不打印前缀0x 
global put_int
put_int:
;-----------------------------------------------------------
    ; 每4bit一个16进制数字，取8次，存到buffer（注意适配小端字节序）  
	pushad
	mov ebp, esp 				; 习惯
	mov eax,[ebp + 4 + 4*8] 	; call的返回地址占4字节+pushad的8个4字节
	mov edx, eax
	mov edi, 7             		; 指定在put_int_buffer中初始的偏移量
	mov ecx, 8			  		; loop 8次
	mov ebx, put_int_buffer

	.16based_4bits:			      
		and edx, 0x0000000F	 	; 取低4位 
		cmp edx, 9			   	; 数字0～9和a~f需要分别处理成对应的字符
	jg  .is_A2F 
		add edx, '0'			; ≤9, 加上'0'的ASCII序号即该数字的ASCII码
	jmp .store
	.is_A2F:
		sub edx, 10				; A~F 减去10 加上'A'的ASCII序号即A~F的ASCII码
		add edx, 'A'
  
	.store: 	 				; 按照类似“大端”的顺序存储到缓冲区put_int_buffer
		mov [ebx+edi], dl		; 此时dl中是数字对应的字符的ascii码
		dec edi 
		shr eax, 4 				; 接下来四位继续
		mov edx, eax  
	loop .16based_4bits

	; 现在开始打印 put_int_buffer 中的字符
	; 先把高位的0去掉，如 00000123 变成 123
	.ready_to_print:
		inc edi			      	; 此时edi为-1，加1
	.skip_prefix_0:  
		cmp edi, 8			 	; edi若等于8是比较的第九次，说明待打印的字符串全0 
	je  .full0 
 	.go_on_skip:     			; 跳过0，最后让edi作非0的最高位字符的偏移量
		mov cl, [put_int_buffer+edi]
		inc edi
		cmp cl, '0' 
 	je  .skip_prefix_0		    ; 是0，继续判断下一位
		dec edi			        ; 找到了最高非0位，此时edi在上面的inc操作中指向了下一个字符，恢复edi为当前     
	jmp .put_each_num


	.full0:
		mov cl, '0'			 	; 输入的数字为全0时，则只打印0
	.put_each_num:
		push ecx			 	; cl中为可打印的字符
		call put_char 	
		add  esp, 4
		inc  edi			  	; 使edi指向下一个字符，放到cl寄存器
	 	mov  cl, [put_int_buffer+edi]	        
		cmp  edi, 8
	jl .put_each_num 			; 等于8的时候就不打印了
		popad
	ret

 
global set_cursor
set_cursor:
   pushad
   mov bx, [esp+36]
;;;;;;; 1 先设置高8位 ;;;;;;;;
   mov dx, 0x03d4			  ;索引寄存器
   mov al, 0x0e				  ;用于提供光标位置的高8位
   out dx, al
   mov dx, 0x03d5			  ;通过读写数据端口0x3d5来获得或设置光标位置 
   mov al, bh
   out dx, al

;;;;;;; 2 再设置低8位 ;;;;;;;;;
   mov dx, 0x03d4
   mov al, 0x0f
   out dx, al
   mov dx, 0x03d5 
   mov al, bl
   out dx, al
   popad
   ret

