#include <grub/macho.h>
#include <grub/machoload.h>

#define SUFFIX(x) x ## 32
typedef struct grub_macho_header32 grub_macho_header_t;
typedef struct grub_macho_segment32 grub_macho_segment_t;
typedef grub_uint32_t grub_macho_addr_t;
typedef struct grub_macho_thread32 grub_macho_thread_t;
#define offsetXX offset32
#define ncmdsXX ncmds32
#define cmdsizeXX cmdsize32
#define cmdsXX cmds32
#define endXX end32
#define uncompressedXX uncompressed32
#define compressedXX compressed32
#define uncompressed_sizeXX uncompressed_size32
#define compressed_sizeXX compressed_size32
#define XX "32"
#define GRUB_MACHO_MAGIC GRUB_MACHO_MAGIC32
#define GRUB_MACHO_CMD_SEGMENT GRUB_MACHO_CMD_SEGMENT32
#include "machoXX.c"

