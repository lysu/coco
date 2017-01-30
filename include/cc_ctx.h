#ifndef LIBCOCO_CC_CTX_H
#define LIBCOCO_CC_CTX_H

#include <stdlib.h>

typedef void *(*coctx_pfn_t)(void *s, void *s2);

struct coctx_s {
#if defined(__i386__)
    void *regs[ 8 ];
#else
    void *regs[14];
#endif
    size_t ss_size;
    char *ss_sp;
};

typedef struct coctx_s coctx_t;

int coctx_init(coctx_t *ctx);
int coctx_make(coctx_t *ctx, coctx_pfn_t pfn, const void *s, const void *s1);

#endif //LIBCOCO_CC_CTX_H
