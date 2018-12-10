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

    
    ; get cursor, function id 3 -> AH, page_id 0 -> bh
    mov ah, 3		; get cursor
    mov bh, 0		; page num of cursor
    
    int 0x10        ; get dx: dh = cursor row number, dl = cursor col number 


    mov ax, message
    mov bp, ax

    mov cx, 5       ; msg length, not counting '\0'
    mov ax, 0x1301  ; function id 0x13, al=0,1,2,3: write char type, 1=show char only, cursor follow new
    mov bx, 0x2     ; bh = page number = 0, bl = char property, 0x2 = black back & green front

    int 0x10        ; exec

    jmp -2

    message db "MBR First Met"
    times 510-($-$$) db 0
    db 0x55,0xaa


