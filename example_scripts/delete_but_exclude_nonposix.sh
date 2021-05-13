#!/bin/bash

# NOTE: This non-POSIX version is faster but requires bash/ksh/zsh etc.

# This is a shell script that deletes match sets like jdupes -dN does, but
# excludes any file paths from deletion that match any of the grep regex
# patterns passed to the script. Use it like this:
#
# jdupes whatever | ./delete_but_exclude.sh regex1 [regex2] [...]

# Announce what this script does so the user knows what's going on
echo "jdupes script - delete duplicates that don't match specified patterns"

# If no parameters are passed, give usage info and abort
test -z "$1" && echo "usage: $0 regex1 [regex2] [...]" && exit 1

# Exit status will be 0 on success, 1 on any failure
EXITSTATUS=0

# Skip the first file in each match set
FIRSTFILE=1
while read -r LINE
	do
	# Remove Windows CR characters if present in name
	LINE=${LINE/$'\r'/}
	# Reset on a blank line; next line will be a first file
	test -z "$LINE" && FIRSTFILE=1 && continue
	# If this is the first file, take no action
	test $FIRSTFILE -eq 1 && FIRSTFILE=0 && echo $'\n'"[+] $LINE" && continue
	# Move the file specified on the line to the directory specified
	for RX in "$1" "$2" "$3" "$4" "$5" "$6" "$7" "$8" "$9"
		do test -z "$RX" && continue
		if [[ $LINE =~ $RX ]]
			then
			echo "[+] $LINE"
			else
			if rm -f "$LINE"
				then echo "[-] $LINE"
				else echo "[!] $LINE"
				EXITSTATUS=1
			fi
		fi
	done
done

exit $EXITSTATUS
