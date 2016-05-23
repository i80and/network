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

int min(int a, int b) {
    return (a < b)? a : b;
}
