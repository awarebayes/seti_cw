#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "mysock.h"
#include "util.h"

int create_socket(const char *host, const char *port)
{
  struct addrinfo hints = {
      .ai_flags = AI_NUMERICSERV,
      .ai_family = AF_UNSPEC,
      .ai_socktype = SOCK_STREAM,
  };
  struct addrinfo *ai, *p;
  int ret, in_socket = 0;

  if ((ret = getaddrinfo(host, port, &hints, &ai)))
  {
    die("getaddrinfo: %s", gai_strerror(ret));
  }

  for (p = ai; p; p = p->ai_next)
  {
    if ((in_socket = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) <
        0)
    {
      continue;
    }
    if (setsockopt(in_socket, SOL_SOCKET, SO_REUSEADDR, &(int){1},
                   sizeof(int)) < 0)
    {
      die("setsockopt:");
    }
    if (bind(in_socket, p->ai_addr, p->ai_addrlen) < 0)
    {

      if (close(in_socket) < 0)
      {
        die("close:");
      }
      continue;
    }
    break;
  }
  freeaddrinfo(ai);
  if (!p)
  {

    if (errno == EACCES)
    {
      die("Only rood can bind to priveledged ports");
    }
    else
    {
      die("bind:");
    }
  }

  if (listen(in_socket, SOMAXCONN) < 0)
  {
    die("listen:");
  }

  return in_socket;
}

int set_socket_timeout(int fd, int sec)
{
  struct timeval tv;

  tv.tv_sec = sec;
  tv.tv_usec = 0;

  if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0 ||
      setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0)
  {
    log_warn("setsockopt:");
    return 1;
  }

  return 0;
}

int unblock_socket(int fd)
{
  int flags;

  if ((flags = fcntl(fd, F_GETFL, 0)) < 0)
  {
    log_warn("fcntl:");
    return 1;
  }

  flags |= O_NONBLOCK;

  if (fcntl(fd, F_SETFL, flags) < 0)
  {
    log_warn("fcntl:");
    return 1;
  }

  return 0;
}

int get_socket_inaddr(const struct sockaddr_storage *in_sa, char *str,
                      size_t len)
{
  switch (in_sa->ss_family)
  {
  case AF_INET:
    if (!inet_ntop(AF_INET, &(((struct sockaddr_in *)in_sa)->sin_addr), str,
                   len))
    {
      log_warn("inet_ntop:");
      return 1;
    }
    break;
  case AF_INET6:
    if (!inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)in_sa)->sin6_addr), str,
                   len))
    {
      log_warn("inet_ntop:");
      return 1;
    }
    break;
  case AF_UNIX:
    snprintf(str, len, "uds");
    break;
  default:
    snprintf(str, len, "-");
  }

  return 0;
}

int sockets_same_addr(const struct sockaddr_storage *sa1,
                      const struct sockaddr_storage *sa2)
{

  if (sa1->ss_family != sa2->ss_family)
  {
    return 0;
  }

  switch (sa1->ss_family)
  {
  case AF_INET:
    return ((struct sockaddr_in *)sa1)->sin_addr.s_addr ==
           ((struct sockaddr_in *)sa2)->sin_addr.s_addr;
  default:
    return strcmp(((struct sockaddr_un *)sa1)->sun_path,
                  ((struct sockaddr_un *)sa2)->sun_path) == 0;
  }
}

void close_all_sockets()
{
  int sockfd;
  struct sockaddr_in6 addr;
  socklen_t addrlen = sizeof(addr);

  // Iterate through all file descriptors
  for (sockfd = getdtablesize(); sockfd >= 0; --sockfd)
  {
    if (getsockname(sockfd, (struct sockaddr *)&addr, &addrlen) == 0)
    {
      if (addr.sin6_family == AF_INET)
      {
        // Close the socket
        printf("Closed socket...\n");
        close(sockfd);
      }
    }
  }
}
