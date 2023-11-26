#pragma once

#include <regex.h>
#include <stddef.h>
#include <time.h>

#include "configuration.h"

struct my_buffer {
  char data[BUFFER_SIZE];
  size_t length;
};

#undef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#undef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#undef LEN
#define LEN(x) (sizeof(x) / sizeof *(x))

extern char *argv0;

void log_warn(const char *fmt, ...);
void log_info(const char *fmt, ...);
void die(const char *fmt, ...);

int esnprintf(char *, size_t, const char *, ...);
int get_time_stamp(char *, size_t, time_t);

int tokenize_space(const char *, char **, size_t);

void *realloc_array(void *, size_t, size_t);
long long string_to_num(const char *, long long, long long, const char **);
int append_before(char *, size_t, const char *);

int buffer_prepend(struct my_buffer *, const char *, ...);
