#!/bin/sh

# This is a shell script that demonstrates how to process the standard
# jdupes output (known as "printmatches") to perform custom actions.
# Use it like this:
#
# jdupes whatever_parameters_you_like | ./example.sh script_parameters
#
# If you are on Windows, jdupes uses backslash path separators which
# must be converted to forward slashes before piping to this script,
# and carriage returns (\r) must also be deleted from jdupes output
# (tested on MSYS2 MinGW, probably true for similar environments):
#
# jdupes params | tr '\\' / | tr -d '\r' | ./example.sh script_params
#
# The general structure of jdupes pipe scripts are:
# * Initialize conditions
# * Iterates through a match set and act on items
# * Reset conditions and restart when a blank line is reached

# This script moves all duplicate files to a different directory
# without duplicating the directory structure. It can be easily
# modified to make the required directories and create a "mirror"
# consisting of duplicates that 'jdupes -rdN' would delete.

# Announce what this script does so the user knows what's going on
echo "jdupes example script - moving duplicate files to a directory"

# If first parameter isn't a valid directory, give usage info and abort
test ! -d "$1" && echo "usage: $0 destination_dir_to_move_files_to" && exit 1

# Exit status will be 0 on success, 1 on any failure
EXITSTATUS=0

# Skip the first file in each match set
FIRSTFILE=1
while read LINE
	do echo "$LINE"
	# Reset on a blank line; next line will be a first file
	test -z "$LINE" && FIRSTFILE=1 && continue
	# If this is the first file, take no action
	test $FIRSTFILE -eq 1 && FIRSTFILE=0 && continue
	# Move the file specified on the line to the directory specified
	if mv -f "$LINE" "$1"
		then
		# Print the action that was taken
		echo "'$LINE' => '$1/$(basename "$LINE")'"
		else
		echo "Failed to move: '$LINE' => '$1/$(basename "$LINE")'" >&2
		EXITSTATUS=1
	fi
done

exit $EXITSTATUS
