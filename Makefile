music-manager.bin: main.c
	$(CC) main.c -o mpcv -Wall -Wextra -pedantic -std=c99
