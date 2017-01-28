#ifndef LIBCOCO_CC_EPOLL_H
#define LIBCOCO_CC_EPOLL_H

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#ifndef __APPLE__

#include <sys/epoll.h>

struct co_epoll_res {
    int size;
    struct epoll_event *events;
    struct kevent *eventlist;
};

int 	co_epoll_wait(int epfd,struct co_epoll_res *events,int maxevents,int timeout);
int 	co_epoll_ctl(int epfd,int op,int fd,struct epoll_event *);
int 	co_epoll_create(size_t size);
struct 	co_epoll_res *co_epoll_res_alloc(int n);
void 	co_epoll_res_free(struct co_epoll_res *);

#else

#include <sys/event.h>

enum EPOLL_EVENTS {
    EPOLLIN = 0X001,
    EPOLLPRI = 0X002,
    EPOLLOUT = 0X004,
    EPOLLERR = 0X008,
    EPOLLHUP = 0X010,
    EPOLLRDNORM = 0x40,
    EPOLLWRNORM = 0x004,
};

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

typedef union epoll_data {
    void *ptr;
    int fd;
    uint32_t u32;
    uint64_t u64;

} epoll_data_t;

struct epoll_event {
    uint32_t events;
    epoll_data_t data;
};

struct co_epoll_res {
    size_t size;
    struct epoll_event *events;
    struct kevent *eventlist;
};

int co_epoll_wait(int epfd, struct co_epoll_res *events, int maxevents, int timeout);
int co_epoll_ctl(int epfd, int op, int fd, struct epoll_event *);
int co_epoll_create(size_t size);
struct co_epoll_res *co_epoll_res_alloc(size_t n);
void co_epoll_res_free(struct co_epoll_res *);

#endif

#endif //LIBCOCO_CC_EPOLL_H
