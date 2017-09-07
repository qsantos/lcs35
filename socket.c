#define _POSIX_C_SOURCE 200112L

#include "socket.h"

#include <string.h>
#include <netdb.h>
#include <unistd.h>

int tcp_connect(const char* interface, const char* port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* result;
    getaddrinfo(interface, port, &hints, &result);

    int sock;
    struct addrinfo* cur = result;
    while (cur) {
        sock = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
        if (sock == -1) {
            continue; // try next addrinfo
        }
        if (connect(sock, cur->ai_addr, cur->ai_addrlen) == 0) {
            break; // keep this one
        }
        close(sock);
        cur = cur->ai_next;
    }

    if (cur == NULL) {
        sock = -1;
    }
    freeaddrinfo(result);
    return sock;
}

int tcp_listento(const char* interface, const char* port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo* result;
    if (getaddrinfo(interface, port, &hints, &result)) {
        return -1;
    }

    int sock;
    struct addrinfo* cur = result;
    while (cur) {
        sock = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
        if (sock == -1) {
            continue;
        }

        int v = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(int));

        if (bind(sock, cur->ai_addr, cur->ai_addrlen) != -1 && listen(sock, 10) != -1) {
            break;
        }

        close(sock);
        cur = cur->ai_next;
    }

    if (!cur) {
        sock = -1;
    }
    freeaddrinfo(result);
    return sock;
}

int tcp_listen(const char* port) {
    return tcp_listento(NULL, port);
}

int tcp_accept(int sock) {
    int client = accept(sock, NULL, NULL);
    if (client < 0) {
        return -1;
    }

    return client;
}
