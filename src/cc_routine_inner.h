#ifndef LIBCOCO_CC_ROUTINE_INNER_H
#define LIBCOCO_CC_ROUTINE_INNER_H

#include "../include/cc_routine.h"
#include "cc_ctx.h"

typedef struct st_co_routine_env_s st_co_routine_env_t;

struct st_co_spec_s {
    void *value;
};
typedef struct st_co_spec_s st_co_spec_t;

struct st_stack_mem_s {
    st_co_routine_t *ocupy_co;
    size_t stack_size;
    char *stack_bp;
    char *stack_buffer;
};
typedef struct st_stack_mem_s st_stack_mem_t;

struct st_share_stack_s {
    unsigned int alloc_idx;
    size_t stack_size;
    int count;
    st_stack_mem_t **stack_array;
};
typedef struct st_share_stack_s st_share_stack_t;

struct st_co_routine_s {
    st_co_routine_env_t *env;
    pfn_co_routine_t pfn;
    void *arg;
    coctx_t ctx;

    char start;
    char end;
    char is_main;
    char enable_sys_hook;
    char is_share_stack;

    void *pv_env;

    st_stack_mem_t *stack_mem;

    char* stack_sp;
    size_t save_size;
    char* save_buffer;

    st_co_spec_t a_spec[1024];
};

void co_init_curr_thread_env();
st_co_routine_env_t *co_get_curr_thread_env();

void co_free(st_co_routine_t * co);
void co_yield_env(st_co_routine_env_t *env);

#endif //LIBCOCO_CC_ROUTINE_INNER_H
