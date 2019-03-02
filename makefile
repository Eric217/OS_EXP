
# $＠，目标文件名集合 @ = at = aim at
# $<，依赖文件中的第1个文件 < 指向最左侧，第一个
# $^，所有依赖文件的集合，自动去重。^很像从上往下罩的动作，集合
# $?，所有比目标文件 mtime 新的依赖文件集合 
PROJECT = .

SRC_DIR = $(PROJECT)/src
OBJ_DIR = $(PROJECT)/.build/object
BIN_DIR = $(PROJECT)/.build/binary

MAPFILE = $(PROJECT)/.build/kernel.map
DISK    = $(PROJECT)/run/c.img

ENTRY_POINT = 0xc0001500

AS = @nasm
CC = @gcc-elf
LD = @ld-elf

LIB = -I $(SRC_DIR)/lib/ -I $(SRC_DIR)/lib/kernel/ -I $(SRC_DIR)/lib/user/ \
	  -I $(SRC_DIR)/kernel/ -I $(SRC_DIR)/device/ -I $(SRC_DIR)/thread/

ASFLAGS = -f elf
CFLAGS = -Wall $(LIB) -c -fno-builtin -W -Wstrict-prototypes -Wmissing-prototypes 
LDFLAGS = -Ttext $(ENTRY_POINT) -e main -Map $(MAPFILE)

OBJS = $(OBJ_DIR)/main.o $(OBJ_DIR)/init.o $(OBJ_DIR)/interrupt.o \
      $(OBJ_DIR)/timer.o $(OBJ_DIR)/kernel.o $(OBJ_DIR)/print.o \
      $(OBJ_DIR)/debug.o $(OBJ_DIR)/memory.o $(OBJ_DIR)/bitmap.o $(OBJ_DIR)/string.o \
      $(OBJ_DIR)/thread.o $(OBJ_DIR)/list.o $(OBJ_DIR)/switch.o 

##############     c代码编译     ###############
$(OBJ_DIR)/main.o: $(SRC_DIR)/kernel/main.c 
	$(CC) $(CFLAGS) $< -o $@

$(OBJ_DIR)/init.o: $(SRC_DIR)/kernel/init.c 
	$(CC) $(CFLAGS) $< -o $@

$(OBJ_DIR)/interrupt.o: $(SRC_DIR)/kernel/interrupt.c 
	$(CC) $(CFLAGS) $< -o $@

$(OBJ_DIR)/timer.o: $(SRC_DIR)/device/timer.c 
	$(CC) $(CFLAGS) $< -o $@

$(OBJ_DIR)/debug.o: $(SRC_DIR)/kernel/debug.c 
	$(CC) $(CFLAGS) $< -o $@

$(OBJ_DIR)/string.o: $(SRC_DIR)/lib/string.c 
	$(CC) $(CFLAGS) $< -o $@

$(OBJ_DIR)/bitmap.o: $(SRC_DIR)/lib/kernel/bitmap.c 
	$(CC) $(CFLAGS) $< -o $@

$(OBJ_DIR)/memory.o: $(SRC_DIR)/kernel/memory.c 
	$(CC) $(CFLAGS) $< -o $@

$(OBJ_DIR)/thread.o: $(SRC_DIR)/thread/thread.c 
	$(CC) $(CFLAGS) $< -o $@

$(OBJ_DIR)/list.o: $(SRC_DIR)/lib/kernel/list.c 
	$(CC) $(CFLAGS) $< -o $@


##############    汇编代码编译    ###############
$(OBJ_DIR)/kernel.o: $(SRC_DIR)/kernel/kernel.S
	$(AS) $(ASFLAGS) $< -o $@
$(OBJ_DIR)/print.o: $(SRC_DIR)/lib/kernel/print.S
	$(AS) $(ASFLAGS) $< -o $@
$(OBJ_DIR)/switch.o: $(SRC_DIR)/thread/switch.S
	$(AS) $(ASFLAGS) $< -o $@

##############    链接所有目标文件    #############
$(BIN_DIR)/kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

# MBR & LOADER 
$(BIN_DIR)/mbr.bin: $(SRC_DIR)/boot/mbr.S 
	$(AS) $< -I $(SRC_DIR)/boot/include/ -o $@
$(BIN_DIR)/loader.bin: $(SRC_DIR)/boot/loader.S 
	$(AS) $< -I $(SRC_DIR)/boot/include/ -o $@


.PHONY : mk_dir hd clean all all-r 

mk_dir:
	@if [[ ! -d $(OBJ_DIR) ]];then mkdir $(OBJ_DIR);fi
	@if [[ ! -d $(BIN_DIR) ]];then mkdir $(BIN_DIR);fi
hd:
	@dd if=$(BIN_DIR)/mbr.bin 		of=$(DISK) bs=512 count=1 	 		conv=notrunc  
	@dd if=$(BIN_DIR)/loader.bin 	of=$(DISK) bs=512 count=4 	seek=2 	conv=notrunc  
	@dd if=$(BIN_DIR)/kernel.bin 	of=$(DISK) bs=512 count=200 seek=8  conv=notrunc

clean:
	@cd $(OBJ_DIR) && rm -f ./*  
	@cd $(BIN_DIR) && rm -f ./*
	@rm -f $(MAPFILE)

build: $(BIN_DIR)/kernel.bin $(BIN_DIR)/mbr.bin $(BIN_DIR)/loader.bin 
all-r: mk_dir clean build hd
all: mk_dir build hd
