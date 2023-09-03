#!/bin/bash

[[ -z "$1" || ! -e "$1" ]] && echo "Specify a hash database to clean" >&2 && exit 1

HASHDB="$1"
TEMPDB="_jdupes_hashdb_clean.tmp"
ERR=0; CNT=0
LINELEN=87

[ "$HASHDB" = "." ] && HASHDB="jdupes_hashdb.txt"

clean_exit () {
	echo "Terminated, cleaning up." >&2
	rm -f "$TEMPDB"
	exit 1
}

trap clean_exit INT TERM HUP ABRT QUIT

if ! grep -q -m 1 '^jdupes hashdb:2,' "$HASHDB"
	then echo "Must be a version 2 database, exiting" >&2
	exit 1
fi

SRCLINES="$(wc -l "$HASHDB" | cut -d' ' -f1)"
SRCLINES="$((SRCLINES - 1))"

echo "Cleaning out hash database $HASHDB [$SRCLINES entries]" >&2

head -n 1 "$HASHDB" > "$TEMPDB" || ERR=1

echo "Sorting items (this may take a little time)..." >&2

while read LINE
	do
	NAME="${LINE:$LINELEN}"
	[ ! -e "$NAME" ] && echo "$LINE" >&2 && continue
	echo "$LINE" >> "$TEMPDB" || ERR=1
	CNT=$((CNT + 1))
	echo -n "Processed $CNT/$SRCLINES lines ($((CNT * 100 / SRCLINES))%)"$'\r'
done < <(grep -v '^jdupes hashdb:' "$HASHDB" | sort -k7 -t,)

if [ $ERR -eq 1 ]
	then echo "Error writing out lines, not overwriting hash database" >&2
	rm -f "$TEMPDB"
	exit 1

	else
	mv -f "$TEMPDB" "$HASHDB"
	echo "Wrote $CNT entries; cleaned out $((SRCLINES - CNT)) entries" >&2
fi
