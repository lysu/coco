#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <memory.h>
#include <sys/queue.h>
#include "cc_routine.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
struct task2_s {
    STAILQ_ENTRY(task2_s) c_tqe;
    co_routine_t *co;
    int fd;
};
STAILQ_HEAD(_task2_q, task2_s);

static struct _task2_q g_readwrite;
static int g_listen_fd = -1;

static int SetNonBlock(int iSock) {
    int iFlags;

    iFlags = fcntl(iSock, F_GETFL, 0);
    iFlags |= O_NONBLOCK;
    iFlags |= O_NDELAY;
    int ret = fcntl(iSock, F_SETFL, iFlags);
    return ret;
}

static void *readwrite_routine(void *arg) {

    co_enable_hook_sys();

    struct task2_s *co = arg;
    char buf[1024 * 16];
    for (;;) {
        if (-1 == co->fd) {
            STAILQ_INSERT_HEAD(&g_readwrite, co, c_tqe);
            co_yield_ct();
            continue;
        }

        int fd = co->fd;
        co->fd = -1;

        for (;;) {
            struct pollfd pf = {0};
            pf.fd = fd;
            pf.events = (POLLIN | POLLERR | POLLHUP);
            co_poll(co_get_epoll_ct(), &pf, 1, 1000);

            ssize_t ret = read(fd, buf, sizeof(buf));
            if (ret > 0) {
                ret = write(fd, buf, (size_t) ret);
            }
            if (ret <= 0) {
                close(fd);
                break;
            }
        }

    }
}

int co_accept(int fd, struct sockaddr *addr, socklen_t *len);

static void *accept_routine(void *ignore) {
    co_enable_hook_sys();
    printf("accept_routine\n");
    fflush(stdout);
    for (;;) {
        //printf("pid %ld g_readwrite.size %ld\n",getpid(),g_readwrite.size());
        if (STAILQ_EMPTY(&g_readwrite)) {
            printf("empty\n"); //sleep
            struct pollfd pf = {0};
            pf.fd = -1;
            poll(&pf, 1, 1000);
            continue;
        }
        struct sockaddr_in addr; //maybe sockaddr_un;
        memset(&addr, 0, sizeof(addr));
        socklen_t len = sizeof(addr);

        int fd = co_accept(g_listen_fd, (struct sockaddr *) &addr, &len);
        if (fd < 0) {
            struct pollfd pf = {0};
            pf.fd = g_listen_fd;
            pf.events = (POLLIN | POLLERR | POLLHUP);
            co_poll(co_get_epoll_ct(), &pf, 1, 1000);
            continue;
        }
        if (STAILQ_EMPTY(&g_readwrite)) {
            close(fd);
            continue;
        }
        SetNonBlock(fd);
        struct task2_s *co = STAILQ_FIRST(&g_readwrite);
        co->fd = fd;
        STAILQ_REMOVE(&g_readwrite, co, task2_s, c_tqe);
        co_resume(co->co);
    }
    return 0;
}

static void set_addr2(const char *pszIP, const unsigned short shPort, struct sockaddr_in *addr) {
    bzero(addr, sizeof(struct sockaddr_in));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(shPort);
    in_addr_t nIP = 0;
    if (!pszIP || '\0' == *pszIP ||
        0 == strcmp(pszIP, "0") ||
        0 == strcmp(pszIP, "0.0.0.0") ||
        0 == strcmp(pszIP, "*")) {
        nIP = htonl(INADDR_ANY);
    } else {
        nIP = inet_addr(pszIP);
    }
    addr->sin_addr.s_addr = nIP;

}

static int
create_tcp_socket_2(const unsigned short shPort, const char *pszIP, int bReuse) {
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd >= 0) {
        if (shPort != 0) {
            if (bReuse) {
                int nReuseAddr = 1;
                setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &nReuseAddr, sizeof(nReuseAddr));
            }
            struct sockaddr_in addr;
            set_addr2(pszIP, shPort, &addr);
            int ret = bind(fd, (struct sockaddr *) &addr, sizeof(addr));
            if (ret != 0) {
                close(fd);
                return -1;
            }
        }
    }
    return fd;
}


int main(int argc, char *argv[]) {

    init_hook_sys_call();

    STAILQ_INIT(&g_readwrite);

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    int cnt = atoi(argv[3]);
    int proccnt = atoi(argv[4]);

    g_listen_fd = create_tcp_socket_2((const unsigned short) port, ip, 1);
    listen(g_listen_fd, 1024);
    printf("listen %d %s:%d\n", g_listen_fd, ip, port);

    SetNonBlock(g_listen_fd);

    for (int k = 0; k < proccnt; k++) {

        pid_t pid = fork();
        if (pid > 0) {
            continue;
        } else if (pid < 0) {
            break;
        }
        for (int i = 0; i < cnt; i++) {
            struct task2_s *task = calloc(1, sizeof(struct task2_s));
            task->fd = -1;

            co_create(&(task->co), NULL, readwrite_routine, task);
            co_resume(task->co);

        }
        co_routine_t *accept_co = NULL;
        co_create(&accept_co, NULL, accept_routine, 0);
        co_resume(accept_co);

        co_eventloop(co_get_epoll_ct(), 0, 0);

        exit(0);
    }
    return 0;
}

#pragma clang diagnostic pop