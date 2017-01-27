#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include "cc_routine.h"

void* co_test(void* args) {
    printf("test in co_test\n");
    return NULL;
}

int main() {
    st_co_routine_t *consumer_routine;
    co_create(&consumer_routine, NULL, co_test, NULL);
    co_resume(consumer_routine);
    printf("test in return from co_test\n");
}
