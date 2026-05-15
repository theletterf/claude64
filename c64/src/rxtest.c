#include <conio.h>
#include <cbm.h>
#include <serial.h>
#include <peekpoke.h>

/* Hex printer — cprintf %x not guaranteed on all cc65 targets */
static void phex(unsigned char v)
{
    static const char h[] = "0123456789abcdef";
    cputc(h[v >> 4]);
    cputc(h[v & 15]);
}

int main(void)
{
    struct ser_params p;
    unsigned char ch, st, last_st = 0xFFu;

    clrscr();
    cputs("swiftlink rx test\n");
    cputs("connect: 127.0.0.1:25232\n\n");

    if (ser_load_driver("c64-swlink.ser")) { cputs("ERR:load\n"); cgetc(); return 1; }
    cputs("driver ok\n");

    p.baudrate  = SER_BAUD_2400;
    p.databits  = SER_BITS_8;
    p.stopbits  = SER_STOP_1;
    p.parity    = SER_PAR_NONE;
    p.handshake = SER_HS_HW;

    if (ser_open(&p)) { cputs("ERR:open\n"); cgetc(); return 1; }
    cputs("port open\n");

    /* Send a line so the proxy has something to echo back */
    {
        const char *ping = "ping\n";
        const char *p = ping;
        while (*p) { ser_put((unsigned char)*p++); }
    }
    cputs("sent ping - waiting...\n\n");

    while (1) {
        st = PEEK(0xDE01u);          /* ACIA status register */

        /* Print ACIA status whenever it changes */
        if (st != last_st) {
            cputs("ST:"); phex(st); cputs("  ");
            last_st = st;
        }

        /* Path 1: NMI ring buffer (interrupt-driven) */
        if (ser_get(&ch) == SER_ERR_OK) {
            cputs("NMI["); phex(ch); cputs("] ");
        }

        /* Path 2: direct ACIA poll (works even if NMI never fires) */
        if (PEEK(0xDE01u) & 0x08u) {    /* RDRF bit */
            ch = PEEK(0xDE00u);
            cputs("POL["); phex(ch); cputs("] ");
        }

        if (kbhit() && cgetc() == 0x03u) break;  /* RUN/STOP to exit */
    }

    ser_close();
    return 0;
}
