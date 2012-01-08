#!/bin/bash

set -e

HERE=$(cd $(dirname "${BASH_SOURCE[0]}") > /dev/null && pwd)
cd "$HERE"

rm build/*.o &> /dev/null || true

if [ "$1" = "-n" ]; then
    NI_CFLAGS=""
    NI_LDFLAGS=""
    shift
else
    NI_CFLAGS="-DWITH_NI"
    NI_LDFLAGS="-lnidaqmxbase"
fi

CFLAGS="$CFLAGS $NI_CFLAGS -I$HERE -ggdb"
LDFLAGS="$LDFLAGS -ggdb"

if [ "$(uname -s)" != "Darwin" ]; then
    LDFLAGS="$LDFLAGS -lrt"
fi

function compile_c() {
    echo "- Compiling $1.c"
    ALDF="-lpthread"
    if [ "$(uname -s)" = "Darwin" ]; then
        ALDF=""
    fi
    gcc -std=gnu99 -Wall -Werror -pedantic $ALDF -c $CFLAGS \
        -Idaemon -Icommon -Igensrc -o "build/$(basename $1).o" $1.c
}

echo "- Generating protos"
cd protos &> /dev/null
for f in *.proto; do
    protoc-c --c_out=../gensrc "$f"
done
cd ..

if [ "$#" -lt 1 -o "$1" = "client" ]; then
    rm build/*.o &> /dev/null || true
    echo
    echo "Building Utils"
    compile_c common/utils

    echo
    echo "Building Client"

    compile_c client/libpmlab
    compile_c client/pmlabclient
    for f in gensrc/*.c; do
        compile_c ${f%*.c}
    done
    echo "- Linking pmlabclient"
    if [ -f build/libpmlab ]; then
        rm build/libpmlab
    fi
    gcc $LDFLAGS -lprotobuf-c -o build/pmlabclient build/*.o
fi

if [ "$#" -lt 1 -o "$1" = "daemon" ]; then
    rm build/*.o &> /dev/null || true
    echo
    echo "Building Utils"
    compile_c common/utils

    echo
    echo "Building Daemon"
    compile_c daemon/daemon
    compile_c daemon/handler
    compile_c daemon/sync
    for f in gensrc/*.c; do
        compile_c ${f%*.c}
    done
    echo "- Linking deamon"
    if [ -f build/daemon ]; then
        rm build/daemon
    fi
    gcc $LDFLAGS $NI_LDFLAGS -lprotobuf-c -lpthread -o build/daemon build/*.o
fi
