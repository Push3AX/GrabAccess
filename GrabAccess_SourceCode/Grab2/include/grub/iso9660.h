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

#ifndef GRUB_ISO9660_H
#define GRUB_ISO9660_H	1

#include <grub/types.h>
#include <grub/file.h>

#define GRUB_ISO9660_FSTYPE_DIR		0040000
#define GRUB_ISO9660_FSTYPE_REG		0100000
#define GRUB_ISO9660_FSTYPE_SYMLINK	0120000
#define GRUB_ISO9660_FSTYPE_MASK	0170000

#define GRUB_ISO9660_LOG2_BLKSZ		2
#define GRUB_ISO9660_BLKSZ		2048

#define GRUB_ISO9660_RR_DOT		2
#define GRUB_ISO9660_RR_DOTDOT		4

#define GRUB_ISO9660_VOLDESC_BOOT	0
#define GRUB_ISO9660_VOLDESC_PRIMARY	1
#define GRUB_ISO9660_VOLDESC_SUPP	2
#define GRUB_ISO9660_VOLDESC_PART	3
#define GRUB_ISO9660_VOLDESC_END	255

/* The head of a volume descriptor.  */
struct grub_iso9660_voldesc
{
  grub_uint8_t type;
  grub_uint8_t magic[5];
  grub_uint8_t version;
} GRUB_PACKED;

struct grub_iso9660_date2
{
  grub_uint8_t year;
  grub_uint8_t month;
  grub_uint8_t day;
  grub_uint8_t hour;
  grub_uint8_t minute;
  grub_uint8_t second;
  grub_uint8_t offset;
} GRUB_PACKED;

/* A directory entry.  */
struct grub_iso9660_dir
{
  grub_uint8_t len;
  grub_uint8_t ext_sectors;
  grub_uint32_t first_sector;
  grub_uint32_t first_sector_be;
  grub_uint32_t size;
  grub_uint32_t size_be;
  struct grub_iso9660_date2 mtime;
  grub_uint8_t flags;
  grub_uint8_t unused2[6];
  grub_uint8_t namelen;
} GRUB_PACKED;

struct grub_iso9660_date
{
  grub_uint8_t year[4];
  grub_uint8_t month[2];
  grub_uint8_t day[2];
  grub_uint8_t hour[2];
  grub_uint8_t minute[2];
  grub_uint8_t second[2];
  grub_uint8_t hundredth[2];
  grub_uint8_t offset;
} GRUB_PACKED;

/* The primary volume descriptor.  Only little endian is used.  */
struct grub_iso9660_primary_voldesc
{
  struct grub_iso9660_voldesc voldesc;
  grub_uint8_t unused1[33];
  grub_uint8_t volname[32];
  grub_uint8_t unused2[16];
  grub_uint8_t escape[32];
  grub_uint8_t unused3[12];
  grub_uint32_t path_table_size;
  grub_uint8_t unused4[4];
  grub_uint32_t path_table;
  grub_uint8_t unused5[12];
  struct grub_iso9660_dir rootdir;
  grub_uint8_t unused6[624];
  struct grub_iso9660_date created;
  struct grub_iso9660_date modified;
} GRUB_PACKED;

/* A single entry in the path table.  */
struct grub_iso9660_path
{
  grub_uint8_t len;
  grub_uint8_t sectors;
  grub_uint32_t first_sector;
  grub_uint16_t parentdir;
  grub_uint8_t name[0];
} GRUB_PACKED;

/* An entry in the System Usage area of the directory entry.  */
struct grub_iso9660_susp_entry
{
  grub_uint8_t sig[2];
  grub_uint8_t len;
  grub_uint8_t version;
  grub_uint8_t data[0];
} GRUB_PACKED;

/* The CE entry.  This is used to describe the next block where data
   can be found.  */
struct grub_iso9660_susp_ce
{
  struct grub_iso9660_susp_entry entry;
  grub_uint32_t blk;
  grub_uint32_t blk_be;
  grub_uint32_t off;
  grub_uint32_t off_be;
  grub_uint32_t len;
  grub_uint32_t len_be;
} GRUB_PACKED;

grub_uint64_t
grub_iso9660_get_last_read_pos (grub_file_t file);

grub_uint64_t
grub_iso9660_get_last_file_dirent_pos(grub_file_t file);

#endif
