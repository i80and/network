#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>

#include "flatjson.h"
#include "service_exec.h"
#include "validate.h"
#include "util.h"

static int run(char* const[], pid_t* pid, bool);
static int run_and_read(char* const[], char*);

static int run(char* const commands[], pid_t* pid, bool include_stderr) {
    if(commands == NULL) { return 0; }
    if(pid == NULL) { die("Given NULL pid"); }

    int fds[2];
    if(pipe(fds)) {
        die("Failed to open pipe");
    }

    posix_spawn_file_actions_t action;
    posix_spawn_file_actions_init(&action);
    posix_spawn_file_actions_addclose(&action, fds[0]);
    posix_spawn_file_actions_adddup2(&action, fds[1], 1);
    posix_spawn_file_actions_addclose(&action, fds[1]);

    if(include_stderr) {
        posix_spawn_file_actions_adddup2(&action, 1, 2);
    }

    int status = posix_spawn(pid, commands[0],
                             &action,
                             NULL,
                             commands,
                             NULL);
    if(status != 0) {
        die("Failed to spawn process");
    }
    close(fds[1]);

    return fds[0];
}

static int run_and_read(char* const commands[], char* buf) {
    if(buf == NULL) { die("Got null buffer"); }
    pid_t pid;
    int status;

    int fd = run(commands, &pid, false);
    FILE* f = fdopen(fd, "r");
    if(f == NULL) {
        close(fd);
        waitpid(pid, NULL, 0);
        die("Failed to open read pipe");
    }

    ssize_t n_read = fread(buf, 1, EXEC_BUF_LEN-1, f);
    buf[n_read] = '\0';

    // If we overflow the buffer, count on SIGPIPE to terminate the child.
    fclose(f);
    waitpid(pid, &status, 0);
    return status;
}

static void dispatch(struct imsgbuf* ibuf, enum exec_type program, char* msg) {
    int32_t status = EXEC_RESPONSE_OK;
    char iface[IF_NAMESIZE] = {0};
    static char* buf = NULL;
    if(buf == NULL) { buf = malloc(EXEC_BUF_LEN); }
    if(buf == NULL) { die("Failed to allocate buffer"); }
    buf[0] = '\0';

    // Check if we were provided an interface on which to operate
    bool have_iface = (msg != NULL) &&
                      (msg = (char*)flatjson_next(msg, iface, sizeof(iface), NULL)) != NULL &&
                      validate_iface(iface);

    switch(program) {
        case EXEC_IFCONFIG_LIST_INTERFACES: {
            char* const args[] = {"/sbin/ifconfig", NULL};
            if(run_and_read(args, buf) > 0) { status = EXEC_RESPONSE_ERROR; }
            break;
        }
        case EXEC_IFCONFIG_LIST_PSEUDO_INTERFACES: {
            char* const args[] = {"/sbin/ifconfig", "-C", NULL};
            if(run_and_read(args, buf) > 0) { status = EXEC_RESPONSE_ERROR; }
            break;
        }
        case EXEC_IFCONFIG_DOWN: {
            if(!have_iface) {
                status = EXEC_RESPONSE_ERROR;
                break;
            }

            char* const args[] = {"/sbin/ifconfig", iface, "down", NULL};
            if(run_and_read(args, buf) > 0) { status = EXEC_RESPONSE_ERROR; }
            break;
        }
        case EXEC_NETSTART: {
            if(!have_iface) {
                status = EXEC_RESPONSE_ERROR;
                break;
            }

            char* const args[] = {"/bin/sh", "/etc/netstart", iface, NULL};
            if(run_and_read(args, buf) > 0) { status = EXEC_RESPONSE_ERROR; }
            break;
        }
        case EXEC_LOGEVENT: {
            char* const args[] = {"/usr/libexec/loghwevent", msg, NULL};
            if(run_and_read(args, buf) > 0) { status = EXEC_RESPONSE_ERROR; }
            break;
        }
        default:
            warn("Unknown exec mode");
            break;
    }

    imsg_compose(ibuf, status, 0, 0, -1, buf, strlen(buf) + 1);
    imsg_flush(ibuf);
}

void service_exec(struct imsgbuf* ibuf) {
    pledge("stdio proc exec", NULL);

    while(1) {
        int n = imsg_read(ibuf);
        if(n < 0) { die("Error reading ibuf"); }
        if(n == 0) { break; }

        while(1) {
            struct imsg imsg;
            n = imsg_get(ibuf, &imsg);
            if(n <= 0) { break; }

            dispatch(ibuf, imsg.hdr.type, (char* const)imsg.data);
            imsg_free(&imsg);
        }
    }
}

struct imsgbuf service_exec_ibuf;
