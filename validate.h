#pragma once

#include <stdbool.h>

#define IFACE_LEN 20
#define FLAGS_LEN 100
#define IFCONFIG_KEY_LEN 20
#define IFCONFIG_VALUE_LEN FLAGS_LEN

bool validate_iface(const char*);
bool validate_stanza(const char*);
bool parse_ifconfig_header(const char*, char[IFACE_LEN], char[FLAGS_LEN], int*);
bool parse_ifconfig_kv(const char*, char[IFCONFIG_KEY_LEN], char [IFCONFIG_VALUE_LEN]);
