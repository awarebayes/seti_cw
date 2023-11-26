#pragma once
#define FIELD_MAX 200
#define BUFFER_SIZE 8192

static struct {
  char *extension;
  char *typestr;
} mime_types_list[] = {
    {"html", "text/html; charset=utf-8"},
    {"svg", "image/svg+xml; charset=utf-8"},
    {"txt", "text/plain; charset=utf-8"},
    {"tar", "application/tar"},
    {"jpeg", "image/jpg"},
    {"webm", "video/webm"},
    {"jpg", "image/jpg"},
    {"css", "text/css; charset=utf-8"},
    {"gz", "application/x-gtar"},
    {"gif", "image/gif"},
    {"pdf", "application/x-pdf"},
    {"md", "text/plain; charset=utf-8"},
    {"png", "image/png"},
    {"mp4", "video/mp4"},
    {"js", "text/javascript; charset=utf-8"},
    {"swf", "application/x-shockwave-flash"},
};
