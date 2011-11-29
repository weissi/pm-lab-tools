#!/bin/bash

set -e

HERE=$(cd $(dirname ${BASH_SOURCE[0]}) > /dev/null && pwd)
cd "$HERE"

function compile_c() {
    echo "- Compiling $1.c"
    gcc -std=gnu99 -Wall -Werror -pedantic -lrt -lpthread -c \
        -Idaemon -o "build/$(basename $1).o" $1.c
}

compile_c daemon/daemon
compile_c daemon/handler
compile_c daemon/sync
gcc -lrt -lpthread -o build/daemon build/*.o
