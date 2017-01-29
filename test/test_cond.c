#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/queue.h>
#include "cc_routine.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
struct task_s {
    STAILQ_ENTRY(task_s) c_tqe;
    int id;
};
STAILQ_HEAD(_task_q, task_s);

struct env_s {
    struct co_cond_s* cond;
    struct _task_q task_queue;
};

void* producer(void *args) {
    co_enable_hook_sys();
    struct env_s* env= args;
    int id = 0;
    while (1) {
        struct task_s* task = calloc(1, sizeof(struct task_s));
        task->id = id++;
        STAILQ_INSERT_TAIL(&env->task_queue, task, c_tqe);
        printf("%s:%d produce task %d\n", __func__, __LINE__, task->id);
        co_cond_signal(env->cond);
        poll(NULL, 0, 1000);
    }
    return NULL;
}

void* consumer(void *args) {
    co_enable_hook_sys();
    struct env_s* env = args;
    while (1) {
        if (STAILQ_EMPTY(&env->task_queue)) {
            co_cond_timedwait(env->cond, -1);
            continue;
        }
        struct task_s* task = STAILQ_FIRST(&env->task_queue);
        STAILQ_REMOVE(&env->task_queue, task, task_s, c_tqe);
        printf("%s:%d consume task %d\n", __func__, __LINE__, task->id);
        free(task);
    }
    return NULL;
}

int main() {

    init_hook_sys_call();

    struct env_s* env = calloc(1, sizeof(struct env_s));
    STAILQ_INIT(&env->task_queue);
    env->cond = co_cond_alloc();

    co_routine_t* consumer_routine;
    co_create(&consumer_routine, NULL, consumer, env);
    co_resume(consumer_routine);

    co_routine_t* producer_routine;
    co_create(&producer_routine, NULL, producer, env);
    co_resume(producer_routine);

    co_eventloop(co_get_epoll_ct(), NULL, NULL);
    return 0;
}

#pragma clang diagnostic pop