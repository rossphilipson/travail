/*
 * string.c:  provides string formatting fns
 *
 * Copyright (c) 2010-2011, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <types.h>
#include <stdbool.h>
#include <slexec.h>
#include <ctype.h>
#include <string.h>
#include <misc.h>

static bool div64(uint64_t num, uint32_t base, uint64_t *quot, uint32_t *rem)
{
    uint32_t high = num >> 32;
    uint32_t low = (uint32_t)num;

    /* check exceptions */
    if ( (quot == NULL) || (rem == NULL) || (base == 0) )
        return false;

    if ( high == 0 ) {
        *quot = low / base;
        *rem = low % base;
    }
    else {
        uint64_t hquo = high / base;
        uint32_t hrem = high % base;
        uint32_t lquo;

        /*
         * use "divl" instead of "/" to avoid the link error
         * undefined reference to `__udivdi3'
         */
        asm volatile ( "divl %4;"
                       : "=a"(lquo), "=d"(*rem)
                       : "a"(low), "d"(hrem), "r"(base));
        *quot = (hquo << 32) + lquo;
    }

    return true;
}

/*
 * write the character into the buffer
 * return the position of the buffer after writting
 */
static unsigned long write_char_to_buffer(char *buf, size_t buf_len,
                                          unsigned long buf_pos, char ch)
{
    /* check buffer overflow? */
    if ( buf_pos >= buf_len )
        return 0;

    *(buf + buf_pos) = ch;
    return buf_pos + 1;
}

/*
 * write pad_len pads into the buffer
 * return the position of the buffer after writting
 */
static unsigned long write_pads_to_buffer(char *buf, size_t buf_len,
                                          unsigned long buf_pos, char pad,
                                          size_t pad_len)
{
    for ( unsigned int i = 0; i < pad_len; i++ )
        buf_pos = write_char_to_buffer(buf, buf_len, buf_pos, pad);

    return buf_pos;
}

/* %[flags][width][.precision][length]specifier */
typedef struct {
    /* flag */
#define LEFT_ALIGNED (1 << 0) /* '-' */
#define SIGNED       (1 << 1) /* '+' */
#define SPACE        (1 << 2) /* ' ' */
#define PREFIX       (1 << 3) /* '#' */
#define ZERO_PADDED  (1 << 4) /* '0' */
    int flag;
    /* width & precision */
    unsigned int width, precision;
    /* length */
    enum {NORM, LONG, LONGLONG} flag_long;
    /* specifier */
    int base;
    bool cap;
    bool sign;
    bool digit;
} modifiers_t;

/*
 * write the string into the buffer regarding flags
 * return the position of the buffer after writing
 */
static unsigned long write_string_to_buffer(char *buf, size_t buf_len,
                                            unsigned long buf_pos,
                                            const char* str, size_t strlen,
                                            modifiers_t *mods)
{
    unsigned int i;

    if (mods->precision > 0)
        strlen = (strlen > mods->precision) ? mods->precision : strlen;

    mods->width = ( mods->width > strlen ) ? mods->width - strlen : 0;
    if ( mods->flag & LEFT_ALIGNED ) { /* left align */
        for ( i = 0; i < strlen; i++ )
            buf_pos = write_char_to_buffer(buf, buf_len, buf_pos, str[i]);
        buf_pos = write_pads_to_buffer(buf, buf_len, buf_pos, ' ', mods->width);
    }
    else { /* right align */
        /* if not digit, don't considering pad '0' */
        char pad = ( mods->digit && (mods->flag & ZERO_PADDED) ) ? '0' : ' ';

        buf_pos = write_pads_to_buffer(buf, buf_len, buf_pos, pad, mods->width);
        for ( i = 0; i < strlen; i++ )
            buf_pos = write_char_to_buffer(buf, buf_len, buf_pos, str[i]);
    }

    return buf_pos;
}

/* convert a integer to a string regarding flags, qualifier, specifier, etc. */
static size_t int2str(long long val, char *str, size_t strlen,
                      const modifiers_t *mods)
{
    unsigned int i;
    size_t length = 0, number_length = 0;
    unsigned long number_start = 0;
    const char hexdig_lowercase[] = "0123456789abcdef";
    const char hexdig_uppercase[] = "0123456789ABCDEF";
    unsigned long long nval;

    /* check, we support octal/decimal/hex only */
    if ( (mods->base != 8) && (mods->base != 10) && (mods->base != 16) )
        return 0;

    if ( str == NULL || strlen == 0 )
        return 0;

    if ( mods->flag & PREFIX ) {
        if ( mods->base == 8 )
            *(str + length++) = '0'; /* add prefix 0 for octal */
        else if ( mods->base == 16 ) {
            if ( strlen < 2 )
                return 0;

            /* add prefix 0x/0X for hex */
            *(str + length++) = '0';
            *(str + length++) = ( mods->cap ) ? 'X' : 'x';
        }
    }

    /*
     * if it is shown as signed decimal(%d), we consider to add -/+/' '
     * but, if it is an unsigned number, no need to add -/+/' '
     */
    if ( mods->base == 10 && mods->sign ) {
        if ( val < 0 ) { /* negative */
            *(str + length++) = '-';
            val = -val;
        }
        else { /* positive */
            if ( mods->flag & SIGNED )
                *(str + length++) = '+';
            else if ( mods->flag & SPACE )
                *(str + length++) = ' ';
        }
    }

    /* truncate to unsigned long or unsigned int if type of val is */
    if ( mods->flag_long == LONGLONG )
        nval = (unsigned long long)val;
    else if ( mods->flag_long == LONG )
        nval = (unsigned long long)(unsigned long)val;
    else
        nval = (unsigned long long)(unsigned int)val;

    /* convert */
    number_start = length;
    do {
        /* overflow? */
        if ( length >= strlen )
            break;

        uint32_t rem = 0;
        if ( !div64(nval, mods->base, &nval, &rem) )
            return 0;
        *(str + length) = ( mods->cap ) ?
            hexdig_uppercase[rem] : hexdig_lowercase[rem];
        length++;
        number_length++;
    } while ( nval );

    /* handle precision */
    while ( number_length < mods->precision ) {
        /* overflow? */
        if ( length >= strlen )
            break;

        *(str + length) = '0';
        length++;
        number_length++;
    }

    /* reverse */
    for ( i = 0; i < number_length/2; i++ ) {
        char ch;

        ch = *(str + number_start + i);
        *(str + number_start + i)
            = *(str + number_start + (number_length - i - 1));
        *(str + number_start + (number_length - i - 1)) = ch;
    }

    return length;
}

int sl_vscnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    unsigned int buf_pos = 0; /* return value doesn't count the last '\0' */
    const char *fmt_ptr;
    modifiers_t mods;

    /* check buf */
    if ( (buf == NULL) || (size == 0) )
        return 0;

    /* check fmt */
    if ( fmt == NULL )
        return 0;

    sl_memset(&mods, 0, sizeof(mods));

    while ( buf_pos < size ) {
        bool success;

        /* handle normal characters */
        while ( *fmt != '%' ) {
                buf_pos = write_char_to_buffer(buf, size, buf_pos, *fmt);
            if ( *fmt == '\0' )
                return buf_pos - 1;
            fmt++;
        }

        /* handle %: %[flags][width][.precision][length]specifier */
        /*
         * start to parse the syntax of %, save the position of fmt
         * in case that append the string to the buffer if % substring
         * doesnot match the syntax
         */
        fmt_ptr = fmt + 1; /* skip '%' */
        success = true;    /* assume parsing % substring would succeed */

        /* parsing flags */
        while ( true ) {
            switch ( *fmt_ptr ) {
            case '-':
                mods.flag |= LEFT_ALIGNED;
                break;
            case '+':
                mods.flag |= SIGNED ;
                break;
            case ' ':
                mods.flag |= SPACE;
                break;
            case '#':
                mods.flag |= PREFIX;
                break;
            case '0':
                mods.flag |= ZERO_PADDED;
                break;
            default:
                goto handle_width;
            }
            fmt_ptr++;
        }

        /* parsing width */
handle_width:
        if ( *fmt_ptr == '*' ) {
            mods.width = va_arg(ap, int);
            fmt_ptr++;
        }
        else
            mods.width = sl_strtoul(fmt_ptr, (char **)&fmt_ptr, 10);

        if ( *fmt_ptr == '.' ) {
            /* skip . */
            fmt_ptr++;

            /* parsing precision */
            if ( *fmt_ptr == '*' ) {
                mods.precision = va_arg(ap, int);
                fmt_ptr++;
            }
            else
                mods.precision = sl_strtoul(fmt_ptr, (char **)&fmt_ptr, 10);
        }

        /* parsing qualifier: h l L;
         * h is ignored here, and 'L' and 'j' are treated as 'll'
         */
        mods.flag_long = NORM;
        if ( *fmt_ptr == 'L' || *fmt_ptr == 'j' ) {
            mods.flag_long = LONGLONG;
            fmt_ptr++;
        }
        else if ( *fmt_ptr == 'l' && *(fmt_ptr + 1) == 'l' ) {
            mods.flag_long = LONGLONG;
            fmt_ptr += 2;
        }
        else if ( *fmt_ptr == 'l' ) {
            mods.flag_long = LONG;
            fmt_ptr++;
        }

#define write_number_to_buffer(__buf, __size, __buf_pos, __mods)           \
({                                                                         \
    char __str[32];                                                        \
    size_t __real_strlen;                                                  \
    if ( __mods.flag_long == LONGLONG ) {                                  \
        long long __number = 0;                                            \
        __number = va_arg(ap, long long);                                  \
        __real_strlen = int2str(__number, __str, sizeof(__str), &__mods);  \
    }                                                                      \
    else if ( __mods.flag_long == LONG ) {                                 \
        long __number = 0;                                                 \
        __number = va_arg(ap, long);                                       \
        __real_strlen = int2str(__number, __str, sizeof(__str), &__mods);  \
    }                                                                      \
    else {                                                                 \
        int __number = 0;                                                  \
        __number = va_arg(ap, int);                                        \
        __real_strlen = int2str(__number, __str, sizeof(__str), &__mods);  \
    }                                                                      \
    __mods.digit = true;                                                   \
    write_string_to_buffer(                                                \
        __buf, __size, __buf_pos, __str, __real_strlen, &__mods);          \
})

        /* parsing specifier */
        mods.base = 10;
        mods.cap = mods.sign = false;
        switch ( *fmt_ptr ) {
        case 'c':
            {
                char str[1];

                str[0] = (char)va_arg(ap, int);
                mods.digit = false;
                buf_pos = write_string_to_buffer(
                              buf, size, buf_pos, str, sl_strlen(str), &mods);
                break;
            }
        case 's':
            {
                char *str;

                str = va_arg(ap, char *);
                mods.digit = false;
                buf_pos = write_string_to_buffer(
                              buf, size, buf_pos, str, sl_strlen(str), &mods);
                break;
            }
        case 'o':
            mods.base = 8;
            buf_pos = write_number_to_buffer(buf, size, buf_pos, mods);
            break;

        case 'X':
            mods.cap = true;
            mods.base = 16;
            buf_pos = write_number_to_buffer(buf, size, buf_pos, mods);
            break;

        case 'p':
            mods.flag |= PREFIX;    /* print prefix 0x for %p */
            mods.flag_long = LONG;
        /* FALLTHROUGH */
        case 'x':
            mods.base = 16;
            buf_pos = write_number_to_buffer(buf, size, buf_pos, mods);
            break;

        case 'i':
        case 'd':
            mods.sign = true;
            buf_pos = write_number_to_buffer(buf, size, buf_pos, mods);
            break;

        case 'u':
            buf_pos = write_number_to_buffer(buf, size, buf_pos, mods);
            break;
        case 'e':
        case 'E':
            /* ignore */
            break;
        case '%':
            buf_pos = write_char_to_buffer(buf, size, buf_pos, '%');
            break;
        default:
            success = false;
            break;
        } /* switch for specifier */

        fmt_ptr++;         /* skip the above character */
        if ( success )
            fmt = fmt_ptr;
        else {
            /* parsing % substring error, treat it as a normal string */
            /* *fmt = '%' */
            buf_pos = write_char_to_buffer(buf, size, buf_pos, *fmt++);
        }
    } /* while */

    buf[buf_pos - 1] = '\0'; /* if the buffer is overflowed. */
    return buf_pos - 1;
}

int sl_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int count;

    va_start(ap, fmt);
    count = sl_vscnprintf(buf, size, fmt, ap);
    va_end(ap);

    return count;
}

/*
 * index() is also present as the strchr() in the kernel; it does exactly the
 * same thing as it's userland equivalent.
 */
char *sl_index(const char *p, int ch)
{
    union {
        const char *cp;
        char *p;
    } u;

    if (p == NULL)
        return(NULL);

    u.cp = p;

    for (;; ++u.p) {
        if (*u.p == ch)
            return (u.p);
        if (*u.p == '\0')
            return NULL;
    }
    /* NOTREACHED */
}

/*
 * Compare memory regions.
 */
int sl_memcmp(const void *s1, const void *s2, size_t n)
{
    if (s1 == NULL || s2 == NULL)
        return (-1);

    if (s1 == s2)
        return (0);

    if (n != 0) {
        const unsigned char *p1 = s1, *p2 = s2;

        do {
            if (*p1++ != *p2++)
                return (*--p1 - *--p2);
        } while (--n != 0);
    }

    return (0);
}

/*
 * sizeof(word) MUST BE A POWER OF TWO
 * SO THAT wmask BELOW IS ALL ONES
 */
typedef	int    word;		/* "word" used for optimal copy speed */

#define	wsize  sizeof(word)
#define wmask  (wsize - 1)

/*
 * Copy a block of memory, handling overlap.
 * This is the routine that actually implements
 * (the portable versions of) bcopy, memcpy, and memmove.
 */
void *sl_memcpy(void *dst0, const void *src0, size_t length)
{
    char *dst;
    const char *src;
    size_t t;

    dst = dst0;
    src = src0;

    if (dst0 == NULL || src0 == NULL)
        return NULL;

    if (length == 0 || dst == src) /* nothing to do */
        goto done;

/* Macros: loop-t-times; and loop-t-times, t>0 */
#define	TLOOP(s) if (t) TLOOP1(s)
#define	TLOOP1(s) do { s; } while (--t)

    if ((unsigned long)dst < (unsigned long)src) {
        /* Copy forward. */
        t = (int)src; /* only need low bits */

        if ((t | (int)dst) & wmask) {
            /*
             * Try to align operands.  This cannot be done
             * unless the low bits match.
             */
            if ((t ^ (int)dst) & wmask || length < wsize)
                t = length;
            else
                t = wsize - (t & wmask);

            length -= t;
            TLOOP1(*dst++ = *src++);
        }

        /* Copy whole words, then mop up any trailing bytes. */
        t = length / wsize;
        TLOOP(*(word *)dst = *(const word *)src; src += wsize;
        dst += wsize);
        t = length & wmask;
        TLOOP(*dst++ = *src++);
    } else {
        /*
         * Copy backwards.  Otherwise essentially the same.
         * Alignment works as before, except that it takes
         * (t&wmask) bytes to align, not wsize-(t&wmask).
         */
        src += length;
        dst += length;
        t = (int)src;

        if ((t | (int)dst) & wmask) {
            if ((t ^ (int)dst) & wmask || length <= wsize)
                t = length;
            else
                t &= wmask;

            length -= t;
            TLOOP1(*--dst = *--src);
        }

        t = length / wsize;
        TLOOP(src -= wsize; dst -= wsize;
        *(word *)dst = *(const word *)src);
        t = length & wmask;
        TLOOP(*--dst = *--src);
    }

done:
    return (dst0);
}

/*
 * Compare strings.
 */
int sl_strcmp(register const char *s1, register const char *s2)
{
    if (s1 == NULL || s2 == NULL)
        return (-1);
    if (s1 == s2)
        return (0);

    while (*s1 == *s2++)
        if (*s1++ == 0)
            return (0);

    return (*(const unsigned char *)s1 - *(const unsigned char *)(s2 - 1));
}

size_t sl_strlen(str)
	const char *str;
{
	register const char *s;

	if (str == NULL)
		return 0;
	for (s = str; *s; ++s);
	return(s - str);
}

int sl_strncmp(register const char *s1, register const char *s2,
               register size_t n)
{
    if (s1 == NULL || s2 == NULL)
        return(-1);
    if (n == 0 || s1 == s2)
        return (0);

    do {
        if (*s1 != *s2++)
            return (*(const unsigned char *)s1 -
                    *(const unsigned char *)(s2 - 1));
        if (*s1++ == 0)
            break;
    } while (--n != 0);

    return (0);
}

/*
 * Copy src to dst, truncating or null-padding to always copy n bytes.
 * Return dst.
 */
char *sl_strncpy(char * __restrict dst, const char * __restrict src, size_t n)
{
    if (dst == NULL || src == NULL)
        return NULL;

    if (n != 0) {
        register char *d = dst;
        register const char *s = src;

        do {
            if ((*d++ = *s++) == 0) {
                /* NUL pad the remaining n-1 bytes */
                while (--n != 0)
                   *d++ = 0;
                break;
            }
        } while (--n != 0);
    }

    return (dst);
}

#define ULONG_MAX     0xFFFFFFFFUL

/*
 * Convert a string to an unsigned long integer.
 *
 * Ignores `locale' stuff.  Assumes that the upper and lower case
 * alphabets and digits are each contiguous.
 */
unsigned long sl_strtoul(const char *nptr, char **endptr, int base)
{
    const char *s = nptr;
    unsigned long acc, cutoff;
    unsigned char c;
    int neg = 0, any, cutlim;

    if (nptr == NULL)
        return ULONG_MAX;

    /* See strtol for comments as to the logic used. */
    do {
        c = *s++;
    } while (isspace(c));

    if (c == '-') {
        neg = 1;
        c = *s++;
    } else if (c == '+')
        c = *s++;

    if ((base == 0 || base == 16) &&
        c == '0' && (*s == 'x' || *s == 'X')) {
        c = s[1];
        s += 2;
        base = 16;
    }

    if (base == 0)
        base = c == '0' ? 8 : 10;

    cutoff = (unsigned long)ULONG_MAX / (unsigned long)base;
    cutlim = (unsigned long)ULONG_MAX % (unsigned long)base;
    for (acc = 0, any = 0;; c = *s++) {
        if (isdigit(c))
            c -= '0';
        else if (isalpha(c))
            c -= isupper(c) ? 'A' - 10 : 'a' - 10;
        else
            break;

        if (c >= base)
            break;

        if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim))
            any = -1;
        else {
            any = 1;
            acc *= base;
            acc += c;
        }
    }

    if (any < 0)
        acc = ULONG_MAX;
    else if (neg)
        acc = -acc;

    if (endptr != 0)
        *((const char **)endptr) = any ? s - 1 : nptr;

    return (acc);
}

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
