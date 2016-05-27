#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "flatjson.h"

#define PUSH(x) do { to_push = (x); goto push; } while(0)

enum state {
    STATE_NONE,
    STATE_STRING,
    STATE_ESCAPE,
};

const char* flatjson_next(const char* text,
                          char* buf,
                          size_t buf_len,
                          enum flatjson* error) {
    if(error != NULL) { *error = FLATJSON_OK; }
    buf[0] = '\0';

    enum state state = STATE_NONE;
    size_t bufi = 0;
    for(char ch; (ch = text[0]) != '\0'; text += 1) {
        char to_push = 0;

        switch(state) {
            case STATE_NONE: {
                if(ch == '"') {
                    state = STATE_STRING;
                    continue;
                }
                break;
            }
            case STATE_STRING: {
                if(ch == '"') {
                    text += 1;
                    return text;
                }

                if(ch == '\\') {
                    state = STATE_ESCAPE;
                    continue;
                }

                PUSH(ch);
                break;
            }
            case STATE_ESCAPE: {
                state = STATE_STRING;

                if(ch == 'n') { PUSH('\n'); }
                else if(ch == '"') { PUSH('"'); }
                else if(ch == '\\') { PUSH('\\'); }
                else if(ch == '/') { PUSH('/'); }
                else if(ch == 'b') { PUSH('\b'); }
                else if(ch == 'r') { PUSH('\r'); }
                else {
                    if(error != NULL) { *error = FLATJSON_ERROR_INVALID; }
                    return NULL;
                }

                break;
            }
        }
        continue;

push:   if(bufi+1 >= buf_len) {
            if(error != NULL) { *error = FLATJSON_ERROR_OVERFLOW; }
            return NULL;
        }
        buf[bufi++] = to_push;
        buf[bufi] = '\0';
    }

    return NULL;
}

int flatjson_escape(const char* text, char* buf, size_t buf_len) {
    size_t i = 0;
    char ch;
    while((ch = text[0]) != '\0') {
        bool in_escape = false;

        switch(ch) {
            case '"':
                in_escape = true;
                ch = '"';
                break;
            case '\n':
                in_escape = true;
                ch = 'n';
                break;
            default:
                break;
        }

        if(i+2 >= buf_len) { return 1; }
        if(in_escape) { buf[i++] = '\\'; }
        buf[i++] = ch;
        buf[i] = '\0';
        text += 1;
    }

    return 0;
}

void flatjson_send_singleton(FILE* f, const char* text) {
    char escaped[2048];
    flatjson_escape(text, escaped, sizeof(escaped));

    fprintf(f, "[\"%s\"]", escaped);
}

void flatjson_start_send(FILE* f) {
    fputs("[", f);
}

void flatjson_send(FILE* f, const char* text, bool* first) {
    char escaped[2048];
    flatjson_escape(text, escaped, sizeof(escaped));

    if(!*first) {
        fprintf(f, ", \"%s\"", escaped);
        return;
    }

    fprintf(f, "\"%s\"", text);
    *first = false;
}

void flatjson_finish_send(FILE* f) {
    fputs("]", f);
}
