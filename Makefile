BINDIR=/usr/bin

fiemap: fiemap.o 

fiemap.o: fiemap.c Makefile

CFLAGS += -O2 -Wall

clean:
	rm -rf fiemap fiemap.o fiemap*snap

install: fiemap
	mkdir -p ${DESTDIR}${BINDIR}
	cp fiemap ${DESTDIR}${BINDIR}

