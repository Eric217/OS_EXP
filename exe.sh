#! /bin/bash
cd $(cd "$(dirname "$0")";pwd)

# 执行参数
#    增量编译并执行
# r  重新编译并执行
# s  跳过编译并执行
# rb 仅重新编译
# b  仅增量编译
 
if [[ $# = 1 ]]; then
	if [[ "r" = $1 ]]; then
		echo -e "\nall rebuilding...\n"
		make all-r || exit 1
	elif [[ "s" = $1 ]]; then
		echo -e "\nskip building...\n"
	
	elif [[ "b" = $1 ]]; then
		echo -e "\nbuild only...\n"
		make all 
		exit 0

	elif [[ "rb" = $1 ]]; then
		echo -e "\nrebuild only...\n"
		make all-r
		exit 0	

	else
		echo -e "\nunrecognized order!\n"
		exit 0	
	fi
  
else
	echo -e "\nbuilding...\n"
	make all || exit 1
fi

echo "start running"

cd run
bochs -q -f bochsrc