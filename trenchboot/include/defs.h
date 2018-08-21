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

/* MSRs */

#define MSR_EFER  0xc0000080
#define VM_CR_MSR 0xc0010114

/* EFER bits */
#define EFER_SCE 0  /* SYSCALL/SYSRET */
#define EFER_LME 8  /* Long Mode enable */
#define EFER_LMA 10 /* Long Mode Active (read-only) */
#define EFER_NXE 11  /* no execute */
#define EFER_SVME 12   /* SVM extensions enable */

/* VM CR MSR bits */
#define VM_CR_DPD 0
#define VM_CR_R_INIT 1
#define VM_CR_DIS_A20M 2
#define VM_CR_SVME_DISABLE 4

#endif /* __DEFS_H__ */
