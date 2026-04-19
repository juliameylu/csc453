CC = gcc
AR = ar
RANLIB = ranlib
CFLAGS = -Wall -g -fPIC -m64 -I.

OBJS = lwp.o scheduler.o magic64.o

.PHONY: all lwp intel-all clean

all: lwp

lwp: liblwp.a liblwp.so
	@echo done

liblwp.a: $(OBJS)
	$(AR) rcs liblwp.a $(OBJS)
	$(RANLIB) liblwp.a

liblwp.so: $(OBJS)
	$(CC) $(CFLAGS) -shared -o liblwp.so $(OBJS)

lwp.o: lwp.c lwp.h scheduler.h fp.h
	$(CC) $(CFLAGS) -c -o lwp.o lwp.c

scheduler.o: scheduler.c scheduler.h lwp.h
	$(CC) $(CFLAGS) -c -o scheduler.o scheduler.c

magic64.o: magic64.S lwp.h fp.h
	$(CC) $(CFLAGS) -c -o magic64.o magic64.S

intel-all: lib64/liblwp.so

# Alternate shared-library output path used for the Intel target
lib64/liblwp.so: lib64 lwp64.o scheduler64.o magic6464.o
	$(CC) $(CFLAGS) -shared -o lib64/liblwp.so lwp64.o scheduler64.o magic6464.o

lib64:
	mkdir -p lib64

lwp64.o: lwp.c lwp.h scheduler.h fp.h
	$(CC) $(CFLAGS) -c -o lwp64.o lwp.c

scheduler64.o: scheduler.c scheduler.h lwp.h
	$(CC) $(CFLAGS) -c -o scheduler64.o scheduler.c

magic6464.o: magic64.S lwp.h fp.h
	$(CC) $(CFLAGS) -c -o magic6464.o magic64.S

# Rebuild with debug logging enabled
debug:
	$(MAKE) clean
	$(MAKE) CFLAGS='-Wall -g -fPIC -m64 -I. -DLWP_DEBUG'

clean:
	rm -f $(OBJS) lwp64.o scheduler64.o magic6464.o
	rm -f liblwp.a liblwp.so
	rm -rf lib64
	rm -f *~ TAGS
