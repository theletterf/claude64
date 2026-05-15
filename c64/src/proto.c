#include "proto.h"

ProtoType proto_parse(char *line, char **content)
{
    if (line[1] == ' ') {
        switch (line[0]) {
            case 'S': *content = line + 2; return PROTO_START;
            case 'T': *content = line + 2; return PROTO_TEXT;
            case 'E': *content = line + 2; return PROTO_END;
            case 'R': *content = line + 2; return PROTO_STATUS;
        }
    }
    *content = line;
    return PROTO_UNKNOWN;
}
