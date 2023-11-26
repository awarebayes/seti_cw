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
    /*
     * we were passed a "fresh" connection which should now
     * try to receive the header, reset buf beforehand
     */
    memset(&c->buf, 0, sizeof(c->buf));

    c->m_state = CONN_RECV_HEADER;
    /* fallthrough */
  case CONN_RECV_HEADER:
    /* receive header */
    done = 0;
    if ((s = receive_header_http(c->m_file_descriptor, &c->buf, &done)))
    {
      prepare_err_resp_http(&c->m_req, &c->m_resp, s);
      goto response;
    }
    if (!done)
    {
      /* not done yet */
      return;
    }

    /* parse header */
    if ((s = parse_header_http(c->buf.data, &c->m_req)))
    {
      prepare_err_resp_http(&c->m_req, &c->m_resp, s);
      goto response;
    }

    /* prepare response struct */
    prepare_resp_http(&c->m_req, &c->m_resp, srv);
  response:
    /* generate response header */
    if ((s = prep_header_buf_http(&c->m_resp, &c->buf)))
    {
      prepare_err_resp_http(&c->m_req, &c->m_resp, s);
      if ((s = prep_header_buf_http(&c->m_resp, &c->buf)))
      {
        /* couldn't generate the header, we failed for good */
        c->m_resp.m_status = s;
        goto err;
      }
    }

    c->m_state = CONN_SEND_HEADER;
    /* fallthrough */
  case CONN_SEND_HEADER:
    if ((s = send_buffer_http(c->m_file_descriptor, &c->buf)))
    {
      c->m_resp.m_status = s;
      goto err;
    }
    if (c->buf.length > 0)
    {
      /* not done yet */
      return;
    }

    c->m_state = CONN_SEND_BODY;
    /* fallthrough */
  case CONN_SEND_BODY:
    if (c->m_req.m_method == METH_GET)
    {
      if (c->buf.length == 0)
      {
        /* fill buffer with body data */
        if ((s = data_fct[c->m_resp.m_type](&c->m_resp, &c->buf,
                                            &c->m_progr)))
        {
          /* too late to do any real error handling */
          c->m_resp.m_status = s;
          goto err;
        }

        /* if the buffer remains empty, we are done */
        if (c->buf.length == 0)
        {
          break;
        }
      }
      else
      {
        /* send buffer */
        if ((s = send_buffer_http(c->m_file_descriptor, &c->buf)))
        {
          /* too late to do any real error handling */
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

  /*
   * determine the most-unimportant connection 'minc' of the in-address
   * with most connections; this algorithm has a complexity of O(n²)
   * in time but is O(1) in space; there are algorithms with O(n) in
   * time and space, but this would require memory allocation,
   * which we avoid. Given the simplicity of the inner loop and
   * relatively small number of slots per thread, this is fine.
   */
  for (i = 0, minc = NULL, maxcnt = 0; i < nslots; i++)
  {
    /*
     * we determine how many connections have the same
     * in-address as connection[i], but also minimize over
     * that set with other criteria, yielding a general
     * minimizer c. We first set it to connection[i] and
     * update it, if a better candidate shows up, in the inner
     * loop
     */
    c = &connection[i];

    for (j = 0, cnt = 0; j < nslots; j++)
    {
      if (!sockets_same_addr(&connection[i].m_sock_storage,
                             &connection[j].m_sock_storage))
      {
        continue;
      }
      cnt++;

      /* minimize over state */
      if (connection[j].m_state < c->m_state)
      {
        c = &connection[j];
      }
      else if (connection[j].m_state == c->m_state)
      {
        /* minimize over progress */
        if (c->m_state == CONN_SEND_BODY &&
            connection[i].m_resp.m_type != c->m_resp.m_type)
        {
          /*
           * mixed response types; progress
           * is not comparable
           *
           * the res-type-enum is ordered as
           * DIRLISTING, ERROR, FILE, i.e.
           * in rising priority, because a
           * file transfer is most important,
           * followed by error-messages.
           * Dirlistings as an "interactive"
           * feature (that take up lots of
           * resources) have the lowest
           * priority
           */
          if (connection[i].m_resp.m_type < c->m_resp.m_type)
          {
            c = &connection[j];
          }
        }
        else if (connection[j].m_progr < c->m_progr)
        {
          /*
           * for CONN_SEND_BODY with same response
           * type, CONN_RECV_HEADER and CONN_SEND_BODY
           * it is sufficient to compare the
           * raw progress
           */
          c = &connection[j];
        }
      }
    }

    if (cnt > maxcnt)
    {
      /* this run yielded an even greedier in-address */
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

  /* find vacant connection (i.e. one with no fd assigned to it) */
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
    c->m_resp.m_status = 0;
    log_con(c);
    reset_con(c);
  }

  /* accept connection */
  if ((c->m_file_descriptor =
           accept(in_socket, (struct sockaddr *)&c->m_sock_storage,
                  &(socklen_t){sizeof(c->m_sock_storage)})) < 0)
  {
    if (errno != EAGAIN && errno != EWOULDBLOCK)
    {
      /*
       * this should not happen, as we received the
       * event that there are pending connections here
       */
      log_warn("accept:");
    }
    return NULL;
  }

  /* set socket to non-blocking mode */
  if (unblock_socket(c->m_file_descriptor))
  {
    /* we can't allow blocking sockets */
    return NULL;
  }

  return c;
}
