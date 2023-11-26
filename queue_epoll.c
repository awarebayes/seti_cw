#ifdef EPOLLFL
#include <stddef.h>
#include <stdio.h>

#include <sys/epoll.h>

#include "queue.h"
#include "util.h"

int queue_create(void)
{
	int qfd;

	if ((qfd = epoll_create1(0)) < 0)
	{
		warn("epoll_create1:");
	}
	log_info("Created queue %d\n", qfd);

	return qfd;
}

int queue_add_fd(int qfd, int fd, enum queue_event_type t, int shared,
				 const void *data, int is_primary)
{
	(void)is_primary;
	struct epoll_event e;

	if (shared)
	{

		e.events = EPOLLEXCLUSIVE;
	}
	else
	{

		e.events = EPOLLET;
	}

	switch (t)
	{
	case QUEUE_EVENT_IN:
		e.events |= EPOLLIN;
		break;
	case QUEUE_EVENT_OUT:
		e.events |= EPOLLOUT;
		break;
	}
	if (t == QUEUE_EVENT_IN)
	{
		log_info("Added fd %d to queue %d with data to %p to read \n", fd, qfd, data);
	}

	if (t == QUEUE_EVENT_OUT)
	{
		log_info("Added fd %d to queue %d with data to %p to write \n", fd, qfd, data);
	}

	e.data.ptr = (void *)data;

	if (epoll_ctl(qfd, EPOLL_CTL_ADD, fd, &e) < 0)
	{
		warn("epoll_ctl:");
		return -1;
	}
	return 0;
}

int queue_mod_fd(int qfd, int fd, enum queue_event_type t, const void *data)
{
	struct epoll_event e;

	e.events = EPOLLET;

	switch (t)
	{
	case QUEUE_EVENT_IN:
		e.events |= EPOLLIN;
		break;
	case QUEUE_EVENT_OUT:
		e.events |= EPOLLOUT;
		break;
	}

	if (t == QUEUE_EVENT_IN)
	{
		log_info("Modified fd %d to queue %d with data to %p to read \n", fd, qfd, data);
	}

	if (t == QUEUE_EVENT_OUT)
	{
		log_info("Modified fd %d to queue %d with data to %p to write \n", fd, qfd, data);
	}

	e.data.ptr = (void *)data;

	if (epoll_ctl(qfd, EPOLL_CTL_MOD, fd, &e) < 0)
	{
		warn("epoll_ctl:");
		return -1;
	}
	return 0;
}

int queue_rem_fd(int qfd, int fd)
{
	struct epoll_event e;
	log_info("Removed fd: %d\n", fd);

	if (epoll_ctl(qfd, EPOLL_CTL_DEL, fd, &e) < 0)
	{
		warn("epoll_ctl:");
		return -1;
	}
	return 0;
}

ssize_t
queue_wait(int qfd, queue_event *e, size_t elen)
{
	ssize_t nready;

	if ((nready = epoll_wait(qfd, e, elen, -1)) < 0)
	{
		warn("epoll_wait:");
		return -1;
	}
	return nready;
}

void *
queue_event_get_data(const queue_event *e)
{

	log_info("Getting event data for %d fd got %p\n", e->data.fd, e->data.ptr);
	return e->data.ptr;
}

int queue_event_is_error(const queue_event *e)
{
	return (e->events & ~(EPOLLIN | EPOLLOUT)) ? 1 : 0;
}
#endif