#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "buffer.h"
#include "http.h"
#include "util.h"

enum status (*const data_fct[])(const struct resp_t *, struct my_buffer *,
                                size_t *) = {
    [RESTYPE_DIRLISTING] = prepare_dir_listing_buffer,
    [RESTYPE_ERROR] = prepare_error_buffer,
    [RESTYPE_FILE] = prepare_file_buffer,
};

static int compareent(const struct dirent **d1, const struct dirent **d2)
{
  int v;

  v = ((*d2)->d_type == DT_DIR ? 1 : -1) - ((*d1)->d_type == DT_DIR ? 1 : -1);
  if (v)
  {
    return v;
  }

  return strcmp((*d1)->d_name, (*d2)->d_name);
}

static char *suffix(int t)
{
  switch (t)
  {
  case DT_FIFO:
    return "|";
  case DT_DIR:
    return "/";
  case DT_LNK:
    return "@";
  case DT_SOCK:
    return "=";
  }

  return "";
}

static void html_escape(const char *src, char *dst, size_t dst_siz)
{
  const struct
  {
    char c;
    char *s;
  } escape[] = {
      {'&', "&amp;"},
      {'<', "&lt;"},
      {'>', "&gt;"},
      {'"', "&quot;"},
      {'\'', "&#x27;"},
  };
  size_t i, j, k, esclen;

  for (i = 0, j = 0; src[i] != '\0'; i++)
  {
    for (k = 0; k < LEN(escape); k++)
    {
      if (src[i] == escape[k].c)
      {
        break;
      }
    }
    if (k == LEN(escape))
    {

      if (j == dst_siz - 1)
      {
        break;
      }
      else
      {
        dst[j++] = src[i];
      }
    }
    else
    {
      esclen = strlen(escape[k].s);

      if (j >= dst_siz - esclen)
      {
        break;
      }
      else
      {
        memcpy(&dst[j], escape[k].s, esclen);
        j += esclen;
      }
    }
  }
  dst[j] = '\0';
}

enum status prepare_dir_listing_buffer(const struct resp_t *res,
                                       struct my_buffer *buf,
                                       size_t *progress)
{
  enum status s = 0;
  struct dirent **e;
  size_t i;
  int dirlen;
  char esc[PATH_MAX * 10];
  char esc_abspath[PATH_MAX * 10];
  char abspath[PATH_MAX * 10];

  memset(buf, 0, sizeof(*buf));

  if ((dirlen = scandir(res->m_internal_path, &e, NULL, compareent)) < 0)
  {
    return STATUS_FORBIDDEN;
  }

  if (*progress == 0)
  {
    html_escape(res->m_path, esc, MIN(PATH_MAX, sizeof(esc)));
    if (buffer_append(buf,
                      "<!DOCTYPE html>\n<html>\n\t<head>"
                      "<title>Index of %s</title></head>\n"
                      "\t<body>\n\t\t<a href=\"..\">..</a>",
                      esc) < 0)
    {
      s = STATUS_INTERNAL_SERVER_ERROR; // Timeout
      goto cleanup;
    }
  }

  for (i = *progress; i < (size_t)dirlen; i++)
  {
    if (e[i]->d_name[0] == '.')
    {
      continue;
    }

    html_escape(e[i]->d_name, esc, sizeof(esc));

    memset(abspath, 0, sizeof(*abspath));
    sprintf(abspath, "%s%s", res->m_internal_path, e[i]->d_name);
    html_escape(abspath, esc_abspath, sizeof(esc_abspath));
    if (buffer_append(buf, "<br />\n\t\t<a href=\"%s%s\">%s%s</a>", esc_abspath,
                      (e[i]->d_type == DT_DIR) ? "/" : "", esc,
                      suffix(e[i]->d_type)))
    {
      break;
    }
  }
  *progress = i;

  if (*progress == (size_t)dirlen)
  {
    if (buffer_append(buf, "\n\t</body>\n</html>\n") < 0)
    {
      s = STATUS_INTERNAL_SERVER_ERROR;
      goto cleanup;
    }
    (*progress)++;
  }

cleanup:
  while (dirlen--)
  {
    free(e[dirlen]);
  }
  free(e);

  return s;
}

enum status prepare_error_buffer(const struct resp_t *res,
                                 struct my_buffer *buf, size_t *progress)
{
  memset(buf, 0, sizeof(*buf));

  if (*progress == 0)
  {
    if (buffer_append(buf,
                      "<!DOCTYPE html>\n<html>\n\t<head>\n"
                      "\t\t<title>%d %s</title>\n\t</head>\n"
                      "\t<body>\n\t\t<h1>%d %s</h1>\n"
                      "\t</body>\n</html>\n",
                      res->m_status, status_str[res->m_status], res->m_status,
                      status_str[res->m_status]))
    {
      return STATUS_INTERNAL_SERVER_ERROR;
    }
    (*progress)++;
  }

  return 0;
}

enum status prepare_file_buffer(const struct resp_t *res, struct my_buffer *buf,
                                size_t *progress)
{
  FILE *fp;
  enum status s = 0;
  ssize_t r;
  size_t remaining;

  memset(buf, 0, sizeof(*buf));

  if (!(fp = fopen(res->m_internal_path, "r")))
  {
    s = STATUS_FORBIDDEN;
    goto cleanup;
  }

  if (fseek(fp, res->m_file.lower + *progress, SEEK_SET))
  {
    s = STATUS_INTERNAL_SERVER_ERROR;
    goto cleanup;
  }

  remaining = res->m_file.upper - res->m_file.lower + 1 - *progress;
  while ((r = fread(buf->data + buf->length, 1,
                    MIN(sizeof(buf->data) - buf->length, remaining), fp)))
  {
    if (r < 0)
    {
      s = STATUS_INTERNAL_SERVER_ERROR;
      goto cleanup;
    }
    buf->length += r;
    *progress += r;
    remaining -= r;
  }

cleanup:
  if (fp)
  {
    fclose(fp);
  }

  return s;
}
