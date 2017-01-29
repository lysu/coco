#include "cc_epoll.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <string.h>

#ifndef __APPLE__

int	co_epoll_create(size_t size) {
    return epoll_create(size);
}

struct co_epoll_res *co_epoll_res_alloc(int n) {
    struct co_epoll_res * ptr = malloc(sizeof(struct co_epoll_res));
    ptr->size = n;
    ptr->events = calloc(n, sizeof(struct epoll_event));
    return ptr;
}

int	co_epoll_wait(int epfd, struct co_epoll_res *events, int maxevents, int timeout) {
    return epoll_wait(epfd, events->events, maxevents, timeout);
}

int	co_epoll_ctl(int epfd, int op, int fd, struct epoll_event * ev) {
	return epoll_ctl(epfd, op, fd, ev);
}

void co_epoll_res_free(struct co_epoll_res *ptr) {
    if(!ptr) {
        return;
    }
	if(ptr->events) {
	    free(ptr->events);
    }
	free(ptr);
}

#else

#define ROW_SIZE 1024
#define COL_SIZE 1024

struct fd_map { // million of fd , 1024 * 1024
    void **m_pp[ 1024 ];
};

struct kevent_pair_t {
    int fire_idx;
    int events;
    uint64_t u64;
};

int co_epoll_create(size_t size) {
    return kqueue();
}

struct co_epoll_res *co_epoll_res_alloc(size_t n) {
    struct co_epoll_res *ptr = malloc(sizeof(struct co_epoll_res));
    ptr->size = n;
    ptr->events = calloc(n, sizeof(struct epoll_event));
    ptr->eventlist = calloc(n, sizeof(struct kevent));
    return ptr;
}

int co_epoll_wait(int epfd, struct co_epoll_res *events, int maxevents, int timeout) {
    struct timespec t = {0};
    if (timeout > 0) {
        t.tv_sec = timeout;
    }
    int ret = kevent(epfd,
                     NULL, 0, //register null
                     events->eventlist, maxevents,//just retrieval
                     (-1 == timeout) ? NULL : &t);
    int j = 0;
    for (int i = 0; i < ret; i++) {
        struct kevent *kev = &events->eventlist[i];
        struct kevent_pair_t *ptr = kev->udata;
        struct epoll_event *ev = events->events + i;
        if (0 == ptr->fire_idx) {
            ptr->fire_idx = i + 1;
            memset(ev, 0, sizeof(*ev));
            ++j;
        } else {
            ev = events->events + ptr->fire_idx - 1;
        }
        if (EVFILT_READ == kev->filter) {
            ev->events |= EPOLLIN;
        } else if (EVFILT_WRITE == kev->filter) {
            ev->events |= EPOLLOUT;
        }
        ev->data.u64 = ptr->u64;
    }
    for (int i = 0; i < ret; i++) {
        ((struct kevent_pair_t *) (events->eventlist[i].udata))->fire_idx = 0;
    }
    return j;
}

struct fd_map *fd_map_new() {
    return calloc(1, sizeof(struct fd_map));
}

void fd_map_destroy(struct fd_map *fdmap) {
    for(int i = 0; i < sizeof(fdmap->m_pp) / sizeof(fdmap->m_pp[0]); i++) {
        if(fdmap->m_pp[i]) {
            free(fdmap->m_pp[i]);
            fdmap->m_pp[i] = NULL;
        }
    }
}

int fd_map_set(struct fd_map *fdmap, int fd, const void * ptr) {
    int idx = fd / ROW_SIZE;
    if(idx < 0 || idx >= sizeof(fdmap->m_pp)/sizeof(fdmap->m_pp[0])) {
        assert(__LINE__ == 0);
        return -__LINE__;
    }
    if(!fdmap->m_pp[ idx ]) {
        fdmap->m_pp[ idx ] = (void**)calloc(1, sizeof(void*) * COL_SIZE);
    }
    fdmap->m_pp[idx][fd % COL_SIZE] = (void*)ptr;
    return 0;
}

int fd_map_clear(struct fd_map *fdmap, int fd) {
    fd_map_set(fdmap, fd, NULL);
    return 0;
}

void *fd_map_get(struct fd_map *fdmap, int fd) {
    int idx = fd / ROW_SIZE;
    if(idx < 0 || idx >= sizeof(fdmap->m_pp)/sizeof(fdmap->m_pp[0])) {
        return NULL;
    }
    void **lp = fdmap->m_pp[idx];
    if(!lp) return NULL;

    return lp[fd % COL_SIZE];
}

__thread struct fd_map *s_fd_map = NULL;

static inline struct fd_map *get_fd_map() {
    if(!s_fd_map) {
        s_fd_map = fd_map_new();
    }
    return s_fd_map;
}

int co_epoll_del(int epfd, int fd) {
    struct timespec t = {0};
    struct kevent_pair_t *ptr = fd_map_get(get_fd_map(), fd);
    if(!ptr) return 0;
    if (EPOLLIN & ptr->events) {
        struct kevent kev = {0};
        kev.ident = (uintptr_t) fd;
        kev.filter = EVFILT_READ;
        kev.flags = EV_DELETE;
        kevent(epfd, &kev, 1, NULL, 0, &t);
    }
    if (EPOLLOUT & ptr->events) {
        struct kevent kev = {0};
        kev.ident = (uintptr_t) fd;
        kev.filter = EVFILT_WRITE;
        kev.flags = EV_DELETE;
        kevent(epfd, &kev, 1, NULL, 0, &t);
    }
    fd_map_clear(get_fd_map(), fd);
    free(ptr);
    return 0;
}

int co_epoll_ctl(int epfd, int op, int fd, struct epoll_event *ev) {
    if (EPOLL_CTL_DEL == op) {
        return co_epoll_del(epfd, fd);
    }
    int flags = (EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP);
    if (ev->events & ~flags) {
        return -1;
    }
    if(EPOLL_CTL_ADD == op && fd_map_get(get_fd_map(), fd)) {
        errno = EEXIST;
        return -1;
    } else if(EPOLL_CTL_MOD == op && !fd_map_get(get_fd_map(), fd)) {
        errno = ENOENT;
        return -1;
    }

    struct kevent_pair_t *ptr = fd_map_get(get_fd_map(), fd );
    if(!ptr) {
        ptr = calloc(1,sizeof(struct kevent_pair_t));
        fd_map_set(get_fd_map(), fd, ptr);
    }

    int ret = 0;
    struct timespec t = {0};

    if(EPOLL_CTL_MOD == op) {
        if(ptr->events & EPOLLIN) {
            struct kevent kev = {0};
            EV_SET(&kev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
            kevent(epfd, &kev, 1, NULL, 0, &t);
        }
        if(ptr->events & EPOLLOUT) {
            struct kevent kev = {0};
            EV_SET(&kev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
            ret = kevent(epfd, &kev, 1, NULL, 0, &t);
            // printf("delete write ret %d\n",ret );
        }
    }

    do {
        if(ev->events & EPOLLIN) {
            struct kevent kev = {0};
            EV_SET(&kev, fd, EVFILT_READ, EV_ADD, 0, 0, ptr);
            ret = kevent(epfd, &kev, 1, NULL, 0, &t);
            if(ret) break;
        }
        if(ev->events & EPOLLOUT) {
            struct kevent kev = {0};
            EV_SET(&kev, fd, EVFILT_WRITE, EV_ADD, 0, 0, ptr);
            ret = kevent(epfd, &kev, 1, NULL, 0, &t);
            if(ret) break;
        }
    } while(0);

    if(ret) {
        fd_map_clear(get_fd_map(), fd);
        free(ptr);
        return ret;
    }

    ptr->events = ev->events;
    ptr->u64 = ev->data.u64;

    return ret;
}

void co_epoll_res_free(struct co_epoll_res *ptr) {
    if (!ptr) {
        return;
    }
    if (ptr->events) {
        free(ptr->events);
    }
    if (ptr->eventlist) {
        free(ptr->eventlist);
    }
    free(ptr);
}

#endif
