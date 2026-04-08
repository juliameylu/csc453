pipeit: pipeit.c
	gcc -Wall -Wextra -pedantic -o pipeit pipeit.c

valgrind: pipeit
	valgrind --leak-check=full --show-leak-kinds=all ./pipeit

clean:
	rm -f pipeit outfile
