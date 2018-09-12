
#include <types.h>

void* memcpy(void* dst, const void* src, size_t count)
{
	u8* dst8 = (u8*)dst;
	u8* src8 = (u8*)src;

	while (count--)
		*dst8++ = *src8++;

	return dst;
}
