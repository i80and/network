#include <stdlib.h>
#include <stdio.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/event.h>
#include <sys/stat.h>
#include <net/route.h>
#include <signal.h>
#include <fcntl.h>
#include <imsg.h>

#include "flatjson.h"
#include "util.h"
#include "validate.h"
#include "service_exec.h"
#include "service_write.h"

void handle_list(FILE*, bool);

void sighandler(int signo) {
    write(2, "Received signal\n", 16);
    cleanup();
    _exit(0);
}

static int monitor_ifaces(void) {
    int rt_fd = socket(PF_ROUTE, SOCK_RAW, 0);
    unsigned int rtfilter = ROUTE_FILTER(RTM_IFINFO);
    setsockopt(rt_fd, PF_ROUTE, ROUTE_MSGFILTER, &rtfilter, sizeof(rtfilter));

    rtfilter = RTABLE_ANY;
    setsockopt(rt_fd, PF_ROUTE, ROUTE_TABLEFILTER, &rtfilter, sizeof(rtfilter));

    return rt_fd;
}

void spawn_service(struct imsgbuf* ibuf, void(*f)(struct imsgbuf*)) {
    struct imsgbuf child_ibuf;
    int fds[2];
    if(socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, fds) == -1) {
        die("Failed to set up socketpair");
    }

    switch(fork()) {
      case -1: die("Failed to fork service"); break;
      case 0:
        // Child
        close(fds[0]);
        imsg_init(&child_ibuf, fds[1]);
        f(&child_ibuf);
        exit(0);
      default:
        // Parent
        close(fds[1]);
        imsg_init(ibuf, fds[0]);
        break;
    }
}

void service_send(struct imsgbuf* ibuf, u_int32_t type, const char* msg) {
    const size_t msg_len = (msg == NULL)? 0 : (strlen(msg) + 1);
    imsg_compose(ibuf, type, 0, 0, -1, msg, msg_len);
    imsg_flush(ibuf);
}

int32_t service_pop(struct imsgbuf* ibuf, char* buf, size_t buf_len) {
    if(buf != NULL) { buf[0] = '\0'; }

    int n = imsg_read(ibuf);
    if(n < 0) { die("Error reading"); }
    if(n == 0) { return -1; }

    struct imsg imsg;
    n = imsg_get(ibuf, &imsg);
    if(n <= 0) { die("Got no message"); }

    if(buf != NULL && imsg.data != NULL) {
        // We should only ever pass strings, but just to be safe, always
        // make sure that the buffer we return is nul-terminated.
        const size_t data_len = imsg.hdr.len - IMSG_HEADER_SIZE;
        strlcpy(buf, imsg.data, min(buf_len, data_len));
    }

    u_int32_t type = imsg.hdr.type;
    imsg_free(&imsg);
    return type;
}

int list_pseudo_classes(char* buf, size_t buf_len) {
    buf[0] = '\0';
    service_send(&service_exec_ibuf, EXEC_IFCONFIG_LIST_PSEUDO_INTERFACES, NULL);
    int32_t result = service_pop(&service_exec_ibuf, buf, buf_len);
    if(result != EXEC_RESPONSE_OK) {
        return 1;
    }

    return 0;
}

void drop_permissions(const char* username) {
    struct passwd* passwd = getpwnam(username);
    if(passwd == NULL) { die("Failed to get user information"); }
    struct group* group = getgrnam(username);
    if(group == NULL) { die("Failed to get group information"); }

    if(chroot("/var/empty") != 0) { die("Failed to chroot"); }
    if(chdir("/") != 0) { die("Failed to chdir"); }

    if(setgroups(0, NULL) == -1) { die("Failed to set supplementary groups"); }
    if(setgid(group->gr_gid) == -1) { die("Failed to set group"); }
    if(setuid(passwd->pw_uid) == -1) { die("Failed to set user"); }

    pledge("stdio unix", NULL);
}

void handle_list(FILE* sock, bool details) {
    char pseudo_classes[PSEUDO_CLASSES_LEN];
    if(list_pseudo_classes(pseudo_classes, sizeof(pseudo_classes))) {
        die("Failed to enumerate pseudo classes");
    }

    char* output_text = malloc(1024 * 1024);
    if(output_text == NULL) { die("Allocating output buffer failed"); }

    service_send(&service_exec_ibuf, EXEC_IFCONFIG_LIST_INTERFACES, NULL);
    int32_t result = service_pop(&service_exec_ibuf, output_text, 1024 * 1024);
    if(result != EXEC_RESPONSE_OK) {
        flatjson_send_singleton(sock, "error");
        fputs("\n", sock);
        free(output_text);
        return;
    }

    bool first_message = true;
    flatjson_start_send(sock);
    flatjson_send(sock, "ok", &first_message);

    char* cursor;
    char iface[IF_NAMESIZE];
    bool skipping = false;
    while((cursor = strsep(&output_text, "\n")) != NULL) {
        char rendered[IF_NAMESIZE + 10];
        char key[IFCONFIG_KEY_LEN];
        char flags[FLAGS_LEN];
        int mtu;
        if(parse_ifconfig_header(cursor, iface, flags, &mtu)) {
            if(iface_is_pseudo(iface, pseudo_classes)) {
                skipping = true;
                continue;
            } else {
                if(!details) {
                    flatjson_send(sock, iface, &first_message);
                    continue;
                }
                skipping = false;
            }

            snprintf(rendered, sizeof(rendered), "%s.flags", iface);
            flatjson_send(sock, rendered, &first_message);
            flatjson_send(sock, flags, &first_message);
            snprintf(rendered, sizeof(rendered), "%s.mtu", iface);
            flatjson_send(sock, rendered, &first_message);
            snprintf(rendered, sizeof(rendered), "%d", mtu);
            flatjson_send(sock, rendered, &first_message);
            continue;
        }

        if(!skipping && details && parse_ifconfig_kv(cursor, key, flags)) {
            snprintf(rendered, sizeof(rendered), "%s.%s", iface, key);
            flatjson_send(sock, rendered, &first_message);
            flatjson_send(sock, flags, &first_message);
        }
    }

    flatjson_finish_send(sock);
    fprintf(sock, "\n");
    free(output_text);
}

void handle_configure(FILE* sock, const char* args) {
    service_send(&service_write_ibuf, WRITE_WRITE, args);
    int32_t result = service_pop(&service_write_ibuf, NULL, 0);
    if(result == WRITE_RESPONSE_OK) {
        flatjson_send_singleton(sock, "ok");
    } else {
        flatjson_send_singleton(sock, "error");
    }

    fputs("\n", sock);
}

void handle_connect(FILE* sock, const char* args) {
    // Attempt to autoconfigure, if there is no current configuration
    service_send(&service_write_ibuf, WRITE_AUTOCONFIGURE, args);
    service_pop(&service_write_ibuf, NULL, 0);
    
    service_send(&service_exec_ibuf, EXEC_NETSTART, args);
    int32_t result = service_pop(&service_exec_ibuf, NULL, 0);
    if(result == EXEC_RESPONSE_OK) {
        flatjson_send_singleton(sock, "ok");
    } else {
        flatjson_send_singleton(sock, "error");
    }

    fputs("\n", sock);
}

void handle_disconnect(FILE* sock, const char* args) {
    char iface[IF_NAMESIZE];
    strlcpy(iface, args, sizeof(iface));

    service_send(&service_exec_ibuf, EXEC_IFCONFIG_DOWN, args);
    int32_t result = service_pop(&service_exec_ibuf, iface, strlen(iface));
    if(result == EXEC_RESPONSE_OK) {
        flatjson_send_singleton(sock, "ok");
    } else {
        flatjson_send_singleton(sock, "error");
    }

    fputs("\n", sock);
}

void handle(int fd) {
    char buf[200];
    ssize_t n_read;
    while((n_read = read(fd, buf, sizeof(buf))) > 0) {
        buf[n_read-1] = '\0';
        
        char* bufp = buf;
        char* cursor;
        while((cursor = strsep(&bufp, "\n")) != NULL) {
            int fdd = dup(fd);
            FILE* f = fdopen(fdd, "a");

            char command[20];
            char const* const remainder = flatjson_next(chomp(buf), command, sizeof(command), NULL);
            if(strcmp(command, "list") == 0) {
                handle_list(f, true);
            } else if(strcmp(command, "configure") == 0) {
                handle_configure(f, remainder);
            } else if(strcmp(command, "connect") == 0) {
                handle_connect(f, remainder);
            } else if(strcmp(command, "disconnect") == 0) {
                handle_disconnect(f, remainder);
            } else {
                warn("Unknown command");
            }

            fclose(f);
        }
    }
}

void handle_iface_change(int monitor) {
    char buf[2048];
    read(monitor, buf, sizeof(buf));
    
    struct rt_msghdr* rtm = (struct rt_msghdr*)&buf;
    struct if_msghdr ifm;
    memcpy(&ifm, rtm, sizeof(ifm));

    char iface[IF_NAMESIZE];
    if(if_indextoname(ifm.ifm_index, iface) == NULL) {
        warn("Failed to look up iface by index");
        return;
    }
    
    bool up = LINK_STATE_IS_UP(ifm.ifm_data.ifi_link_state);
    const char* term = up? "up" : "down";

    snprintf(buf, sizeof(buf), "%s %s", term, iface);
    service_send(&service_exec_ibuf, EXEC_LOGEVENT, buf);
    int32_t result = service_pop(&service_exec_ibuf, NULL, 0);
    if(result != EXEC_RESPONSE_OK) {
        warn("Failed to log iface change");
        return;
    }
}

void serve(const char* sockpath, const char* username) {
    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(sockfd < 0) { die("Failed to create socket"); }

    struct sockaddr_storage client_addr;
    socklen_t client_socklen = sizeof(client_addr);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sockpath, sizeof(addr.sun_path)-1);

    unlink(sockpath);
    if(bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        die("Failed to bind to socket");
    }

    if(chmod(sockpath, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP) == -1) {
        die("Failed to set socket permissions");
    }

    struct group* group = getgrnam("network");
    if(group == NULL) { die("Failed to get network group information"); }

    if(chown(sockpath, 0, group->gr_gid) == -1) {
        die("Failed to set socket ownership");
    }

    int monitor = monitor_ifaces();
    if(monitor < 0) { die("Failed to monitor ifaces"); }

    drop_permissions(username);
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);

    if(listen(sockfd, 5) == -1) {
        die("Error listening");
    }

    const int kq = kqueue();
    if(kq == -1) { die("Failed to create kqueue"); }

    struct kevent event_set[10];
    struct kevent watch;
    EV_SET(&watch, sockfd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    if(kevent(kq, &watch, 1, NULL, 0, NULL) == -1) {
        die("Failed to add kevent watch");
    }

    EV_SET(&watch, monitor, EVFILT_READ, EV_ADD, 0, 0, NULL);
    if(kevent(kq, &watch, 1, NULL, 0, NULL) == -1) {
        die("Failed to add kevent watch");
    }    

    printf("Listening\n");
    while(1) {
        const int nev = kevent(kq, NULL, 0, event_set, 10, NULL);

        if(nev < 1) { die("Error waiting on kqueue"); }
        for(int i = 0; i < nev; i += 1) {
            struct kevent* event = &event_set[i];
            if(event->flags & EV_EOF) {
               EV_SET(&watch, event->ident, EVFILT_READ, EV_DELETE, 0, 0, NULL);
               if(kevent(kq, &watch, 1, NULL, 0, NULL) == -1) {
                   die("Error removing connection from watch");
               }
               close(event->ident);
            } else if((int)event->ident == monitor) {
                handle_iface_change(monitor);
            } else if((int)event->ident == sockfd) {
                int fd = accept(sockfd, (struct sockaddr*)&client_addr, &client_socklen);
                if(fd == -1) { die("Error accepting connection"); }
                if(fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
                    die("Error changing to non-blocking mode");
                }

                EV_SET(&watch, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
                if(kevent(kq, &watch, 1, NULL, 0, NULL) == -1) {
                    die("Error adding watch on new connection");
                }
            } else {
                handle((int)event->ident);
            }
        }
    }
}

void usage(void) {
    printf("usage: networkd [-s <sockpath>] [-u <user>]\n");
    exit(1);
}

int main(int argc, char** argv) {
    // Harden our malloc flags
    extern char *malloc_options;
    malloc_options = "SC";

    char* sockpath = "/var/run/networkd.sock";
    char* username = "_networkd";
    char flag = '\0';
    for(int i = 1; i < argc; i += 1) {
        char* arg = argv[i];
        if(flag == '\0') {
            if(arg[0] != '-') { usage(); }
            flag = arg[1];
            continue;
        }

        switch(flag) {
            case 's':
                sockpath = arg;
                break;
            case 'u':
                username = arg;
                break;
            default:
                usage();
                break;
        }

        flag = '\0';
    }

    if(flag != '\0') { usage(); }

    // Start child workers for privsep
    spawn_service(&service_exec_ibuf, service_exec);
    spawn_service(&service_write_ibuf, service_write);

    // Main loop
    serve(sockpath, username);

    return 0;
}
