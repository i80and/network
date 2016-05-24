#pragma once

#include <stdbool.h>

#define IFACE_LEN 20
#define FLAGS_LEN 100
#define IFCONFIG_KEY_LEN 20
#define IFCONFIG_VALUE_LEN FLAGS_LEN
#define PSEUDO_CLASSES_LEN 200

bool validate_iface(const char*);
bool validate_stanza(const char*);
bool parse_ifconfig_header(const char*, char[IFACE_LEN], char[FLAGS_LEN], int*);
bool parse_ifconfig_kv(const char*, char[IFCONFIG_KEY_LEN], char [IFCONFIG_VALUE_LEN]);

// Check whether a given interface name is within a list of space-separated
// interface prefixes. The list of prefixes should not be larger than
// PSEUDO_CLASSES_LEN bytes.
bool iface_is_pseudo(const char*, const char*);
