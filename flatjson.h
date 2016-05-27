#pragma once

#include <stdio.h>
#include <stdbool.h>

enum flatjson {
    FLATJSON_OK=0,
    FLATJSON_ERROR_OVERFLOW,
    FLATJSON_ERROR_INVALID,

    FLATJSON_DONE
};

const char* flatjson_next(const char*, char*, size_t, enum flatjson*);
int flatjson_escape(const char*, char*, size_t);

void flatjson_send_singleton(FILE*, const char*);
void flatjson_start_send(FILE*);
void flatjson_send(FILE*, const char*, bool*);
void flatjson_finish_send(FILE*);
