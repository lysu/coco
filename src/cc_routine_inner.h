#ifndef LIBCOCO_CC_ROUTINE_INNER_H
#define LIBCOCO_CC_ROUTINE_INNER_H

#include "../include/cc_routine.h"
#include "cc_ctx.h"

typedef struct co_routine_env_s co_routine_env_t;

struct co_spec_s {
    void *value;
};
typedef struct co_spec_s co_spec_t;

struct stack_mem_s {
    co_routine_t *ocupy_co;
    size_t stack_size;
    char *stack_bp;
    char *stack_buffer;
};
typedef struct stack_mem_s stack_mem_t;

struct share_stack_s {
    unsigned int alloc_idx;
    size_t stack_size;
    int count;
    stack_mem_t **stack_array;
};
typedef struct share_stack_s share_stack_t;

struct co_routine_s {
    co_routine_env_t *env;
    pfn_co_routine_t pfn;
    void *arg;
    coctx_t ctx;

    char start;
    char end;
    char is_main;
    char enable_sys_hook;
    char is_share_stack;

    void *pv_env;

    stack_mem_t *stack_mem;

    char* stack_sp;
    size_t save_size;
    char* save_buffer;

    co_spec_t a_spec[1024];
};

typedef struct timeout_s st_timeout_t;
typedef struct timeout_item_s timeout_item_t;

st_timeout_t *alloc_timeout(size_t size);
void free_timeout(st_timeout_t *ap_timeout);
int add_timeout(st_timeout_t *ap_timeout, timeout_item_t *ap_item, uint64_t all_now);

co_epoll_t * alloc_epoll();
void free_epoll(co_epoll_t *ctx);

void co_init_curr_thread_env();
co_routine_env_t *co_get_curr_thread_env();
void set_epoll(co_routine_env_t *env, co_epoll_t *ev);
co_routine_t *get_curr_thread_co();

void co_free(co_routine_t * co);
void co_yield_env(co_routine_env_t *env);

#endif //LIBCOCO_CC_ROUTINE_INNER_H
