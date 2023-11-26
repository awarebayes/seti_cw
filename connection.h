#pragma once

#include "http.h"
#include "srv.h"
#include "util.h"

enum conn_state_t
{
  CONN_VACANT,
  CONN_RECV_HEADER,
  CONN_SEND_HEADER,
  CONN_SEND_BODY,
  NUM_CONNECT_STATES,
};

struct conn_t
{
  enum conn_state_t m_state;
  int m_file_descriptor;
  struct sockaddr_storage m_sock_storage;
  struct req_t m_req;
  struct resp_t m_resp;
  struct my_buffer buf;
  size_t m_progr;
};

struct conn_t *accept_con(int, struct conn_t *, size_t);
void log_con(const struct conn_t *);
void reset_con(struct conn_t *);
void serve_con(struct conn_t *, const struct server *);
