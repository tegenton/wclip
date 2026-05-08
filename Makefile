# wclip - Wayland clipboard
include config.mk

SRC = wclip.c
OBJ = ${SRC:.c=.o}

all: wclip

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk

config.h: config.def.h
	cp config.def.h $@

wclip: ${OBJ}
	${CC} -o $@ $^ ${LDFLAGS}

clean:
	rm -f wclip ${OBJ} wclip-${VERSION}.tar.gz

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
