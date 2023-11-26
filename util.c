#include "util.h"
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

char *argv0;

int get_time_stamp(char *buf, size_t len, time_t t)
{
  struct tm tm;

  if (gmtime_r(&t, &tm) == NULL ||
      strftime(buf, len, "%a, %d %b %Y %T GMT", &tm) == 0)
  {
    return 1;
  }

  return 0;
}

static void log_err(const char *fmt, va_list ap)
{
  vfprintf(stderr, fmt, ap);

  if (fmt[0] && fmt[strlen(fmt) - 1] == ':')
  {
    fputc(' ', stderr);
    perror(NULL);
  }
  else
  {
    fputc('\n', stderr);
  }
}

void log_warn(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  log_err(fmt, ap);
  va_end(ap);
}

void log_info(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

void die(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  log_err(fmt, ap);
  va_end(ap);

  exit(1);
}

int esnprintf(char *str, size_t size, const char *fmt, ...)
{
  va_list ap;
  int ret;

  va_start(ap, fmt);
  ret = vsnprintf(str, size, fmt, ap);
  va_end(ap);

  return (ret < 0 || (size_t)ret >= size);
}

int tokenize_space(const char *s, char **t, size_t tlen)
{
  const char *tok;
  size_t i, j, toki, spaces;

  for (i = 0; i < tlen; i++)
  {
    t[i] = NULL;
  }
  toki = 0;

  if (!s || *s == ' ')
  {
    return 1;
  }
start:

  for (; *s == ' '; s++)
    ;

  if (*s == '\0')
  {
    goto err;
  }

  for (tok = s, spaces = 0;; s++)
  {
    if (*s == '\\' && *(s + 1) == ' ')
    {
      spaces++;
      s++;
      continue;
    }
    else if (*s == ' ')
    {

      goto token;
    }
    else if (*s == '\0')
    {

      goto token;
    }
  }
token:
  if (toki >= tlen)
  {
    goto err;
  }
  if (!(t[toki] = malloc(s - tok - spaces + 1)))
  {
    die("malloc:");
  }
  for (i = 0, j = 0; j < s - tok - spaces + 1; i++, j++)
  {
    if (tok[i] == '\\' && tok[i + 1] == ' ')
    {
      i++;
    }
    t[toki][j] = tok[i];
  }
  t[toki][s - tok - spaces] = '\0';
  toki++;

  if (*s == ' ')
  {
    s++;
    goto start;
  }

  return 0;
err:
  for (i = 0; i < tlen; i++)
  {
    free(t[i]);
    t[i] = NULL;
  }

  return 1;
}

int append_before(char *str, size_t size, const char *prefix)
{
  size_t len = strlen(str), prefixlen = strlen(prefix);

  if (len + prefixlen + 1 > size)
  {
    return 1;
  }

  memmove(str + prefixlen, str, len + 1);
  memcpy(str, prefix, prefixlen);

  return 0;
}

#define INVALID 1
#define TOOSMALL 2
#define TOOLARGE 3


#define MUL_NO_OVERFLOW ((size_t)1 << (sizeof(size_t) * 4))

void *realloc_array(void *optr, size_t nmemb, size_t size)
{
  if ((nmemb >= MUL_NO_OVERFLOW || size >= MUL_NO_OVERFLOW) && nmemb > 0 &&
      SIZE_MAX / nmemb < size)
  {
    errno = ENOMEM;
    return NULL;
  }
  return realloc(optr, size * nmemb);
}

int buffer_append(struct my_buffer *buf, const char *suffixfmt, ...)
{
  va_list ap;
  int ret;

  va_start(ap, suffixfmt);
  ret = vsnprintf(buf->data + buf->length, sizeof(buf->data) - buf->length,
                  suffixfmt, ap);
  va_end(ap);

  if (ret < 0 || (size_t)ret >= (sizeof(buf->data) - buf->length))
  {

    memset(buf->data + buf->length, 0, sizeof(buf->data) - buf->length);
    return 1;
  }

  buf->length += ret;

  return 0;
}

long long string_to_num(const char *numstr, long long minval, long long maxval,
                        const char **errstrp)
{
  long long ll = 0;
  int error = 0;
  char *ep;
  struct errval
  {
    const char *errstr;
    int err;
  } ev[4] = {
      {NULL, 0},
      {"invalid", EINVAL},
      {"too small", ERANGE},
      {"too large", ERANGE},
  };

  ev[0].err = errno;
  errno = 0;
  if (minval > maxval)
  {
    error = INVALID;
  }
  else
  {
    ll = strtoll(numstr, &ep, 10);
    if (numstr == ep || *ep != '\0')
      error = INVALID;
    else if ((ll == LLONG_MIN && errno == ERANGE) || ll < minval)
      error = TOOSMALL;
    else if ((ll == LLONG_MAX && errno == ERANGE) || ll > maxval)
      error = TOOLARGE;
  }
  if (errstrp != NULL)
    *errstrp = ev[error].errstr;
  errno = ev[error].err;
  if (error)
    ll = 0;

  return ll;
}
