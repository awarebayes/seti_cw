#pragma once

#include <regex.h>
#include <stddef.h>

struct virtual_host
{
	char *dir;
	char *chost;
	char *prefix;
	regex_t re;
	char *regex;
};

struct to_from
{
	char *from;
	char *to;
	char *chost;
};

struct server
{
	size_t map_len;
	char *port;
	char *host;
	size_t virtual_host_len;
	char *doc_idx;
	struct to_from *to_from_map;
	int list_directories;
};

void init_thread_pool_for_server(int, size_t, size_t, const struct server *);
