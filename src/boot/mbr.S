; Main Bootstrap
; -------------------------------------------
%include "boot.inc"
SECTION MBR vstart=0x7c00       ; start position

    mov ax, cs                  ; init sreg
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax

    mov sp, 0x7c00              ; init stack pointer

    mov ax, 0xb800                
    mov gs, ax                  ; referring to mem layout, graphics mem starts at 0xb8000
                                ; real mode, 0xb8000/16 == 0xb800 

    ; BIOS int 0x10 read ah for function id
    ; 上卷窗口, function id 0x06 -> AH
    ; AL = 上卷的行数(如果为0,表示全部)
    ; BH = 上卷行属性
    ; cx = (CL,CH) = 窗口左上角的(X,Y)位置
    ; dx = (DL,DH) = 窗口右下角的(X,Y)位置
 
    mov ax, 0x600               ; ax = AH append AL = 0x600 ??? how
    mov bx, 0x700
    mov cx, 0		; left top	      0,  0
    mov dx, 0x184f	; right bottom	  80, 25
			        ; VGA text mode, 80 chars per line for 25 lines
			        ; 0x18 = 24, 0x4f = 79	
    int 0x10        ; screen clear.	

    ; go over note 3.3.2
    mov byte [gs:0], 'M'
    mov byte [gs:1], 0xA4

    mov byte [gs:2], 'B'
    mov byte [gs:3], 0xA4
    
    mov byte [gs:4], 'R'
    mov byte [gs:5], 0xA4

    ; args:    
    mov eax, LOADER_START_SECTOR    ; 2
    mov bx,  LOADER_BASE_ADDR       ; 0x900
    mov cx,  4                      ; sectors read
    
    call rd_disk_m_16

    jmp LOADER_BASE_ADDR
    
;-----------------------------------------------
;读取硬盘 n 个扇区
    rd_disk_m_16:
;-----------------------------------------------
                                    ; eax   sector start (LBA)
                                    ; ebx    将数据写入的内存地址   
                                    ; ecx    sector count
        ; back up
        mov esi, eax        
        mov di,  cx         

        ; sector count
        mov dx, 0x1f2   ; i/o port
        mov al, cl      ; c-low
        out dx, al 
        mov eax,esi    ; recover        

        ; write LBA 
        ; 7~0 -> 0x1f3, LBA-low
        mov dx, 0x1f3
        out dx, al
        
        ; 15~8 -> 0x1f4, LBA-mid
        mov cl, 8
        shr eax,cl     ; eax = extension + AH + AL
        mov dx, 0x1f4
        out dx, al      ; 右移八位输出 mid
        
        ; 23~16 -> 0x1f5, LBA-high
        shr eax,cl
        mov dx, 0x1f5
        out dx, al
        
        ; 27~24 & MOD & DEV -> 0x1f6, device reg 
        shr eax,cl
        and al, 0x0f    ; DEV = 1, al and 0001 0000
        or  al, 0xe0     ; MOD = 1, position 5 & 7 fixed 1, al or 1110 0000
                        ; 能凑出来就行
        mov dx, 0x1f6
        out dx, al

         
        ; command read 0x20 -> 0x1f7
        mov dx, 0x1f7
        mov al, 0x20
        out dx, al
        ; disk working...
        
        .not_ready:
            nop             ; 空操作，增加延迟，减少打扰硬盘
            in  al, dx       ; 0x1f7 now read as status
            and al, 0x88    ; 取出第4、7 位，4 = ready, 7 = busy
            cmp al, 0x08    ; 不改变al，al-0x08 结果输出到ZF CF PF
                            ; 若为0 则有 ?001 0000 = ready
            jnz .not_ready  ; ZF 非0，即BUSY 时，跳转 .not_ready 
            
            ; ready
            ; 读一次2 字节，一个扇区512 字节，需要读di 个扇区：
            mov ax, di
            mov dx, 256     ; 共需要读 di*512/2 = di*256 次
            mul dx          ; 这里结果一定16 位以内，因此只需要ax
            mov cx, ax      ; cx=ax 为loop 次数
        
            mov dx, 0x1f0
            .reading:
                in   ax,  dx
                mov [bx], ax
                add  bx,  2
                loop .reading
                ret            
; ----------------------- rd_disk_m_16 -------------------------


    times 510-($-$$) db 0
    db 0x55,0xaa


