# wclip version
VERSION = 1.1

# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

PROTOPREFIX = /usr/share

# includes and libs
#INCS =
LIBS = -lwayland-client -lmagic

# flags
CPPFLAGS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_XOPEN_SOURCE=700L -D_POSIX_C_SOURCE=200809L -DVERSION=\"${VERSION}\"
CFLAGS = -std=c99 -pedantic -Wall -Os ${INCS} ${CPPFLAGS}
LDFLAGS = ${LIBS}

# compiler and linker
CC = cc
