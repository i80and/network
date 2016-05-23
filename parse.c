#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "parse.h"

char* chomp(char* text) {
    while(isspace(text[0])) { text += 1; }
    char* end = text + (strlen(text) - 1);
    while(isspace(end[0])) {
        end[0] = '\0';
        end -= 1;
    }

    return text;
}

char* escape(const char* text, char* buf, size_t buf_len) {
    size_t i = 0;

    while(text[0] != '\0') {
        bool in_escape = false;
        char ch = text[0];

        switch(ch) {
            case ' ':
                in_escape = true;
                ch = ' ';
                break;
            case '\n':
                in_escape = true;
                ch = 'n';
                break;
            default:
                break;
        }

        if(i+2 >= buf_len) { return NULL; }
        if(in_escape) { buf[i++] = '\\'; }
        buf[i++] = ch;
        buf[i] = '\0';
        text += 1;
        
    }

    return buf;
}

const char* unescape(const char* text, char* buf, size_t buf_len) {
    if(text == NULL) {
        if(buf_len > 0) { buf[0] = '\0'; }
        return NULL;
    }

    bool in_escape = false;
    size_t i = 0;

    while(text[0] != '\0') {
        char ch = text[0];
        if(in_escape) {
            in_escape = false;
            if(ch == 'n') { ch = '\n'; }
            goto PUSH;
        } else {
            switch(ch) {
                case '\\':
                    in_escape = true;
                    break;
                case ' ':
                    return text+1;
                    break;
                default:
                    goto PUSH;
            }
        }

        text += 1;
        continue;

PUSH:   if(i+1 >= buf_len) { return NULL; }
        buf[i] = ch;
        buf[i+1] = '\0';
        i += 1;
        text += 1;
    }

    return NULL;
}
