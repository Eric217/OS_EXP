#! /bin/bash
cd $(cd "$(dirname "$0")";pwd)

if [[ $# = 1 ]]; then
	if [[ "r" = $1 ]]; then
		echo -e "\nall rebuilding...\n"
		make all-r
	else
		echo -e "\nskip building\n"
	fi
else
	echo -e "\nbuilding all (with cache)...\n"
	make all
fi

echo "start running"

cd run
bochs -q -f bochsrc