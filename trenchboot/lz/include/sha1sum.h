#ifndef __SHA1SUM_H__
#define __SHA1SUM_H__

#define SHA1_TOTAL_BYTES 20

typedef struct {
    u32  h0,h1,h2,h3,h4;
    u32  nblocks;
    unsigned char buf[64];
    int  count;
} SHA1_CONTEXT;

void sha1sum(SHA1_CONTEXT *ctx, void *ptr, u32 len);

#endif /* __SHA1SUM_H__ */
