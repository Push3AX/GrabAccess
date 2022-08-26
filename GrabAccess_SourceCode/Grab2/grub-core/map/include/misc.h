 /*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019,2020  Free Software Foundation, Inc.
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
 *
 */

#ifndef GRUB_MAPLIB_MISC_H
#define GRUB_MAPLIB_MISC_H

#include <stddef.h>
#include <stdint.h>

#include <grub/types.h>
#include <grub/file.h>
#include <grub/fs.h>
#include <grub/extcmd.h>

#define CD_BOOT_SECTOR 17
#define CD_BLOCK_SIZE 2048
#define CD_SHIFT 11

#define FD_BLOCK_SIZE 512 /* 0x200 */
#define FD_SHIFT 9
#define BLOCK_OF_1_44MB 0xB40

#define MAX_FILE_NAME_STRING_SIZE 255
#define MBR_START_LBA 0
#define PRIMARY_PART_HEADER_LBA 1
#define VDISK_MEDIA_ID 0x1

#define PAGE_SIZE 4096
#define MBR_TYPE_PCAT 0x01
#define SIGNATURE_TYPE_MBR 0x01

#ifdef GRUB_MACHINE_EFI

#include <grub/efi/api.h>
#include <grub/efi/disk.h>

extern block_io_protocol_t blockio_template;

enum options_map
{
  MAP_MEM,
  MAP_BLOCK,
  MAP_TYPE,
  MAP_RT,
  MAP_RO,
  MAP_ELT,
  MAP_NB,
  MAP_UNMAP,
  MAP_FIRST,
  MAP_NOG4D,
  MAP_NOVT,
  MAP_VTOY,
#if 0
  MAP_ALT,
#endif
};

enum grub_efivdisk_type
grub_vdisk_check_type (const char *name, grub_file_t file,
                       enum grub_efivdisk_type type);

grub_efi_status_t
grub_efivdisk_connect_driver (grub_efi_handle_t controller,
                              const grub_efi_char16_t *name);

grub_err_t
grub_efivdisk_install (struct grub_efivdisk_data *disk,
                       struct grub_arg_list *state);

grub_err_t
grub_efivpart_install (struct grub_efivdisk_data *disk,
                       struct grub_arg_list *state);

static inline void
grub_efi_dprintf_dp (grub_efi_device_path_t *dp)
{
  char *text_dp = NULL;
  text_dp = grub_efi_device_path_to_str (dp);
  if (!text_dp)
    return;
  grub_dprintf ("map", "%s\n", text_dp);
  grub_free (text_dp);
}

void grub_efi_set_first_disk (grub_efi_handle_t handle);

#endif

wchar_t *grub_wstrstr (const wchar_t *str, const wchar_t *search_str);

void grub_pause_boot (void);

void grub_pause_fatal (const char *fmt, ...);

grub_file_t file_open (const char *name, int mem, int bl, int rt);

void file_read (grub_file_t file, void *buf, grub_size_t len, grub_off_t offset);

void
file_write (grub_file_t file, const void *buf, grub_size_t len, grub_off_t offset);

void file_close (grub_file_t file);

extern int grub_isefi;

#endif
