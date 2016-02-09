#
# Makefile for rcom
# (c) 2001 by David Gerber <zapek@meanmachine.ch>
#
# $Id: makefile,v 1.2 2002/11/29 10:42:55 zapek Exp $

OBJS = rcom.o
CFLAGS = -O2
LDFLAGS = -s
EXE = rcom

.c.o:
	$(CC) $(CFLAGS) -c $<

all: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $(EXE)

clean:
	-rm *.o $(EXE)

archive: clean
	-rm -rf rcom
	-rm rcom.tar.gz
	mkdir -p rcom
	cp rcom.c INSTALL LICENSE README makefile rcom
	tar -czvf rcom.tar.gz rcom
	-rm -rf rcom
