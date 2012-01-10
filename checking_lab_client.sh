#!/bin/bash

HERE=$(cd $(dirname "${BASH_SOURCE[0]}") > /dev/null && pwd)

cd "$HERE"

oldnum=0

if [ ! -z $1 ]; then
    SLEEP=$1
else
    SLEEP=10
fi

numstr="START"
errs=0
pread=0

while read count num; do
    numstr="$numstr,$num"
    if [ $oldnum -ne 0 -a $(( $oldnum + 1 )) -ne $num ]; then
        errs=$(( $errs + 1 ))
        numstr="$numstr(XXX)"
    fi
    oldnum=$num
    pread=$(( $pread + 1 ))
done < <(
    ( build/pmlabclient localhost 12345 2> /dev/null & PMC=$!;
      sleep $SLEEP;
      kill $PMC; wait ) 2> /dev/null | cut -d. -f1 |  uniq -c
)

if [ $errs -eq 0 ]; then
    echo "OK (read $pread packets): $numstr"
    exit 0
else
    echo "ERROR: $errs ($numstr)"
    exit 1
fi
