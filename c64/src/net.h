#ifndef NET_H
#define NET_H

#define NET_OK     0
#define NET_ERR    1
#define NET_NODATA 2

/* Set by net_init() to the raw SER_ERR_* code so callers can print diagnostics. */
extern unsigned char net_install_err;
extern unsigned char net_open_err;

/* Prepare the device-8 filesystem bridge. Returns NET_OK or NET_ERR. */
unsigned char net_init(void);

void net_close(void);

/* Send a single byte. Retries on OVERFLOW. */
unsigned char net_putc(char c);

/* Send a null-terminated string. */
unsigned char net_puts(const char *s);

/* Send string + '\n'. */
unsigned char net_putline(const char *s);

/* Non-blocking receive. Returns NET_NODATA when nothing available. */
unsigned char net_getc(char *c);

/* Blocking read until '\n'. Strips '\r'. Returns line length. */
int net_getline(char *buf, int maxlen);

#endif
