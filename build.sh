#! /bin/bash

# 调用在前，实现在后的顺序来链接

nasm 		-o $OS_BUILD/binary/mbr.bin  	-I $OS_SRC/boot/include/ 				$OS_SRC/boot/mbr.S 			&&	\
nasm 		-o $OS_BUILD/binary/loader.bin 	-I $OS_SRC/boot/include/ 				$OS_SRC/boot/loader.S 		&&	\

nasm -f elf -o $OS_BUILD/object/kernel.o 											$OS_SRC/kernel/kernel.S 	&&  \
nasm -f elf -o $OS_BUILD/object/print.o 											$OS_SRC/lib/kernel/print.S  &&  \

gcc-elf -c  -o $OS_BUILD/object/interrupt.o -I $OS_SRC/lib/kernel/ -I $OS_SRC/lib/ 	$OS_SRC/kernel/interrupt.c 	&&  \
gcc-elf -c  -o $OS_BUILD/object/init.o 		-I $OS_SRC/lib/kernel/ -I $OS_SRC/lib/ 								    \
										 	-I $OS_SRC/device/						$OS_SRC/kernel/init.c 		&&  \
gcc-elf -c 	-o $OS_BUILD/object/main.o 		-I $OS_SRC/lib/kernel/ -I $OS_SRC/lib/  $OS_SRC/kernel/main.c 		&&  \
gcc-elf -c 	-o $OS_BUILD/object/timer.o  	-I $OS_SRC/lib/kernel/ -I $OS_SRC/lib/ 	$OS_SRC/device/timer.c 		&&  \




ld-elf 		-o $OS_BUILD/binary/kernel.bin 	\
	$OS_BUILD/object/main.o 			 	\
	$OS_BUILD/object/init.o  				\
	$OS_BUILD/object/interrupt.o 			\
	$OS_BUILD/object/timer.o 				\
	$OS_BUILD/object/kernel.o 				\
	$OS_BUILD/object/print.o 				\
	-Ttext 0xc0001500 -e main && \
 
 
dd if=$OS_BUILD/binary/mbr.bin 	 	of=$OS_RUN/c.img bs=512 count=1 	 		conv=notrunc && \
dd if=$OS_BUILD/binary/loader.bin 	of=$OS_RUN/c.img bs=512 count=4 	seek=2 	conv=notrunc && \
dd if=$OS_BUILD/binary/kernel.bin 	of=$OS_RUN/c.img bs=512 count=200 	seek=8 	conv=notrunc 
