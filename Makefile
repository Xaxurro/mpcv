music-manager.bin: main.c
	$(CC) main.c -o music-manager.bin -Wall -Wextra -pedantic -std=c99
