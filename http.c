#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <regex.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "configuration.h"
#include "http.h"
#include "util.h"

const char *req_field_str[] = {
    [REQ_RANGE] = "Range",
    [REQ_HOST] = "Host",
    [REQ_IF_MODIFIED_SINCE] = "If-Modified-Since",
};

const char *req_method_str[] = {
    [METH_GET] = "GET",
    [METH_HEAD] = "HEAD",
};

const char *status_str[] = {[STATUS_OK] = "OK",
                            [STATUS_FORBIDDEN] = "Forbidden",
                            [STATUS_NOT_FOUND] = "Not Found",
                            [STATUS_METHOD_NOT_ALLOWED] = "Method Not Allowed",
                            [STATUS_METHOD_NOT_ALLOWED] = "Not Modified",
                            [STATUS_INTERNAL_SERVER_ERROR] =
                                "Internal Server Error"};

const char *res_field_str[] = {
    [RES_ACCEPT_RANGES] = "Accept-Ranges",
    [RES_ALLOW] = "Allow",
    [RES_LOCATION] = "Location",
    [RES_LAST_MODIFIED] = "Last-Modified",
    [RES_CONTENT_LENGTH] = "Content-Length",
    [RES_CONTENT_RANGE] = "Content-Range",
    [RES_CONTENT_TYPE] = "Content-Type",
};

static void decode(const char src[PATH_MAX], char dest[PATH_MAX])
{
  size_t i;
  uint8_t n;
  const char *s;

  for (s = src, i = 0; *s; i++)
  {
    if (*s == '%' && isxdigit((unsigned char)s[1]) &&
        isxdigit((unsigned char)s[2]))
    {
      sscanf(s + 1, "%2hhx", &n);
      dest[i] = n;
      s += 3;
    }
    else
    {
      dest[i] = *s++;
    }
  }
  dest[i] = '\0';
}

enum status send_buffer_http(int fd, struct my_buffer *buf)
{
  ssize_t r;

  if (buf == NULL)
  {
    return STATUS_INTERNAL_SERVER_ERROR;
  }

  while (buf->length > 0)
  {
    if ((r = write(fd, buf->data, buf->length)) <= 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        return 0;
      }
      else
      {
        return STATUS_INTERNAL_SERVER_ERROR;
      }
    }
    memmove(buf->data, buf->data + r, buf->length - r);
    buf->length -= r;
  }

  return 0;
}

enum status receive_header_http(int fd, struct my_buffer *buf, int *done)
{
  enum status s;
  ssize_t r;

  while (1)
  {
    if ((r = read(fd, buf->data + buf->length,
                  sizeof(buf->data) - buf->length)) < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        /*
         * socket is drained, return normally,
         * but set done to zero
         */
        *done = 0;
        return 0;
      }
      else
      {
        s = STATUS_INTERNAL_SERVER_ERROR; // timeout
        goto err;
      }
    }
    else if (r == 0)
    {
      // unexpected EOF
      s = STATUS_INTERNAL_SERVER_ERROR; // Bad request
      goto err;
    }
    buf->length += r;

    /* check if we are done (header terminated) */
    if (buf->length >= 4 &&
        !memcmp(buf->data + buf->length - 4, "\r\n\r\n", 4))
    {
      break;
    }

    /* buffer is full or read over, but header is not terminated */
    if (r == 0 || buf->length == sizeof(buf->data))
    {
      s = STATUS_INTERNAL_SERVER_ERROR; // response is too big
      goto err;
    }
  }

  /* header is complete, remove last \r\n and set done */
  buf->length -= 2;
  *done = 1;

  return 0;
err:
  memset(buf, 0, sizeof(*buf));
  return s;
}

enum status prep_header_buf_http(const struct resp_t *res,
                                 struct my_buffer *buf)
{
  char tstmp[FIELD_MAX];
  size_t i;

  /* reset buffer */
  memset(buf, 0, sizeof(*buf));

  /* generate timestamp */
  if (get_time_stamp(tstmp, sizeof(tstmp), time(NULL)))
  {
    goto err;
  }

  /* write data */
  if (buffer_append(buf,
                     "HTTP/1.1 %d %s\r\n"
                     "Date: %s\r\n"
                     "Connection: close\r\n",
                     res->m_status, status_str[res->m_status], tstmp))
  {
    goto err;
  }

  for (i = 0; i < NUM_RES_FIELDS; i++)
  {
    if (res->m_field[i][0] != '\0' &&
        buffer_append(buf, "%s: %s\r\n", res_field_str[i], res->m_field[i]))
    {
      goto err;
    }
  }

  if (buffer_append(buf, "\r\n"))
  {
    goto err;
  }

  return 0;
err:
  memset(buf, 0, sizeof(*buf));
  return STATUS_INTERNAL_SERVER_ERROR;
}

enum status parse_header_http(const char *header_str, struct req_t *req)
{
  struct in6_addr addr;
  size_t i, mlen;
  const char *path_start, *end, *query_start;
  const char *fragment_start, *temp;
  char *m, *n;
  /*
   * Here is a quick overview of whats going on
   * path?query#fragment
   * ^   ^     ^        ^
   * |   |     |        |
   * ps  qs    fs       end
   */

  memset(req, 0, sizeof(*req));

  // Method
  for (i = 0; i < NUM_REQ_METHODS; i++)
  {
    mlen = strlen(req_method_str[i]);
    if (!strncmp(req_method_str[i], header_str, mlen))
    {
      req->m_method = i;
      break;
    }
  }
  if (i == NUM_REQ_METHODS)
  {
    return STATUS_METHOD_NOT_ALLOWED;
  }

  // method must be followed by single whitespace
  if (header_str[mlen] != ' ')
  {
    return STATUS_INTERNAL_SERVER_ERROR;
  }

  // preparing for next step
  path_start = header_str + mlen + 1;

  // Resource

  if (!(end = strchr(path_start, ' ')))
  {
    return STATUS_INTERNAL_SERVER_ERROR;
  }

  for (query_start = path_start; query_start < end; query_start++)
  {
    if (!isprint(*query_start))
    {
      return STATUS_INTERNAL_SERVER_ERROR;
    }
    if (*query_start == '?')
    {
      break;
    }
  }
  if (query_start == end)
  {
    query_start = NULL;
  }

  for (fragment_start = path_start; fragment_start < end; fragment_start++)
  {
    if (!isprint(*fragment_start))
    {
      return STATUS_INTERNAL_SERVER_ERROR;
    }
    if (*fragment_start == '#')
    {
      break;
    }
  }
  if (fragment_start == end)
  {
    fragment_start = NULL;
  }

  if (query_start != NULL && fragment_start != NULL &&
      fragment_start < query_start)
  {
    query_start = NULL;
  }

  if (query_start != NULL)
  {
    temp = query_start;
  }
  else if (fragment_start != NULL)
  {
    temp = fragment_start;
  }
  else
  {
    temp = end;
  }
  if ((size_t)(temp - path_start + 1) > LEN(req->m_path))
  {
    return STATUS_INTERNAL_SERVER_ERROR; // large request
  }
  memcpy(req->m_path, path_start, temp - path_start);
  req->m_path[temp - path_start] = '\0';
  decode(req->m_path, req->m_path);

  // Write query
  if (query_start != NULL)
  {
    /* query ends either at s (if fragment present) or q */
    temp = (fragment_start != NULL) ? fragment_start : end;

    if ((size_t)(temp - (query_start + 1) + 1) > LEN(req->m_query))
    {
      return STATUS_INTERNAL_SERVER_ERROR;
    }
    memcpy(req->m_query, query_start + 1, temp - (query_start + 1));
    req->m_query[temp - (query_start + 1)] = '\0';
  }

  /* write fragment if present */
  if (fragment_start != NULL)
  {
    /* the fragment always starts at s + 1 and ends at q */
    if ((size_t)(end - (fragment_start + 1) + 1) > LEN(req->m_fragment))
    {
      return STATUS_INTERNAL_SERVER_ERROR;
    }
    memcpy(req->m_fragment, fragment_start + 1, end - (fragment_start + 1));
    req->m_fragment[end - (fragment_start + 1)] = '\0';
  }

  /* basis for next step */
  path_start = end + 1;

  /* HTTP-VERSION */
  if (strncmp(path_start, "HTTP/", sizeof("HTTP/") - 1))
  {
    return STATUS_INTERNAL_SERVER_ERROR;
  }
  path_start += sizeof("HTTP/") - 1;
  if (strncmp(path_start, "1.0", sizeof("1.0") - 1) &&
      strncmp(path_start, "1.1", sizeof("1.1") - 1))
  {
    return STATUS_INTERNAL_SERVER_ERROR; // Unsupported version of http
  }
  path_start += sizeof("1.*") - 1;

  /* check terminator */
  if (strncmp(path_start, "\r\n", sizeof("\r\n") - 1))
  {
    return STATUS_INTERNAL_SERVER_ERROR;
  }

  /* basis for next step */
  path_start += sizeof("\r\n") - 1;

  /*
   * parse request-fields
   */

  /* match field type */
  for (; *path_start != '\0';)
  {
    for (i = 0; i < NUM_REQ_FIELDS; i++)
    {
      if (!strncasecmp(path_start, req_field_str[i],
                       strlen(req_field_str[i])))
      {
        break;
      }
    }
    if (i == NUM_REQ_FIELDS)
    {
      /* unmatched field, skip this line */
      if (!(end = strstr(path_start, "\r\n")))
      {
        return STATUS_INTERNAL_SERVER_ERROR;
      }
      path_start = end + (sizeof("\r\n") - 1);
      continue;
    }

    path_start += strlen(req_field_str[i]);

    /* a single colon must follow the field name */
    if (*path_start != ':')
    {
      return STATUS_INTERNAL_SERVER_ERROR;
    }

    /* skip whitespace */
    for (++path_start; *path_start == ' ' || *path_start == '\t'; path_start++)
      ;

    /* extract field content */
    if (!(end = strstr(path_start, "\r\n")))
    {
      return STATUS_INTERNAL_SERVER_ERROR;
    }
    if ((size_t)(end - path_start + 1) > LEN(req->m_field[i]))
    {
      return STATUS_INTERNAL_SERVER_ERROR;
    }
    memcpy(req->m_field[i], path_start, end - path_start);
    req->m_field[i][end - path_start] = '\0';

    /* go to next line */
    path_start = end + (sizeof("\r\n") - 1);
  }

  /*
   * clean up host
   */

  m = strrchr(req->m_field[REQ_HOST], ':');
  n = strrchr(req->m_field[REQ_HOST], ']');

  /* strip port suffix but don't interfere with IPv6 bracket notation
   * as per RFC 2732 */
  if (m && (!n || m > n))
  {
    /* port suffix must not be empty */
    if (*(m + 1) == '\0')
    {
      return STATUS_INTERNAL_SERVER_ERROR;
    }
    *m = '\0';
  }

  /* strip the brackets from the IPv6 notation and validate the address */
  if (n)
  {
    /* brackets must be on the outside */
    if (req->m_field[REQ_HOST][0] != '[' || *(n + 1) != '\0')
    {
      return STATUS_INTERNAL_SERVER_ERROR;
    }

    /* remove the right bracket */
    *n = '\0';
    m = req->m_field[REQ_HOST] + 1;

    /* validate the contained IPv6 address */
    if (inet_pton(AF_INET6, m, &addr) != 1)
    {
      return STATUS_INTERNAL_SERVER_ERROR;
    }

    /* copy it into the host field */
    memmove(req->m_field[REQ_HOST], m, n - m + 1);
  }

  return 0;
}

static void enc_str(const char src[PATH_MAX], char dest[PATH_MAX])
{
  size_t i;
  const char *s;

  for (s = src, i = 0; *s && i < (PATH_MAX - 4); s++)
  {
    if (iscntrl(*s) || (unsigned char)*s > 127)
    {
      i += snprintf(dest + i, PATH_MAX - i, "%%%02X", (unsigned char)*s);
    }
    else
    {
      dest[i] = *s;
      i++;
    }
  }
  dest[i] = '\0';
}

static enum status norm_path(char *uri, int *redirect)
{
  size_t len;
  int last = 0;
  char *p, *q;

  /* require and skip first slash */
  if (uri[0] != '/')
  {
    return STATUS_INTERNAL_SERVER_ERROR;
  }
  p = uri + 1;

  /* get length of URI */
  len = strlen(p);

  for (; !last;)
  {
    /* bound uri component within (p,q) */
    if (!(q = strchr(p, '/')))
    {
      q = strchr(p, '\0');
      last = 1;
    }

    if (*p == '\0')
    {
      break;
    }
    else if (p == q || (q - p == 1 && p[0] == '.'))
    {
      /* "/" or "./" */
      goto squash;
    }
    else if (q - p == 2 && p[0] == '.' && p[1] == '.')
    {
      /* "../" */
      if (p != uri + 1)
      {
        /* place p right after the previous / */
        for (p -= 2; p > uri && *p != '/'; p--)
          ;
        p++;
      }
      goto squash;
    }
    else
    {
      /* move on */
      p = q + 1;
      continue;
    }
  squash:
    /* squash (p,q) into void */
    if (last)
    {
      *p = '\0';
      len = p - uri;
    }
    else
    {
      memmove(p, q + 1, len - ((q + 1) - uri) + 2);
      len -= (q + 1) - p;
    }
    if (redirect != NULL)
    {
      *redirect = 1;
    }
  }

  return 0;
}

static enum status add_virt_host_prefix(char uri[PATH_MAX], int *redirect,
                                        const struct server *srv,
                                        const struct resp_t *res)
{
  return 0;
}

static enum status apply_prefix_mapping_to_path(char uri[PATH_MAX],
                                                int *redirect,
                                                const struct server *srv,
                                                const struct resp_t *res)
{
  return 0;
}

static enum status ensure_dirslash(char uri[PATH_MAX], int *redirect)
{
  size_t len;

  /* append '/' to URI if not present */
  len = strlen(uri);
  if (len + 1 + 1 > PATH_MAX)
  {
    return STATUS_INTERNAL_SERVER_ERROR;
  }
  if (len > 0 && uri[len - 1] != '/')
  {
    uri[len] = '/';
    uri[len + 1] = '\0';
    if (redirect != NULL)
    {
      *redirect = 1;
    }
  }

  return 0;
}

static enum status handle_range(const char *str, size_t size, size_t *lower,
                                size_t *upper)
{
  char first[FIELD_MAX], last[FIELD_MAX];
  const char *start_first, *start_last, *end, *err;
  /*
   *  byte-range=first-last\0
   *             ^     ^   ^
   *             |     |   |
   *             sf     sl   e
   */

  /* default to the complete range */
  *lower = 0;
  *upper = size - 1;

  /* done if no range-string is given */
  if (str == NULL || *str == '\0')
  {
    return 0;
  }

  /* skip opening statement */
  if (strncmp(str, "bytes=", sizeof("bytes=") - 1))
  {
    return STATUS_INTERNAL_SERVER_ERROR;
  }
  start_first = str + (sizeof("bytes=") - 1);

  /* check string (should only contain numbers and a hyphen) */
  for (end = start_first, start_last = NULL; *end != '\0'; end++)
  {
    if (*end < '0' || *end > '9')
    {
      if (*end == '-')
      {
        if (start_last != NULL)
        {
          /* we have already seen a hyphen */
          return STATUS_INTERNAL_SERVER_ERROR;
        }
        else
        {
          /* place q after the hyphen */
          start_last = end + 1;
        }
      }
      else
      {
        return STATUS_INTERNAL_SERVER_ERROR;
      }
    }
  }
  if (start_last == NULL)
  {
    /* the input string must contain a hyphen */
    return STATUS_INTERNAL_SERVER_ERROR;
  }
  end = start_last + strlen(start_last);

  if ((size_t)((start_last - 1) - start_first + 1) > sizeof(first) ||
      (size_t)(end - start_last + 1) > sizeof(last))
  {
    return STATUS_INTERNAL_SERVER_ERROR;
  }
  memcpy(first, start_first, (start_last - 1) - start_first);
  first[(start_last - 1) - start_first] = '\0';
  memcpy(last, start_last, end - start_last);
  last[end - start_last] = '\0';

  if (first[0] != '\0')
  {
    /*
     * range has format "first-last" or "first-",
     * i.e. return bytes 'first' to 'last' (or the
     * last byte if 'last' is not given),
     * inclusively, and byte-numbering beginning at 0
     */
    *lower = string_to_num(first, 0, MIN(SIZE_MAX, LLONG_MAX), &err);
    if (!err)
    {
      if (last[0] != '\0')
      {
        *upper = string_to_num(last, 0, MIN(SIZE_MAX, LLONG_MAX), &err);
      }
      else
      {
        *upper = size - 1;
      }
    }
    if (err)
    {
      /* one of the strtonum()'s failed */
      return STATUS_INTERNAL_SERVER_ERROR;
    }

    /* check ranges */
    if (*lower > *upper || *lower >= size)
    {
      return STATUS_INTERNAL_SERVER_ERROR;
    }

    /* adjust upper limit to be at most the last byte */
    *upper = MIN(*upper, size - 1);
  }
  else
  {
    /* last must not also be empty */
    if (last[0] == '\0')
    {
      return STATUS_INTERNAL_SERVER_ERROR;
    }

    /*
     * Range has format "-num", i.e. return the 'num'
     * last bytes
     */

    /*
     * use upper as a temporary storage for 'num',
     * as we know 'upper' is size - 1
     */
    *upper = string_to_num(last, 0, MIN(SIZE_MAX, LLONG_MAX), &err);
    if (err)
    {
      return STATUS_INTERNAL_SERVER_ERROR;
    }

    /* determine lower */
    if (*upper > size)
    {
      /* more bytes requested than we have */
      *lower = 0;
    }
    else
    {
      *lower = size - *upper;
    }

    /* set upper to the correct value */
    *upper = size - 1;
  }

  return 0;
}

void prepare_resp_http(const struct req_t *req, struct resp_t *res,
                       const struct server *srv)
{
  enum status s, tmps;
  struct in6_addr addr;
  struct stat st;
  struct tm tm = {0};
  size_t i;
  int redirect, hasport, ipv6host;
  static char tmppath[PATH_MAX];
  char *p, *mime;

  /* empty all response fields */
  memset(res, 0, sizeof(*res));

  /* copy request-path to response-path and clean it up */
  redirect = 0;
  memcpy(res->m_path, req->m_path,
         MIN(sizeof(res->m_path), sizeof(req->m_path)));
  if ((tmps = norm_path(res->m_path, &redirect)) ||
      (tmps = add_virt_host_prefix(res->m_path, &redirect, srv, res)) ||
      (tmps = apply_prefix_mapping_to_path(res->m_path, &redirect, srv, res)) ||
      (tmps = norm_path(res->m_path, &redirect)))
  {
    s = tmps;
    goto err;
  }

  /* reject all non-well-known hidden targets (see RFC 8615) */
  if (strstr(res->m_path, "/.") &&
      strncmp(res->m_path, "/.well-known/", sizeof("/.well-known/") - 1))
  {
    s = STATUS_FORBIDDEN;
    goto err;
  }

  /*
   * generate and stat internal path based on the cleaned up request
   * path and the virtual host while ignoring query and fragment
   * (valid according to RFC 3986)
   */
  if (esnprintf(res->m_internal_path, sizeof(res->m_internal_path), "/%s/%s",
                "", res->m_path))
  {
    s = STATUS_INTERNAL_SERVER_ERROR;
    goto err;
  }
  if ((tmps = norm_path(res->m_internal_path, NULL)))
  {
    s = tmps;
    goto err;
  }
  if (stat(res->m_internal_path, &st) < 0)
  {
    s = (errno == EACCES) ? STATUS_FORBIDDEN : STATUS_NOT_FOUND;
    goto err;
  }

  /*
   * if the path points at a directory, make sure both the path
   * and internal path have a trailing slash
   */
  if (S_ISDIR(st.st_mode))
  {
    if ((tmps = ensure_dirslash(res->m_path, &redirect)) ||
        (tmps = ensure_dirslash(res->m_internal_path, NULL)))
    {
      s = tmps;
      goto err;
    }
  }

  if (S_ISDIR(st.st_mode))
  {
    /*
     * when we serve a directory, we first check if there
     * exists a directory index. If not, we either make
     * a directory listing (if enabled) or send an error
     */

    /*
     * append docindex to internal_path temporarily
     * (internal_path is guaranteed to end with '/')
     */
    if (esnprintf(tmppath, sizeof(tmppath), "%s%s", res->m_internal_path,
                  srv->doc_idx))
    {
      s = STATUS_INTERNAL_SERVER_ERROR;
      goto err;
    }

    /* stat the temporary path, which must be a regular file */
    if (stat(tmppath, &st) < 0 || !S_ISREG(st.st_mode))
    {
      if (srv->list_directories)
      {
        /* serve directory listing */

        /* check if directory is accessible */
        if (access(res->m_internal_path, R_OK) != 0)
        {
          s = STATUS_FORBIDDEN;
          log_info("Directory was inaccessible\n");
          goto err;
        }
        else
        {
          res->m_status = STATUS_OK;
        }
        res->m_type = RESTYPE_DIRLISTING;

        if (esnprintf(res->m_field[RES_CONTENT_TYPE],
                      sizeof(res->m_field[RES_CONTENT_TYPE]), "%s",
                      "text/html; charset=utf-8"))
        {
          s = STATUS_INTERNAL_SERVER_ERROR;
          goto err;
        }

        return;
      }
      else
      {
        /* reject */
        if (!S_ISREG(st.st_mode) || errno == EACCES)
        {
          log_info("S_ISREG is forbidden after errno EACCES\n");
          s = STATUS_FORBIDDEN;
        }
        else
        {
          s = STATUS_NOT_FOUND;
        }
        goto err;
      }
    }
    else
    {
      /* the docindex exists; copy tmppath to internal path */
      if (esnprintf(res->m_internal_path, sizeof(res->m_internal_path), "%s",
                    tmppath))
      {
        s = STATUS_INTERNAL_SERVER_ERROR;
        goto err;
      }
    }
  }

  /* modified since */
  if (req->m_field[REQ_IF_MODIFIED_SINCE][0])
  {
    /* parse field */
    if (!strptime(req->m_field[REQ_IF_MODIFIED_SINCE], "%a, %d %b %Y %T GMT",
                  &tm))
    {
      s = STATUS_INTERNAL_SERVER_ERROR;
      goto err;
    }

    /* compare with last modification date of the file */
    if (difftime(st.st_mtim.tv_sec, timegm(&tm)) <= 0)
    {
      res->m_status = STATUS_NOT_MODIFIED;
      log_info("Not modified status 304\n");
      return;
    }
  }

  /* range */
  if ((s = handle_range(req->m_field[REQ_RANGE], st.st_size,
                        &(res->m_file.lower), &(res->m_file.upper))))
  {
    if (s == STATUS_INTERNAL_SERVER_ERROR)
    {
      res->m_status = STATUS_INTERNAL_SERVER_ERROR;

      if (esnprintf(res->m_field[RES_CONTENT_RANGE],
                    sizeof(res->m_field[RES_CONTENT_RANGE]), "bytes */%zu",
                    st.st_size))
      {
        s = STATUS_INTERNAL_SERVER_ERROR;
        goto err;
      }

      return;
    }
    else
    {
      goto err;
    }
  }

  /* mime */
  mime = "application/octet-stream";
  if ((p = strrchr(res->m_internal_path, '.')))
  {
    for (i = 0; i < LEN(mime_types_list); i++)
    {
      if (!strcmp(mime_types_list[i].extension, p + 1))
      {
        mime = mime_types_list[i].typestr;
        break;
      }
    }
  }

  /* fill response struct */
  res->m_type = RESTYPE_FILE;

  /* check if file is readable */
  if (access(res->m_internal_path, R_OK))
  {
    res->m_status = STATUS_FORBIDDEN;
    log_info("Access forbidden file\n");
  }
  else
  {
    res->m_status = STATUS_OK;
  }

  if (esnprintf(res->m_field[RES_ACCEPT_RANGES],
                sizeof(res->m_field[RES_ACCEPT_RANGES]), "%s", "bytes"))
  {
    s = STATUS_INTERNAL_SERVER_ERROR;
    goto err;
  }

  if (esnprintf(res->m_field[RES_CONTENT_LENGTH],
                sizeof(res->m_field[RES_CONTENT_LENGTH]), "%zu",
                res->m_file.upper - res->m_file.lower + 1))
  {
    s = STATUS_INTERNAL_SERVER_ERROR;
    goto err;
  }
  if (req->m_field[REQ_RANGE][0] != '\0')
  {
    if (esnprintf(res->m_field[RES_CONTENT_RANGE],
                  sizeof(res->m_field[RES_CONTENT_RANGE]), "bytes %zd-%zd/%zu",
                  res->m_file.lower, res->m_file.upper, st.st_size))
    {
      s = STATUS_INTERNAL_SERVER_ERROR;
      goto err;
    }
  }
  if (esnprintf(res->m_field[RES_CONTENT_TYPE],
                sizeof(res->m_field[RES_CONTENT_TYPE]), "%s", mime))
  {
    s = STATUS_INTERNAL_SERVER_ERROR;
    goto err;
  }
  if (get_time_stamp(res->m_field[RES_LAST_MODIFIED],
                     sizeof(res->m_field[RES_LAST_MODIFIED]),
                     st.st_mtim.tv_sec))
  {
    s = STATUS_INTERNAL_SERVER_ERROR;
    goto err;
  }

  return;
err:
  prepare_err_resp_http(req, res, s);
}

void prepare_err_resp_http(const struct req_t *req, struct resp_t *res,
                           enum status s)
{
  /* used later */
  (void)req;

  /* empty all response fields */
  memset(res, 0, sizeof(*res));

  res->m_type = RESTYPE_ERROR;
  res->m_status = s;

  if (esnprintf(res->m_field[RES_CONTENT_TYPE],
                sizeof(res->m_field[RES_CONTENT_TYPE]),
                "text/html; charset=utf-8"))
  {
    res->m_status = STATUS_INTERNAL_SERVER_ERROR;
  }

  if (res->m_status == STATUS_METHOD_NOT_ALLOWED)
  {
    if (esnprintf(res->m_field[RES_ALLOW], sizeof(res->m_field[RES_ALLOW]),
                  "Allow: GET, HEAD"))
    {
      res->m_status = STATUS_INTERNAL_SERVER_ERROR;
    }
  }
}
