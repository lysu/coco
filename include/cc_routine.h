#ifndef LIBCOCO_CC_ROUTINE_H
#define LIBCOCO_CC_ROUTINE_H

#include <stdint.h>
#include <sys/poll.h>
#include <pthread.h>
#include <stdlib.h>

typedef struct co_routine_s co_routine_t;
typedef struct share_stack_s share_stack_t;

struct co_routine_attr_s {
    size_t stack_size;
    share_stack_t *share_stack;
};
typedef struct co_routine_attr_s co_routine_attr_t;

static inline void st_co_routine_attr_init(co_routine_attr_t *attr) {
    attr->stack_size = 128 * 1024;
    attr->share_stack = NULL;
}

typedef struct co_epoll_s co_epoll_t;
typedef int (*pfn_co_eventloop_t)(void *);
typedef void *(*pfn_co_routine_t)(void *);

int co_create(co_routine_t **co, const co_routine_attr_t *attr, pfn_co_routine_t routine, void *arg);
void co_resume(co_routine_t *co);
void co_yield(co_routine_t *co);
void co_yield_ct();
void co_release(co_routine_t *co);

co_routine_t *co_self();

int co_poll(co_epoll_t *ctx, struct pollfd fds[], nfds_t nfds, int timeout_ms);
void co_eventloop(co_epoll_t *ct, pfn_co_eventloop_t pfn, void *arg);

void init_hook_sys_call();
void co_enable_hook_sys();
void co_disable_hook_sys();
int co_is_enable_sys_hook();

struct co_cond_s;

struct co_cond_s *co_cond_alloc();
int co_cond_free(struct co_cond_s *cc);

int co_cond_signal(struct co_cond_s *);
int co_cond_broadcast(struct co_cond_s *);
int co_cond_timedwait(struct co_cond_s *,int timeout_ms);

co_epoll_t *co_get_epoll_ct();

#endif //LIBCOCO_CC_ROUTINE_H
