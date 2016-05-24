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
#include <signal.h>
#include <fcntl.h>
#include <imsg.h>

#include "parse.h"
#include "util.h"
#include "validate.h"
#include "service_exec.h"
#include "service_write.h"
#include "ifstate.h"

#define SOCKET_PATH "/var/run/network.sock"

void handle_list(FILE*, bool);

void sighandler(int signo) {
    write(2, "Received signal\n", 16);
    cleanup();
    _exit(0);
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

void drop_permissions(void) {
    struct passwd* passwd = getpwnam("daemon");
    if(passwd == NULL) { die("Failed to get user information"); }
    struct group* group = getgrnam("daemon");
    if(group == NULL) { die("Failed to get group information"); }

    if(setgroups(0, NULL) == -1) { die("Failed to set supplementary groups"); }
    if(setgid(group->gr_gid) == -1) { die("Failed to set group"); }
    if(setuid(passwd->pw_uid) == -1) { die("Failed to set user"); }

    //pledge("stdio unix tmppath rpath", NULL);
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
        fprintf(sock, "error\n");
        free(output_text);
        return;
    }

    char const* space = "";
    char* cursor;
    char iface[IF_NAMESIZE];
    bool skipping = false;
    while((cursor = strsep(&output_text, "\n")) != NULL) {
        char key[IFCONFIG_KEY_LEN];
        char flags[FLAGS_LEN];
        int mtu;
        if(parse_ifconfig_header(cursor, iface, flags, &mtu)) {
            if(iface_is_pseudo(iface, pseudo_classes)) {
                skipping = true;
                continue;
            } else {
                if(!details) {
                    fprintf(sock, "%s%s", space, iface);
                    space = " ";
                    continue;
                }
                skipping = false;
            }

            // We don't need to escape flags because it cannot have whitespace
            fprintf(sock, "%s%s.flags %s", space, iface, flags);
            fprintf(sock, " %s.mtu %d", iface, mtu);
            space = " ";
            continue;
        }

        if(!skipping && details && parse_ifconfig_kv(cursor, key, flags)) {
            char escaped[FLAGS_LEN];
            escape(flags, escaped, sizeof(escaped));
            fprintf(sock, " %s.%s %s", iface, key, escaped);
        }
    }

    fprintf(sock, "\n");
    free(output_text);
}

void handle_configure(FILE* sock, const char* args) {
    service_send(&service_write_ibuf, WRITE_WRITE, args);
    int32_t result = service_pop(&service_write_ibuf, NULL, 0);
    if(result != WRITE_RESPONSE_OK) {
        fprintf(sock, "error\n");
    }

    fprintf(sock, "ok\n");
}

void handle_connect(FILE* sock, const char* args) {
    // Attempt to autoconfigure, if there is no current configuration
    service_send(&service_write_ibuf, WRITE_AUTOCONFIGURE, args);
    service_pop(&service_write_ibuf, NULL, 0);
    
    service_send(&service_exec_ibuf, EXEC_NETSTART, args);
    int32_t result = service_pop(&service_exec_ibuf, NULL, 0);
    if(result != EXEC_RESPONSE_OK) {
        fprintf(sock, "error\n");
    }

    fprintf(sock, "ok\n");
}

void handle_disconnect(FILE* sock, const char* args) {
    char iface[IF_NAMESIZE];
    strlcpy(iface, args, sizeof(iface));

    service_send(&service_write_ibuf, EXEC_IFCONFIG_DOWN, args);
    int32_t result = service_pop(&service_write_ibuf, iface, strlen(iface));
    if(result != EXEC_RESPONSE_OK) {
        fprintf(sock, "error\n");
    }

    fprintf(sock, "ok\n");
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
            char const* remainder = unescape(chomp(buf), command, sizeof(command));
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

void serve(void) {
    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(sockfd < 0) { die("Failed to create socket"); }

    struct sockaddr_storage client_addr;
    socklen_t client_socklen = sizeof(client_addr);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path)-1);

    unlink(SOCKET_PATH);
    if(bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        die("Failed to bind to socket");
    }

    if(chmod(SOCKET_PATH, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP) == -1) {
        die("Failed to set socket permissions");
    }

    struct group* group = getgrnam("network");
    if(group == NULL) { die("Failed to get network group information"); }

    if(chown(SOCKET_PATH, 0, group->gr_gid) == -1) {
        die("Failed to set socket ownership");
    }

    drop_permissions();
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

    int monitor = monitor_ifaces();
    if(monitor < 0) { die("Failed to monitor ifaces"); }
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

int main(void) {
    // Harden our malloc flags
    extern char *malloc_options;
    malloc_options = "SC";

    // Start child workers for privsep
    spawn_service(&service_exec_ibuf, service_exec);
    spawn_service(&service_write_ibuf, service_write);

    // Main loop
    serve();

    return 0;
}
