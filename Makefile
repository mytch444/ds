# ds - display manager
# © Quentin Carbonneaux 2010
# Large to the purpose of the program by
# (C) Mytchel Hammond 2015

include config.mk

INCS = -I. -I/usr/include -I${X11INC}
LIBS = -L/usr/lib -lc -L${X11LIB} -lX11 -lXext
CFLAGS = -g -ansi -pedantic -Wall ${INCS} ${FEATURES}
LDFLAGS = ${LIBS}


all: ds

ds: ds.o
	${CC} -o ds ${LDFLAGS} ds.o

ds.o: config.h

clean:
	-rm *.o ds

install: all
	${INSTALL} -o ${owner} -g ${group} ds ${bindir}/ds
	${INSTALL} -o ${owner} -g ${group} ds.1 ${mandir}/ds.1

uninstall:
	-rm ${bindir}/ds
	-rm ${mandir}/ds.1
	@echo Unsatisfied ? send your remarks at qcarbonneaux@gmail.com

.PHONY: clean install
