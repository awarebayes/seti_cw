#pragma once

#include "http.h"
#include "util.h"

extern enum status (*const data_fct[])(const struct resp_t *,
                                       struct my_buffer *, size_t *);

enum status prepare_dir_listing_buffer(const struct resp_t *,
                                       struct my_buffer *, size_t *);
enum status prepare_file_buffer(const struct resp_t *, struct my_buffer *,
                                size_t *);
enum status prepare_error_buffer(const struct resp_t *, struct my_buffer *,
                                 size_t *);
