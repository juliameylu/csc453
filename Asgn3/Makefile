CC = gcc
CFLAGS = -Wall -g -I.
CHECK_CFLAGS = $(CFLAGS) -Wextra -Werror -Wno-deprecated-declarations
LDLIBS = -lpthread -lrt

.PHONY: all check valgrind clean

all: dine

dine: dine.o
	$(CC) $(CFLAGS) -o dine dine.o $(LDLIBS)
	@echo done

dine.o: dine.c dine.h
	$(CC) $(CFLAGS) -c -o dine.o dine.c

check: dine.c dine.h
	$(CC) $(CHECK_CFLAGS) -fsyntax-only dine.c

valgrind: dine
	valgrind --leak-check=full --show-leak-kinds=all ./dine $(RUN)

clean:
	rm -f dine.o dine
	rm -f *~ TAGS
