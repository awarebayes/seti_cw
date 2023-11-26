#include "queue.h"
#include <sys/select.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>

#define MAX_QUEUES 10
#define MAX_FD_PER_QUEUE 1024

static fd_set readfds[MAX_QUEUES] = {0};
static fd_set writefds[MAX_QUEUES] = {0};
static int num_fds[MAX_QUEUES] = {0};
static int maxim_filedesc[MAX_QUEUES] = {0};
static pthread_mutex_t num_queues_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct
{
    int queue_id;
    int fd;
    enum queue_event_type type;
    void *data;
} file_desc_info;

static file_desc_info fd_array[MAX_QUEUES][MAX_FD_PER_QUEUE];

static int num_queues = 0; // Number of queues created

int reset_queue_on_ddos(int qfd) 
{
}

int isFdOpen(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1)
    {
        // An error occurred; the file descriptor is likely closed.
        return 0;
    }
    else
    {
        // No error; the file descriptor is open.
        return 1;
    }
}

void print_fdarray(int queue)
{
    for (int i = 0; i < 10; i++)
    {
        file_desc_info fdf = fd_array[queue][i];
    }
}

int queue_create(void)
{
    int new_queue_index = -1;

    pthread_mutex_lock(&num_queues_mutex);

    if (num_queues < MAX_QUEUES)
    {
        // Initialize the read and write sets for the new queue
        FD_ZERO(&readfds[num_queues]);
        FD_ZERO(&writefds[num_queues]);
        maxim_filedesc[num_queues] = -1;
        num_fds[num_queues] = 0;

        new_queue_index = num_queues;
        num_queues++;

        log_info("Created queue %d\n", new_queue_index);
    }

    pthread_mutex_unlock(&num_queues_mutex);

    return num_queues - 1; // Return the index of the created queue
}

int queue_add_fd(int qfd, int fd, enum queue_event_type type, int shared, const void *data)
{

    if (qfd < 0 || qfd >= num_queues)
    {
        return -1; // Invalid queue index
    }

    if (num_fds[qfd] >= MAX_FD_PER_QUEUE)
    {
        return -1; // Maximum number of file descriptors reached for this queue
    }

    if (fd > maxim_filedesc[qfd])
    {
        maxim_filedesc[qfd] = fd;
    }

    if (type == QUEUE_EVENT_IN)
    {
        FD_SET(fd, &readfds[qfd]);
    }
    else if (type == QUEUE_EVENT_OUT)
    {
        FD_SET(fd, &writefds[qfd]);
    }

    // You can store additional information about the file descriptor
    // and its event type as needed for your application.

    file_desc_info info;
    info.queue_id = qfd;
    info.fd = fd;
    info.type = type;
    info.data = (void *)data;
    fd_array[qfd][num_fds[qfd]] = info;

    num_fds[qfd]++;

    if (type == QUEUE_EVENT_IN)
    {
        log_info("Added fd %d to queue %d with data to %p (%d) to read \n", fd, qfd, fd_array[qfd][num_fds[qfd] - 1].data, data);
    }

    if (type == QUEUE_EVENT_OUT)
    {
        log_info("Added fd %d to queue %d with data to %p to write \n", fd, qfd, fd_array[qfd][num_fds[qfd] - 1].data);
    }

    return 0;
}

int queue_mod_fd(int qfd, int fd, enum queue_event_type type, const void *data)
{
    if (qfd < 0 || qfd >= num_queues)
    {
        return -1; // Invalid queue index
    }

    if (fd < 0 || fd > maxim_filedesc[qfd])
    {
        return -1; // Invalid file descriptor
    }

    if (type == QUEUE_EVENT_IN)
    {
        if (!FD_ISSET(fd, &writefds[qfd]))
        {
            FD_SET(fd, &readfds[qfd]);
            FD_CLR(fd, &writefds[qfd]);
        }
        log_info("Modified fd %d to queue %d with data to %p to read \n", fd, qfd, data);
    }

    if (type == QUEUE_EVENT_OUT)
    {
        if (!FD_ISSET(fd, &writefds[qfd]))
        {
            FD_SET(fd, &writefds[qfd]);
            FD_CLR(fd, &readfds[qfd]);
        }
        log_info("Modified fd %d to queue %d with data to %p to write \n", fd, qfd, data);
    }

    int exact_found = 0;
    for (int i = 0; i < num_fds[qfd]; i++)
    {
        if (fd_array[qfd][i].fd == fd)
        {
            // Update the data pointer associated with the file descriptor
            fd_array[qfd][i].data = (void *)data;
            fd_array[qfd][i].type = type;
            exact_found = 1;
            break;
        }
    }

    assert(isFdOpen(fd));

    log_info("Modified\n");
    return 0;
}

int queue_rem_fd(int qfd, int fd)
{
    log_info("Called to remove fd %d from queue %d\n", fd, qfd);
    if (qfd < 0 || qfd >= num_queues)
    {
        return -1; // Invalid queue index
    }

    if (fd < 0 || fd > maxim_filedesc[qfd])
    {
        return -1; // Invalid file descriptor
    }

    if (FD_ISSET(fd, &readfds[qfd]))
    {
        FD_CLR(fd, &readfds[qfd]);
    }

    if (FD_ISSET(fd, &writefds[qfd]))
    {
        FD_CLR(fd, &writefds[qfd]);
    }

    log_info("Going to remove fd %d from queue %d\n", fd, qfd);

    // You may also remove the associated data, but in this example, we keep it intact.

    // Shift the remaining file descriptors to maintain a compact array (optional).
    for (int i = 0; i < num_fds[qfd]; i++)
    {
        if (fd_array[qfd][i].fd == fd)
        {
            memset(&fd_array[qfd][i], sizeof(file_desc_info), 0);
        }
    }

    log_info("Removed fd %d from queue %d\n", fd, qfd);
    num_fds[qfd]--; // Watchme!

    return 0;
}

void log_fd_set(const fd_set *set, int q)
{
    log_info("File Descriptors in the Set for q %d: ", q);
    for (int fd = 0; fd < FD_SETSIZE; fd++)
    {
        if (FD_ISSET(fd, set))
        {
            log_info("%d ", fd);
        }
    }
}

ssize_t queue_wait(int qfd, queue_event *events, size_t event_len)
{
    fd_set readfds_copy;
    fd_set writefds_copy;
    if (qfd < 0 || qfd >= num_queues)
    {
        return -1; // Invalid queue index
    }

    if (event_len == 0)
    {
        return 0; // No events requested
    }

    int timeout = 0;

    while (1) {

        FD_ZERO(&readfds_copy);
        FD_ZERO(&writefds_copy);
        memcpy(&readfds_copy, &readfds[qfd], sizeof(fd_set));
        memcpy(&writefds_copy, &writefds[qfd], sizeof(fd_set));

        int maxfd = maxim_filedesc[qfd] + 1; // Maximum file descriptor

        struct timeval tv;
        tv.tv_usec = 0;
        tv.tv_sec = 1;

        ssize_t nready = pselect(maxfd, &readfds_copy, &writefds_copy, NULL, &tv, NULL);

        // log_info("Got events %d from queue %d\n", nready, qfd);


        if (nready < 0)
        {
            
            log_warn("DDOS Detected! Broke my file descriptor damn\n");
            /*
            close_all_sockets();
            sleep(0.5);
            char *args[] = {"misha_server", NULL};
            int res = execv("misha_server", args);
            */
            remove_closed_fds(qfd);
        } 
        
        if (nready > 0)
        { 
            break;
        }
    }

    ssize_t events_found = 0;

    for (int fd = 0; fd <= maxim_filedesc[qfd] && events_found < event_len; fd++)
    {
        if (FD_ISSET(fd, &readfds_copy))
        {
            events[events_found].events = QUEUE_EVENT_IN;
            events[events_found].fd = fd;        // Populate the data structure
            events[events_found].queue_id = qfd; // Populate the data structure
            events_found++;
        }

        if (FD_ISSET(fd, &writefds_copy))
        {
            events[events_found].events = QUEUE_EVENT_OUT;
            events[events_found].fd = fd;        // Populate the data structure
            events[events_found].queue_id = qfd; // Populate the data structure
            events_found++;
        }
    }

    return events_found;
}

void remove_closed_fds(int q)
{
    for (int i = 0; i < num_fds[q]; i++)
    {
        int fd = fd_array[q][i].fd;
        if (!isFdOpen(fd))
        {
            queue_rem_fd(q, fd);
        }
    }
}

void *queue_event_get_data(const queue_event *event)
{
    log_info("Getting event data for fd %d from queue %d\n", event->fd, event->queue_id);
    // print_fdarray(event->queue_id);
    int q = event->queue_id;
    for (int i = 0; i < num_fds[q]; i++)
    {
        if (fd_array[q][i].fd == event->fd && fd_array[q][i].type == event->events)
        {
            log_info("Getting event data for fd %d got %p\n", event->fd, fd_array[q][i].data);
            void *data = fd_array[q][i].data;
            return data;
        }
    }

    return NULL; // Data not found
}

int queue_event_is_error(const queue_event *event)
{
    // In this implementation, you can consider any event type not equal to IN or OUT as an error.
    // Adjust this logic as needed based on your specific error event handling.
    int is_error = (event->events != QUEUE_EVENT_IN && event->events != QUEUE_EVENT_OUT);

    if (!isFdOpen(event->fd))
    {
        return is_error = 1;
    }

    log_info("Is error call: %d\n", is_error);

    return is_error;
}