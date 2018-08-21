#ifndef __DEFS_H__
#define __DEFS_H__

#define PAGE_SHIFT       12
#define PAGE_SIZE        (1 << PAGE_SHIFT)
#define PAGE_MASK        (~(PAGE_SIZE-1))

#ifdef __ASSEMBLY__
#define ENTRY(name)                             \
  .globl name;                                  \
  name:

#define ENTRY_ALIGN(name)                       \
  .globl name;                                  \
  .align 16,0x90;                               \
  name:
#endif

#define __packed        __attribute__ ((packed))
#define __maybe_unused  __attribute__ ((unused))

#endif /* __DEFS_H__ */
