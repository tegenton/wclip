# wclip - Wayland clipboard
include config.mk

.POSIX:
.SUFFIXES:

# TODO: clean this up, how do i get suffix rules to work with paths
.SUFFIXES: .xml .h .c .o

all: wclip

.xml.h:
	wayland-scanner client-header < $< > $@
.xml.c:
	wayland-scanner private-code < $< > $@

.c.o:
	${CC} -c ${CFLAGS} $<

ext-data-control-v1.h: ${PROTOPREFIX}/wayland-protocols/staging/ext-data-control/ext-data-control-v1.xml
	wayland-scanner client-header < $< > $@
ext-data-control-v1.c: ${PROTOPREFIX}/wayland-protocols/staging/ext-data-control/ext-data-control-v1.xml
	wayland-scanner private-code < $< > $@

config.h: config.def.h
	cp config.def.h $@

wclip.o: wclip.c ext-data-control-v1.h

wclip: wclip.o ext-data-control-v1.o
	${CC} -o $@ $^ ${LDFLAGS}

clean:
	rm -f wclip wclip.o ext-data-control-v1.* wclip-${VERSION}.tar.gz

dist: clean
	mkdir -p wclip-${VERSION}
	cp LICENSE Makefile README config.def.h config.mk\
		wclip.1 ${SRC} wclip-${VERSION}
	tar -cf wclip-${VERSION}.tar wclip-${VERSION}
	gzip wclip-${VERSION}.tar
	rm -rf wclip-${VERSION}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f wclip ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/wclip
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	sed "s/VERSION/${VERSION}/g" < wclip.1 > ${DESTDIR}${MANPREFIX}/man1/wclip.1
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/wclip.1

uninstall:
	rm -rf ${DESTDIR}${PREFIX}/bin/wclip\
		${DESTDIR}${MANPREFIX}/man1/wclip.1

.PHONY: all clean dist install uninstall
