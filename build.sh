#!/bin/bash

set -e

HERE=$(cd $(dirname ${BASH_SOURCE[0]}) > /dev/null && pwd)
cd "$HERE"

function compile_c() {
    echo "- Compiling $1.c"
    gcc -std=gnu99 -Wall -Werror -pedantic -lrt -lpthread -c $CFLAGS \
        -Idaemon -Igensrc -o "build/$(basename $1).o" $1.c
}

echo "- Generating protos"
cd protos &> /dev/null
for f in *.proto; do
    protoc-c --c_out=../gensrc "$f"
done
cd ..

compile_c daemon/daemon
compile_c daemon/handler
compile_c daemon/sync
for f in gensrc/*.c; do
    compile_c ${f%*.c}
done
gcc $LDFLAGS -lprotobuf-c -lrt -lpthread -o build/daemon build/*.o
