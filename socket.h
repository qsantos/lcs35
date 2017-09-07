#ifndef SOCKET_H
#define SOCKET_H

int tcp_connect(const char* interface, const char* port);
int tcp_listento(const char* interface, const char* port);
int tcp_listen(const char* port);
int tcp_accept(int socket);

#endif
