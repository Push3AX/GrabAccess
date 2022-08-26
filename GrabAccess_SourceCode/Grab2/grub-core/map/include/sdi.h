/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2020  Free Software Foundation, Inc.
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

#ifndef GRUB_BOOTSDI_H
#define GRUB_BOOTSDI_H

#include <grub/types.h>

#define GRUB_SDI_MAGIC "$SDI0001"

#define GRUB_SDI_ALIGN_OFS  0x70
#define GRUB_SDI_ALIGN      0x02

#define GRUB_SDI_LEN      3170304
#define GRUB_SDI_NTFS_OFS 8192
#define GRUB_SDI_NTFS_LEN 3160576

#define GRUB_SDI_CHKSUM     0x39
#define GRUB_SDI_CHKSUM_OFS 0x1f8

#define GRUB_SDI_TOC_OFS    0x400
#define GRUB_SDI_TOC_SIZE   0x40
#define GRUB_SDI_PART_OFS   0x2000
#define GRUB_SDI_PART_LEN   0x303c00
#define GRUB_SDI_PART_ID    0x07

#define GRUB_SDI_WIM_OFS    0x306000

struct grub_sdi_header
{
  char magic[8];
  grub_uint64_t mdb_type;
  grub_uint64_t boot_code_offset;
  grub_uint64_t boot_code_size;
  grub_uint64_t vendor_id;
  grub_uint64_t device_id;
  grub_packed_guid_t device_model;
  grub_uint64_t device_role;
  grub_uint64_t reserved1;
  grub_packed_guid_t runtime_guid;
  grub_uint64_t runtime_oemrev;
  grub_uint64_t reserved2;
  grub_uint64_t page_align;
  grub_uint64_t reserved3[48];
  grub_uint64_t checksum;
} GRUB_PACKED;

struct grub_sdi_toc_record
{
  char blob_type[8];
  grub_uint64_t attr;
  grub_uint64_t offset;
  grub_uint64_t size;
  grub_uint64_t base_addr;
  grub_uint64_t reserved[3];
} GRUB_PACKED;

void grub_load_bootsdi (void);
void grub_unload_bootsdi (void);

#endif
