#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>
#include "cc_ctx.h"
#include "cc_routine.h"
#include "cc_routine_inner.h"

void *routine_func(void *args) {
    co_enable_hook_sys();
    int *routineid = (int *) args;
    while (1) {
        char sBuff[128];
        sprintf(sBuff, "from routineid %d stack addr %p\n", *routineid, sBuff);

        printf("%s", sBuff);
        poll(NULL, 0, 1000); //sleep 1s
    }
    return NULL;
}

int main() {
    init_hook_sys_call();
    share_stack_t *share_stack = co_alloc_sharestack(1, 1024 * 128);
    co_routine_attr_t attr;
    co_routine_attr_init(&attr);
    attr.stack_size = 0;
    attr.share_stack = share_stack;

    co_routine_t *co[2];
    int routineid[2];
    for (int i = 0; i < 2; i++) {
        routineid[i] = i;
        co_create(&co[i], &attr, routine_func, routineid + i);
        co_resume(co[i]);
    }
    co_eventloop(co_get_epoll_ct(), NULL, NULL);
    return 0;
}