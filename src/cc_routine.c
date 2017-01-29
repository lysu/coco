#include <unistd.h>
#include <sys/syscall.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include "cc_routine.h"
#include "cc_routine_inner.h"
#include "cc_routine_specific.h"
#include "cc_epoll.h"
#include "pthread.h"
#include <stdio.h>

extern void coctx_swap(coctx_t *,coctx_t*);

struct st_co_routine_env_s {
    st_co_routine_t *call_stack[128];
    int call_stack_size;
    st_co_epoll_t *epoll;

    st_co_routine_t *pending_co;
    st_co_routine_t *ocupy_co;
};

static st_co_routine_env_t *g_arr_co_env_per_thread[204800] = {0};

static inline pid_t get_pid() {
    static __thread pid_t pid = 0;
    static __thread pid_t tid = 0;
    if (!pid || !tid || pid != getpid()) {
        pid = getpid();
#if defined( __APPLE__ )
        tid = syscall(SYS_gettid);
        if (-1 == (long) tid) {
            tid = pid;
        }
#else
        tid = syscall( __NR_gettid );
#endif

    }
    return tid;
}

static unsigned long long get_tick_ms() {
#if defined( __LIBCO_RDTSCP__)
    static uint32_t khz = getCpuKhz();
	return counter() / khz;
#else
    struct timeval now = { 0 };
    gettimeofday(&now, NULL);
    unsigned long long u = (unsigned long long) now.tv_sec;
    u *= 1000;
    u += now.tv_usec / 1000;
    return u;
#endif
}

st_stack_mem_t *co_alloc_stackmem(size_t stack_size) {
    st_stack_mem_t *stack_mem = malloc(sizeof(st_stack_mem_t));
    stack_mem->ocupy_co = NULL;
    stack_mem->stack_size = stack_size;
    stack_mem->stack_buffer = malloc(stack_size);
    stack_mem->stack_bp = stack_mem->stack_buffer + stack_size;
    return stack_mem;
}

static st_stack_mem_t *co_get_stackmem(st_share_stack_t *share_stack) {
    if (!share_stack) {
        return NULL;
    }
    int idx = share_stack->alloc_idx % share_stack->count;
    share_stack->alloc_idx++;
    return share_stack->stack_array[idx];
}

#define EPOLL_SIZE 1024 * 10

typedef struct st_timeout_item_link_s st_timeout_item_link_t;

struct st_co_epoll_s {
    int epoll_fd;
    st_timeout_t *timeout;
    st_timeout_item_link_t *timeout_list;
    st_timeout_item_link_t *active_list;
    struct co_epoll_res *result;
};

struct st_timeout_item_link_s {
    st_timeout_item_t *head;
    st_timeout_item_t *tail;
};

#define MAX_TIMEOUT  40 * 1000

typedef void (*pfn_on_prepare_t)( st_timeout_item_t *,struct epoll_event *ev, st_timeout_item_link_t *active);
typedef void (*pfn_on_process_t)( st_timeout_item_t *);

#define TIMEOUT_ITEM_UNSET \
    st_timeout_item_t *prev; \
    st_timeout_item_t *next; \
    st_timeout_item_link_t *link; \
    unsigned long long expire_time; \
    pfn_on_prepare_t pfn_prepare; \
    pfn_on_process_t pfn_process; \
    void *arg; \
    int timeouted;

struct st_timeout_item_s {
    TIMEOUT_ITEM_UNSET
};

struct st_timeout_s {
    st_timeout_item_link_t *items;
    size_t item_size;

    unsigned long long start;
    long long start_idx;
};

void remove_from_timeout_link(st_timeout_item_t *ap) {
    st_timeout_item_link_t *lst = ap->link;
    if(!lst) return ;
    assert( lst->head && lst->tail );

    if(ap == lst->head) {
        lst->head = ap->next;
        if(lst->head) {
            lst->head->prev = NULL;
        }
    } else {
        if(ap->prev) {
            ap->prev->next = ap->next;
        }
    }

    if(ap == lst->tail) {
        lst->tail = ap->prev;
        if(lst->tail) {
            lst->tail->next = NULL;
        }
    } else {
        ap->next->prev = ap->prev;
    }

    ap->prev = ap->next = NULL;
    ap->link = NULL;
}

void add_timeout_tail(st_timeout_item_link_t *ap_link, st_timeout_item_t *ap) {
    if( ap->link ) {
        return ;
    }
    if(ap_link->tail) {
        ap_link->tail->next = ap;
        ap->next = NULL;
        ap->prev = ap_link->tail;
        ap_link->tail = ap;
    } else {
        ap_link->head = ap_link->tail = ap;
        ap->next = ap->prev = NULL;
    }
    ap->link = ap_link;
}

st_timeout_t *alloc_timeout(size_t size) {
    st_timeout_t *lp = calloc(1, sizeof(st_timeout_t));
    lp->item_size = size;
    lp->items = calloc(lp->item_size, sizeof(st_timeout_item_link_t));
    lp->start = get_tick_ms();
    lp->start_idx = 0;
    return lp;
}

void free_timeout(st_timeout_t *ap_timeout) {
    free(ap_timeout->items);
    free(ap_timeout);
}

int add_timeout(st_timeout_t *ap_timeout, st_timeout_item_t *ap_item, uint64_t all_now) {
    if (ap_timeout->start == 0) {
        ap_timeout->start = all_now;
        ap_timeout->start_idx = 0;
    }
    if (all_now < ap_timeout->start) {
        return __LINE__;
    }
    if (ap_item->expire_time < all_now) {
        return __LINE__;
    }
    int diff = (int) (ap_item->expire_time - ap_timeout->start);
    if (diff >= ap_timeout->item_size) {
        return __LINE__;
    }
    size_t slot = (ap_timeout->start_idx + diff) % ap_timeout->item_size;
    add_timeout_tail(ap_timeout->items + slot, ap_item);
    return 0;
}

struct st_poll_item_s {
    TIMEOUT_ITEM_UNSET
    struct pollfd *self;
    struct st_poll_s *poll;
    struct epoll_event st_event;
};
typedef struct st_poll_item_s st_poll_item_t;

struct st_poll_s {
    TIMEOUT_ITEM_UNSET
    struct pollfd *fds;
    nfds_t nfds;
    st_poll_item_t *poll_items;
    int all_event_detach;
    int epoll_fd;
    int raise_cnt;
};
typedef struct st_poll_s st_poll_t;

static uint32_t poll_event2epoll(short events) {
    uint32_t e = 0;
    if( events & POLLIN ) 	e |= EPOLLIN;
    if( events & POLLOUT )  e |= EPOLLOUT;
    if( events & POLLHUP ) 	e |= EPOLLHUP;
    if( events & POLLERR )	e |= EPOLLERR;
    if( events & POLLRDNORM ) e |= EPOLLRDNORM;
    if( events & POLLWRNORM ) e |= EPOLLWRNORM;
    return e;
}

static short epoll_event2poll(uint32_t events) {
    short e = 0;
    if( events & EPOLLIN ) 	e |= POLLIN;
    if( events & EPOLLOUT ) e |= POLLOUT;
    if( events & EPOLLHUP ) e |= POLLHUP;
    if( events & EPOLLERR ) e |= POLLERR;
    if( events & EPOLLRDNORM ) e |= POLLRDNORM;
    if( events & EPOLLWRNORM ) e |= POLLWRNORM;
    return e;
}

void on_poll_process_event(st_timeout_item_t * ap) {
    st_co_routine_t *co = ap->arg;
    co_resume( co );
}

void on_poll_prepare_pfn(st_timeout_item_t * ap, struct epoll_event *e, st_timeout_item_link_t *active) {
    st_poll_item_t *lp = (st_poll_item_t *)ap;
    lp->self->revents = epoll_event2poll(e->events);

    st_poll_t *pPoll = lp->poll;
    pPoll->raise_cnt++;

    if(!pPoll->all_event_detach) {
        pPoll->all_event_detach = 1;
        st_timeout_item_t *t = (st_timeout_item_t *) pPoll;
        remove_from_timeout_link(t);
        add_timeout_tail(active, t);
    }
}

st_co_routine_t *get_curr_co(st_co_routine_env_t *env) {
    return env->call_stack[ env->call_stack_size - 1 ];
}

typedef int (*poll_pfn_t)(struct pollfd fds[], nfds_t nfds, int timeout);
int co_poll_inner(st_co_epoll_t *ctx, struct pollfd fds[], nfds_t nfds, int timeout, poll_pfn_t pollfunc) {
    if (timeout > MAX_TIMEOUT) {
        timeout = MAX_TIMEOUT;
    }
    int epfd = ctx->epoll_fd;
    st_co_routine_t *self = co_self();

    st_poll_t *arg = calloc(1, sizeof(st_poll_t));
    arg->epoll_fd = epfd;
    arg->fds = calloc(nfds, sizeof(struct pollfd));
    arg->nfds = nfds;

    st_poll_item_t *arr = calloc(2, sizeof(st_poll_item_t));
    if (nfds < sizeof(arr) / sizeof(arr[0]) && !self->is_share_stack) {
        arg->poll_items = arr;
    } else {
        arg->poll_items = calloc(nfds, sizeof(st_poll_item_t));
    }
    arg->pfn_process = on_poll_process_event;
    arg->arg = get_curr_co(co_get_curr_thread_env());

    for(nfds_t i = 0; i < nfds; i++) {
        arg->poll_items[i].self = arg->fds + i;
        arg->poll_items[i].poll = arg;

        arg->poll_items[i].pfn_prepare = on_poll_prepare_pfn;
        struct epoll_event *ev = &arg->poll_items[i].st_event;

        if(fds[i].fd > -1) {
            ev->data.ptr = arg->poll_items + i;
            ev->events = poll_event2epoll(fds[i].events);

            int ret = co_epoll_ctl(epfd, EPOLL_CTL_ADD, fds[i].fd, ev);
            if (ret < 0 && errno == EPERM && nfds == 1 && pollfunc != NULL) {
                if( arg->poll_items != arr ) {
                    free( arg->poll_items );
                    arg->poll_items = NULL;
                }
                free(arg->fds);
                free(arg);
                return pollfunc(fds, nfds, timeout);
            }
        }
    }

    unsigned long long now = get_tick_ms();
    arg->expire_time = now + timeout;
    st_timeout_item_t *timeout_item = (st_timeout_item_t *) arg;
    int ret = add_timeout(ctx->timeout, timeout_item, now);
    if(ret != 0) {
        errno = EINVAL;
        if(arg->poll_items != arr) {
            free( arg->poll_items );
            arg->poll_items = NULL;
        }
        free(arg->fds);
        free(arg);
        return -__LINE__;
    }

    co_yield_env( co_get_curr_thread_env() );

    remove_from_timeout_link(timeout_item);
    for(nfds_t i = 0; i < nfds; i++) {
        int fd = fds[i].fd;
        if(fd > -1) {
            co_epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &arg->poll_items[i].st_event);
        }
        fds[i].revents = arg->fds[i].revents;
    }

    int raise_cnt = arg->raise_cnt;
    if(arg->poll_items != arr) {
        free( arg->poll_items );
        arg->poll_items = NULL;
    }

    free(arg->fds);
    free(arg);

    return raise_cnt;
}

st_co_routine_t *get_curr_thread_co() {
    st_co_routine_env_t *env = co_get_curr_thread_env();
    if( !env ) return NULL;
    return get_curr_co(env);
}

st_co_routine_t *co_self() {
    return get_curr_thread_co();
}

struct st_co_cond_t;

struct st_co_cond_item_t {
    struct st_co_cond_item_t *prev;
    struct st_co_cond_item_t *next;
    struct st_co_cond_t *link;

    st_timeout_item_t timeout;
};

struct st_co_cond_t {
    struct st_co_cond_item_t *head;
    struct st_co_cond_item_t *tail;
};

int co_poll(st_co_epoll_t *ctx, struct pollfd fds[], nfds_t nfds, int timeout_ms) {
    return co_poll_inner(ctx, fds, nfds, timeout_ms, NULL);
}

static inline void join(st_timeout_item_link_t* ap_link, st_timeout_item_link_t *ap_other) {
    if( !ap_other->head ) {
        return ;
    }
    st_timeout_item_t *lp = ap_other->head;
    while(lp) {
        lp->link = ap_link;
        lp = lp->next;
    }
    lp = ap_other->head;
    if(ap_link->tail) {
        ap_link->tail->next = lp;
        lp->prev = ap_link->tail;
        ap_link->tail = ap_other->tail;
    } else {
        ap_link->head = ap_other->head;
        ap_link->tail = ap_other->tail;
    }

    ap_other->head = ap_other->tail = NULL;
}

static inline void take_all_timeout(st_timeout_t *ap_timeout, unsigned long long all_now, st_timeout_item_link_t *ap_result) {
    if (ap_timeout->start == 0) {
        ap_timeout->start = all_now;
        ap_timeout->start_idx = 0;
    }
    if (all_now < ap_timeout->start) {
        return;
    }
    size_t cnt = (size_t) (all_now - ap_timeout->start + 1);
    if (cnt > ap_timeout->item_size) {
        cnt = ap_timeout->item_size;
    }
    if (cnt < 0) {
        return;
    }
    for (int i = 0; i < cnt; ++i) {
        int idx = (int) ((ap_timeout->start_idx + i) % ap_timeout->item_size);
        join(ap_result, ap_timeout->items + idx);
    }
    ap_timeout->start = all_now;
    ap_timeout->start_idx += (long long int) (cnt - 1);
}

static inline void pop_head_timeout(st_timeout_item_link_t *ap_link) {
    if(!ap_link->head) {
        return ;
    }
    st_timeout_item_t *lp = ap_link->head;
    if(ap_link->head == ap_link->tail) {
        ap_link->head = ap_link->tail = NULL;
    } else {
        ap_link->head = ap_link->head->next;
    }

    lp->prev = lp->next = NULL;
    lp->link = NULL;

    if(ap_link->head) {
        ap_link->head->prev = NULL;
    }
}

void co_eventloop(st_co_epoll_t *ctx, pfn_co_eventloop_t pfn, void *arg) {
    if (!ctx->result) {
        ctx->result = co_epoll_res_alloc(EPOLL_SIZE);
    }
    struct co_epoll_res *result = ctx->result;

    for (;;) {
        int ret = co_epoll_wait(ctx->epoll_fd, result, EPOLL_SIZE, 1);

        st_timeout_item_link_t *active = ctx->active_list;
        st_timeout_item_link_t *timeout = ctx->timeout_list;

        memset(timeout, 0, sizeof(st_timeout_item_link_t));

        for (int i = 0; i < ret; ++i) {
            st_timeout_item_t *item = result->events[i].data.ptr;
            if (item->pfn_prepare) {
                item->pfn_prepare(item, &result->events[i], active);
            } else {
                add_timeout_tail(active, item);
            }
        }

        unsigned long long now = get_tick_ms();
        take_all_timeout(ctx->timeout, now, timeout);

        st_timeout_item_t *lp = timeout->head;
        while (lp) {
            lp->timeouted = 1;
            lp = lp->next;
        }

        join(active, timeout);

        lp = active->head;
        while (lp) {
            pop_head_timeout(active);
            if (lp->pfn_process) {
                lp->pfn_process(lp);
            }
            lp = active->head;
        }
        if (pfn) {
            if (pfn(arg) == -1) {
                break;
            }
        }
    }
}

st_co_epoll_t * alloc_epoll() {
    st_co_epoll_t *ctx = calloc(1, sizeof(st_co_epoll_t));
    ctx->epoll_fd = 0;
    ctx->timeout = alloc_timeout(60 * 1000);
    ctx->timeout_list = calloc(1, sizeof(st_timeout_item_link_t));
    ctx->active_list = calloc(1, sizeof(st_timeout_item_link_t));
    return ctx;
}

void free_epoll(st_co_epoll_t *ctx) {

}

st_co_routine_t *co_create_env(st_co_routine_env_t *env, const st_co_routine_attr_t *attr,
                               pfn_co_routine_t pfn, void *arg) {
    st_co_routine_attr_t at;
    st_co_routine_attr_init(&at);
    if (attr) {
        memcpy(&at, attr, sizeof(at));
    }
    if (at.stack_size <= 0) {
        at.stack_size = 128 * 1024;
    } else if (at.stack_size > 1024 * 1024 * 8) {
        at.stack_size = 1024 * 1024 * 8;
    }
    if (at.stack_size & 0xFFF) {
        at.stack_size &= ~0xFFF;
        at.stack_size += 0x1000;
    }
    st_co_routine_t *lp = malloc(sizeof(st_co_routine_t));
    lp->env = env;
    lp->pfn = pfn;
    lp->arg = arg;

    st_stack_mem_t *stack_mem = NULL;
    if (at.share_stack) {
        stack_mem = co_get_stackmem(at.share_stack);
        at.stack_size = at.share_stack->stack_size;
    } else {
        stack_mem = co_alloc_stackmem(at.stack_size);
    }
    lp->stack_mem = stack_mem;

    lp->ctx.ss_size = at.stack_size;
    lp->ctx.ss_sp = stack_mem->stack_buffer;

    lp->start = 0;
    lp->end = 0;
    lp->is_main = 0;
    lp->enable_sys_hook = 0;
    lp->is_share_stack = at.share_stack != NULL;

    lp->save_size = 0;
    lp->save_buffer = NULL;

    return lp;
}

void co_init_curr_thread_env() {
    pid_t pid = get_pid();
    st_co_routine_env_t *env = calloc(1, sizeof(st_co_routine_env_t));
    g_arr_co_env_per_thread[pid] = env;

    env->call_stack_size = 0;
    st_co_routine_t *co_main = co_create_env(env, NULL, NULL, NULL);
    co_main->is_main = 1;

    env->pending_co = NULL;
    env->ocupy_co = NULL;

    coctx_init(&co_main->ctx);

    env->call_stack[env->call_stack_size++] = co_main;

    st_co_epoll_t *ev = alloc_epoll();
    set_epoll( env,ev );
}

st_co_routine_env_t *co_get_curr_thread_env() {
    return g_arr_co_env_per_thread[get_pid()];
}

void set_epoll(st_co_routine_env_t *env, st_co_epoll_t *ev) {
    env->epoll = ev;
}

void save_stack_buffer(st_co_routine_t* ocupy_co) {
    st_stack_mem_t* stack_mem = ocupy_co->stack_mem;
    size_t len = stack_mem->stack_bp - ocupy_co->stack_sp;
    if (ocupy_co->save_buffer) {
        free(ocupy_co->save_buffer), ocupy_co->save_buffer = NULL;
    }
    ocupy_co->save_buffer = (char*)malloc(len);
    ocupy_co->save_size = len;
    memcpy(ocupy_co->save_buffer, ocupy_co->stack_sp, len);
}

void co_swap(st_co_routine_t *curr, st_co_routine_t *pending_co) {
    st_co_routine_env_t *env = co_get_curr_thread_env();

    char c;
    curr->stack_sp = &c;

    if (!pending_co->is_share_stack) {
        env->pending_co = NULL;
        env->ocupy_co = NULL;
    } else {
        env->pending_co = pending_co;
        st_co_routine_t *ocupy_co = pending_co->stack_mem->ocupy_co;
        pending_co->stack_mem->ocupy_co = pending_co;
        env->ocupy_co = ocupy_co;
        if (ocupy_co && ocupy_co != pending_co) {
            save_stack_buffer(ocupy_co);
        }
    }

    coctx_swap(&(curr->ctx), &(pending_co->ctx));

    st_co_routine_env_t* curr_env = co_get_curr_thread_env();
    st_co_routine_t* update_ocupy_co =  curr_env->ocupy_co;
    st_co_routine_t* update_pending_co = curr_env->pending_co;

    if (update_ocupy_co && update_pending_co && update_ocupy_co != update_pending_co) {
        if (update_pending_co->save_buffer && update_pending_co->save_size > 0) {
            memcpy(update_pending_co->stack_sp, update_pending_co->save_buffer, update_pending_co->save_size);
        }
    }
}

void co_yield_env(st_co_routine_env_t *env) {
    st_co_routine_t *curr = env->call_stack[env->call_stack_size - 1];
    st_co_routine_t *last = env->call_stack[env->call_stack_size - 2];

    env->call_stack_size--;

    co_swap(curr, last);
}

void co_yield_ct() {
    co_yield_env(co_get_curr_thread_env());
}

void co_yield(st_co_routine_t *co) {
    co_yield_env(co->env);
}

int co_create(st_co_routine_t **ppco, const st_co_routine_attr_t *attr, pfn_co_routine_t routine, void *arg) {
    if (!co_get_curr_thread_env()) {
        co_init_curr_thread_env();
    }
    *ppco = co_create_env(co_get_curr_thread_env(), attr, routine, arg);
    return 0;
}

static void *co_routine_func(void *a_co, void *arg) {
    st_co_routine_t *co = a_co;
    if (co->pfn) {
        co->pfn(co->arg);
    }
    co->end = 1;
    co_yield_env(co->env);
    return 0;
}

void co_resume(st_co_routine_t *co) {
    st_co_routine_env_t *env = co->env;
    st_co_routine_t *curr_routine = env->call_stack[env->call_stack_size - 1];
    if (!co->start) {
        coctx_make(&co->ctx, co_routine_func, co, 0);
        co->start = 1;
    }
    env->call_stack[env->call_stack_size++] = co;
    co_swap(curr_routine, co);
}

int co_is_enable_sys_hook() {
    st_co_routine_t *co = get_curr_thread_co();
    return (co && co->enable_sys_hook);
}

void co_disable_hook_sys() {
    st_co_routine_t *co = get_curr_thread_co();
    if(co) {
        co->enable_sys_hook = 0;
    }
}

void *co_getspecific(pthread_key_t key) {
    st_co_routine_t *co = get_curr_thread_co();
    if(!co || co->is_main) {
        return pthread_getspecific( key );
    }
    return co->a_spec[key].value;
}

int co_setspecific(pthread_key_t key, const void *value) {
    st_co_routine_t *co = get_curr_thread_co();
    if(!co || co->is_main) {
        return pthread_setspecific( key,value );
    }
    co->a_spec[key].value = (void*)value;
    return 0;
}

st_co_epoll_t *co_get_epoll_ct() {
    if(!co_get_curr_thread_env()) {
        co_init_curr_thread_env();
    }
    return co_get_curr_thread_env()->epoll;
}


static void on_signal_process_event(st_timeout_item_t * ap ) {
    st_co_routine_t *co = ap->arg;
    co_resume(co);
}

struct st_co_cond_item_t *co_cond_pop(struct st_co_cond_t *link);
int co_cond_signal(struct st_co_cond_t *si) {
    struct st_co_cond_item_t * sp = co_cond_pop(si);
    if(!sp) {
        return 0;
    }
    remove_from_timeout_link(&sp->timeout);
    add_timeout_tail(co_get_curr_thread_env()->epoll->active_list, &sp->timeout);
    return 0;
}

int co_cond_broadcast(struct st_co_cond_t *si) {
    for(;;) {
        struct st_co_cond_item_t * sp = co_cond_pop( si );
        if( !sp ) return 0;
        remove_from_timeout_link(&sp->timeout);
        add_timeout_tail(co_get_curr_thread_env()->epoll->active_list, &sp->timeout);
    }
    return 0;
}

static inline void pop_head_cond(struct st_co_cond_t *ap_link) {
    if( !ap_link->head ) {
        return ;
    }
    struct st_co_cond_item_t *lp = ap_link->head;
    if( ap_link->head == ap_link->tail ) {
        ap_link->head = ap_link->tail = NULL;
    } else {
        ap_link->head = ap_link->head->next;
    }

    lp->prev = lp->next = NULL;
    lp->link = NULL;

    if( ap_link->head ) {
        ap_link->head->prev = NULL;
    }
}

void add_cond_tail(struct st_co_cond_t *ap_link, struct st_co_cond_item_t *ap) {
    if( ap->link ) {
        return ;
    }
    if(ap_link->tail) {
        ap_link->tail->next = ap;
        ap->next = NULL;
        ap->prev = ap_link->tail;
        ap_link->tail = ap;
    } else {
        ap_link->head = ap_link->tail = ap;
        ap->next = ap->prev = NULL;
    }
    ap->link = ap_link;
}


void remove_from_cond_link(struct st_co_cond_item_t *ap) {
    struct st_co_cond_t *lst = ap->link;
    if(!lst) return ;
    assert( lst->head && lst->tail );

    if(ap == lst->head) {
        lst->head = ap->next;
        if(lst->head) {
            lst->head->prev = NULL;
        }
    } else {
        if(ap->prev) {
            ap->prev->next = ap->next;
        }
    }

    if(ap == lst->tail) {
        lst->tail = ap->prev;
        if(lst->tail) {
            lst->tail->next = NULL;
        }
    } else {
        ap->next->prev = ap->prev;
    }

    ap->prev = ap->next = NULL;
    ap->link = NULL;
}

int co_cond_timedwait(struct st_co_cond_t *link,int ms ) {
    struct st_co_cond_item_t* psi = calloc(1, sizeof(struct st_co_cond_item_t));
    psi->timeout.arg = get_curr_thread_co();
    psi->timeout.pfn_process = on_signal_process_event;

    if(ms > 0) {
        unsigned long long now = get_tick_ms();
        psi->timeout.expire_time = now + ms;

        int ret = add_timeout(co_get_curr_thread_env()->epoll->timeout, &psi->timeout, now);
        if( ret != 0 ) {
            free(psi);
            return ret;
        }
    }
    add_cond_tail(link, psi);

    co_yield_ct();

    remove_from_cond_link(psi);
    free(psi);

    return 0;
}

struct st_co_cond_t *co_cond_alloc() {
    return calloc(1, sizeof(struct st_co_cond_t));
}

int co_cond_free(struct st_co_cond_t * cc) {
    free( cc );
    return 0;
}

struct st_co_cond_item_t *co_cond_pop(struct st_co_cond_t *link ) {
    struct st_co_cond_item_t *p = link->head;
    if(p) {
        pop_head_cond(link);
    }
    return p;
}



