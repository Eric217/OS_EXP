; ---------------- Configurations -----------------

; --------------- loader and kernel ---------------
LOADER_BASE_ADDR 	equ 0x900 
LOADER_START_SECTOR equ 0x2

; -------------- descriptor properties ------------
DESC_G_4K			equ 100000000000000000000000b	; 段界限 4K 粒度
DESC_D_32			equ  10000000000000000000000b 	; 32 位操作数
DESC_L 				equ   0000000000000000000000b 	; 不支持 64 位代码段
DESC_AVL 			equ    000000000000000000000b  	; 用户可用性，无实际意义 (OS可用)
DESC_LIMIT_CODE2 	equ     11110000000000000000b 	; 19～16 位段界限
DESC_P 				equ         1000000000000000b 	; 段存在于 RAM
DESC_DPL_0 			equ          000000000000000b   ; 特权级 0 (max)
DESC_S_CODE 		equ            1000000000000b 	; 内存段 (not 系统段)
DESC_TYPE_CODE 		equ             1000_00000000b 	; XCRA/XEWA;    	

; x=1, c=0, r=0, a=0 代码段是可执行的，非一致性，不可读，巳访问位 a 清 0
; x=0, e=0, w=1, a=0 数据段是不可执行的，向上扩展的，可写，己访问位 a 清 0
DESC_TYPE_DATA 		equ 0010_00000000b

DESC_LIMIT_DATA2 	equ DESC_LIMIT_CODE2
DESC_LIMIT_VIDEO2 	equ 0000_000000000000000b

DESC_DPL_1 			equ 01_0000000000000b
DESC_DPL_2 			equ 10_0000000000000b
DESC_DPL_3	 		equ 11_0000000000000b

DESC_S_DATA 		equ DESC_S_CODE
DESC_S_sys 			equ 0_000000000000b


DESC_CODE_HIGH4 	equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + DESC_L + DESC_AVL + DESC_LIMIT_CODE2 + DESC_P + DESC_DPL_0 + DESC_S_CODE + DESC_TYPE_CODE + 0x00

DESC_DATA_HIGH4 	equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + DESC_L + DESC_AVL + DESC_LIMIT_DATA2 + DESC_P + DESC_DPL_0 + DESC_S_DATA + DESC_TYPE_DATA + 0x00

DESC_VIDEO_HIGH4 	equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + DESC_L + DESC_AVL + DESC_LIMIT_VIDEO2 + DESC_P + DESC_DPL_0 + DESC_S_DATA + DESC_TYPE_DATA + 0x0b

; ------------- selector properties --------------
RPL0 	equ 00b
RPL1 	equ 01b
RPL2 	equ 10b
RPL3 	equ 11b
TI_GDT  equ 000b
TI_LDT  equ 100b 


