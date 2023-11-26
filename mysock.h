#pragma once

#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>

int create_socket(const char *, const char *);
int set_socket_timeout(int, int);
int unblock_socket(int);
int get_socket_inaddr(const struct sockaddr_storage *, char *, size_t);
int sockets_same_addr(const struct sockaddr_storage *,
                      const struct sockaddr_storage *);
void close_all_sockets(void);
