#!/bin/bash

# Generate package folders with variant builds

# Number of parallel make processes
if [ -z "$PM" ]
	then PM=12
	[ -d /sys/devices/system/cpu ] && \
		PM=$(find /sys/devices/system/cpu -maxdepth 1 -mindepth 1 -type d | grep '/cpu[0-9][0-9]*' | wc -l) && \
		PM=$((PM * 2))
fi

NAME="jdupes"

VER="$(cat version.h | grep '#define VER "' | cut -d\" -f2)"
echo "Program version: $VER"

[ -z "$TA" ] && TA=__NONE__
[ ! -z "$1" ] && ARCH="$1"
[[ "$ARCH" = "linux-x64" || "$ARCH" = "x86_64" || "$ARCH" = "x86-64" ]] && TA=linux && ARCH=x86_64 && CF=-m64
[[ "$ARCH" = "linux-x32" || "$ARCH" = "x32" ]] && TA=linux && ARCH=x32 && CF=-mx32
[[ "$ARCH" = "linux-i686" || "$ARCH" = "linux-i386" || "$ARCH" = "i686" || "$ARCH" = "i386" ]] && TA=linux && ARCH=i386 && CF=-m32


UNAME_S="$(uname -s | tr '[:upper:]' '[:lower:]')"
UNAME_P="$(uname -p)"
UNAME_M="$(uname -m)"

# Detect macOS
if [[ "$TA" = "macos" || "$UNAME_S" = "darwin" ]]
	then
	PKGTYPE=zip
	TA=mac32
	test "$UNAME_M" = "x86_64" && TA=mac64
fi

# Detect Power Macs under macOS
if [[ "$TA" = "macppc" || "$UNAME_P" = "Power Macintosh" || "$UNAME_P" = "powerpc" ]]
	then
	TA=macppc32
	test "$(sysctl hw.cpu64bit_capable)" = "hw.cpu64bit_capable: 1" && TA=macppc64
	[ -z "$PKGTYPE" ] && PKGTYPE=zip
fi

# Detect Linux
if [[ "$TA" = "linux" || "$UNAME_S" = "linux" ]]
	then
	TA="linux-$UNAME_M"
	[ ! -z "$ARCH" ] && TA="linux-$ARCH"
	[ -z "$PKGTYPE" ] && PKGTYPE=xz
fi

# Fall through - assume Windows
if [[ "$TA" = "windows" || "$TA" = "__NONE__" ]]
	then
	[ -z "$PKGTYPE" ] && PKGTYPE=zip
	[ -z "$ARCH" ] && ARCH=$(gcc -v 2>&1 | grep Target | cut -d\  -f2- | cut -d- -f1)
	[[ "$ARCH" = "i686" || "$ARCH" = "i386" ]] && TA=win32
	[ "$ARCH" = "x86_64" ] && TA=win64
	[ "$UNAME_S" = "MINGW32_NT-5.1" ] && TA=winxp
	EXT=".exe"
fi

echo "Target architecture: $TA"
test "$TA" = "__NONE__" && echo "Failed to detect system type" && exit 1
PKGNAME="${NAME}-${VER}-$TA"

echo "Generating package for: $PKGNAME"
mkdir -p "$PKGNAME" || exit 1
test ! -d "$PKGNAME" && echo "Can't create directory for package" && exit 1
cp CHANGES.txt README.md LICENSE.txt $PKGNAME/ || exit 1
if [ -d "../libjodycode" ]
	then
	echo "Rebuilding nearby libjodycode first"
	WD="$(pwd)"
	cd ../libjodycode
	make clean && make -j$PM CFLAGS_EXTRA="$CF"
	cd "$WD"
fi
E1=1; E2=1; E3=1; E4=1
make clean && make CFLAGS_EXTRA="$CF" -j$PM ENABLE_DEDUPE=1 static_jc stripped && cp $NAME$EXT $PKGNAME/$NAME$EXT && E1=0
make clean && make CFLAGS_EXTRA="$CF" -j$PM ENABLE_DEDUPE=1 LOUD=1 static_jc stripped && cp $NAME$EXT $PKGNAME/${NAME}-loud$EXT && E2=0
make clean && make CFLAGS_EXTRA="$CF" -j$PM LOW_MEMORY=1 static_jc stripped && cp $NAME$EXT $PKGNAME/${NAME}-lowmem$EXT && E3=0
make clean && make CFLAGS_EXTRA="$CF" -j$PM BARE_BONES=1 static_jc stripped && cp $NAME$EXT $PKGNAME/${NAME}-barebones$EXT && E4=0
strip ${PKGNAME}/${NAME}*$EXT
make clean
test $((E1 + E2 + E3 + E4)) -gt 0 && echo "Error building packages; aborting." && exit 1
# Make a fat binary on macOS x86_64 if possible
if [ "$TA" = "mac64" ] && ld -v 2>&1 | grep -q 'archs:.*i386'
	then
	ERR=0
	TYPE=-i386; CE=-m32
	# On macOS Big Sur (Darwin 20) or higher, try to build a x86_64 + arm64 binary
	[ $(uname -r | cut -d. -f1) -ge 20 ] && TYPE=-arm64 && CE="-target arm64-apple-macos11"
	if [ -d "../libjodycode" ]
		then
		echo "Rebuilding nearby libjodycode first"
		WD="$(pwd)"
		cd ../libjodycode
		make clean && make -j$PM CFLAGS_EXTRA="$CE"
		cd "$WD"
	fi
	for X in '' '-loud' '-lowmem' '-barebones'
		do make clean && make -j$PM CFLAGS_EXTRA="$CE" stripped && cp $NAME$EXT $PKGNAME/$NAME$X$EXT$TYPE || ERR=1
		[ $ERR -eq 0 ] && lipo -create -output $PKGNAME/jdupes_temp $PKGNAME/$NAME$X$EXT$TYPE $PKGNAME/$NAME$X$EXT && mv $PKGNAME/jdupes_temp $PKGNAME/$NAME$X$EXT
	done
	make clean
	test $ERR -gt 0 && echo "Error building packages; aborting." && exit 1
	rm -f $PKGNAME/$NAME$EXT$TYPE $PKGNAME/$NAME-loud$EXT$TYPE $PKGNAME/$NAME-lowmem$EXT$TYPE $PKGNAME/$NAME-barebones$EXT$TYPE
fi
test "$PKGTYPE" = "zip" && zip -9r $PKGNAME.zip $PKGNAME/
test "$PKGTYPE" = "tar"  && tar -c $PKGNAME/ > $PKGNAME.pkg.tar
test "$PKGTYPE" = "gz"  && tar -c $PKGNAME/ | gzip -9 > $PKGNAME.pkg.tar.gz
test "$PKGTYPE" = "xz"  && tar -c $PKGNAME/ | xz -e > $PKGNAME.pkg.tar.xz
echo "Package generation complete."
