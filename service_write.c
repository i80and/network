#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "service_write.h"
#include "flatjson.h"
#include "util.h"
#include "validate.h"

static enum write_type configure(const char* interface, char const* args) {
    char path[50];
    char stanza[200];
    snprintf(path, sizeof(path), "/etc/hostname.%s", interface);

    FILE* f = fopen(path, "w");
    if(f == NULL) { return WRITE_RESPONSE_ERROR; }

    while((args = flatjson_next(args, stanza, sizeof(stanza), NULL)) != NULL) {
        if(!validate_stanza(stanza)) {
            warn("Illegal stanza");
            continue;
        }

        if(fprintf(f, "%s\n", stanza) < 0) {
            warn("Writing error");
        }
    }

    fclose(f);
    return WRITE_RESPONSE_OK;
}

static enum write_type autoconfigure(const char* interface) {
    char path[50];
    snprintf(path, sizeof(path), "/etc/hostname.%s", interface);

    FILE* f = fopen(path, "wx");
    if(f == NULL) { return WRITE_RESPONSE_OK; }
    if(fprintf(f, "dhcp\n") < 0) { warn("Writing error"); }
    fclose(f);
    return WRITE_RESPONSE_OK;
}

static void dispatch(struct imsgbuf* ibuf, enum write_type type, const char* msg) {
    char interface[IF_NAMESIZE] = {0};
    flatjson_next(msg, interface, sizeof(interface), NULL);
    if(!validate_iface(interface)) {
        imsg_compose(ibuf, WRITE_RESPONSE_ERROR, 0, 0, -1, NULL, 0);
        imsg_flush(ibuf);
        return;
    }

    enum write_type result = 0;
    switch(type) {
        case WRITE_WRITE:
            result = configure(interface, msg);
            break;
        case WRITE_AUTOCONFIGURE:
            result = autoconfigure(interface);
            break;
        default:
            warn("Unknown write mode");
            return;
    }

    imsg_compose(ibuf, result, 0, 0, -1, NULL, 0);
    imsg_flush(ibuf);
}

void service_write(struct imsgbuf* ibuf) {
    pledge("stdio wpath cpath", NULL);

    while(1) {
        int n = imsg_read(ibuf);
        if(n < 0) { die("Error reading ibuf"); }
        if(n == 0) { return; }

        while(1) {
            struct imsg imsg;
            n = imsg_get(ibuf, &imsg);
            if(n <= 0) { break; }

            dispatch(ibuf, imsg.hdr.type, (const char*)imsg.data);
            imsg_free(&imsg);
        }
    }
}

struct imsgbuf service_write_ibuf;
