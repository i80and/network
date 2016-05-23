#pragma once

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/uio.h>
#include <imsg.h>

#define WRITE_BUF_LEN (1024 * 1024)

enum write_type {
    WRITE_WRITE,
    WRITE_AUTOCONFIGURE,

    WRITE_RESPONSE_OK,
    WRITE_RESPONSE_ERROR
};

void service_write(struct imsgbuf*);

extern struct imsgbuf service_write_ibuf;
