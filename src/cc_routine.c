#include <unistd.h>
#include <sys/syscall.h>
#include <string.h>
#include "../include/cc_routine.h"
#include "cc_routine_inner.h"

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
}

st_co_routine_env_t *co_get_curr_thread_env() {
    return g_arr_co_env_per_thread[get_pid()];
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



