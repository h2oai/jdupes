#!/bin/sh

WINRES="winres.rc"

# Get version number components
VER="$(grep '^#define VER "' version.h | cut -d\" -f2)"
V1="$(echo "$VER" | cut -d. -f1)"; test -z "$V1" && V1=0
V2="$(echo "$VER" | cut -d. -f2)"; test -z "$V2" && V2=0
V3="$(echo "$VER" | cut -d. -f3)"; test -z "$V3" && V3=0
V4="$(echo "$VER" | cut -d. -f4)"; test -z "$V4" && V4=0
PRODVER="$V1,$V2,$V3,$V4"
echo "$VER  =  $PRODVER"

# Actually change the manifest version information
sed -i 's/\([A-Z]*\)VERSION [0-9],.*/\1VERSION '"$PRODVER/"';s/"\([A-Za-z]*\)Version", "[0-9],.*"/"\1Version", '"\"$PRODVER\"/" "$WINRES"
