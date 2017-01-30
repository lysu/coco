#include <sys/socket.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <sys/un.h>

#include <dlfcn.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>

#include <netinet/in.h>
#include <errno.h>
#include <time.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <pthread.h>

#include <resolv.h>
#include <netdb.h>

#include <time.h>
#include "cc_routine.h"
#include "cc_routine_inner.h"
#include "cc_routine_specific.h"


typedef long long ll64_t;

struct rpchook_t {
    int user_flag;
    struct sockaddr_in dest; //maybe sockaddr_un;
    int domain; //AF_LOCAL , AF_INET

    struct timeval read_timeout;
    struct timeval write_timeout;
};

static inline pid_t GetPid() {
    char **p = (char **) pthread_self();
    return p ? *(pid_t *) (p + 18) : getpid();
}

static struct rpchook_t *g_rpchook_socket_fd[102400] = {0};

typedef int (*socket_pfn_t)(int domain, int type, int protocol);

typedef int (*connect_pfn_t)(int socket, const struct sockaddr *address, socklen_t address_len);

typedef int (*close_pfn_t)(int fd);

typedef ssize_t (*read_pfn_t)(int fildes, void *buf, size_t nbyte);

typedef ssize_t (*write_pfn_t)(int fildes, const void *buf, size_t nbyte);

typedef ssize_t (*sendto_pfn_t)(int socket, const void *message, size_t length,
                                int flags, const struct sockaddr *dest_addr,
                                socklen_t dest_len);

typedef ssize_t (*recvfrom_pfn_t)(int socket, void *buffer, size_t length,
                                  int flags, struct sockaddr *address,
                                  socklen_t *address_len);

typedef size_t (*send_pfn_t)(int socket, const void *buffer, size_t length, int flags);

typedef ssize_t (*recv_pfn_t)(int socket, void *buffer, size_t length, int flags);

typedef int (*poll_pfn_t)(struct pollfd fds[], nfds_t nfds, int timeout);

typedef int (*setsockopt_pfn_t)(int socket, int level, int option_name,
                                const void *option_value, socklen_t option_len);

typedef int (*fcntl_pfn_t)(int fildes, int cmd, ...);

typedef struct tm *(*localtime_r_pfn_t)(const time_t *timep, struct tm *result);

typedef void *(*pthread_getspecific_pfn_t)(pthread_key_t key);

typedef int (*pthread_setspecific_pfn_t)(pthread_key_t key, const void *value);

typedef int (*setenv_pfn_t)(const char *name, const char *value, int overwrite);

typedef int (*unsetenv_pfn_t)(const char *name);

typedef char *(*getenv_pfn_t)(const char *name);

typedef struct hostent *(*gethostbyname_pfn_t)(const char *name);

typedef res_state (*__res_state_pfn_t)();

typedef int (*__poll_pfn_t)(struct pollfd fds[], nfds_t nfds, int timeout);

static socket_pfn_t g_sys_socket_func;
static connect_pfn_t g_sys_connect_func;
static close_pfn_t g_sys_close_func;

static read_pfn_t g_sys_read_func;
static write_pfn_t g_sys_write_func;

static sendto_pfn_t g_sys_sendto_func;
static recvfrom_pfn_t g_sys_recvfrom_func;

static send_pfn_t g_sys_send_func;
static recv_pfn_t g_sys_recv_func;

static poll_pfn_t g_sys_poll_func;

static setsockopt_pfn_t g_sys_setsockopt_func;
static fcntl_pfn_t g_sys_fcntl_func;

static setenv_pfn_t g_sys_setenv_func;
static unsetenv_pfn_t g_sys_unsetenv_func;
static getenv_pfn_t g_sys_getenv_func;
static __res_state_pfn_t g_sys___res_state_func;

static gethostbyname_pfn_t g_sys_gethostbyname_func;

static __poll_pfn_t g_sys___poll_func;

void init_hook_sys_call() {

    g_sys_socket_func = (socket_pfn_t) dlsym(RTLD_NEXT, "socket");
    g_sys_connect_func = (connect_pfn_t) dlsym(RTLD_NEXT, "connect");
    g_sys_close_func = (close_pfn_t) dlsym(RTLD_NEXT, "close");

    g_sys_read_func = (read_pfn_t) dlsym(RTLD_NEXT, "read");
    g_sys_write_func = (write_pfn_t) dlsym(RTLD_NEXT, "write");

    g_sys_sendto_func = (sendto_pfn_t) dlsym(RTLD_NEXT, "sendto");
    g_sys_recvfrom_func = (recvfrom_pfn_t) dlsym(RTLD_NEXT, "recvfrom");

    g_sys_send_func = (send_pfn_t) dlsym(RTLD_NEXT, "send");
    g_sys_recv_func = (recv_pfn_t) dlsym(RTLD_NEXT, "recv");

    g_sys_poll_func = (poll_pfn_t) dlsym(RTLD_NEXT, "poll");

    g_sys_setsockopt_func = (setsockopt_pfn_t) dlsym(RTLD_NEXT, "setsockopt");
    g_sys_fcntl_func = (fcntl_pfn_t) dlsym(RTLD_NEXT, "fcntl");

    g_sys_setenv_func = (setenv_pfn_t) dlsym(RTLD_NEXT, "setenv");
    g_sys_unsetenv_func = (unsetenv_pfn_t) dlsym(RTLD_NEXT, "unsetenv");
    g_sys_getenv_func = (getenv_pfn_t) dlsym(RTLD_NEXT, "getenv");
    g_sys___res_state_func = (__res_state_pfn_t) dlsym(RTLD_NEXT, "__res_state");

    g_sys_gethostbyname_func = (gethostbyname_pfn_t) dlsym(RTLD_NEXT, "gethostbyname");

    g_sys___poll_func = (__poll_pfn_t) dlsym(RTLD_NEXT, "__poll");

}


static inline unsigned long long get_tick_count() {
    uint32_t lo, hi;
    __asm__ __volatile__ (
    "rdtscp" : "=a"(lo), "=d"(hi)
    );
    return ((unsigned long long) lo) | (((unsigned long long) hi) << 32);
}

struct rpchook_connagent_head_t {
    unsigned char bVersion;
    struct in_addr iIP;
    unsigned short hPort;
    unsigned int iBodyLen;
    unsigned int iOssAttrID;
    unsigned char bIsRespNotExist;
    unsigned char sReserved[6];
}__attribute__((packed));


#define HOOK_SYS_FUNC(name) if(!g_sys_##name##_func) { g_sys_##name##_func = (name##_pfn_t)dlsym(RTLD_NEXT,#name); }

static inline ll64_t diff_ms(struct timeval *begin, struct timeval *end) {
    ll64_t u = (end->tv_sec - begin->tv_sec);
    u *= 1000 * 10;
    u += (end->tv_usec - begin->tv_usec) / (100);
    return u;
}

static inline struct rpchook_t *get_by_fd(int fd) {
    if (fd > -1 && fd < (int) sizeof(g_rpchook_socket_fd) / (int) sizeof(g_rpchook_socket_fd[0])) {
        return g_rpchook_socket_fd[fd];
    }
    return NULL;
}

static inline struct rpchook_t *alloc_by_fd(int fd) {
    if (fd > -1 && fd < (int) sizeof(g_rpchook_socket_fd) / (int) sizeof(g_rpchook_socket_fd[0])) {
        struct rpchook_t *lp = calloc(1, sizeof(struct rpchook_t));
        lp->read_timeout.tv_sec = 1;
        lp->write_timeout.tv_sec = 1;
        g_rpchook_socket_fd[fd] = lp;
        return lp;
    }
    return NULL;
}

static inline void free_by_fd(int fd) {
    if (fd > -1 && fd < (int) sizeof(g_rpchook_socket_fd) / (int) sizeof(g_rpchook_socket_fd[0])) {
        struct rpchook_t *lp = g_rpchook_socket_fd[fd];
        if (lp) {
            g_rpchook_socket_fd[fd] = NULL;
            free(lp);
        }
    }
    return;
}

int socket(int domain, int type, int protocol) {
    HOOK_SYS_FUNC(socket);

    if (!co_is_enable_sys_hook()) {
        return g_sys_socket_func(domain, type, protocol);
    }
    int fd = g_sys_socket_func(domain, type, protocol);
    if (fd < 0) {
        return fd;
    }

    struct rpchook_t *lp = alloc_by_fd(fd);
    lp->domain = domain;

    fcntl(fd, F_SETFL, g_sys_fcntl_func(fd, F_GETFL, 0));

    return fd;
}

int co_accept(int fd, struct sockaddr *addr, socklen_t *len) {
    int cli = accept(fd, addr, len);
    if (cli < 0) {
        return cli;
    }
    alloc_by_fd(cli);
    return cli;
}

int connect(int fd, const struct sockaddr *address, socklen_t address_len) {
    HOOK_SYS_FUNC(connect);

    if (!co_is_enable_sys_hook()) {
        return g_sys_connect_func(fd, address, address_len);
    }

    //1.sys call
    int ret = g_sys_connect_func(fd, address, address_len);

    struct rpchook_t *lp = get_by_fd(fd);
    if (!lp) return ret;

    if (sizeof(lp->dest) >= address_len) {
        memcpy(&(lp->dest), address, (int) address_len);
    }
    if (O_NONBLOCK & lp->user_flag) {
        return ret;
    }

    if (!(ret < 0 && errno == EINPROGRESS)) {
        return ret;
    }

    //2.wait
    int pollret = 0;
    struct pollfd pf = {0};

    for (int i = 0; i < 3; i++) //25s * 3 = 75s
    {
        memset(&pf, 0, sizeof(pf));
        pf.fd = fd;
        pf.events = (POLLOUT | POLLERR | POLLHUP);

        pollret = poll(&pf, 1, 25000);

        if (1 == pollret) {
            break;
        }
    }
    if (pf.revents & POLLOUT) //connect succ
    {
        errno = 0;
        return 0;
    }

    //3.set errno
    int err = 0;
    socklen_t errlen = sizeof(err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen);
    if (err) {
        errno = err;
    } else {
        errno = ETIMEDOUT;
    }
    return ret;
}


int close(int fd) {
    HOOK_SYS_FUNC(close);

    if (!co_is_enable_sys_hook()) {
        return g_sys_close_func(fd);
    }

    free_by_fd(fd);
    int ret = g_sys_close_func(fd);

    return ret;
}

ssize_t read(int fd, void *buf, size_t nbyte) {
    HOOK_SYS_FUNC(read);

    if (!co_is_enable_sys_hook()) {
        return g_sys_read_func(fd, buf, nbyte);
    }
    struct rpchook_t *lp = get_by_fd(fd);

    if (!lp || (O_NONBLOCK & lp->user_flag)) {
        ssize_t ret = g_sys_read_func(fd, buf, nbyte);
        return ret;
    }
    int timeout = (int) ((lp->read_timeout.tv_sec * 1000)
                         + (lp->read_timeout.tv_usec / 1000));

    struct pollfd pf = {0};
    pf.fd = fd;
    pf.events = (POLLIN | POLLERR | POLLHUP);

    poll(&pf, 1, timeout);

    ssize_t readret = g_sys_read_func(fd, (char *) buf, nbyte);

    if (readret < 0) {
        // TODO: logger
    }

    return readret;

}

ssize_t write(int fd, const void *buf, size_t nbyte) {
    HOOK_SYS_FUNC(write);

    if (!co_is_enable_sys_hook()) {
        return g_sys_write_func(fd, buf, nbyte);
    }
    struct rpchook_t *lp = get_by_fd(fd);

    if (!lp || (O_NONBLOCK & lp->user_flag)) {
        ssize_t ret = g_sys_write_func(fd, buf, nbyte);
        return ret;
    }
    size_t wrotelen = 0;
    int timeout = (int) ((lp->write_timeout.tv_sec * 1000)
                         + (lp->write_timeout.tv_usec / 1000));

    ssize_t writeret = g_sys_write_func(fd, (const char *) buf + wrotelen, nbyte - wrotelen);

    if (writeret == 0) {
        return writeret;
    }

    if (writeret > 0) {
        wrotelen += writeret;
    }
    while (wrotelen < nbyte) {

        struct pollfd pf = {0};
        pf.fd = fd;
        pf.events = (POLLOUT | POLLERR | POLLHUP);
        poll(&pf, 1, timeout);

        writeret = g_sys_write_func(fd, (const char *) buf + wrotelen, nbyte - wrotelen);

        if (writeret <= 0) {
            break;
        }
        wrotelen += writeret;
    }
    if (writeret <= 0 && wrotelen == 0) {
        return writeret;
    }
    return wrotelen;
}

ssize_t sendto(int socket, const void *message, size_t length,
               int flags, const struct sockaddr *dest_addr,
               socklen_t dest_len) {
    /*
        1.no enable sys call ? sys
        2.(!lp || lp is non block) ? sys
        3.try
        4.wait
        5.try
    */
    HOOK_SYS_FUNC(sendto);
    if (!co_is_enable_sys_hook()) {
        return g_sys_sendto_func(socket, message, length, flags, dest_addr, dest_len);
    }

    struct rpchook_t *lp = get_by_fd(socket);
    if (!lp || (O_NONBLOCK & lp->user_flag)) {
        return g_sys_sendto_func(socket, message, length, flags, dest_addr, dest_len);
    }

    ssize_t ret = g_sys_sendto_func(socket, message, length, flags, dest_addr, dest_len);
    if (ret < 0 && EAGAIN == errno) {
        int timeout = (int) ((lp->write_timeout.tv_sec * 1000)
                             + (lp->write_timeout.tv_usec / 1000));


        struct pollfd pf = {0};
        pf.fd = socket;
        pf.events = (POLLOUT | POLLERR | POLLHUP);
        poll(&pf, 1, timeout);

        ret = g_sys_sendto_func(socket, message, length, flags, dest_addr, dest_len);

    }
    return ret;
}

ssize_t recvfrom(int socket, void *buffer, size_t length,
                 int flags, struct sockaddr *address,
                 socklen_t *address_len) {
    HOOK_SYS_FUNC(recvfrom);
    if (!co_is_enable_sys_hook()) {
        return g_sys_recvfrom_func(socket, buffer, length, flags, address, address_len);
    }

    struct rpchook_t *lp = get_by_fd(socket);
    if (!lp || (O_NONBLOCK & lp->user_flag)) {
        return g_sys_recvfrom_func(socket, buffer, length, flags, address, address_len);
    }

    int timeout = (int) ((lp->read_timeout.tv_sec * 1000)
                         + (lp->read_timeout.tv_usec / 1000));


    struct pollfd pf = {0};
    pf.fd = socket;
    pf.events = (POLLIN | POLLERR | POLLHUP);
    poll(&pf, 1, timeout);

    ssize_t ret = g_sys_recvfrom_func(socket, buffer, length, flags, address, address_len);
    return ret;
}

ssize_t send(int socket, const void *buffer, size_t length, int flags) {
    HOOK_SYS_FUNC(send);

    if (!co_is_enable_sys_hook()) {
        return g_sys_send_func(socket, buffer, length, flags);
    }
    struct rpchook_t *lp = get_by_fd(socket);

    if (!lp || (O_NONBLOCK & lp->user_flag)) {
        return g_sys_send_func(socket, buffer, length, flags);
    }
    size_t wrotelen = 0;
    int timeout = (int) ((lp->write_timeout.tv_sec * 1000)
                         + (lp->write_timeout.tv_usec / 1000));

    ssize_t writeret = g_sys_send_func(socket, buffer, length, flags);
    if (writeret == 0) {
        return writeret;
    }

    if (writeret > 0) {
        wrotelen += writeret;
    }
    while (wrotelen < length) {

        struct pollfd pf = {0};
        pf.fd = socket;
        pf.events = (POLLOUT | POLLERR | POLLHUP);
        poll(&pf, 1, timeout);

        writeret = g_sys_send_func(socket, (const char *) buffer + wrotelen, length - wrotelen, flags);

        if (writeret <= 0) {
            break;
        }
        wrotelen += writeret;
    }
    if (writeret <= 0 && wrotelen == 0) {
        return writeret;
    }
    return wrotelen;
}

ssize_t recv(int socket, void *buffer, size_t length, int flags) {
    HOOK_SYS_FUNC(recv);

    if (!co_is_enable_sys_hook()) {
        return g_sys_recv_func(socket, buffer, length, flags);
    }
    struct rpchook_t *lp = get_by_fd(socket);

    if (!lp || (O_NONBLOCK & lp->user_flag)) {
        return g_sys_recv_func(socket, buffer, length, flags);
    }
    int timeout = (int) ((lp->read_timeout.tv_sec * 1000)
                         + (lp->read_timeout.tv_usec / 1000));

    struct pollfd pf = {0};
    pf.fd = socket;
    pf.events = (POLLIN | POLLERR | POLLHUP);

    poll(&pf, 1, timeout);

    ssize_t readret = g_sys_recv_func(socket, buffer, length, flags);

    if (readret < 0) {
        //TODO: logger
//        co_log_err("CO_ERR: read fd %d ret %ld errno %d poll ret %d timeout %d",
//                   socket, readret, errno, pollret, timeout);
    }

    return readret;

}

extern int co_poll_inner(co_epoll_t *ctx, struct pollfd fds[], nfds_t nfds, int timeout, poll_pfn_t pollfunc);

int poll(struct pollfd fds[], nfds_t nfds, int timeout) {

    HOOK_SYS_FUNC(poll);

    if (!co_is_enable_sys_hook()) {
        return g_sys_poll_func(fds, nfds, timeout);
    }

    return co_poll_inner(co_get_epoll_ct(), fds, nfds, timeout, g_sys_poll_func);

}

int setsockopt(int fd, int level, int option_name,
               const void *option_value, socklen_t option_len) {
    HOOK_SYS_FUNC(setsockopt);

    if (!co_is_enable_sys_hook()) {
        return g_sys_setsockopt_func(fd, level, option_name, option_value, option_len);
    }
    struct rpchook_t *lp = get_by_fd(fd);

    if (lp && SOL_SOCKET == level) {
        struct timeval *val = (struct timeval *) option_value;
        if (SO_RCVTIMEO == option_name) {
            memcpy(&lp->read_timeout, val, sizeof(*val));
        } else if (SO_SNDTIMEO == option_name) {
            memcpy(&lp->write_timeout, val, sizeof(*val));
        }
    }
    return g_sys_setsockopt_func(fd, level, option_name, option_value, option_len);
}


int fcntl(int fildes, int cmd, ...) {
    HOOK_SYS_FUNC(fcntl);

    if (fildes < 0) {
        return __LINE__;
    }

    va_list arg_list;
    va_start(arg_list, cmd);

    int ret = -1;
    struct rpchook_t *lp = get_by_fd(fildes);
    switch (cmd) {
        case F_DUPFD: {
            int param = va_arg(arg_list, int);
            ret = g_sys_fcntl_func(fildes, cmd, param);
            break;
        }
        case F_GETFD: {
            ret = g_sys_fcntl_func(fildes, cmd);
            break;
        }
        case F_SETFD: {
            int param = va_arg(arg_list, int);
            ret = g_sys_fcntl_func(fildes, cmd, param);
            break;
        }
        case F_GETFL: {
            ret = g_sys_fcntl_func(fildes, cmd);
            break;
        }
        case F_SETFL: {
            int param = va_arg(arg_list, int);
            int flag = param;
            if (co_is_enable_sys_hook() && lp) {
                flag |= O_NONBLOCK;
            }
            ret = g_sys_fcntl_func(fildes, cmd, flag);
            if (0 == ret && lp) {
                lp->user_flag = param;
            }
            break;
        }
        case F_GETOWN: {
            ret = g_sys_fcntl_func(fildes, cmd);
            break;
        }
        case F_SETOWN: {
            int param = va_arg(arg_list, int);
            ret = g_sys_fcntl_func(fildes, cmd, param);
            break;
        }
        case F_GETLK: {
            struct flock *param = va_arg(arg_list, struct flock *);
            ret = g_sys_fcntl_func(fildes, cmd, param);
            break;
        }
        case F_SETLK: {
            struct flock *param = va_arg(arg_list, struct flock *);
            ret = g_sys_fcntl_func(fildes, cmd, param);
            break;
        }
        case F_SETLKW: {
            struct flock *param = va_arg(arg_list, struct flock *);
            ret = g_sys_fcntl_func(fildes, cmd, param);
            break;
        }
    }

    va_end(arg_list);

    return ret;
}

struct co_sys_env_s {
    char *name;
    char *value;
};
struct co_sys_env_arr_s {
    struct co_sys_env_s *data;
    size_t cnt;
};

static struct co_sys_env_arr_s *dup_co_sysenv_arr(struct co_sys_env_arr_s *arr) {
    struct co_sys_env_arr_s *lp = calloc(sizeof(struct co_sys_env_arr_s), 1);
    if (arr->cnt) {
        lp->data = (struct co_sys_env_s *) calloc(sizeof(struct co_sys_env_s) * arr->cnt, 1);
        lp->cnt = arr->cnt;
        memcpy(lp->data, arr->data, sizeof(struct co_sys_env_s) * arr->cnt);
    }
    return lp;
}

static int co_sysenv_comp(const void *a, const void *b) {
    return strcmp(((struct co_sys_env_s *) a)->name, ((struct co_sys_env_s *) b)->name);
}

static struct co_sys_env_arr_s g_co_sysenv = {0};

void co_set_env_list(const char *name[], size_t cnt) {
    if (g_co_sysenv.data) {
        return;
    }
    g_co_sysenv.data = calloc(1, sizeof(struct co_sys_env_s) * cnt);

    for (size_t i = 0; i < cnt; i++) {
        if (name[i] && name[i][0]) {
            g_co_sysenv.data[g_co_sysenv.cnt++].name = strdup(name[i]);
        }
    }
    if (g_co_sysenv.cnt > 1) {
        qsort(g_co_sysenv.data, g_co_sysenv.cnt, sizeof(struct co_sys_env_s), co_sysenv_comp);
        struct co_sys_env_s *lp = g_co_sysenv.data;
        struct co_sys_env_s *lq = g_co_sysenv.data + 1;
        for (size_t i = 1; i < g_co_sysenv.cnt; i++) {
            if (strcmp(lp->name, lq->name)) {
                ++lp;
                if (lq != lp) {
                    *lp = *lq;
                }
            }
            ++lq;
        }
        g_co_sysenv.cnt = lp - g_co_sysenv.data + 1;
    }

}

int setenv(const char *n, const char *value, int overwrite) {
    HOOK_SYS_FUNC(setenv)
    if (co_is_enable_sys_hook() && g_co_sysenv.data) {
        co_routine_t *self = co_self();
        if (self) {
            if (!self->pv_env) {
                self->pv_env = dup_co_sysenv_arr(&g_co_sysenv);
            }
            struct co_sys_env_arr_s *arr = self->pv_env;

            struct co_sys_env_s name = {(char *) n, 0};

            struct co_sys_env_s *e = bsearch(&name, arr->data, arr->cnt, sizeof(name), co_sysenv_comp);

            if (e) {
                if (overwrite || !e->value) {
                    if (e->value) free(e->value);
                    e->value = (value ? strdup(value) : 0);
                }
                return 0;
            }
        }

    }
    return g_sys_setenv_func(n, value, overwrite);
}

int unsetenv(const char *n) {
    HOOK_SYS_FUNC(unsetenv)
    if (co_is_enable_sys_hook() && g_co_sysenv.data) {
        co_routine_t *self = co_self();
        if (self) {
            if (!self->pv_env) {
                self->pv_env = dup_co_sysenv_arr(&g_co_sysenv);
            }
            struct co_sys_env_arr_s *arr = self->pv_env;

            struct co_sys_env_s name = {(char *) n, 0};

            struct co_sys_env_s *e = bsearch(&name, arr->data, arr->cnt, sizeof(name), co_sysenv_comp);

            if (e) {
                if (e->value) {
                    free(e->value);
                    e->value = 0;
                }
                return 0;
            }
        }

    }
    return g_sys_unsetenv_func(n);
}

char *getenv(const char *n) {
    HOOK_SYS_FUNC(getenv)
    if (co_is_enable_sys_hook() && g_co_sysenv.data) {
        co_routine_t *self = co_self();

        struct co_sys_env_s name = {(char *) n, 0};

        if (!self->pv_env) {
            self->pv_env = dup_co_sysenv_arr(&g_co_sysenv);
        }
        struct co_sys_env_arr_s *arr = (struct co_sys_env_arr_s *) (self->pv_env);

        struct co_sys_env_s *e = bsearch(&name, arr->data, arr->cnt, sizeof(name), co_sysenv_comp);

        if (e) {
            return e->value;
        }

    }
    return g_sys_getenv_func(n);

}

struct hostent *co_gethostbyname(const char *name);

struct hostent *gethostbyname(const char *name) {
    HOOK_SYS_FUNC(gethostbyname);

#ifdef __APPLE__
    return g_sys_gethostbyname_func(name);
#else
    if (!co_is_enable_sys_hook())
    {
        return g_sys_gethostbyname_func(name);
    }
    return co_gethostbyname(name);
#endif

}


struct res_state_wrap {
    struct __res_state state;
};
CO_ROUTINE_SPECIFIC(res_state_wrap);

res_state __res_state() {
    HOOK_SYS_FUNC(__res_state);

    if (!co_is_enable_sys_hook()) {
        return g_sys___res_state_func();
    }

    return &(routine_data_routine_res_state_wrap()->state);
}

int __poll(struct pollfd fds[], nfds_t nfds, int timeout) {
    return poll(fds, nfds, timeout);
}

struct hostbuf_wrap {
    struct hostent host;
    char *buffer;
    size_t iBufferSize;
    int host_errno;
};

CO_ROUTINE_SPECIFIC(hostbuf_wrap);

#ifndef __APPLE__
struct hostent *co_gethostbyname(const char *name)
{
    if (!name) {
        return NULL;
    }

    struct hostbuf_wrap *__co_hostbuf_wrap = routine_data_routine_hostbuf_wrap();

    if (__co_hostbuf_wrap->buffer && __co_hostbuf_wrap->iBufferSize > 1024) {
        free(__co_hostbuf_wrap->buffer);
        __co_hostbuf_wrap->buffer = NULL;
    }
    if (!__co_hostbuf_wrap->buffer) {
        __co_hostbuf_wrap->buffer = (char*)malloc(1024);
        __co_hostbuf_wrap->iBufferSize = 1024;
    }

    struct hostent *host = &__co_hostbuf_wrap->host;
    struct hostent *result = NULL;
    int *h_errnop = &(__co_hostbuf_wrap->host_errno);

    int ret = -1;
    while ((ret = gethostbyname_r(name, host, __co_hostbuf_wrap->buffer,
                __co_hostbuf_wrap->iBufferSize, &result, h_errnop) == ERANGE) &&
                (*h_errnop == NETDB_INTERNAL)) {
        free(__co_hostbuf_wrap->buffer);
        __co_hostbuf_wrap->iBufferSize = __co_hostbuf_wrap->iBufferSize * 2;
        __co_hostbuf_wrap->buffer = (char*)malloc(__co_hostbuf_wrap->iBufferSize);
    }

    if (ret == 0 && (host == result)) {
        return host;
    }
    return NULL;
}
#endif

void co_enable_hook_sys() { //这函数必须在这里,否则本文件会被忽略！！！
    co_routine_t *co = get_curr_thread_co();
    if (co) {
        co->enable_sys_hook = 1;
    }
}
