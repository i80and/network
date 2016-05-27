#pragma once

void cleanup(void);
void die(const char* msg) __attribute__ ((noreturn));
void warn(const char* msg);

char* chomp(char*);
int min(int, int);
