#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "cc_routine_specific.h"
#include "cc_routine.h"

struct routine_args_s {
    co_routine_t *co;
    int routine_id;
};

struct routine_specific_data_s {
    int idx;
};

CO_ROUTINE_SPECIFIC(routine_specific_data_s);

void *routine_func(void *args) {
    co_enable_hook_sys();
    struct routine_args_s *routine_args = args;
    routine_data_routine_routine_specific_data_s()->idx = routine_args->routine_id;
    while (1) {
        printf("%s:%d routine specific data idx %d\n", __func__, __LINE__,
               routine_data_routine_routine_specific_data_s()->idx);
        poll(NULL, 0, 1000);
    }
    return NULL;
}

int main() {
    struct routine_args_s args[10];
    for (int i = 0; i < 10; i++) {
        args[i].routine_id = i;
        co_create(&args[i].co, NULL, routine_func, (void *) &args[i]);
        co_resume(args[i].co);
    }
    co_eventloop(co_get_epoll_ct(), NULL, NULL);
    return 0;
}
