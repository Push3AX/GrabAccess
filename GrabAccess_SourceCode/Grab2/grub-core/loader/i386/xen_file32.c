#define GRUB_TARGET_WORDSIZE 32
#define XX		32
#define grub_le_to_cpu_addr grub_le_to_cpu32
#define ehdrXX ehdr32
#define grub_xen_get_infoXX grub_xen_get_info32
#define FOR_ELF_PHDRS FOR_ELF32_PHDRS
#include "xen_fileXX.c"
