#define MKIMAGE_ELF64 1

# define SUFFIX(x)	x ## 64
# define ELFCLASSXX	ELFCLASS64
# define Elf_Ehdr	Elf64_Ehdr
# define Elf_Phdr	Elf64_Phdr
# define Elf_Nhdr	Elf64_Nhdr
# define Elf_Addr	Elf64_Addr
# define Elf_Sym	Elf64_Sym
# define Elf_Off	Elf64_Off
# define Elf_Shdr	Elf64_Shdr
# define Elf_Rela       Elf64_Rela
# define Elf_Rel        Elf64_Rel
# define Elf_Word       Elf64_Word
# define Elf_Half       Elf64_Half
# define Elf_Section    Elf64_Section
# define ELF_R_SYM(val)		ELF64_R_SYM(val)
# define ELF_R_TYPE(val)		ELF64_R_TYPE(val)
# define ELF_ST_TYPE(val)		ELF64_ST_TYPE(val)

#define XEN_NOTE_SIZE		120
#define XEN_PVH_NOTE_SIZE	24

#ifndef GRUB_MKIMAGEXX
#include "grub-mkimagexx.c"
#endif
