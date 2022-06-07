#!/bin/bash

# Run Syzkaller USB fuzzing
cd $GOPATH/src/github.com/google/syzkaller
export PATH=$AGPATH/build/qemu/install/bin:$PATH
export LD_LIBRARY_PATH=$AGPATH/build/libagamotto:$LD_LIBRARY_PATH

t=$1
log=$AGPATH/$1-$2.log
bin/syz-manager -config $AGPATH/configs/syzkaller/generated/agamotto-usb.$t.cfg 2>&1 |tee $log
