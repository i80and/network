#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include "validate.h"
#include "util.h"

static bool iface_pat_init;
static regex_t iface_pat;

static bool stanza_pat_init;
static regex_t stanza_pat;

static bool ifconfig_header_pat_init;
static regex_t ifconfig_header_pat;

static bool ifconfig_kv_pat_init;
static regex_t ifconfig_kv_pat;

bool validate_iface(const char* text) {
    if(!iface_pat_init) {
        int reti = regcomp(&iface_pat, "^[a-z]+[0-9]*$", REG_EXTENDED);
        if(reti) { die("Failed to compile iface regex"); }
        iface_pat_init = true;
    }

    return regexec(&iface_pat, text, 0, NULL, 0) == 0;
}

bool validate_stanza(const char* text) {
    if(!stanza_pat_init) {
        int reti = regcomp(&stanza_pat, "^dhcp|rtsol|(nwid .*)|(wpakey .*)|(dest [0-9:\\.])|(inet [0-9\\.]+ [0-9\\.]+ [0-9\\.]+)|(inet6 [a-f0-9:]+ [a-f0-9:]+ [a-f0-9:]+)$", REG_EXTENDED);
        if(reti) { die("Failed to compile stanza regex"); }
        stanza_pat_init = true;
    }

    return regexec(&stanza_pat, text, 0, NULL, 0) == 0;
}

bool parse_ifconfig_header(const char* text,
                           char iface[IFACE_LEN],
                           char flags[FLAGS_LEN],
                           int* mtu) {
    if(!ifconfig_header_pat_init) {
        int reti = regcomp(&ifconfig_header_pat, "^([a-z]+[0-9]*): flags=[0-9]+<([A-Z,]*)> mtu ([0-9]+)$", REG_EXTENDED);
        if(reti) { die("Failed to compile ifconfig_header regex"); }
        ifconfig_header_pat_init = true;
    }

    regmatch_t matches[4];
    int result = regexec(&ifconfig_header_pat,
                 text,
                 sizeof(matches) / sizeof(regmatch_t),
                 matches,
                 0);
    if(result != 0 || matches[1].rm_so < 0 || matches[2].rm_so < 0 || matches[3].rm_so < 0) {
        return false;
    }

    if(iface == NULL || flags == NULL || mtu == NULL) { return true; }

    const size_t iface_len = min(IFACE_LEN, matches[1].rm_eo - matches[1].rm_so);
    strlcpy(iface, text + matches[1].rm_so, iface_len);

    const size_t flags_len = min(FLAGS_LEN, matches[2].rm_eo - matches[2].rm_so);
    strlcpy(flags, text + matches[2].rm_so, flags_len);

    *mtu = atoi(text + matches[3].rm_so);

    return true;
}

bool parse_ifconfig_kv(const char* text,
                       char key[IFCONFIG_KEY_LEN],
                       char value[IFCONFIG_VALUE_LEN]) {
    if(!ifconfig_kv_pat_init) {
        int reti = regcomp(&ifconfig_kv_pat, "^\t([a-z]+):? ([^\n]+)$", REG_EXTENDED);
        if(reti) { die("Failed to compile ifconfig_kv regex"); }
        ifconfig_kv_pat_init = true;
    }

    regmatch_t matches[3];
    int result = regexec(&ifconfig_kv_pat,
                 text,
                 sizeof(matches) / sizeof(regmatch_t),
                 matches,
                 0);
    if(result != 0 || matches[1].rm_so < 0 || matches[2].rm_so < 0) {
        return false;
    }

    if(key == NULL || value == NULL) { return true; }

    const size_t key_len = min(IFCONFIG_KEY_LEN, matches[1].rm_eo - matches[1].rm_so + 1);
    strlcpy(key, text + matches[1].rm_so, key_len);

    const size_t value_len = min(IFCONFIG_VALUE_LEN, matches[2].rm_eo - matches[2].rm_so + 1);
    strlcpy(value, text + matches[2].rm_so, value_len);

    return true;
}