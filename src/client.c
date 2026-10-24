#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "net.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080

int main() {
    int sock = 0;

    // Create socket file descriptor
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        return 1;
    }

    printf("Socket successfully created.\n");
    close(sock);
    return 0;
}