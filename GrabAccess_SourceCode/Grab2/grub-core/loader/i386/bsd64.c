#define SUFFIX(x) x ## 64
#define GRUB_TARGET_WORDSIZE 64
#define OBJSYM 1
#include <grub/types.h>
typedef grub_uint64_t grub_freebsd_addr_t;
#include "bsdXX.c"
