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

#ifndef GRUB_FOR_DOS_H
#define GRUB_FOR_DOS_H

#include <grub/types.h>

#define G4D_MAX_ADDR 0x9F000

/* The size of the drive map.  */
#define DRIVE_MAP_SIZE            8

/* The size of the drive_map_slot struct.  */
#define DRIVE_MAP_SLOT_SIZE       24

/* The fragment of the drive map.  */
#define DRIVE_MAP_FRAGMENT        32

#define FRAGMENT_MAP_SLOT_SIZE    0x280

struct g4d_drive_map_slot
{
  /* Remember to update DRIVE_MAP_SLOT_SIZE once this is modified.
   * The struct size must be a multiple of 4.
   */
  unsigned char from_drive;
  unsigned char to_drive;            /* 0xFF indicates a memdrive */

  unsigned char max_head;

  unsigned char max_sector:6;     //unused
  unsigned char disable_lba:1;    //unused
  unsigned char read_only:1;      //unused

  unsigned short to_cylinder:13;  //unused
  unsigned short from_cdrom:1;
  unsigned short to_cdrom:1;      //unused
  unsigned short to_support_lba:1;//unused

  unsigned char to_head;          //unused

  unsigned char to_sector:6;
  unsigned char fake_write:1;     //unused
  unsigned char in_situ:1;        //unused

  unsigned long long start_sector;
  unsigned long long sector_count;
} GRUB_PACKED;

struct g4d_fragment_map_slot
{
  unsigned short slot_len;
  unsigned char from;
  unsigned char to;
  unsigned long long fragment_data[0];
};

struct g4d_fragment
{
  unsigned long long start_sector;
  unsigned long long sector_count;
};

void g4d_add_drive (grub_file_t file, int is_cdrom);

#endif
