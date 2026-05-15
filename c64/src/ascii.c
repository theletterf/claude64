/*
 * ASCII <-> PETSCII conversion for lowercase-charset mode (CHR$(14)).
 * Adapted from RHML by Scott Hutter — https://github.com/xlar54/rhml
 */

#include "ascii.h"

/* Convert a single ASCII character to PETSCII for screen output.
 * In lowercase mode: uppercase lives at 0xC1-0xDA, lowercase at 0x41-0x5A. */
char ascii_to_petscii(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)((unsigned char)c + 128);
    if (c >= 'a' && c <= 'z') return (char)((unsigned char)c - 32);
    if (c == '\n')             return (char)0x0D;
    return c;
}
