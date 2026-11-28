#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "protocol.h"

static int connect_server(const char *host, const char *port_str) {
    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port_str, &hints, &res) != 0) return -1;

    int fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == -1) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <host> <port> <submit|status|cancel|fetch> ...\n", argv);
        return 1;
    }

    const char *host = argv;
    const char *port_str = argv;
    const char *verb = argv;

    int fd = connect_server(host, port_str);
    if (fd < 0) {
        // Точный формат ошибки подключения по §5.2
        fprintf(stderr, "client: cannot connect to %s:%s\n", host, port_str);
        return 3;
    }

    if (strcmp(verb, "submit") == 0) {
        if (argc < 6) {
            fprintf(stderr, "Usage: %s <host> <port> submit \"<cmd>\" <timeout> [priority]\n", argv);
            close(fd);
            return 1;
        }
        const char *cmd = argv;
        int timeout = atoi(argv);
        int prio = (argc >= 7) ? atoi(argv) : 0;

        char enc_cmd[MAX_CMD_LEN * 2];
        encode_newlines(cmd, enc_cmd, sizeof(enc_cmd));

        char req[4096];
        int rlen = snprintf(req, sizeof(req), "SUBMIT %d %d %s\n", timeout, prio, enc_cmd);
        send_all(fd, req, (size_t)rlen);

        char resp[256];
        if (recv_line(fd, resp, sizeof(resp)) > 0) {
            printf("%s\n", resp);
            close(fd);
            return 0;
        }
        close(fd);
        return 1;
    }

    if (strcmp(verb, "status") == 0) {
        if (argc < 5) { close(fd); return 1; }
        const char *id = argv;

        char req[128];
        snprintf(req, sizeof(req), "STATUS %s\n", id);
        send_all(fd, req, strlen(req));

        char resp[256];
        if (recv_line(fd, resp, sizeof(resp)) > 0) {
            printf("%s\n", resp);
            close(fd);
            if (strstr(resp, "UNKNOWN") != NULL) return 2;
            return 0;
        }
        close(fd);
        return 1;
    }

    if (strcmp(verb, "cancel") == 0) {
        if (argc < 5) { close(fd); return 1; }
        const char *id = argv;

        char req[128];
        snprintf(req, sizeof(req), "CANCEL %s\n", id);
        send_all(fd, req, strlen(req));

        char resp[256];
        if (recv_line(fd, resp, sizeof(resp)) > 0) {
            printf("%s\n", resp);
            close(fd);
            if (strstr(resp, "UNKNOWN") != NULL) return 2;
            if (strstr(resp, "ALREADY FINISHED") != NULL) return 1;
            return 0;
        }
        close(fd);
        return 1;
    }

    if (strcmp(verb, "fetch") == 0) {
        if (argc < 5) { close(fd); return 1; }
        const char *id = argv;

        char req[128];
        snprintf(req, sizeof(req), "FETCH %s\n", id);
        send_all(fd, req, strlen(req));

        char hdr[256];
        if (recv_line(fd, hdr, sizeof(hdr)) <= 0) {
            close(fd);
            return 1;
        }

        if (strncmp(hdr, "UNKNOWN ", 8) == 0) {
            printf("%s UNKNOWN\n", id);
            close(fd);
            return 2;
        }
        if (strncmp(hdr, "NOT_FINISHED ", 13) == 0) {
            // По §5.3: ничего в stdout, сообщение в stderr, код возврата 1
            fprintf(stderr, "%s NOT FINISHED\n", id);
            close(fd);
            return 1;
        }
        if (strncmp(hdr, "OK ", 3) == 0) {
            size_t len = (size_t)strtoull(hdr + 3, NULL, 10);
            if (len > 0) {
                char *buf = malloc(len);
                if (buf && recv_all(fd, buf, len) == 0) {
                    fwrite(buf, 1, len, stdout);
                    fflush(stdout);
                }
                free(buf);
            }
            close(fd);
            return 0;
        }
    }

    close(fd);
    return 1;
}