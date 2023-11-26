#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "connection.h"
#include "queue.h"
#include "srv.h"
#include "util.h"

struct data_for_worker
{
	int m_in_socket;
	size_t m_num_slots;
	const struct server *m_serv;
};

static void *
create_worker(void *data)
{
	queue_event *event = NULL;
	struct conn_t *connection, *c, *newc;
	struct data_for_worker *d = (struct data_for_worker *)data;
	int queue_fd;
	ssize_t nready;
	size_t i;

	if (!(connection = calloc(d->m_num_slots, sizeof(*connection))))
	{
		die("calloc:");
	}

	if ((queue_fd = queue_create()) < 0)
	{
		exit(1);
	}

	if (queue_add_fd(queue_fd, d->m_in_socket, QUEUE_EVENT_IN, 1, NULL) < 0)
	{
		exit(1);
	}

	if (!(event = realloc_array(event, d->m_num_slots, sizeof(*event))))
	{
		die("reallocarray:");
	}

	for (;;)
	{

		if ((nready = queue_wait(queue_fd, event, d->m_num_slots)) < 0)
		{
			exit(1);
		}

		for (i = 0; i < (size_t)nready; i++)
		{
			c = queue_event_get_data(&event[i]);

			if (queue_event_is_error(&event[i]))
			{
				if (c != NULL)
				{
					queue_rem_fd(queue_fd, c->m_file_descriptor);
					c->m_resp.m_status = 0;
					log_con(c);
					reset_con(c);
				}

				continue;
			}

			if (c == NULL)
			{

				if (!(newc = accept_con(d->m_in_socket,
										connection,
										d->m_num_slots)))
				{
					continue;
				}

				if (queue_add_fd(queue_fd, newc->m_file_descriptor,
								 QUEUE_EVENT_IN,
								 0, newc) < 0)
				{

					continue;
				}
			}
			else
			{

				int cfd = c->m_file_descriptor;

				serve_con(c, d->m_serv);

				if (c->m_file_descriptor == 0)
				{

					queue_rem_fd(queue_fd, cfd);
					memset(c, 0, sizeof(struct conn_t));
					continue;
				}

				switch (c->m_state)
				{
				case CONN_RECV_HEADER:
					if (queue_mod_fd(queue_fd, c->m_file_descriptor,
									 QUEUE_EVENT_IN,
									 c) < 0)
					{
						reset_con(c);
						queue_rem_fd(queue_fd, cfd);
						break;
					}
					break;
				case CONN_SEND_HEADER:
				case CONN_SEND_BODY:
					if (queue_mod_fd(queue_fd, c->m_file_descriptor,
									 QUEUE_EVENT_OUT,
									 c) < 0)
					{

						int fd = c->m_file_descriptor;
						reset_con(c);
						queue_rem_fd(queue_fd, cfd);
						break;
					}
					break;
				default:
					break;
				}
			}
		}
	}

	return NULL;
}

void init_thread_pool_for_server(int in_socket, size_t nthreads, size_t nslots,
								 const struct server *srv)
{
	pthread_t *thread = NULL;
	struct data_for_worker *d = NULL;
	size_t i;

	if (!(d = realloc_array(d, nthreads, sizeof(*d))))
	{
		die("reallocarray:");
	}
	for (i = 0; i < nthreads; i++)
	{
		d[i].m_in_socket = in_socket;
		d[i].m_num_slots = nslots;
		d[i].m_serv = srv;
	}

	if (!(thread = realloc_array(thread, nthreads, sizeof(*thread))))
	{
		die("reallocarray:");
	}
	for (i = 0; i < nthreads; i++)
	{
		if (pthread_create(&thread[i], NULL, create_worker, &d[i]) != 0)
		{
			if (errno == EAGAIN)
			{
				die("You need to run as root or have "
					"CAP_SYS_RESOURCE set, or are trying "
					"to create more threads than the "
					"system can offer");
			}
			else
			{
				die("pthread_create:");
			}
		}
	}

	for (i = 0; i < nthreads; i++)
	{
		if ((errno = pthread_join(thread[i], NULL)))
		{
			log_warn("pthread_join:");
		}
	}
}
