; Main Bootstrap
; -------------------------------------------
; BIOS int 0x10 read ah for function id
SECTION MBR vstart=0x7c00       ; start position


    mov ax,cs                   ; init sreg
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov fs,ax

    mov sp,0x7c00               ; init stack pointer

    mov ax,0xb800                
    mov gs,ax                   ; referring to mem layout, graphics mem starts at 0xb8000
                                ; real mode, 0xb8000/16 == 0xb800 

    ; clear screen (up scroll), function id 0x06 -> AH, num of lines -> AL (0 is ALL)
    ; BH = up scroll line property
    ; cx = (CL,CH) = window left top position
    ; dx = (DL,DH) = window righ bot position, for 25line*80char, 24=0x18, 79=0x4f
 
    mov ax,0x600                ; ax = AH append AL = 0x600 ??? how
    mov bx,0x700
    mov cx,0		; left top	 0,  0
    mov dx,0x184f	; right bottom	80, 25
			        ; VGA text mode, 80 chars per line for 25 lines
			        ; 0x18 = 24, 0x4f = 79	
    int 0x10        ; screen clear.	

    ; look up note 3.3.2
    mov byte [gs:0x00], 'O'
    mov byte [gs:0x01], 0xA4    

    mov byte [gs:0x02], 'K'
    mov byte [gs:0x03], 0xA4


    jmp -2

    times 510-($-$$) db 0
    db 0x55,0xaa


