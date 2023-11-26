#pragma once

#include <limits.h>
#include <sys/socket.h>

#include "configuration.h"
#include "srv.h"
#include "util.h"

enum req_field
{
  REQ_HOST,
  REQ_RANGE,
  REQ_IF_MODIFIED_SINCE,
  NUM_REQ_FIELDS,
};

extern const char *req_field_str[];

enum req_method
{
  METH_GET,
  M_HEAD,
  NUM_REQ_METHODS,
};

extern const char *req_method_str[];

struct req_t
{
  enum req_method m_method;
  char m_path[PATH_MAX];
  char m_query[FIELD_MAX];
  char m_fragment[FIELD_MAX];
  char m_field[NUM_REQ_FIELDS][FIELD_MAX];
};

enum status
{
  STATUS_OK = 200,
  STATUS_NOT_MODIFIED = 304,
  STATUS_FORBIDDEN = 403,
  STATUS_NOT_FOUND = 404,
  STATUS_METHOD_NOT_ALLOWED = 405,
  STATUS_INTERNAL_SERVER_ERROR = 500,
};

extern const char *status_str[];

enum response_field
{
  RES_ACCEPT_RANGES,
  RES_ALLOW,
  RES_LOCATION,
  RES_LAST_MODIFIED,
  RES_CONTENT_LENGTH,
  RES_CONTENT_RANGE,
  RES_CONTENT_TYPE,
  NUM_RES_FIELDS,
};

extern const char *res_field_str[];

enum res_type
{
  RESTYPE_DIRLISTING,
  RESTYPE_ERROR,
  RESTYPE_FILE,
  NUM_RES_TYPES,
};

struct resp_t
{
  struct virtual_host *m_virt_host;
  enum status m_status;
  char m_path[PATH_MAX];
  char m_internal_path[PATH_MAX];
  struct
  {
    size_t lower;
    size_t upper;
  } m_file;
  enum res_type m_type;
  char m_field[NUM_RES_FIELDS][FIELD_MAX];
};

enum status send_buffer_http(int, struct my_buffer *);
enum status prep_header_buf_http(const struct resp_t *, struct my_buffer *);
enum status parse_header_http(const char *, struct req_t *);
void prepare_err_resp_http(const struct req_t *, struct resp_t *, enum status);
void prepare_resp_http(const struct req_t *, struct resp_t *,
                       const struct server *);
enum status receive_header_http(int, struct my_buffer *, int *);
