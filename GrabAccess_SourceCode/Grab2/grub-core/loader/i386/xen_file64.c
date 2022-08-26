#define GRUB_TARGET_WORDSIZE 64
#define XX		64
#define grub_le_to_cpu_addr grub_le_to_cpu64
#define ehdrXX ehdr64
#define grub_xen_get_infoXX grub_xen_get_info64
#define FOR_ELF_PHDRS FOR_ELF64_PHDRS
#include "xen_fileXX.c"
