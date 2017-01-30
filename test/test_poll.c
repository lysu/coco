#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>

#include "cc_routine.h"

struct fd_item {
    SLIST_ENTRY(fd_item) c_list;
    int fd;
};
SLIST_HEAD(_fd_l, fd_item);


struct task_t {
    STAILQ_ENTRY(task_t) c_tqe;
    co_routine_t *co;
    int fd;
    struct sockaddr_in addr;
};

STAILQ_HEAD(_task_q, task_t);

struct task_list_s {
    struct _task_q list;
    int len;
};

struct task_list_s *task_list_new() {
    struct task_list_s *l = calloc(1, sizeof(struct task_list_s));
    STAILQ_INIT(&l->list);
    return l;
}

void task_list_add(struct task_list_s *list, struct task_t *task) {
    STAILQ_INSERT_TAIL(&list->list, task, c_tqe);
    list->len++;
}

struct task_t *task_list_get(struct task_list_s *list, int i) {
    struct task_t *task = STAILQ_FIRST(&list->list);
    if (task == NULL) {
        return NULL;
    }
    for (int j = 0; j < list->len; ++j) {
        if (j == i) {
            return task;
        }
        task = STAILQ_NEXT(task, c_tqe);
    }
    return NULL;
}

struct task_list_s *task_list_copy(struct task_list_s *tasks) {
    struct task_list_s *new_tasks = task_list_new();
    struct task_t *task = STAILQ_FIRST(&tasks->list);
    while (task != NULL) {
        struct task_t *new_task = calloc(1, sizeof(struct task_t));
        new_task->addr = task->addr;
        new_task->fd = task->fd;
        task_list_add(new_tasks, new_task);
        task = STAILQ_NEXT(task, c_tqe);
    }
    return new_tasks;
}

static int set_non_block(int iSock) {
    int flags;
    flags = fcntl(iSock, F_GETFL, 0);
    flags |= O_NONBLOCK;
    flags |= O_NDELAY;
    return fcntl(iSock, F_SETFL, flags);
}

static void set_addr(const char *pszIP, const unsigned short shPort, struct sockaddr_in *addr) {
    bzero(addr, sizeof(struct sockaddr_in));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(shPort);
    in_addr_t nIP = 0;
    if (!pszIP || '\0' == *pszIP
        || 0 == strcmp(pszIP, "0")
        || 0 == strcmp(pszIP, "0.0.0.0")
        || 0 == strcmp(pszIP, "*")) {
        nIP = htonl(INADDR_ANY);
    } else {
        nIP = inet_addr(pszIP);
    }
    addr->sin_addr.s_addr = nIP;

}

static int create_tcp_socket(const unsigned short shPort, const char *pszIP, int bReuse) {
    if (pszIP == NULL) {
        pszIP = "*";
    }
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd >= 0) {
        if (shPort != 0) {
            if (bReuse) {
                int nReuseAddr = 1;
                setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &nReuseAddr, sizeof(nReuseAddr));
            }
            struct sockaddr_in addr;
            set_addr(pszIP, shPort, &addr);
            int ret = bind(fd, (struct sockaddr *) &addr, sizeof(addr));
            if (ret != 0) {
                close(fd);
                return -1;
            }
        }
    }
    return fd;
}

static void *poll_routine(void *arg) {
    co_enable_hook_sys();
    struct task_list_s *v = arg;
    for (size_t i = 0; i < v->len; i++) {
        int fd = create_tcp_socket(0, "*", 0);
        printf("ret fd: %d--------\n", fd);
        set_non_block(fd);
        struct task_t *task_item = task_list_get(v, (int) i);
        task_item->fd = fd;

        int ret = connect(fd, (struct sockaddr *) &task_item->addr, sizeof(task_item->addr));
        printf("co %p connect i %ld ret %d errno %d (%s) fd:(%d)\n",
               co_self(), i, ret, errno, strerror(errno), fd);
    }

    struct pollfd *pf = (struct pollfd *) calloc((size_t) v->len, sizeof(struct pollfd));
    for (size_t i = 0; i < v->len; i++) {
        struct task_t *task_item = task_list_get(v, (int) i);
        pf[i].fd = task_item->fd;
        pf[i].events = (POLLOUT | POLLERR | POLLHUP);
    }
    fd_set fds;
    FD_ZERO(&fds);
    int fd_size = 0;
    int fd_max = 0;
    size_t wait_cnt = (size_t) v->len;
    for (;;) {
        int ret = poll(pf, (nfds_t) wait_cnt, 1000);
        printf("co %p poll wait %ld ret %d\n", co_self(), wait_cnt, ret);
        for (int i = 0; i < ret; i++) {
            printf("co %p fire fd %d revents 0x%X POLLOUT 0x%X POLLERR 0x%X POLLHUP 0x%X\n",
                   co_self(),
                   pf[i].fd,
                   pf[i].revents,
                   POLLOUT,
                   POLLERR,
                   POLLHUP
            );
            int fd = pf[i].fd;
            if (!FD_ISSET(fd, &fds)) {
                FD_SET(fd, &fds);
                fd_size++;
                if (fd > fd_max) {
                    fd_max = fd;
                }
            }
        }
        if (fd_size == v->len) {
            break;
        }
        if (ret <= 0) {
            break;
        }

        wait_cnt = 0;
        for (size_t i = 0; i < v->len; i++) {
            struct task_t *task_item = task_list_get(v, (int) i);
            if (FD_ISSET(task_item->fd, &fds) && task_item->fd == fd_max) {
                pf[wait_cnt].fd = task_item->fd;
                pf[wait_cnt].events = (POLLOUT | POLLERR | POLLHUP);
                ++wait_cnt;
            }
        }
    }
    for (size_t i = 0; i < v->len; i++) {
        struct task_t *task_item = task_list_get(v, (int) i);
        close(task_item->fd);
        task_item->fd = -1;
    }

    printf("co %p task cnt %i fire %i\n", co_self(), v->len, fd_size);
    return 0;
}

int main(int argc, char *argv[]) {

    init_hook_sys_call();

    struct task_list_s *v = task_list_new();
    for (int i = 1; i < argc; i += 2) {
        struct task_t *task = calloc(1, sizeof(struct task_t));
        set_addr(argv[i], (const unsigned short) atoi(argv[i + 1]), &task->addr);
        task_list_add(v, task);
    }

    printf("--------------------- main -------------------\n");
    struct task_list_s *v2 = task_list_copy(v);
    poll_routine(v2);

    printf("--------------------- routine -------------------\n");
    for (int i = 0; i < 10; i++) {
        struct task_list_s *v3 = task_list_copy(v);
        co_routine_t *co = NULL;
        co_create(&co, NULL, poll_routine, v3);
        printf("routine i %d\n", i);
        co_resume(co);
    }

    co_eventloop(co_get_epoll_ct(), 0, 0);

    return 0;
}
//./test_poll 127.0.0.1 12365 127.0.0.1 12222 192.168.1.1 1000 192.168.1.2 1111


