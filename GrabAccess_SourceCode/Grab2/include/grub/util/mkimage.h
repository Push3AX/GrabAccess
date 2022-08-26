/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef GRUB_UTIL_MKIMAGE_HEADER
#define GRUB_UTIL_MKIMAGE_HEADER	1

struct grub_mkimage_layout
{
  size_t exec_size;
  size_t kernel_size;
  size_t bss_size;
  size_t sbat_size;
  grub_uint64_t start_address;
  void *reloc_section;
  size_t reloc_size;
  size_t align;
  grub_size_t ia64jmp_off;
  grub_size_t tramp_off;
  grub_size_t got_off;
  grub_size_t got_size;
  unsigned ia64jmpnum;
  grub_uint32_t bss_start;
  grub_uint32_t end;
};

/* Private header. Use only in mkimage-related sources.  */
char *
grub_mkimage_load_image32 (const char *kernel_path,
			   size_t total_module_size,
			   struct grub_mkimage_layout *layout,
			   const struct grub_install_image_target_desc *image_target);
char *
grub_mkimage_load_image64 (const char *kernel_path,
			   size_t total_module_size,
			   struct grub_mkimage_layout *layout,
			   const struct grub_install_image_target_desc *image_target);
void
grub_mkimage_generate_elf32 (const struct grub_install_image_target_desc *image_target,
			     int note, char **core_img, size_t *core_size,
			     Elf32_Addr target_addr,
			     struct grub_mkimage_layout *layout);
void
grub_mkimage_generate_elf64 (const struct grub_install_image_target_desc *image_target,
			     int note, char **core_img, size_t *core_size,
			     Elf64_Addr target_addr,
			     struct grub_mkimage_layout *layout);

struct grub_install_image_target_desc
{
  const char *dirname;
  const char *names[6];
  grub_size_t voidp_sizeof;
  int bigendian;
  enum {
    IMAGE_I386_PC, IMAGE_EFI, IMAGE_COREBOOT,
    IMAGE_SPARC64_AOUT, IMAGE_SPARC64_RAW, IMAGE_SPARC64_CDCORE,
    IMAGE_I386_IEEE1275,
    IMAGE_LOONGSON_ELF, IMAGE_QEMU, IMAGE_PPC, IMAGE_YEELOONG_FLASH,
    IMAGE_FULOONG2F_FLASH, IMAGE_I386_PC_PXE, IMAGE_MIPS_ARC,
    IMAGE_QEMU_MIPS_FLASH, IMAGE_UBOOT, IMAGE_XEN, IMAGE_I386_PC_ELTORITO,
    IMAGE_XEN_PVH
  } id;
  enum
    {
      PLATFORM_FLAGS_NONE = 0,
      PLATFORM_FLAGS_DECOMPRESSORS = 2,
      PLATFORM_FLAGS_MODULES_BEFORE_KERNEL = 4,
    } flags;
  unsigned total_module_size;
  unsigned decompressor_compressed_size;
  unsigned decompressor_uncompressed_size;
  unsigned decompressor_uncompressed_addr;
  unsigned reloc_table_offset;
  unsigned link_align;
  grub_uint16_t elf_target;
  unsigned section_align;
  signed vaddr_offset;
  grub_uint64_t link_addr;
  unsigned mod_gap, mod_align;
  grub_compression_t default_compression;
  grub_uint16_t pe_target;
};

#define grub_target_to_host32(x) (grub_target_to_host32_real (image_target, (x)))
#define grub_host_to_target32(x) (grub_host_to_target32_real (image_target, (x)))
#define grub_target_to_host64(x) (grub_target_to_host64_real (image_target, (x)))
#define grub_host_to_target64(x) (grub_host_to_target64_real (image_target, (x)))
#define grub_host_to_target_addr(x) (grub_host_to_target_addr_real (image_target, (x)))
#define grub_target_to_host16(x) (grub_target_to_host16_real (image_target, (x)))
#define grub_host_to_target16(x) (grub_host_to_target16_real (image_target, (x)))

static inline grub_uint32_t
grub_target_to_host32_real (const struct grub_install_image_target_desc *image_target,
			    grub_uint32_t in)
{
  if (image_target->bigendian)
    return grub_be_to_cpu32 (in);
  else
    return grub_le_to_cpu32 (in);
}

static inline grub_uint64_t
grub_target_to_host64_real (const struct grub_install_image_target_desc *image_target,
			    grub_uint64_t in)
{
  if (image_target->bigendian)
    return grub_be_to_cpu64 (in);
  else
    return grub_le_to_cpu64 (in);
}

static inline grub_uint64_t
grub_host_to_target64_real (const struct grub_install_image_target_desc *image_target,
			    grub_uint64_t in)
{
  if (image_target->bigendian)
    return grub_cpu_to_be64 (in);
  else
    return grub_cpu_to_le64 (in);
}

static inline grub_uint32_t
grub_host_to_target32_real (const struct grub_install_image_target_desc *image_target,
			    grub_uint32_t in)
{
  if (image_target->bigendian)
    return grub_cpu_to_be32 (in);
  else
    return grub_cpu_to_le32 (in);
}

static inline grub_uint16_t
grub_target_to_host16_real (const struct grub_install_image_target_desc *image_target,
			    grub_uint16_t in)
{
  if (image_target->bigendian)
    return grub_be_to_cpu16 (in);
  else
    return grub_le_to_cpu16 (in);
}

static inline grub_uint16_t
grub_host_to_target16_real (const struct grub_install_image_target_desc *image_target,
			    grub_uint16_t in)
{
  if (image_target->bigendian)
    return grub_cpu_to_be16 (in);
  else
    return grub_cpu_to_le16 (in);
}

static inline grub_uint64_t
grub_host_to_target_addr_real (const struct grub_install_image_target_desc *image_target, grub_uint64_t in)
{
  if (image_target->voidp_sizeof == 8)
    return grub_host_to_target64_real (image_target, in);
  else
    return grub_host_to_target32_real (image_target, in);
}

static inline grub_uint64_t
grub_target_to_host_real (const struct grub_install_image_target_desc *image_target, grub_uint64_t in)
{
  if (image_target->voidp_sizeof == 8)
    return grub_target_to_host64_real (image_target, in);
  else
    return grub_target_to_host32_real (image_target, in);
}

#define grub_target_to_host(val) grub_target_to_host_real(image_target, (val))

#endif
