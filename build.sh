#! /bin/bash

# 调用在前，实现在后的顺序来链接

nasm $OS_SRC/boot/mbr.S -o $OS_BUILD/binary/mbr.bin -I $OS_SRC/boot/include/ && \
nasm $OS_SRC/boot/loader.S -o $OS_BUILD/binary/loader.bin -I $OS_SRC/boot/include/ && \

nasm -f elf $OS_SRC/kernel/kernel.S -o $OS_BUILD/object/kernel.o && \
nasm -f elf -o $OS_BUILD/object/print.o $OS_SRC/lib/kernel/print.S && \

gcc-elf -I $OS_SRC/lib/kernel/ -I $OS_SRC/lib/ -c -o $OS_BUILD/object/interrupt.o $OS_SRC/kernel/interrupt.c && \

gcc-elf -I $OS_SRC/lib/kernel/ -I $OS_SRC/lib/ -c -o $OS_BUILD/object/init.o $OS_SRC/kernel/init.c && \
gcc-elf -I $OS_SRC/lib/kernel/ -I $OS_SRC/lib/ -c -o $OS_BUILD/object/main.o $OS_SRC/kernel/main.c && \


ld-elf -o $OS_BUILD/binary/kernel.bin \
$OS_BUILD/object/main.o \
$OS_BUILD/object/init.o \
$OS_BUILD/object/interrupt.o \
$OS_BUILD/object/kernel.o \
$OS_BUILD/object/print.o \
-Ttext 0xc0001500 -e main && \
 


dd if=$OS_BUILD/binary/mbr.bin of=$OS_RUN/c.img bs=512 count=1 conv=notrunc && \
dd if=$OS_BUILD/binary/loader.bin of=$OS_RUN/c.img bs=512 count=4 seek=2 conv=notrunc && \
dd if=$OS_BUILD/binary/kernel.bin of=$OS_RUN/c.img bs=512 count=200 seek=8 conv=notrunc 
