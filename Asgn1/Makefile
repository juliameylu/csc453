CC = gcc
CFLAGS = -Wall -g -fPIC

.PHONY: all malloc intel-all clean

all: malloc

malloc: libmalloc.a libmalloc.so
	@echo done

libmalloc.a: malloc.o
	ar rcs libmalloc.a malloc.o
	ranlib libmalloc.a

libmalloc.so: malloc.o
	$(CC) $(CFLAGS) -shared -o libmalloc.so malloc.o

malloc.o: malloc.c
	$(CC) $(CFLAGS) -c -o malloc.o malloc.c

intel-all: lib/libmalloc.so lib64/libmalloc.so

lib/libmalloc.so: lib malloc32.o
	$(CC) $(CFLAGS) -m32 -shared -o lib/libmalloc.so malloc32.o

lib64/libmalloc.so: lib64 malloc64.o
	$(CC) $(CFLAGS) -m64 -shared -o lib64/libmalloc.so malloc64.o

lib:
	mkdir -p lib

lib64:
	mkdir -p lib64

malloc32.o: malloc.c
	$(CC) $(CFLAGS) -m32 -c -o malloc32.o malloc.c

malloc64.o: malloc.c
	$(CC) $(CFLAGS) -m64 -c -o malloc64.o malloc.c

clean:
	rm -f malloc.o malloc32.o malloc64.o libmalloc.a libmalloc.so
	rm -f *~ TAGS

tests: malloc.c tests.c
	gcc -Wall -g -o tests tests.c malloc.c

gdb: tests
	gdb ./tests

