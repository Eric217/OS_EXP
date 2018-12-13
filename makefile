
# $＠，目标文件名集合 @ = at = aim at
# $<，依赖文件中的第1个文件 < 指向最左侧，第一个
# $^，所有依赖文件的集合，自动去重。^很像从上往下罩的动作，集合
# $?，所有比目标文件 mtime 新的依赖文件集合 
 
SRC_DIR = $(HOME)/opt/bochs/my/src
OBJ_DIR = $(HOME)/opt/bochs/my/.build/object
BIN_DIR = $(HOME)/opt/bochs/my/.build/binary
DISK    = $(HOME)/opt/bochs/my/run/c.img

ENTRY_POINT = 0xc0001500
AS = @nasm
CC = @gcc-elf
LD = @ld-elf
LIB = -I $(SRC_DIR)/lib/ -I $(SRC_DIR)/lib/kernel/ -I $(SRC_DIR)/lib/user/ -I $(SRC_DIR)/kernel/ -I $(SRC_DIR)/device/
ASFLAGS = -f elf
CFLAGS = -Wall $(LIB) -c -fno-builtin -W -Wstrict-prototypes -Wmissing-prototypes 
LDFLAGS = -Ttext $(ENTRY_POINT) -e main -Map $(OBJ_DIR)/kernel.map
OBJS = $(OBJ_DIR)/main.o $(OBJ_DIR)/init.o $(OBJ_DIR)/interrupt.o \
      $(OBJ_DIR)/timer.o $(OBJ_DIR)/kernel.o $(OBJ_DIR)/print.o \
      $(OBJ_DIR)/debug.o

##############     c代码编译     ###############
$(OBJ_DIR)/main.o: $(SRC_DIR)/kernel/main.c $(SRC_DIR)/lib/kernel/print.h \
        $(SRC_DIR)/lib/stdint.h $(SRC_DIR)/kernel/init.h
	$(CC) $(CFLAGS) $< -o $@

$(OBJ_DIR)/init.o: $(SRC_DIR)/kernel/init.c $(SRC_DIR)/kernel/init.h $(SRC_DIR)/lib/kernel/print.h \
        $(SRC_DIR)/lib/stdint.h $(SRC_DIR)/kernel/interrupt.h $(SRC_DIR)/device/timer.h
	$(CC) $(CFLAGS) $< -o $@

$(OBJ_DIR)/interrupt.o: $(SRC_DIR)/kernel/interrupt.c $(SRC_DIR)/kernel/interrupt.h \
        $(SRC_DIR)/lib/stdint.h $(SRC_DIR)/kernel/global.h $(SRC_DIR)/lib/kernel/io.h $(SRC_DIR)/lib/kernel/print.h
	$(CC) $(CFLAGS) $< -o $@

$(OBJ_DIR)/timer.o: $(SRC_DIR)/device/timer.c $(SRC_DIR)/device/timer.h $(SRC_DIR)/lib/stdint.h \
         $(SRC_DIR)/lib/kernel/io.h $(SRC_DIR)/lib/kernel/print.h
	$(CC) $(CFLAGS) $< -o $@

$(OBJ_DIR)/debug.o: $(SRC_DIR)/kernel/debug.c $(SRC_DIR)/kernel/debug.h \
        $(SRC_DIR)/lib/kernel/print.h $(SRC_DIR)/lib/stdint.h $(SRC_DIR)/kernel/interrupt.h
	$(CC) $(CFLAGS) $< -o $@

##############    汇编代码编译    ###############
$(OBJ_DIR)/kernel.o: $(SRC_DIR)/kernel/kernel.S
	$(AS) $(ASFLAGS) $< -o $@
$(OBJ_DIR)/print.o: $(SRC_DIR)/lib/kernel/print.S
	$(AS) $(ASFLAGS) $< -o $@

##############    链接所有目标文件    #############
$(BIN_DIR)/kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

# MBR & LOADER 
$(BIN_DIR)/mbr.bin: $(SRC_DIR)/boot/mbr.S 
	$(AS) $< -I $(SRC_DIR)/boot/include/ -o $@
$(BIN_DIR)/loader.bin: $(SRC_DIR)/boot/loader.S 
	$(AS) $< -I $(SRC_DIR)/boot/include/ -o $@


.PHONY : mk_dir hd clean all

mk_dir:
	@if [[ ! -d $(OBJ_DIR) ]];then mkdir $(OBJ_DIR);fi
	@if [[ ! -d $(BIN_DIR) ]];then mkdir $(BIN_DIR);fi
hd:
	@dd if=$(BIN_DIR)/mbr.bin  	of=$(DISK) bs=512 count=1 	 		conv=notrunc  
	@dd if=$(BIN_DIR)/loader.bin of=$(DISK) bs=512 count=4 	seek=2 	conv=notrunc  
	@dd if=$(BIN_DIR)/kernel.bin of=$(DISK) bs=512 count=200 seek=8  conv=notrunc

clean:
	@cd $(OBJ_DIR) && rm -f ./*  
	@cd $(BIN_DIR) && rm -f ./*

build: $(BIN_DIR)/kernel.bin $(BIN_DIR)/mbr.bin $(BIN_DIR)/loader.bin 

all: mk_dir build hd
