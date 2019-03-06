#! /bin/bash
cd $(cd "$(dirname "$0")";pwd)

if [ $1 = "r" ];then 
	make all-r
else
	make all
fi

cd run
bochs -f bochsrc


