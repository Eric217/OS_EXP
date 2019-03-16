#! /bin/bash
cd $(cd "$(dirname "$0")";pwd)

if [[ "r" = $1 ]];then 
	make all-r
elif [[ "skip" = $1 ]]; then
	\n
else
	make all
fi

cd run
bochs -q -f bochsrc