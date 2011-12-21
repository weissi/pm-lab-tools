#!/bin/bash

set -e

HERE=$(cd $(dirname ${BASH_SOURCE[0]}) > /dev/null && pwd)
cd "$HERE"

rm build/* &> /dev/null || true

NI_CFLAGS="-DWITH_NI"
NI_LDFLAGS="-lnidaqmxbase"

CFLAGS="$CFLAGS $NI_CFLAGS -I$HERE -ggdb"
LDFLAGS="$LDFLAGS -ggdb"

function compile_c() {
    echo "- Compiling $1.c"
    gcc -std=gnu99 -Wall -Werror -pedantic -lrt -lpthread -c $CFLAGS \
        -Idaemon -Icommon -Igensrc -o "build/$(basename $1).o" $1.c
}

echo "Building Deamon"


echo "- Generating protos"
cd protos &> /dev/null
for f in *.proto; do
    protoc-c --c_out=../gensrc "$f"
done
cd ..

echo
echo "Building Client"

compile_c client/pmlabclient
for f in gensrc/*.c; do
    compile_c ${f%*.c}
done
echo "- Linking pmlabclient"
gcc $LDFLAGS -lprotobuf-c -o build/pmlabclient build/*.o

compile_c daemon/daemon
compile_c daemon/handler
compile_c daemon/sync
for f in gensrc/*.c; do
    compile_c ${f%*.c}
done
echo "- Linking deamon"
gcc $LDFLAGS $NI_LDFLAGS -lprotobuf-c -lrt -lpthread -o build/daemon build/*.o
