#include "sensorfake_name.h"

#include <string.h>

void sensorfake_name_parse(const char* text, size_t text_len, char* name) {
    size_t length = 0;

    for(size_t i = 0; i < text_len; i++) {
        const char c = text[i];
        if(c == '\n' || c == '\r') {
            /* The name is the first line with something on it */
            if(length > 0) break;
        } else if(c == ' ') {
            /* No leading spaces */
            if(length > 0 && length < SENSORFAKE_NAME_MAX_LENGTH) name[length++] = c;
        } else if(c > ' ' && c <= '~') {
            if(length < SENSORFAKE_NAME_MAX_LENGTH) name[length++] = c;
        }
    }

    /* No trailing spaces */
    while(length > 0 && name[length - 1] == ' ')
        length--;

    if(length == 0) {
        length = strlen(SENSORFAKE_NAME_DEFAULT);
        memcpy(name, SENSORFAKE_NAME_DEFAULT, length);
    }
    name[length] = '\0';
}
