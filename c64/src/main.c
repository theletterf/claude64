#include <conio.h>
#include <cbm.h>
#include <stdlib.h>
#include <string.h>
#include "net.h"
#include "ui.h"
#include "proto.h"

#define LINEBUF_SIZE 200
static char linebuf[LINEBUF_SIZE + 1];

static char lower_ascii(char c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static unsigned char command_is(const char *name)
{
    const char *p = linebuf;

    if (*p != '/') return 0;
    ++p;

    while (*name && *p) {
        if (lower_ascii(*p) != *name) return 0;
        ++p;
        ++name;
    }
    return (*name == '\0' && *p == '\0');
}

static void rx_probe(void)
{
    unsigned long idle = 0;
    unsigned int seen = 0;
    char c;

    net_putline(linebuf);
    ui_status("rx probe...");

    while (idle < 250000UL && seen < 96) {
        unsigned char rc = net_getc(&c);
        if (rc == NET_OK) {
            c &= 0x7F;
            cprintf("%02x ", (unsigned char)c);
            ++seen;
            idle = 0;
        } else if (rc == NET_NODATA) {
            ++idle;
        } else {
            cputs("err ");
            break;
        }
    }

    if (seen == 0) cputs("no bytes");
    cputs("\n");
}

static void raw_probe(void)
{
    unsigned long idle = 0;
    unsigned int seen = 0;
    char c;

    net_putline(linebuf);
    ui_status("raw rx...");

    while (idle < 500000UL && seen < 160) {
        unsigned char rc = net_getc(&c);
        if (rc == NET_OK) {
            c &= 0x7F;
            if (c == '\r') {
                cputs("<cr>");
            } else if (c == '\n') {
                cputs("<lf>");
            } else if (c < 0x20 || c == 0x7F) {
                cputc('.');
            } else {
                cbm_k_bsout((unsigned char)c);
            }
            ++seen;
            idle = 0;
        } else if (rc == NET_NODATA) {
            ++idle;
        } else {
            cputs("<err>");
            break;
        }
    }

    if (seen == 0) cputs("no bytes");
    cputs("\n");
}

static void local_echo_turn(void)
{
    ui_start_response();
    ui_append_text("this is a local answer: ");
    ui_append_text(linebuf);
    ui_end_response();
}

/* Read and echo a line of user input. Returns length (0 = empty). */
static int do_input(void)
{
    int  len = 0;
    char c;

    linebuf[0] = '\0';
    ui_input_begin();
    ui_input_draw(linebuf);

    while (1) {
        c = cgetc();

        if (c == '\r' || c == '\n') {
            ui_input_clear();
            break;
        }
        /* DEL / INST-DEL / backspace */
        if (c == 0x14 || c == 0x08 || c == 0x7F) {
            if (len > 0) {
                --len;
                linebuf[len] = '\0';
                ui_input_draw(linebuf);
            }
            continue;
        }
        /* F1 = clear screen */
        if (c == 0x85) {
            ui_clear();
            ui_input_begin();
            ui_input_draw(linebuf);
            len = 0;
            continue;
        }
        /* RUN/STOP clears the current line */
        if (c == 0x03) {
            len = 0;
            linebuf[0] = '\0';
            ui_input_draw(linebuf);
            continue;
        }
        /* cgetc() returns PETSCII. Map letter ranges to ASCII for the
         * proxy, but display the original PETSCII so case looks right. */
        if (len < LINEBUF_SIZE) {
            char ascii;
            if (c >= 0x41 && c <= 0x5A) {
                ascii = c + 0x20;       /* unshifted letter A-Z (PETSCII) → ASCII a-z */
            } else if (c >= 0xC1 && c <= 0xDA) {
                ascii = c - 0x80;       /* shifted letter → ASCII A-Z */
            } else if (c >= 0x20 && c < 0x40) {
                ascii = c;              /* numbers / common punctuation */
            } else if (c >= 0x5B && c <= 0x60) {
                ascii = c;              /* [ \ ] ^ _ ` */
            } else {
                continue;               /* ignore everything else */
            }
            linebuf[len++] = ascii;
            linebuf[len] = '\0';
            ui_input_draw(linebuf);
        }
    }

    linebuf[len] = '\0';
    return len;
}

/* Send the user turn and stream the assistant response. */
static void do_turn(void)
{
    char ch;
    char one[2];
    unsigned char rc;
    unsigned long idle = 0;

    if (net_putline(linebuf) != NET_OK) {
        ui_status("send error");
        return;
    }
    one[1] = '\0';
    ui_start_response();

    while (1) {
        rc = net_getc(&ch);
        if (rc == NET_NODATA) {
            if (++idle > 700000UL) {
                ui_status("receive timeout");
                return;
            }
            if (kbhit() && cgetc() == 0x03) {
                ui_status("cancelled");
                return;
            }
            continue;
        }
        if (rc != NET_OK) {
            ui_status("receive error");
            return;
        }
        idle = 0;

        ch &= 0x7F;
        if (ch == '\n' || ch == '\r') {
            ui_append_text("");
            continue;
        }
        if (ch >= 0x20 && ch < 0x7F) {
            one[0] = ch;
            ui_append_text(one);
        }
    }
}

int main(void)
{
    ui_init();
    ui_banner();

    if (net_init() != NET_OK) {
        textcolor(2);   /* red */
        cputs("network init failed.\n");
        cprintf("install err: %d\n", (int)net_install_err);
        cprintf("open    err: %d\n", (int)net_open_err);
        cputs("(need c64-swlink.ser on drive 8)\n");
        cgetc();
        return EXIT_FAILURE;
    }

    while (1) {
        if (do_input() == 0) continue;
        ui_user_line(linebuf);
        if (command_is("rx")) {
            rx_probe();
            continue;
        }
        if (command_is("raw")) {
            raw_probe();
            continue;
        }
        if (command_is("local")) {
            while (1) {
                if (do_input() == 0) continue;
                ui_user_line(linebuf);
                if (command_is("exit")) {
                    ui_status("leaving local mode");
                    break;
                }
                local_echo_turn();
            }
            continue;
        }
        do_turn();
    }

    net_close();
    return EXIT_SUCCESS;
}
