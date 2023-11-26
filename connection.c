#include "connection.h"
#include "buffer.h"
#include "http.h"
#include "mysock.h"
#include "srv.h"
#include "util.h"
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

struct data_for_worker
{
  int m_in_socket;
  size_t m_num_slots;
  const struct server *m_serv;
};

void log_con(const struct conn_t *c)
{
  char inaddr_str[INET6_ADDRSTRLEN];
  char tstmp[21];

  if (!strftime(tstmp, sizeof(tstmp), "%Y-%m-%dT%H:%M:%SZ",
                gmtime(&(time_t){time(NULL)})))
  {
    log_warn("strftime: Exceeded buffer capacity");
    tstmp[0] = '\0';
  }

  if (get_socket_inaddr(&c->m_sock_storage, inaddr_str, LEN(inaddr_str)))
  {
    log_warn("get_socket_inaddr: Couldn't generate adress-string");
    inaddr_str[0] = '\0';
  }

  log_info("%s\t%s\t status %s%.*d\t%s\t%s%s%s%s%s\n", tstmp, inaddr_str,
           (c->m_resp.m_status == 0) ? "dropped" : "",
           (c->m_resp.m_status == 0) ? 0 : 3, c->m_resp.m_status,
           c->m_req.m_field[REQ_HOST][0] ? c->m_req.m_field[REQ_HOST] : "-",
           c->m_req.m_path[0] ? c->m_req.m_path : "-",
           c->m_req.m_query[0] ? "?" : "", c->m_req.m_query,
           c->m_req.m_fragment[0] ? "#" : "", c->m_req.m_fragment);
  log_info("Aboba");
}

void reset_con(struct conn_t *c)
{
  if (c != NULL)
  {
    shutdown(c->m_file_descriptor, SHUT_RDWR);
    log_info("closed fd: %d\n", c->m_file_descriptor);
    close(c->m_file_descriptor);
    memset(c, 0, sizeof(*c));
  }
}

void serve_con(struct conn_t *c, const struct server *srv)
{
  enum status s;
  int done;

  switch (c->m_state)
  {
  case CONN_VACANT:
    memset(&c->buf, 0, sizeof(c->buf));

    c->m_state = CONN_RECV_HEADER;

  case CONN_RECV_HEADER:

    done = 0;
    if ((s = receive_header_http(c->m_file_descriptor, &c->buf, &done)))
    {
      prepare_err_resp_http(&c->m_req, &c->m_resp, s);
      goto response;
    }
    if (!done)
    {

      return;
    }

    if ((s = parse_header_http(c->buf.data, &c->m_req)))
    {
      prepare_err_resp_http(&c->m_req, &c->m_resp, s);
      goto response;
    }

    prepare_resp_http(&c->m_req, &c->m_resp, srv);
  response:

    if ((s = prep_header_buf_http(&c->m_resp, &c->buf)))
    {
      prepare_err_resp_http(&c->m_req, &c->m_resp, s);
      if ((s = prep_header_buf_http(&c->m_resp, &c->buf)))
      {

        c->m_resp.m_status = s;
        goto err;
      }
    }

    c->m_state = CONN_SEND_HEADER;

  case CONN_SEND_HEADER:
    if ((s = send_buffer_http(c->m_file_descriptor, &c->buf)))
    {
      c->m_resp.m_status = s;
      goto err;
    }
    if (c->buf.length > 0)
    {

      return;
    }

    c->m_state = CONN_SEND_BODY;

  case CONN_SEND_BODY:
    if (c->m_req.m_method == METH_GET)
    {
      if (c->buf.length == 0)
      {

        if ((s = data_fct[c->m_resp.m_type](&c->m_resp, &c->buf,
                                            &c->m_progr)))
        {

          c->m_resp.m_status = s;
          goto err;
        }

        if (c->buf.length == 0)
        {
          break;
        }
      }
      else
      {

        if ((s = send_buffer_http(c->m_file_descriptor, &c->buf)))
        {

          c->m_resp.m_status = s;
          goto err;
        }
      }
      return;
    }
    break;
  default:
    log_warn("serve: invalid connection state");
    return;
  }
err:
  log_con(c);
  reset_con(c);
}

static struct conn_t *connection_get_drop_candidate(struct conn_t *connection,
                                                    size_t nslots)
{
  struct conn_t *c, *minc;
  size_t i, j, maxcnt, cnt;

  for (i = 0, minc = NULL, maxcnt = 0; i < nslots; i++)
  {
    c = &connection[i];

    for (j = 0, cnt = 0; j < nslots; j++)
    {
      if (!sockets_same_addr(&connection[i].m_sock_storage,
                             &connection[j].m_sock_storage))
      {
        continue;
      }
      cnt++;

      if (connection[j].m_state < c->m_state)
      {
        c = &connection[j];
      }
      else if (connection[j].m_state == c->m_state)
      {

        if (c->m_state == CONN_SEND_BODY &&
            connection[i].m_resp.m_type != c->m_resp.m_type)
        {
          if (connection[i].m_resp.m_type < c->m_resp.m_type)
          {
            c = &connection[j];
          }
        }
        else if (connection[j].m_progr < c->m_progr)
        {
          c = &connection[j];
        }
      }
    }

    if (cnt > maxcnt)
    {

      minc = c;
      maxcnt = cnt;
    }
  }

  return minc;
}

struct conn_t *accept_con(int in_socket, struct conn_t *connection,
                          size_t nslots)
{
  struct conn_t *c = NULL;
  size_t i;

  for (i = 0; i < nslots; i++)
  {
    if (connection[i].m_file_descriptor == 0)
    {
      c = &connection[i];
      break;
    }
  }
  if (i == nslots)
  {
    c = connection_get_drop_candidate(connection, nslots);
    if (c == NULL) 
      return NULL;
    c->m_resp.m_status = 0;
    log_con(c);
    reset_con(c);
  }

  if ((c->m_file_descriptor =
           accept(in_socket, (struct sockaddr *)&c->m_sock_storage,
                  &(socklen_t){sizeof(c->m_sock_storage)})) < 0)
  {
    if (errno != EAGAIN && errno != EWOULDBLOCK)
    {
      log_warn("accept:");
    }
    return NULL;
  }

  if (unblock_socket(c->m_file_descriptor))
  {

    return NULL;
  }

  return c;
}
