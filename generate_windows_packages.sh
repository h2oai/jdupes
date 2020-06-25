#!/bin/sh

# Generate Windows package folders with variant builds

# Number of parallel make processes
PM=12

NAME="jdupes"

VER="$(cat version.h | grep '#define VER "' | cut -d\" -f2)"
echo "Program version: $VER"

TGT=$(gcc -v 2>&1 | grep Target | cut -d\  -f2- | cut -d- -f1)
test "$TGT" = "i686" && TA=win32
test "$TGT" = "x86_64" && TA=win64
echo "Target architecture: $TA"

PKGNAME="${NAME}-${VER}-$TA"

echo "Generating package for: $PKGNAME"
mkdir -p "$PKGNAME"
test ! -d "$PKGNAME" && echo "Can't create directory for package" && exit 1
cp CHANGES README.md LICENSE $PKGNAME/
make clean && make -j$PM stripped && cp ${NAME}.exe $PKGNAME/${NAME}.exe
make clean && make -j$PM LOUD=1 stripped && cp ${NAME}.exe $PKGNAME/${NAME}-loud.exe
make clean && make -j$PM LOW_MEMORY=1 stripped && cp ${NAME}.exe $PKGNAME/${NAME}-lowmem.exe
echo "Package generation complete."
