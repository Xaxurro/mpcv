/*** includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/
#define CTRL_KEY(key) ((key) & 0x1f)
#define ESCAPE '\x1b'
#define DEFAULT_TIME_MESSAGE 5

enum COMMANDS {
	EXIT =		 9999,
	BACKSPACE =	 127,
	MOVE_LEFT =	 1000,
	MOVE_DOWN =	 1001,
	MOVE_UP =	 1002,
	MOVE_RIGHT =	 1003,
	MOVE_PAGE_UP =	 1004,
	MOVE_PAGE_DOWN = 1005,
	SONG_PLAY =	 1006,
	SONG_SEARCH =	 1007,
};

/*** data ***/
typedef struct uiRow {
	int length;
	int renderLength;
	char *characters;
	char *render;
} uiRow;

struct uiDataStruct {
	int cursorRow;		/* cursor position row */
	int cursorColumn;	/* cursor position column */
	int uiOffsetRow;	
	int uiOffsetColumn;
	int screenRows;		/* Rows that the screen has */
	int screenColumns;	/* Columns that the screen has */
	int amountRows;
	uiRow *uiRow;
	char statusMessage[80];
	time_t statusMessageTime;	/* Timestamp status message */
	struct termios originalTermios;
};

struct uiDataStruct uiData;

/*** function declaration ***/
void uiSetStatusMessage(const char *message, ...);
void uiRefreshScreen();
char *uiPrompt(char *prompt, void (*callback)(char *, int));

/*** terminal ***/
void die(const char *string) {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(string);
	exit(1);
}

void disableRawMode() {
	write(STDOUT_FILENO, "\x1b[?7h", 6);				/* Disables Auto-wrap from terminal */
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &uiData.originalTermios) == -1) {
		die("tcsetattr");
	}
}

void enableRawMode() {
	write(STDOUT_FILENO, "\x1b[?7l", 6);				/* Disables Auto-wrap from terminal */
	if (tcgetattr(STDIN_FILENO, &uiData.originalTermios) == -1) {
		die("tcgetattr");
	}
	atexit(disableRawMode);

	struct termios raw = uiData.originalTermios;
	raw.c_iflag &= ~(BRKINT);	/* Disables break condition -> SIGINT  */
	raw.c_iflag &= ~(INPCK);	/* Enables parity checking  */
	raw.c_iflag &= ~(ISTRIP);	/* Disables stripping the 8th bit byu default  */
	raw.c_iflag &= ~(ICRNL);	/* Disables Carriage Return -> New Line (CTRL-M)  */
	raw.c_iflag &= ~(IXON);		/* Disables CTRL-S/Q  */

	raw.c_oflag &= ~(OPOST);	/* Disables post-processing output  */

	raw.c_cflag |= CS8;		/* bit mask Character-size = 8 per byte */

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
		case 'q': return EXIT;
		case 'h': return MOVE_LEFT;
		case 'j': return MOVE_DOWN;
		case 'k': return MOVE_UP;
		case 'l': return MOVE_RIGHT;
		case CTRL_KEY('b'): return MOVE_PAGE_UP;
		case CTRL_KEY('f'): return MOVE_PAGE_DOWN;
		case 'p': return SONG_PLAY;
		case '/': return SONG_SEARCH;
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

int uiReadKeyRaw() {
	int nread;
	char key;
	while ((nread = read(STDIN_FILENO, &key, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}

	if (key != ESCAPE) {
		return key;
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

/*** row operations ***/
void uiUpdateRow(uiRow *row) {
	free(row->render);
	row->render = malloc(row->length + 1);
	
	int iterator;
	int index = 0;
	for (iterator = 0; iterator < row->length; iterator++) {	/* Copy chars */
		row->render[index++] = row->characters[iterator];
	}
	row->render[index] = '\0';
	row->renderLength = index;
}

void uiAppendRow(char *string, size_t length) {
	uiData.uiRow = realloc(uiData.uiRow, sizeof(uiRow) * (uiData.amountRows + 1));

	int currentRow = uiData.amountRows;
	uiData.uiRow[currentRow].length = length;
	uiData.uiRow[currentRow].characters = malloc(length + 1);
	memcpy(uiData.uiRow[currentRow].characters, string, length);
	uiData.uiRow[currentRow].characters[length] = '\0';

	uiData.uiRow[currentRow].renderLength = 0;
	uiData.uiRow[currentRow].render = NULL;
	uiUpdateRow(&uiData.uiRow[currentRow]);

	uiData.amountRows++;
}

/*** music management ***/
void uiOpen() {
	FILE *filePipe = popen("mpc listall", "r");
	if (!filePipe) die("popen");

	char *line = NULL;
	size_t lineCap = 0;
	ssize_t lineLength;

	while ((lineLength = getline(&line, &lineCap, filePipe)) != -1) {					/* Read until EOF */
		while (lineLength > 0 && (line[lineLength - 1] == '\n' || line[lineLength - 1] == '\r')) {	/* Removes Carriage Return */
			lineLength--;
		}
		uiAppendRow(line, lineLength);
	}

	free(line);
	pclose(filePipe);
}

/*** search ***/
void songSearchCallback(char *query, int key) {
	if (key == '\r' || key == ESCAPE) return;

	int index;
	for (index = 0; index < uiData.amountRows; index++) {
		uiRow *row = &uiData.uiRow[index];
		char *match = strstr(row->render, query);
		if (match) {
			uiData.cursorRow = index;
			uiData.cursorColumn = match - row->render;
			uiData.uiOffsetRow = uiData.amountRows;
			break;
		}
	}
}

void songSearch() {
	char *query = uiPrompt("Search: %s (ESC to cancel)", songSearchCallback);

	if (query) {
		free(query);
	}
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
void uiScroll() {
	/* Vertical Scroll */
	if (uiData.cursorRow < uiData.uiOffsetRow) {						/* Top of window? scroll up */
		uiData.uiOffsetRow = uiData.cursorRow;
	}
	if (uiData.cursorRow >= uiData.uiOffsetRow + uiData.screenRows) {			/* Bottom of window? scroll down */
		uiData.uiOffsetRow = uiData.cursorRow - uiData.screenRows + 1;
	}
	/* Horizontal Scroll */
	if (uiData.cursorColumn < uiData.uiOffsetColumn) {					/* Top of window? scroll up */
		uiData.uiOffsetColumn = uiData.cursorColumn;
	}
	if (uiData.cursorColumn >= uiData.uiOffsetColumn + uiData.screenColumns) {		/* Bottom of window? scroll down */
		uiData.uiOffsetColumn = uiData.cursorColumn - uiData.screenColumns + 1;
	}
}

void uiWriteStatusBar(struct stringBuffer *buffer) {
	stringBufferConcat(buffer, "\x1b[7m", 4);	/* Invert Colors */

	char status[80];

	int length = snprintf(status, sizeof(status), "%d songs", uiData.amountRows);
	if (length > uiData.screenColumns) {
		length = uiData.screenColumns;
	}
	stringBufferConcat(buffer, status, length);

	while(length < uiData.screenColumns) {
		stringBufferConcat(buffer, " ", 1);
		length++;
	}

	stringBufferConcat(buffer, "\x1b[m", 4);	/* Return Colors back to normal */
	stringBufferConcat(buffer, "\r\n", 2);		/* Return Colors back to normal */
}

void uiWriteMessageBar(struct stringBuffer *buffer) {
	stringBufferConcat(buffer, "\x1b[K]", 3);	/* Clear -ONLY- the message bar */
	int messageLength = strlen(uiData.statusMessage);
	if (messageLength > uiData.screenColumns) messageLength = uiData.screenColumns;
	if (messageLength && time(NULL) - uiData.statusMessageTime < DEFAULT_TIME_MESSAGE) {
		stringBufferConcat(buffer, uiData.statusMessage, messageLength);
	}
}

void uiWriteLineEmpty(struct stringBuffer *buffer) {
	stringBufferConcat(buffer, "~", 1);
}

void uiWriteLine(struct stringBuffer *buffer, int index) {
	int length = uiData.uiRow[index].renderLength - uiData.uiOffsetColumn;	/* Offset for horziontal Scrolling */
	if (length < 0) length = 0;
	if (length > uiData.screenColumns) length = uiData.screenColumns;
	stringBufferConcat(buffer, "  ", 2);					/* Padding */
	stringBufferConcat(buffer, &uiData.uiRow[index].render[uiData.uiOffsetColumn], length);
}

void uiWriteRows(struct stringBuffer *buffer) {					/* TODO Change thename of variables, it's awful */
	int currentRow;
	for (currentRow = 0; currentRow < uiData.screenRows; currentRow++) {
		int visibleRow = currentRow + uiData.uiOffsetRow;
		if (visibleRow >= uiData.amountRows) {
			uiWriteLineEmpty(buffer);
		} else {
			uiWriteLine(buffer, visibleRow);
		}
		stringBufferConcat(buffer, "\x1b[K", 3);			/* Erase after cursor */
		stringBufferConcat(buffer, "\r\n", 2);				/* write newline */
	}
}

void uiRefreshScreen() {
	uiScroll();

	struct stringBuffer bufferScreen = STRING_BUFFER_INITIAL;

	stringBufferConcat(&bufferScreen, "\x1b[?25l", 6);		/* Hides cursor */
	stringBufferConcat(&bufferScreen, "\x1b[H", 3);			/* Position the cursor */

	uiWriteRows(&bufferScreen);
	uiWriteStatusBar(&bufferScreen);
	uiWriteMessageBar(&bufferScreen);

	char bufferPosition[32];
	snprintf(bufferPosition, sizeof(bufferPosition), "\x1b[%d;%dH", (uiData.cursorRow - uiData.uiOffsetRow) + 1, (uiData.cursorColumn - uiData.uiOffsetColumn) + 1);
	stringBufferConcat(&bufferScreen, bufferPosition, strlen(bufferPosition));

	stringBufferConcat(&bufferScreen, "\x1b[?25h", 6);		/* Hides cursor */

	write(STDOUT_FILENO, bufferScreen.characters, bufferScreen.length);
	stringBufferFree(&bufferScreen);
}

void uiSetStatusMessage(const char *message, ...) {
	va_list arguments;									/* extra argument List */
	va_start(arguments, message);
	vsnprintf(uiData.statusMessage, sizeof(uiData.statusMessage), message, arguments);
	va_end(arguments);
	uiData.statusMessageTime = time(NULL);
}

/*** input ***/

char *uiPrompt(char *prompt, void (*callback)(char *, int)) {
	size_t bufferLengthMax = 128;
	char *buffer = malloc(bufferLengthMax);

	size_t bufferLength = 0;
	buffer[0] = '\0';

	while (1) {
		uiSetStatusMessage(prompt, buffer);
		uiRefreshScreen();

		int character = uiReadKeyRaw();
		if (character == BACKSPACE) {
			if (bufferLength != 0) buffer[bufferLength--] = '\0';
		} else if (character == '\x1b') {				/* If user pressed Escape */
			uiSetStatusMessage("");
			if (callback) callback(buffer, character);
			free(buffer);
			return NULL;
		} else if (character == '\r') {					/* If user pressed Enter */
			if (bufferLength != 0) {				/* and there is something to write */
				uiSetStatusMessage("");
				if (callback) callback(buffer, character);
				return buffer;
			}
		} else if (!iscntrl(character) && character < 128) {		/* Is a printable character */
			if (bufferLength == bufferLengthMax - 1) {
				bufferLengthMax *= 2;
				buffer = realloc(buffer, bufferLengthMax);
			}
			buffer[bufferLength++] = character;
			buffer[bufferLength] = '\0';
		}
		if (callback) callback(buffer, character);
	}
}

void uiMoveCursor(int key) {
	switch (key) {
		case MOVE_LEFT:
			if (uiData.cursorColumn > 0) {
				uiData.cursorColumn--;
			}
			break;
		case MOVE_DOWN:
			if (uiData.cursorRow < uiData.amountRows) {
				uiData.cursorRow++;
			}
			break;
		case MOVE_UP:
			if (uiData.cursorRow > 0) {
				uiData.cursorRow--;
			}
			break;
		case MOVE_RIGHT:
			uiData.cursorColumn++;
			break;
	}
}

void uiMovePage(int key) {
	int rows = uiData.screenRows;
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

void songPlay() {
	int songIndex = uiData.cursorRow + 1;
	char songIndexString[12];
	sprintf(songIndexString, "%d", songIndex);

	struct stringBuffer bufferCommand = STRING_BUFFER_INITIAL;
	stringBufferConcat(&bufferCommand, "mpc play ", 9);		/* Command to execute */
	stringBufferConcat(&bufferCommand, songIndexString, sizeof(songIndexString));		/* Append index of song to play */

	FILE *filePipe = popen(bufferCommand.characters, "r");

	stringBufferFree(&bufferCommand);
	if (!filePipe) die("popen");
	pclose(filePipe);
}

char uiProcessKeyPress() {
	int key = uiReadKey();
	switch (key) {
		case EXIT:
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
		case SONG_PLAY:
			songPlay();
			break;
		case SONG_SEARCH:
			songSearch();
			break;
	}
	return key;
}

/*** init ***/
void initUI() {
	uiData.cursorRow = 0;
	uiData.cursorColumn = 2;
	uiData.uiOffsetRow = 0;
	uiData.uiOffsetColumn = 0;
	uiData.amountRows = 0;
	uiData.uiRow = NULL;
	uiData.statusMessage[0] = '\0';
	uiData.statusMessageTime = 0;
	if (getScreenSize(&uiData.screenRows, &uiData.screenColumns) == -1) die("getScreenSize");
	uiData.screenRows -= 2;		/* Status Bar + Message Bar */
}

int main() {
	enableRawMode();
	initUI();
	uiOpen();

	uiSetStatusMessage("q: exit");
	while (1) {
		uiRefreshScreen();
		uiProcessKeyPress();
	}
	return 0;
}
