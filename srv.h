#pragma once

#include <regex.h>
#include <stddef.h>

struct server
{
	size_t map_len;
	char *port;
	char *host;
	char *doc_idx;
	int list_directories;
};

void init_thread_pool_for_server(int, size_t, size_t, const struct server *);
