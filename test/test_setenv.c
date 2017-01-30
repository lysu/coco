#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "cc_routine.h"

const char *CGI_ENV_HOOK_LIST[] = {"CGINAME"};

struct routine_args_s {
    int routine_id;
};

void set_and_get_env(int routine_id) {
    printf("routineid %d begin\n", routine_id);

    //use poll as sleep
    poll(NULL, 0, 500);

    char sBuf[128];
    sprintf(sBuf, "cgi_routine_%d", routine_id);
    int ret = setenv("CGINAME", sBuf, 1);
    if (ret) {
        printf("%s:%d set env err ret %d errno %d %s\n", __func__, __LINE__,
               ret, errno, strerror(errno));
        return;
    }
    printf("routineid %d set env CGINAME %s\n", routine_id, sBuf);

    poll(NULL, 0, 500);

    char *env = getenv("CGINAME");
    if (!env) {
        printf("%s:%d get env err errno %d %s\n", __func__, __LINE__,
               errno, strerror(errno));
        return;
    }
    printf("routineid %d get env CGINAME %s\n", routine_id, env);
}

void *routine_func(void *args) {
    co_enable_hook_sys();
    struct routine_args_s *g = args;
    set_and_get_env(g->routine_id);
    return NULL;
}

int main(int argc, char *argv[]) {
    init_hook_sys_call();
    co_set_env_list(CGI_ENV_HOOK_LIST, sizeof(CGI_ENV_HOOK_LIST) / sizeof(char *));
    struct routine_args_s args[3];
    for (int i = 0; i < 3; i++) {
        co_routine_t *co = NULL;
        args[i].routine_id = i;
        co_create(&co, NULL, routine_func, &args[i]);
        co_resume(co);
    }
    co_eventloop(co_get_epoll_ct(), NULL, NULL);
    return 0;
}

