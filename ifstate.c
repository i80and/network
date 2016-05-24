#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/route.h>
#include <ifaddrs.h>

#include "ifstate.h"
#include "util.h"

struct cloners {
    char* buf;
    size_t n;
};

static int list_cloners(struct cloners*);
static bool filter_cloners(struct cloners*, const char*);

static int list_cloners(struct cloners* cloners) {
    struct if_clonereq ifcr;
    memset(&ifcr, 0, sizeof(ifcr));

    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if(s < 0) { return 1; }

    // First get a count of elements
    if(ioctl(s, SIOCIFGCLONERS, &ifcr) == -1) {
        return 1;
    }

    char* buf = calloc(ifcr.ifcr_total, IF_NAMESIZE);
    if(buf == NULL) { return 1; }

    ifcr.ifcr_count = ifcr.ifcr_total;
    ifcr.ifcr_buffer = buf;

    // Now actually populate the element buffer
    if(ioctl(s, SIOCIFGCLONERS, &ifcr) == -1) {
        free(buf);
        return 1;
    }

    // Enshrinken the count it changed between our two ioctl calls.
    ifcr.ifcr_count = min(ifcr.ifcr_count, ifcr.ifcr_total);

    cloners->buf = buf;
    cloners->n = ifcr.ifcr_count;

    return 0;
}

static bool filter_cloners(struct cloners* cloners, const char* iface) {
    char prefix[IF_NAMESIZE] = {0};
    {
        int i = 0;
        while(iface[i] != '\0' && isalpha(iface[i])) {
            prefix[i] = iface[i];
            i += 1;
        }
    }

    char* cursor;
    size_t i;
    for(cursor = cloners->buf, i = 0; i < cloners->n; i++, cursor += IF_NAMESIZE) {
        if(strncmp(cursor, prefix, IF_NAMESIZE) == 0) { return true; }
    }

    return false;
}

int fetch_ifaces(void) {
    int status = 0;
    struct cloners cloners = {NULL, 0};
    if(list_cloners(&cloners) != 0) {
        return 1;
    }

    struct ifaddrs* ifap = NULL;
    char* oname = NULL;
    if(getifaddrs(&ifap) != 0) {
        status = 1;
        goto CLEANUP;
    }

    for(struct ifaddrs* ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if(oname && !strcmp(oname, ifa->ifa_name)) { continue; }
        oname = ifa->ifa_name;

        if(filter_cloners(&cloners, oname)) { continue; }
    }

CLEANUP:
    if(ifap != NULL) { freeifaddrs(ifap); }
    if(cloners.buf != NULL) { free(cloners.buf); }
    return status;
}

int monitor_ifaces(void) {
    int rt_fd = socket(PF_ROUTE, SOCK_RAW, 0);
    unsigned int rtfilter = ROUTE_FILTER(RTM_IFINFO);
    setsockopt(rt_fd, PF_ROUTE, ROUTE_MSGFILTER, &rtfilter, sizeof(rtfilter));

    rtfilter = RTABLE_ANY;
    setsockopt(rt_fd, PF_ROUTE, ROUTE_TABLEFILTER, &rtfilter, sizeof(rtfilter));

    return rt_fd;
}
