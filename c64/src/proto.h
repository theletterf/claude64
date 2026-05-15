#ifndef PROTO_H
#define PROTO_H

typedef enum {
    PROTO_START,    /* S <text> */
    PROTO_TEXT,     /* T <text> */
    PROTO_END,      /* E <reason> */
    PROTO_STATUS,   /* R <text> */
    PROTO_UNKNOWN
} ProtoType;

/* Parse a null-terminated line in-place.
 * Sets *content to the payload after the 2-char prefix.
 * Returns PROTO_UNKNOWN for unrecognized lines.
 */
ProtoType proto_parse(char *line, char **content);

#endif
