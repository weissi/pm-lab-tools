#!/bin/bash

gcc -Wall -Werror -pedantic -lrt -lpthread -o build/daemon daemon/daemon.c
