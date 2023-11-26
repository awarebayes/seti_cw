#include "queue_impl.h"
#ifdef EPOLLFL
#include "queue_epoll.c"
#else
#include "queue_select.c"
#endif