#ifndef UI_H
#define UI_H

/* C64 color codes (see https://www.c64-wiki.com/wiki/COLOR) */
#define COL_BLACK       0
#define COL_WHITE       1
#define COL_BLUE        6
#define COL_LIGHT_RED   10   /* "Pink" / Light Red */
#define COL_DARK_GRAY   11
#define COL_MED_GRAY    12

void ui_init(void);
void ui_banner(void);

/* Print a status message in cyan at the current position. */
void ui_status(const char *msg);

/* Print the user input prompt. */
void ui_prompt(void);

/* Call before streaming assistant response (sets color, prints prefix). */
void ui_start_response(void);

/* Add a submitted user line to the transcript. */
void ui_user_line(const char *text);

/* Append a text fragment, handling word-wrap at column 40. */
void ui_append_text(const char *chunk);

/* Flush any buffered word and add a newline. */
void ui_end_response(void);

/* Short SID beep — used as response-complete cue. */
void ui_beep(void);

/* Clear the screen but keep the current color theme. */
void ui_clear(void);

/* Bottom input editor used by the Claude-style layout. */
void ui_input_begin(void);
void ui_input_draw(const char *text);
void ui_input_clear(void);

#endif
