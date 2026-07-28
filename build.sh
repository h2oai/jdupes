#!/bin/sh

# Build jdupes in Docker against a glibc 2.28 baseline.
#
# Usage:
#   ./build.sh                      # build for the host architecture
#   ./build.sh --arch x86_64        # build an x86_64 binary
#   ./build.sh --arch all           # build both aarch64 and x86_64
#   ./build.sh --output /tmp/bins   # where to drop the binaries (default: ./dist)
#
# Building for a non-host architecture runs the container under QEMU emulation.
# If the binfmt handler isn't registered, this script tells you how to add it
# (or does it for you with --install-binfmt).

set -eu

DOCKERFILE=Dockerfile.glibc228
OUTDIR=dist
ARCH=
INSTALL_BINFMT=0
NOCACHE=

usage() {
	cat <<-EOF
	Usage: $0 [options]

	  --arch ARCH        aarch64, x86_64, or all (default: host architecture)
	  --output DIR       output directory for binaries (default: dist)
	  --install-binfmt   register QEMU binfmt handlers if needed (runs a
	                     privileged container; affects the whole machine)
	  --no-cache         build without using the Docker layer cache
	  -h, --help         show this help
	EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
		--arch)		ARCH="$2"; shift 2 ;;
		--arch=*)	ARCH="${1#*=}"; shift ;;
		--output)	OUTDIR="$2"; shift 2 ;;
		--output=*)	OUTDIR="${1#*=}"; shift ;;
		--install-binfmt) INSTALL_BINFMT=1; shift ;;
		--no-cache)	NOCACHE=--no-cache; shift ;;
		-h|--help)	usage; exit 0 ;;
		*)		echo "$0: unknown option '$1'" >&2; usage >&2; exit 1 ;;
	esac
done

HOSTARCH=$(uname -m)
[ -n "$ARCH" ] || ARCH=$HOSTARCH

case "$ARCH" in
	all)			ARCHES="aarch64 x86_64" ;;
	aarch64|arm64)		ARCHES=aarch64 ;;
	x86_64|amd64)		ARCHES=x86_64 ;;
	*)			echo "$0: unsupported architecture '$ARCH'" >&2; exit 1 ;;
esac

command -v docker >/dev/null 2>&1 || { echo "$0: docker not found in PATH" >&2; exit 1; }
[ -f "$DOCKERFILE" ] || { echo "$0: $DOCKERFILE not found; run this from the jdupes source tree" >&2; exit 1; }

# Make sure we can execute foreign-architecture binaries before wasting a pull on it.
check_emulation() {
	arch=$1
	[ "$arch" != "$HOSTARCH" ] || return 0
	[ ! -e "/proc/sys/fs/binfmt_misc/qemu-$arch" ] || return 0

	if [ "$INSTALL_BINFMT" = 1 ]; then
		echo "==> Registering QEMU binfmt handlers (privileged container)"
		docker run --privileged --rm tonistiigi/binfmt --install all
		return 0
	fi

	cat >&2 <<-EOF

	Cannot build for $arch on a $HOSTARCH host: no QEMU binfmt handler is
	registered for $arch, so the build container can't run.

	Register the handlers once with either:

	    ./build.sh --arch $arch --install-binfmt
	    docker run --privileged --rm tonistiigi/binfmt --install all

	EOF
	exit 1
}

mkdir -p "$OUTDIR"
OUTDIR_ABS=$(cd "$OUTDIR" && pwd)

for arch in $ARCHES; do
	case "$arch" in
		aarch64) platform=linux/arm64; base=quay.io/pypa/manylinux_2_28_aarch64:latest ;;
		x86_64)  platform=linux/amd64; base=quay.io/pypa/manylinux_2_28_x86_64:latest ;;
	esac

	check_emulation "$arch"

	tag="jdupes-build:$arch"
	echo "==> Building jdupes for $arch ($platform)"
	docker build $NOCACHE \
		--platform "$platform" \
		--build-arg BASE_IMAGE="$base" \
		-f "$DOCKERFILE" \
		-t "$tag" \
		.

	echo "==> Extracting binary"
	docker run --rm --platform "$platform" \
		-v "$OUTDIR_ABS:/out" \
		"$tag" \
		sh -c "cp /build/jdupes/jdupes /out/jdupes-linux-$arch"

	echo "==> $OUTDIR/jdupes-linux-$arch"
done

echo
echo "Done. Binaries in $OUTDIR:"
ls -l "$OUTDIR_ABS"
