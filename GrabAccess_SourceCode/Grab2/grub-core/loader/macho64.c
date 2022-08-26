#include <grub/macho.h>
#include <grub/machoload.h>

#define SUFFIX(x) x ## 64
typedef struct grub_macho_header64 grub_macho_header_t;
typedef struct grub_macho_segment64 grub_macho_segment_t;
typedef grub_uint64_t grub_macho_addr_t;
typedef struct grub_macho_thread64 grub_macho_thread_t;
#define offsetXX offset64
#define ncmdsXX ncmds64
#define cmdsizeXX cmdsize64
#define cmdsXX cmds64
#define endXX end64
#define uncompressedXX uncompressed64
#define compressedXX compressed64
#define uncompressed_sizeXX uncompressed_size64
#define compressed_sizeXX compressed_size64
#define XX "64"
#define GRUB_MACHO_MAGIC GRUB_MACHO_MAGIC64
#define GRUB_MACHO_CMD_SEGMENT GRUB_MACHO_CMD_SEGMENT64
#include "machoXX.c"

