#!/bin/sh

# Runs the installed fdupes binary and the built fdupes binary and compares
# the output for sameness. Also displays timing statistics.

ERR=0

test ! -e ./fdupes && echo "Build fdupes first, silly" && exit 1

echo -n "Installed fdupes:"
sync
time fdupes -nrq "$@" > foo || ERR=1
echo -en "\nBuilt fdupes:"
sync
time ./fdupes -nrq "$@" > bar || ERR=1
diff -Nau foo bar
rm foo bar
test "$ERR" != "0" && echo "Errors were returned during execution"
