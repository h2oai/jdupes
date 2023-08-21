#!/bin/sh

gcc -Wall -Wextra -std=gnu11 -pedantic -L../libjodycode -I../libjodycode hashdb.c -c -o hashdb.o

gcc hashdb.o ../libjodycode/libjodycode.a -o hashdb
[ -x ./hashdb ] && ./hashdb
