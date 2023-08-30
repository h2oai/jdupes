#!/bin/bash

[[ -z "$1" || ! -e "$1" ]] && echo "Specify a hash database to clean" >&2 && exit 1

HASHDB="$1"
TEMPDB="_jdupes_hashdb_clean.tmp"
ERR=0; CNT=0
LINELEN=87

[ "$HASHDB" = "." ] && HASHDB="jdupes_hashdb.txt"

if ! grep -q -m 1 '^jdupes hashdb:2,' "$HASHDB"
	then echo "Must be a version 2 database, exiting" >&2
	exit 1
fi

SRCLINES="$(wc -l "$HASHDB" | cut -d' ' -f1)"
SRCLINES="$((SRCLINES - 1))"

echo "Cleaning out hash database $HASHDB [$SRCLINES entries]" >&2

while read LINE;
	do if [ $CNT -eq 0 ]
		then echo "$LINE" >> "$TEMPDB" || ERR=1
		CNT=$((CNT + 1))
		continue
	fi
	NAME="${LINE:$LINELEN}"
	[ ! -e "$NAME" ] && echo "$LINE" >&2 && continue
	echo "$LINE" >> "$TEMPDB" || ERR=1
	CNT=$((CNT + 1))
done < "$HASHDB"

if [ $ERR -eq 1 ]
	then echo "Error writing out lines, not overwriting hash database" >&2
	rm -f "$TEMPDB"
	exit 1

	else
	CNT=$((CNT - 1))  # Remove header line
	mv -f "$TEMPDB" "$HASHDB"
	echo "Wrote $CNT entries; cleaned out $((SRCLINES - CNT)) entries" >&2
fi
