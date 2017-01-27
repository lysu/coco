#include "cc_ctx.h"
#include <string.h>

enum {
    kRDI = 7,
    kRSI = 8,
    kRETAddr = 9,
    kRSP = 13,
};

int coctx_init(coctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    return 0;
}

int coctx_make(coctx_t *ctx, coctx_pfn_t pfn, const void *s, const void *s1) {
    char *sp = ctx->ss_sp + ctx->ss_size;
    sp = (char *) ((unsigned long) sp & -16LL);
    memset(ctx->regs, 0, sizeof(ctx->regs));
    ctx->regs[kRSP] = sp - 8;
    ctx->regs[kRETAddr] = (char *) pfn;
    ctx->regs[kRDI] = (char *) s;
    ctx->regs[kRSI] = (char *) s1;
    return 0;
}
