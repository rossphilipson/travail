#include <types.h>

#ifndef __MEM_H__
#define __MEM_H__

static inline void *memcpy(void *dst, const void *src, size_t count)
{
	u8* dst8 = (u8*)dst;
	u8* src8 = (u8*)src;

	while (count--)
		*dst8++ = *src8++;

	return dst;
}

static inline void *memset(void *s, int c, u32 n)
{
    char *buf = (char*)s;

    for ( ; n--; )
        *buf++ = c;

    return buf;
}

#endif /* __MEM_H__ */
