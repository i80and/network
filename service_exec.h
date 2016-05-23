#pragma once

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/uio.h>
#include <imsg.h>

#define EXEC_BUF_LEN (1024 * 1024)

enum exec_type {
    EXEC_IFCONFIG_LIST_INTERFACES,
    EXEC_IFCONFIG_LIST_PSEUDO_INTERFACES,
    EXEC_IFCONFIG_DOWN,
    EXEC_NETSTART,

    EXEC_RESPONSE_OK,
    EXEC_RESPONSE_ERROR
};

void service_exec(struct imsgbuf*);

extern struct imsgbuf service_exec_ibuf;
