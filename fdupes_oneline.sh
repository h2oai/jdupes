#!/bin/sh

# Emulates fdupes -1 output
# Usage: jdupes command line | ./fdupes_oneline.sh

while read LINE
  do if [ -z "$LINE" ]
    then echo
    else echo -n "$LINE" | sed 's/ /\\ /g'; echo -n " "
  fi
done
