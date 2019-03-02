#! /bin/bash
script_path = $(cd "$(dirname "$0")";pwd)
cd ${script_path}

if [ $1 = "r" ];then 
	make all-r
else
	make all
fi

cd ${script_path}/run
bochs -f bochsrc

