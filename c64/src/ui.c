#include <conio.h>
#include <peekpoke.h>
#include <string.h>
#include "ui.h"

#define COLS 40
#define TRANSCRIPT_ROWS 21
#define STATUS_ROW 21
#define INPUT_LABEL_ROW 22
#define INPUT_ROW 23
#define HELP_ROW 24
#define SCREEN_BASE 0x0400u

static unsigned char out_x = 0;
static unsigned char out_y = 0;

static void clear_row(unsigned char y)
{
    unsigned char x;
    gotoxy(0, y);
    for (x = 0; x < COLS; ++x) cputc(' ');
}

static void print_at(unsigned char x, unsigned char y, const char *s)
{
    gotoxy(x, y);
    while (*s && x < COLS) {
        cputc(*s++);
        ++x;
    }
}

static void scroll_transcript(void)
{
    unsigned int src = SCREEN_BASE + COLS;
    unsigned int dst = SCREEN_BASE;
    unsigned int count = (TRANSCRIPT_ROWS - 1) * COLS;
    unsigned int i;

    for (i = 0; i < count; ++i) {
        POKE(dst + i, PEEK(src + i));
    }
    clear_row(TRANSCRIPT_ROWS - 1);
}

static void transcript_newline(void)
{
    out_x = 0;
    if (out_y < TRANSCRIPT_ROWS - 1) {
        ++out_y;
        gotoxy(out_x, out_y);
        return;
    }
    scroll_transcript();
    clear_row(TRANSCRIPT_ROWS - 1);
    clear_row(STATUS_ROW);
    clear_row(INPUT_LABEL_ROW);
    clear_row(INPUT_ROW);
    clear_row(HELP_ROW);
    out_y = TRANSCRIPT_ROWS - 1;
    gotoxy(out_x, out_y);
}

static void transcript_put(char c)
{
    if (c == '\r') return;
    if (c == '\n') {
        transcript_newline();
        return;
    }
    if (out_x >= COLS) transcript_newline();
    gotoxy(out_x, out_y);
    cputc(c);
    ++out_x;
}

static void transcript_print(const char *s)
{
    while (*s) transcript_put(*s++);
}

static void draw_chrome(void)
{
    clear_row(STATUS_ROW);
    clear_row(INPUT_LABEL_ROW);
    clear_row(INPUT_ROW);
    clear_row(HELP_ROW);
    textcolor(COL_DARK_GRAY);
    print_at(0, INPUT_LABEL_ROW, "message:");
    print_at(0, HELP_ROW, "return send  run/stop clear  f1 wipe");
}

void ui_init(void)
{
    bgcolor(COL_LIGHT_RED);
    bordercolor(COL_LIGHT_RED);
    clrscr();
    cputc(14);
    textcolor(COL_DARK_GRAY);
    out_x = 0;
    out_y = 0;
    draw_chrome();
}

void ui_banner(void)
{
    textcolor(COL_WHITE);
    transcript_print("CLAUDE64");
    transcript_newline();
    textcolor(COL_DARK_GRAY);
    transcript_print("Type below. /local tests without network.");
    transcript_newline();
    transcript_newline();
    draw_chrome();
}

void ui_status(const char *msg)
{
    clear_row(STATUS_ROW);
    textcolor(COL_BLUE);
    print_at(0, STATUS_ROW, msg);
    textcolor(COL_DARK_GRAY);
    gotoxy(0, INPUT_ROW);
}

void ui_prompt(void)
{
    ui_input_begin();
}

void ui_start_response(void)
{
    textcolor(COL_BLACK);
    transcript_print("claude: ");
    textcolor(COL_DARK_GRAY);
}

void ui_user_line(const char *text)
{
    textcolor(COL_BLACK);
    transcript_print("you: ");
    textcolor(COL_DARK_GRAY);
    transcript_print(text);
    transcript_newline();
    draw_chrome();
}

void ui_append_text(const char *chunk)
{
    if (*chunk == '\0') {
        transcript_newline();
        return;
    }
    transcript_print(chunk);
}

void ui_end_response(void)
{
    transcript_newline();
    draw_chrome();
}

void ui_beep(void)
{
}

void ui_clear(void)
{
    clrscr();
    out_x = 0;
    out_y = 0;
    draw_chrome();
}

void ui_input_begin(void)
{
    draw_chrome();
    gotoxy(0, INPUT_ROW);
}

void ui_input_draw(const char *text)
{
    unsigned char i;
    unsigned char len = (unsigned char)strlen(text);
    clear_row(INPUT_ROW);
    textcolor(COL_BLACK);
    gotoxy(0, INPUT_ROW);
    for (i = 0; i < len && i < COLS; ++i) {
        cputc(text[i]);
    }
    textcolor(COL_DARK_GRAY);
    gotoxy((len < COLS) ? len : COLS - 1, INPUT_ROW);
}

void ui_input_clear(void)
{
    clear_row(INPUT_ROW);
    gotoxy(0, INPUT_ROW);
}
