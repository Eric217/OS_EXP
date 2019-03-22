#! /bin/bash
cd $(cd "$(dirname "$0")";pwd)

if [[ $# = 2 ]]; then
	if [[ "r" = $1 ]]; then
		make all-r
	fi
else
	make all
fi

cd run
bochs -q -f bochsrc