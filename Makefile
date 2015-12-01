#
# fdupes Makefile
#

#####################################################################
# Standand User Configuration Section                               #
#####################################################################

#
# PREFIX indicates the base directory used as the basis for the
# determination of the actual installation directories.
# Suggested values are "/usr/local", "/usr", "/pkgs/fdupes-$(VERSION)"
#
PREFIX = /usr/local

#
# Certain platforms do not support long options (command line options).
# To disable long options, uncomment the following line.
#
#CFLAGS_CONFIG += -DOMIT_GETOPT_LONG

#
# 'Summarize matches' can use floating point calculations and show
# summaries with fractional amounts. Floating point support can add
# code and in some instances is better to avoid. Uncomment this line
# to only use integer arithmetic and drop all floating point code.
#
CFLAGS_CONFIG += -DNO_FLOAT

#####################################################################
# Developer Configuration Section                                   #
#####################################################################

#
# VERSION determines the program's version number.
#
include VERSION

#
# PROGRAM_NAME determines the installation name and manual page name
#
PROGRAM_NAME=fdupes

#
# BIN_DIR indicates directory where program is to be installed.
# Suggested value is "$(PREFIX)/bin"
#
BIN_DIR = $(PREFIX)/bin

#
# MAN_DIR indicates directory where the fdupes man page is to be
# installed. Suggested value is "$(PREFIX)/man/man1"
#
MAN_BASE_DIR = $(PREFIX)/share/man
MAN_DIR = $(MAN_BASE_DIR)/man1
MAN_EXT = 1

#
# Required External Tools
#

INSTALL = install	# install : UCB/GNU Install compatiable
#INSTALL = ginstall

RM      = rm -f

MKDIR   = mkdir -p
#MKDIR   = mkdirhier
#MKDIR   = mkinstalldirs


#
# Make Configuration
#
CC ?= gcc
COMPILER_OPTIONS = -Wall -Wextra -Wcast-align -Wstrict-aliasing -pedantic -Wstrict-overflow
COMPILER_OPTIONS += -std=gnu99 -O2 -g -D_FILE_OFFSET_BITS=64 -fstrict-aliasing -pipe

# Debugging code inclusion
ifdef DEBUG
COMPILER_OPTIONS += -DDEBUG
endif

# MinGW needs this for printf() conversions to work
ifeq ($(OS), Windows_NT)
	COMPILER_OPTIONS += -D__USE_MINGW_ANSI_STDIO=1
	OBJECT_FILES += getino.o
endif

CFLAGS= $(COMPILER_OPTIONS) -I. -DVERSION=\"$(VERSION)\" $(CFLAGS_CONFIG) $(CFLAGS_EXTRA)

INSTALL_PROGRAM = $(INSTALL) -c -m 0755
INSTALL_DATA    = $(INSTALL) -c -m 0644

#
# ADDITIONAL_OBJECTS - some platforms will need additional object files
# to support features not supplied by their vendor. Eg: GNU getopt()
#
#ADDITIONAL_OBJECTS = getopt.o

OBJECT_FILES += fdupes.o jody_hash.o $(ADDITIONAL_OBJECTS)

#####################################################################
# no need to modify anything beyond this point                      #
#####################################################################

all: fdupes

fdupes: $(OBJECT_FILES)
	$(CC) $(CFLAGS) -o fdupes $(OBJECT_FILES)

installdirs:
	test -d $(DESTDIR)$(BIN_DIR) || $(MKDIR) $(DESTDIR)$(BIN_DIR)
	test -d $(DESTDIR)$(MAN_DIR) || $(MKDIR) $(DESTDIR)$(MAN_DIR)

install: fdupes installdirs
	$(INSTALL_PROGRAM)	fdupes   $(DESTDIR)$(BIN_DIR)/$(PROGRAM_NAME)
	$(INSTALL_DATA)		fdupes.1 $(DESTDIR)$(MAN_DIR)/$(PROGRAM_NAME).$(MAN_EXT)

clean:
	$(RM) $(OBJECT_FILES) fdupes fdupes.exe *~
