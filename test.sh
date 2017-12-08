#!/bin/sh

echo "Test suite for jdupes"

TESTS=tests
TD=test_temp
OUT=output.log

PF='%-35s'

# Clean up
control_c () {
	test -e "$TD" && rm -rf "$TD"
	exit 1
}

trap control_c TERM INT ABRT

# Remove any existing test temporary directory and make the test data
build_testdir () {
	test -e "$TD" && rm -rf "$TD"
	mkdir "$TD" || exit 1
	echo "one" > "$TD/one1" || control_c
	echo "one" > "$TD/one2" || control_c
	echo "one" > "$TD/one3" || control_c
	echo "two" > "$TD/two1" || control_c
	echo "two" > "$TD/two2" || control_c
	ln "$TD/one1" "$TD/one1_hardlink"  || control_c
	ln "$TD/one2" "$TD/one2_hardlink" || control_c
	ln "$TD/one3" "$TD/one3_hardlink" || control_c
	mkdir "$TD/dir1" "$TD/dir1/dir1_1" || control_c
	echo "two" > "$TD/dir1/two2" || control_c
	echo "two" > "$TD/dir1/dir1_1/two2" || control_c
	if ! echo "$EXT" | grep -q nosymlink
		then
		ln -s "$TD/one1" "$TD/one1_symlink" || control_c
		ln -s "$TD/one2" "$TD/one2_symlink" || control_c
		ln -s "$TD/one3" "$TD/one3_symlink" || control_c
		ln -s "$TD/two1" "$TD/two1_symlink" || control_c
		ln -s "$TD/dir1" "$TD/dir1_symlink" || control_c
	fi
}

# Make sure jdupes has been built and get its version
EXT="$(./jdupes -v | grep -i "Compile-time extensions:" | cut -d: -f2-)"
test -z "$EXT" && echo -e "\nYou must build jdupes before running tests." && exit 1
echo -e "\njdupes extensions detected: $EXT\n"

# Get jdupes extensions/feature flags

build_testdir

test "$1" = "generate" && echo "Generated test dir: '$TD'" && exit

FAIL=0

TN=1
printf "$PF" "Test $TN:  no options"
jdupes -q "$TD" > output.log 2>&1
if cmp "$TESTS/$TN" output.log; then echo "PASSED"; else echo "FAILED" && FAIL=1; fi

TN=2
printf "$PF" "Test $TN:  recurse [-r]"
jdupes -qr "$TD" > output.log 2>&1
if cmp "$TESTS/$TN" output.log; then echo "PASSED"; else echo "FAILED" && FAIL=1; fi

# Clean up
test -e "$TD" && rm -rf "$TD"

exit $FAIL
