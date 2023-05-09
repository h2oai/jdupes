# jdupes Makefile

CFLAGS ?= -O2 -g

# PREFIX determines where files will be installed. Common examples
# include "/usr" or "/usr/local".
PREFIX = /usr/local

# Uncomment for -B/--dedupe.
# This can also be enabled at build time: 'make ENABLE_DEDUPE=1'
#CFLAGS += -DENABLE_DEDUPE

# Uncomment for low memory usage at the expense of speed and features
# This can be enabled at build time: 'make LOW_MEMORY=1'
#LOW_MEMORY=1

# Uncomment this to build in hardened mode.
# This can be enabled at build time: 'make HARDEN=1'
#HARDEN=1

# PROGRAM_NAME determines the installation name and manual page name
PROGRAM_NAME = jdupes

# BIN_DIR indicates directory where program is to be installed.
# Suggested value is "$(PREFIX)/bin"
BIN_DIR = $(PREFIX)/bin

# MAN_DIR indicates directory where the jdupes man page is to be
# installed. Suggested value is "$(PREFIX)/man/man1"
MAN_BASE_DIR = $(PREFIX)/share/man
MAN_DIR = $(MAN_BASE_DIR)/man1
MAN_EXT = 1

# Required External Tools
CC ?= gcc
INSTALL = install
RM      = rm -f
RMDIR	= rmdir -p
MKDIR   = mkdir -p
INSTALL_PROGRAM = $(INSTALL) -m 0755
INSTALL_DATA    = $(INSTALL) -m 0644

# Main object files
OBJS += jdupes.o args.o helptext.o extfilter.o travcheck.o libjodycode_check.o
OBJS += act_deletefiles.o act_linkfiles.o act_printmatches.o act_summarize.o act_printjson.o

# Configuration section
COMPILER_OPTIONS = -Wall -Wwrite-strings -Wcast-align -Wstrict-aliasing -Wstrict-prototypes -Wpointer-arith -Wundef
COMPILER_OPTIONS += -Wshadow -Wfloat-equal -Waggregate-return -Wcast-qual -Wswitch-default -Wswitch-enum -Wconversion -Wunreachable-code -Wformat=2
COMPILER_OPTIONS += -std=gnu99 -D_FILE_OFFSET_BITS=64 -fstrict-aliasing -pipe
COMPILER_OPTIONS += -DNO_ATIME

# Remove unused code if requested
ifdef GC_SECTIONS
COMPILER_OPTIONS += -fdata-sections -ffunction-sections
LINK_OPTIONS += -Wl,--gc-sections
endif


UNAME_S=$(shell uname -s)

# Don't use unsupported compiler options on gcc 3/4 (Mac OS X 10.5.8 Xcode)
ifeq ($(UNAME_S), Darwin)
	GCCVERSION = $(shell expr `LC_ALL=C gcc -v 2>&1 | grep 'gcc version ' | cut -d\  -f3 | cut -d. -f1` \>= 5)
else
	GCCVERSION = 1
endif
ifeq ($(GCCVERSION), 1)
	COMPILER_OPTIONS += -Wextra -Wstrict-overflow=5 -Winit-self
endif

# Are we running on a Windows OS?
ifeq ($(OS), Windows_NT)
	ifndef NO_WINDOWS
		ON_WINDOWS=1
	endif
endif

# Debugging code inclusion
ifdef LOUD
DEBUG=1
COMPILER_OPTIONS += -DLOUD_DEBUG
endif
ifdef DEBUG
COMPILER_OPTIONS += -DDEBUG
else
COMPILER_OPTIONS += -DNDEBUG
endif
ifdef HARDEN
COMPILER_OPTIONS += -Wformat -Wformat-security -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE -fpie -Wl,-z,relro -Wl,-z,now
endif

# Catch someone trying to enable BTRFS in flags and turn on ENABLE_DEDUPE
ifneq (,$(findstring DENABLE_BTRFS,$(CFLAGS) $(CFLAGS_EXTRA)))
	ENABLE_DEDUPE=1
endif
ifneq (,$(findstring DENABLE_DEDUPE,$(CFLAGS) $(CFLAGS_EXTRA)))
	ENABLE_DEDUPE=1
endif

# MinGW needs this for printf() conversions to work
ifdef ON_WINDOWS
	ifndef NO_UNICODE
		UNICODE=1
		COMPILER_OPTIONS += -municode
		SUFFIX=.exe
	endif
	COMPILER_OPTIONS += -D__USE_MINGW_ANSI_STDIO=1 -DON_WINDOWS=1
	ifeq ($(UNAME_S), MINGW32_NT-5.1)
		OBJS += winres_xp.o
	else
		OBJS += winres.o
	endif
	override undefine ENABLE_DEDUPE
endif

# Bare-bones mode (for the adventurous lunatic) - includes all LOW_MEMORY options
ifdef BARE_BONES
LOW_MEMORY=1
COMPILER_OPTIONS += -DNO_DELETE -DNO_TRAVCHECK -DBARE_BONES -DNO_ERRORONDUPE
COMPILER_OPTIONS += -DCHUNK_SIZE=4096 -DPATHBUF_SIZE=1024
endif

# Low memory mode
ifdef LOW_MEMORY
USE_JODY_HASH = 1
COMPILER_OPTIONS += -DLOW_MEMORY
COMPILER_OPTIONS += -DNO_HARDLINKS -DNO_SYMLINKS -DNO_USER_ORDER -DNO_PERMS
COMPILER_OPTIONS += -DNO_ATIME -DNO_JSON -DNO_EXTFILTER -DNO_CHUNKSIZE
COMPILER_OPTIONS += -DNO_JODY_SORT
ifndef BARE_BONES
COMPILER_OPTIONS += -DCHUNK_SIZE=16384
endif
endif

# Use jody_hash instead of xxHash if requested
ifdef USE_JODY_HASH
COMPILER_OPTIONS += -DUSE_JODY_HASH
OBJS_CLEAN += xxhash.o
else
ifndef EXTERNAL_HASH_LIB
OBJS += xxhash.o
endif
endif  # USE_JODY_HASH

# Stack size limit can be too small for deep directory trees, so set to 16 MiB
# The ld syntax for Windows is the same for both Cygwin and MinGW
ifndef LOW_MEMORY
ifeq ($(OS), Windows_NT)
COMPILER_OPTIONS += -Wl,--stack=16777216
else ifeq ($(UNAME_S), Darwin)
COMPILER_OPTIONS += -Wl,-stack_size -Wl,0x1000000
else
COMPILER_OPTIONS += -Wl,-z,stack-size=16777216
endif
endif

# Don't do clonefile on Mac OS X < 10.13 (High Sierra)
ifeq ($(UNAME_S), Darwin)
DARWINVER := $(shell expr `uname -r | cut -d. -f1` \< 17)
ifeq "$(DARWINVER)" "1"
COMPILER_OPTIONS += -DNO_CLONEFILE=1
endif
endif

# Compatibility mappings for dedupe feature
ifdef ENABLE_BTRFS
ENABLE_DEDUPE=1
endif
ifdef STATIC_BTRFS_H
STATIC_DEDUPE_H=1
endif

# Dedupe feature (originally only BTRFS, now generalized)
ifdef ENABLE_DEDUPE
COMPILER_OPTIONS += -DENABLE_DEDUPE
OBJS += act_dedupefiles.o
else
OBJS_CLEAN += act_dedupefiles.o
endif
ifdef STATIC_DEDUPE_H
COMPILER_OPTIONS += -DSTATIC_DEDUPE_H
endif

### Find nearby libjodycode
ifdef USE_NEARBY_JC
 ifneq ("$(wildcard ../libjodycode/libjodycode.h)","")
 COMPILER_OPTIONS += -I../libjodycode -L../libjodycode
 endif
endif
ifdef FORCE_JC_DLL
LINK_OPTIONS += -l:../libjodycode/libjodycode.dll
else
LINK_OPTIONS += -ljodycode
endif


CFLAGS += $(COMPILER_OPTIONS) $(CFLAGS_EXTRA)
LDFLAGS += $(LINK_OPTIONS) $(LDFLAGS_EXTRA)


all: libjodycode_hint $(PROGRAM_NAME)

dynamic_jc: $(PROGRAM_NAME)
	$(CC) $(CFLAGS) $(OBJS) -Wl,-Bdynamic $(LDFLAGS) -o $(PROGRAM_NAME)$(SUFFIX)

static_jc: $(PROGRAM_NAME)
	$(CC) $(CFLAGS) $(OBJS) -Wl,-Bstatic $(LDFLAGS) -Wl,-Bdynamic -o $(PROGRAM_NAME)$(SUFFIX)

static: $(PROGRAM_NAME)
	$(CC) $(CFLAGS) $(OBJS) -static $(LDFLAGS) -o $(PROGRAM_NAME)$(SUFFIX)

static_stripped: $(PROGRAM_NAME) static
	strip $(PROGRAM_NAME)$(SUFFIX)

$(PROGRAM_NAME): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $(PROGRAM_NAME)$(SUFFIX)

winres.o: winres.rc winres.manifest.xml
	./tune_winres.sh
	windres winres.rc winres.o

winres_xp.o: winres_xp.rc
	./tune_winres.sh
	windres winres_xp.rc winres_xp.o

installdirs:
	test -e $(DESTDIR)$(BIN_DIR) || $(MKDIR) $(DESTDIR)$(BIN_DIR)
	test -e $(DESTDIR)$(MAN_DIR) || $(MKDIR) $(DESTDIR)$(MAN_DIR)

install: $(PROGRAM_NAME) installdirs
	$(INSTALL_PROGRAM)	$(PROGRAM_NAME)$(SUFFIX)   $(DESTDIR)$(BIN_DIR)/$(PROGRAM_NAME)$(SUFFIX)
	$(INSTALL_DATA)		$(PROGRAM_NAME).1 $(DESTDIR)$(MAN_DIR)/$(PROGRAM_NAME).$(MAN_EXT)

uninstalldirs:
	-test -e $(DESTDIR)$(BIN_DIR) && $(RMDIR) $(DESTDIR)$(BIN_DIR)
	-test -e $(DESTDIR)$(MAN_DIR) && $(RMDIR) $(DESTDIR)$(MAN_DIR)

uninstall: uninstalldirs
	$(RM)	$(DESTDIR)$(BIN_DIR)/$(PROGRAM_NAME)$(SUFFIX)
	$(RM)	$(DESTDIR)$(MAN_DIR)/$(PROGRAM_NAME).$(MAN_EXT)

test:
	./test.sh

stripped: $(PROGRAM_NAME)
	strip $(PROGRAM_NAME)$(SUFFIX)

clean:
	$(RM) $(OBJS) $(OBJS_CLEAN) build_date.h $(PROGRAM_NAME)$(SUFFIX) *~ .*.un~ *.gcno *.gcda *.gcov

distclean: clean
	$(RM) -rf *.pkg.tar* jdupes-*-*/ jdupes-*-*.zip

chrootpackage:
	+./chroot_build.sh

package:
	+./generate_packages.sh $(ARCH)

libjodycode_hint:
	@echo "hint: if ../libjodycode is built and Make fails, try doing 'make USE_NEARBY_JC=1 static_jc'"$$'\n'
