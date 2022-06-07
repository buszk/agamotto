#!/bin/bash

targets=(
	ar5523
	mwifiex
	rsi
)

trap ctrl_c INT

function ctrl_c() {
	echo "Ctrl^C detected. Killing jobs"
	pkill -9 syz-manager
	pkill -9 screen
	exit
}

for i in `seq 8`; do
for t in ${targets[@]};do
	log=$t-$i.log
	if [ -d /data/$USER/agamotto-usb/$t-$i ];then
		continue
	fi
	rm -rf workdir/agamotto-usb.$t
	date
	echo $t $i
	screen -S fuzz -d -m scripts/bench_usb_one.sh $t $i
	sleep 1h
	pkill -9 syz-manager
	pkill -9 screen
	grep -a VMs $log |tail -n 1
	mkdir -p /data/$USER/agamotto-usb/$t-$i
	cp $log /data/$USER/agamotto-usb/$t-$i
	mv workdir/agamotto-usb.$t /data/$USER/agamotto-usb/$t-$i
done
done
