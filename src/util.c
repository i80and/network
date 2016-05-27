#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include "util.h"

void cleanup(void) {}

void die(const char* msg) {
    perror(msg);
    cleanup();
    exit(1);
}

void warn(const char* msg) {
    fprintf(stderr, "WARN: %s\n", msg);
}

char* chomp(char* text) {
    while(isspace(text[0])) { text += 1; }
    char* end = text + (strlen(text) - 1);
    while(isspace(end[0])) {
        end[0] = '\0';
        end -= 1;
    }

    return text;
}

int min(int a, int b) {
    return (a < b)? a : b;
}
