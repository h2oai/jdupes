#!/bin/sh

# Runs the installed fdupes binary and the built fdupes binary and compares
# the output for sameness. Also displays timing statistics.

ERR=0

# Detect installed program type (fdupes or fdupes-jody)
FDUPES=fdupes
fdupes -v 2>/dev/null >/dev/null || FDUPES=fdupes-jody
if [ $FDUPES -v 2>/dev/null >/dev/null ]
	then echo "Cannot run installed fdupes or fdupes-jody"
	exit 1
fi

test ! -e ./fdupes-jody && echo "Build fdupes first, silly" && exit 1

echo -n "Installed '$FDUPES':"
sync
time $FDUPES -nrq "$@" > foo || ERR=1
echo -en "\nBuilt fdupes-jody:"
sync
time ./fdupes-jody -nrq "$@" > bar || ERR=1
diff -Nau foo bar
rm foo bar
test "$ERR" != "0" && echo "Errors were returned during execution"
