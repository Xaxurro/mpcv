/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define CTRL_KEY(key) ((key) & 0x1f)
#define ESCAPE '\x1b'

enum COMMANDS {
	MOVE_LEFT =	1000,
	MOVE_DOWN =	1001,
	MOVE_UP =	1002,
	MOVE_RIGHT =	1003,
	MOVE_PAGE_UP =	1004,
	MOVE_PAGE_DOWN =1005,
};

/*** data ***/
struct editorConfig {
	int cursorRow;
	int cursorColumn;
	int screenRows;
	int screenColumns;
	struct termios original_termios;
};

struct editorConfig config;

/*** terminal ***/
void die(const char *string) {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(string);
	exit(1);
}

void disableRawMode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &config.original_termios) == -1) {
		die("tcsetattr");
	}
}

void enableRawMode() {
	if (tcgetattr(STDIN_FILENO, &config.original_termios) == -1) {
		die("tcgetattr");
	}
	atexit(disableRawMode);

	struct termios raw = config.original_termios;
	raw.c_iflag &= ~(BRKINT);	/* Disables break condition -> SIGINT  */
	raw.c_iflag &= ~(INPCK);	/* Enables parity checking  */
	raw.c_iflag &= ~(ISTRIP);	/* Disables stripping the 8th bit byu default  */
	raw.c_iflag &= ~(ICRNL);	/* Disables Carriage Return -> New Line (CTRL-M)  */
	raw.c_iflag &= ~(IXON);		/* Disables CTRL-S/Q  */

	raw.c_oflag &= ~(OPOST);	/* Disables post-processing output  */

	raw.c_cflag |= ~(CS8);		/* bit mask Character-size = 8 per byte */

	raw.c_lflag &= ~(ECHO);		/* Disables printing */
	raw.c_lflag &= ~(ICANON);	/* Disables read byte by byte instead of line by line */
	raw.c_lflag &= ~(IEXTEN);	/* Disables read byte by byte instead of line by line */
	raw.c_lflag &= ~(ISIG);		/* Disables CTRL-C/Z */

	raw.c_cc[VMIN] = 0;		/* Minimum amount of bytes `read()` needs to return */
	raw.c_cc[VTIME] = 1;		/* Maximum amount of time to wait before `read()` returns (1/10 of a sec) */

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
		die("tcsetattr");
	}
}

int uiReadKey() {
	int nread;
	char key;
	while ((nread = read(STDIN_FILENO, &key, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}

	switch (key) {
		case 'h': return MOVE_LEFT;
		case 'j': return MOVE_DOWN;
		case 'k': return MOVE_UP;
		case 'l': return MOVE_RIGHT;
		case CTRL_KEY('b'): return MOVE_PAGE_UP;
		case CTRL_KEY('f'): return MOVE_PAGE_DOWN;
	}

	/* If is a escape sequence */
	if (key != ESCAPE) {
		return key;
	}

	char sequence[3];

	if (read(STDIN_FILENO, &sequence[0], 1) != 1) return ESCAPE;
	if (read(STDIN_FILENO, &sequence[1], 1) != 1) return ESCAPE;

	if (sequence[0] == '[') {
		switch (sequence[1]) {
			case 'D': return MOVE_LEFT;
			case 'B': return MOVE_DOWN;
			case 'A': return MOVE_UP;
			case 'C': return MOVE_RIGHT;
		}
	}

	return ESCAPE;
}

int getCursorPosition(int *rows, int *cols) {
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

	char buffer[32];
	unsigned int i = 0;
	while (i < sizeof(buffer) -1) {
		if (read(STDIN_FILENO, &buffer[i], 1) != 1) break;
		if (buffer[i] == 'R') break;
		i++;
	}
	buffer[i] = '\0';						/* Remove Escape Sequence */

	if (buffer[0] != '\x1b' || buffer[1] != '[') return -1;
	if (sscanf(&buffer[2], "%d;%d", rows, cols) != 2) return -1;

	return 0;
}

int getScreenSize(int *rows, int *cols) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return getCursorPosition(rows, cols);
	}
	*rows = ws.ws_row;
	*cols = ws.ws_col;
	return 0;
}

/*** append buffer ***/
struct stringBuffer {
	char *characters;
	int length;
};

#define STRING_BUFFER_INITIAL {NULL, 0}

void stringBufferConcat(struct stringBuffer *buffer, const char *string, int length) {
	char *newString = realloc(buffer->characters, buffer->length + length);		/* Gives a block of memory of the size of the buffer + new length */

	if (newString == NULL) return;
	memcpy(&newString[buffer->length], string, length);				/* Concat Strings */
	buffer->characters = newString;
	buffer->length += length;
}

void stringBufferFree(struct stringBuffer *buffer) {
	free(buffer->characters);
}

/*** output ***/
void uiDrawRows(struct stringBuffer *buffer) {
	int y;
	for (y = 0; y < config.screenRows; y++) {
		stringBufferConcat(buffer, "~", 2);

		stringBufferConcat(buffer, "\x1b[K", 3);	/* Erase in line */
		if (y < config.screenRows -1) {
			stringBufferConcat(buffer, "\r\n", 2);
		}
	}
}

void uiRefreshScreen() {
	struct stringBuffer bufferScreen = STRING_BUFFER_INITIAL;

	stringBufferConcat(&bufferScreen, "\x1b[?25l", 6);		/* Hides cursor */
	stringBufferConcat(&bufferScreen, "\x1b[H", 3);			/* Position the cursor */

	uiDrawRows(&bufferScreen);

	char bufferPosition[32];
	snprintf(bufferPosition, sizeof(bufferPosition), "\x1b[%d;%dH", config.cursorRow + 1, config.cursorColumn + 1);
	stringBufferConcat(&bufferScreen, bufferPosition, strlen(bufferPosition));

	stringBufferConcat(&bufferScreen, "\x1b[?25h", 6);		/* Hides cursor */

	write(STDOUT_FILENO, bufferScreen.characters, bufferScreen.length);
	stringBufferFree(&bufferScreen);
}

/*** input ***/
void uiMoveCursor(int key) {
	switch (key) {
		case MOVE_LEFT:
			if (config.cursorColumn > 0) config.cursorColumn--;
			break;
		case MOVE_DOWN:
			if (config.cursorRow < config.screenRows) config.cursorRow++;
			break;
		case MOVE_UP:
			if (config.cursorRow > 0) config.cursorRow--;
			break;
		case MOVE_RIGHT:
			if (config.cursorColumn < config.screenColumns) config.cursorColumn++;
			break;
	}
}

void uiMovePage(int key) {
	int rows = config.screenRows;
	switch(key) {
		case MOVE_PAGE_DOWN:
			while(rows--) {
				uiMoveCursor(MOVE_DOWN);
			}
			break;
		case MOVE_PAGE_UP:
			while(rows--) {
				uiMoveCursor(MOVE_UP);
			}
			break;
	}
}

char uiProcessKeyPress() {
	int key = uiReadKey();
	switch (key) {
		case CTRL_KEY('q'):
			exit(0);
			break;
		case MOVE_LEFT:
		case MOVE_DOWN:
		case MOVE_UP:
		case MOVE_RIGHT:
			uiMoveCursor(key);
			break;
		case MOVE_PAGE_DOWN:
		case MOVE_PAGE_UP:
			uiMovePage(key);
			break;
	}
	return key;
}

/*** init ***/
void initUI() {
	config.cursorRow = 0;
	config.cursorColumn = 1;
	if (getScreenSize(&config.screenRows, &config.screenColumns) == -1) die("getScreenSize");
}

int main() {
	enableRawMode();
	initUI();

	while (1) {
		uiRefreshScreen();
		uiProcessKeyPress();
	}
	return 0;
}
