
CC = gcc
CFLAGS = -Wall -O2
LIBS = -lncurses -lpthread

CTOP = ctop


# default target
.PHONY : all
all: $(CTOP)
	@echo all done!

OBJS =
OBJS += ctop.o
OBJS += nic.o

ctop.o: ctop.h
nic.o: nic.c interface.h ctop.h

#$(LOCATOR): $(LOCATOR_OBJS)
#	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)
$(CTOP): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)


DST = /usr/local/bin/
.PHONY : install
install:
	cp $(CTOP) $(DST)

.PHONY : clean
clean:
	rm -f *.o core a.out ctop
