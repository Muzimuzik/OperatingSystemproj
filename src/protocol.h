#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>

#define TEAM_NUMBER 4
#define MAX_ID_LEN 32
#define MAX_CMD_LEN 1024
#define MAX_OUTPUT_LEN (64 * 1024) // Минимум 64 KiB по требованию §4

// Кодирование/декодирование переводов строк для передачи команды в одну строку по TCP
static inline void encode_newlines(const char *src, char *dst, size_t dst_sz) {
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 2 < dst_sz; i++) {
        if (src[i] == '\n') {
            dst[j++] = '\\';
            dst[j++] = 'n';
        } else if (src[i] == '\\') {
            dst[j++] = '\\';
            dst[j++] = '\\';
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

static inline void decode_newlines(const char *src, char *dst, size_t dst_sz) {
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 1 < dst_sz; i++) {
        if (src[i] == '\\' && src[i + 1] == 'n') {
            dst[j++] = '\n';
            i++;
        } else if (src[i] == '\\' && src[i + 1] == '\\') {
            dst[j++] = '\\';
            i++;
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

static inline int send_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(fd, p + total, len - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        total += (size_t)n;
    }
    return 0;
}

static inline int recv_all(int fd, void *buf, size_t len) {
    char *p = (char *)buf;
    size_t total = 0;
    while (total < len) {
        ssize_t n = recv(fd, p + total, len - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        total += (size_t)n;
    }
    return 0;
}

// Побайтовое чтение строки до '\n' (для клиента и воркера)
static inline ssize_t recv_line(int fd, char *buf, size_t max_len) {
    size_t i = 0;
    while (i + 1 < max_len) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            if (i == 0) return 0;
            break;
        }
        if (c == '\n') break;
        if (c != '\r') buf[i++] = c;
    }
    buf[i] = '\0';
    return (ssize_t)i;
}

#endif