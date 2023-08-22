#!/bin/sh

gcc -DHASHDB_TESTING=1 -Og -g -Wall -Wextra -std=gnu11 -pedantic -L../libjodycode -I../libjodycode hashdb.c -c -o hashdb.o

gcc hashdb.o ../libjodycode/libjodycode.a -o hashdb || exit 1
[ -x ./hashdb ] && ./hashdb
