#pragma once

#include <stddef.h>

char* chomp(char*);
char* escape(const char*, char*, size_t);
const char* unescape(const char*, char*, size_t);
