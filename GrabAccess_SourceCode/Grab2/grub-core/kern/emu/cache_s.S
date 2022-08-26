#ifndef GRUB_MACHINE_EMU
#error "This source is only meant for grub-emu platform"
#endif

/* An executable stack is not required for these functions. */
#if defined (__linux__) && defined (__ELF__)
.section .note.GNU-stack,"",%progbits
#endif

#if defined(__i386__) || defined(__x86_64__)
/* Nothing is necessary.  */
#elif defined(__sparc__)
#include "../sparc64/cache.S"
#elif defined(__powerpc__)
#include "../powerpc/cache.S"
#elif defined(__ia64__) || defined(__arm__) || defined(__aarch64__) || \
      defined(__mips__) || defined(__riscv)
#else
#error "No target cpu type is defined"
#endif
