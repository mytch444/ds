# dm - display manager
# © Quentin Carbonneaux 2010
include config.mk

INCS = -I. -I/usr/include -I${X11INC}
LIBS = -L/usr/lib -lc -lcrypt -L${X11LIB} -lX11 -lXext
CFLAGS = -g -ansi -pedantic -Wall ${INCS} ${FEATURES}
LDFLAGS = ${LIBS}


all: dm

dm: dm.o
	${CC} -o dm ${LDFLAGS} $<

dm.o: config.h

clean:
	-rm *.o dm

install: all
	${INSTALL} -o root dm ${bindir}/dm
	${INSTALL} -o root dm.1 ${mandir}/dm.1

uninstall:
	-rm ${bindir}/dm
	-rm ${mandir}/dm.1
	@echo Unsatisfied ? send your remarks at qcarbonneaux@gmail.com

.PHONY: clean install
