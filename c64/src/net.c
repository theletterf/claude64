#include <serial.h>
#include <peekpoke.h>
#include <stddef.h>
#include "net.h"

unsigned char net_install_err = 0;
unsigned char net_open_err    = 0;

static const struct ser_params params = {
    SER_BAUD_2400,
    SER_BITS_8,
    SER_STOP_1,
    SER_PAR_NONE,
    SER_HS_HW       /* Swiftlink driver requires hardware handshake */
};

/* Swiftlink ACIA registers at $DE00 — used as direct-poll fallback. */
#define ACIA_STATUS 0xDE01u
#define ACIA_DATA   0xDE00u
#define ACIA_RDRF   0x08u   /* Receive Data Register Full */

unsigned char net_init(void)
{
    net_install_err = ser_load_driver("c64-swlink.ser");
    if (net_install_err != SER_ERR_OK) return NET_ERR;

    net_open_err = ser_open(&params);
    if (net_open_err != SER_ERR_OK) return NET_ERR;

    return NET_OK;
}

void net_close(void)
{
    ser_close();
}

unsigned char net_putc(char c)
{
    unsigned char err;
    unsigned int retries = 0;
    do {
        err = ser_put((unsigned char)c);
        if (err == SER_ERR_OVERFLOW && ++retries == 0) return NET_ERR;
    } while (err == SER_ERR_OVERFLOW);
    return (err == SER_ERR_OK) ? NET_OK : NET_ERR;
}

unsigned char net_puts(const char *s)
{
    while (*s) {
        if (net_putc(*s++) != NET_OK) return NET_ERR;
    }
    return NET_OK;
}

unsigned char net_putline(const char *s)
{
    if (net_puts(s) != NET_OK) return NET_ERR;
    return net_putc('\n');
}

unsigned char net_getc(char *c)
{
    unsigned char ch;
    /* Direct ACIA poll: this is the path verified by rxtest under VICE/IP232. */
    if (PEEK(ACIA_STATUS) & ACIA_RDRF) {
        *c = (char)(PEEK(ACIA_DATA) & 0x7F);
        return NET_OK;
    }
    /* NMI ring buffer fallback for environments where the cc65 driver receives. */
    if (ser_get(&ch) == SER_ERR_OK) {
        *c = (char)(ch & 0x7F);
        return NET_OK;
    }
    return NET_NODATA;
}

int net_getline(char *buf, int maxlen)
{
    int len = 0;
    char c;
    unsigned char rc;

    while (len < maxlen - 1) {
        rc = net_getc(&c);
        if (rc == NET_NODATA) continue;
        if (rc != NET_OK)    break;
        if (c == '\n' || c == '\r') {
            if (len > 0) break;
            continue;
        }
        buf[len++] = c;
    }
    buf[len] = '\0';
    return len;
}
