#define SUFFIX(x) x ## 32
#define GRUB_TARGET_WORDSIZE 32
#define OBJSYM 0
#include <grub/types.h>
typedef grub_uint32_t grub_freebsd_addr_t;
#include "bsdXX.c"
