/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019  Free Software Foundation, Inc.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef GRUB_EFI_ELTORITO_HEADER
#define GRUB_EFI_ELTORITO_HEADER

#include <grub/types.h>

#define CDVOL_TYPE_STANDARD 0x0
#define CDVOL_TYPE_CODED    0x1
#define CDVOL_TYPE_END      0xFF

#define CDVOL_ID  "CD001"
#define CDVOL_ELTORITO_ID "EL TORITO SPECIFICATION"

// ELTORITO_CATALOG.Boot.MediaTypes
#define ELTORITO_NO_EMULATION 0x00
#define ELTORITO_12_DISKETTE  0x01
#define ELTORITO_14_DISKETTE  0x02
#define ELTORITO_28_DISKETTE  0x03
#define ELTORITO_HARD_DISK    0x04

//Indicator types
#define ELTORITO_ID_CATALOG               0x01
#define ELTORITO_ID_SECTION_BOOTABLE      0x88
#define ELTORITO_ID_SECTION_NOT_BOOTABLE  0x00
#define ELTORITO_ID_SECTION_HEADER        0x90
#define ELTORITO_ID_SECTION_HEADER_FINAL  0x91

#pragma pack(1)

typedef union
{
  struct
  {
    grub_uint8_t type;
    grub_uint8_t id[5]; ///< "CD001"
    grub_uint8_t reserved[82];
  } unknown;
  struct
  {
    grub_uint8_t type;          ///< Must be 0
    grub_uint8_t id[5];         ///< "CD001"
    grub_uint8_t version;       ///< Must be 1
    grub_uint8_t system_id[32]; ///< "EL TORITO SPECIFICATION"
    grub_uint8_t unused[32];    ///< Must be 0
    grub_uint8_t elt_catalog[4];///< Absolute pointer to first sector of Boot Catalog
    grub_uint8_t unused2[13];   ///< Must be 0
  } boot_record_volume;
  struct
  {
    grub_uint8_t  type;
    grub_uint8_t  id[5];             ///< "CD001"
    grub_uint8_t  version;
    grub_uint8_t  unused;            ///< Must be 0
    grub_uint8_t  system_id[32];
    grub_uint8_t  volume_id[32];
    grub_uint8_t  unused2[8];        ///< Must be 0
    grub_uint32_t vol_space_size[2]; ///< the number of Logical Blocks
  } primary_volume;
} cdrom_volume_descriptor_t;

typedef union
{
  struct
  {
    grub_uint8_t reserved[0x20];
  } unknown;
  /// Catalog validation entry (Catalog header)
  struct
  {
    grub_uint8_t  indicator;     ///< Must be 01
    grub_uint8_t  platform_id;
    grub_uint16_t reserved;
    grub_uint8_t  manufac_id[24];
    grub_uint16_t checksum;
    grub_uint16_t id55AA;
  } catalog;
  /// Initial/Default Entry or Section Entry
  struct
  {
    grub_uint8_t  indicator;     ///< 88 = Bootable, 00 = Not Bootable
    grub_uint8_t  media_type : 4;
    grub_uint8_t  reserved1 : 4; ///< Must be 0
    grub_uint16_t load_segment;
    grub_uint8_t  system_type;
    grub_uint8_t  reserved2;     ///< Must be 0
    grub_uint16_t sector_count;
    grub_uint32_t lba;
  } boot;
  /// Section Header Entry
  struct
  {
    grub_uint8_t  indicator; ///< 90 - Header, more header follw, 91 - Final Header
    grub_uint8_t  platform_id;
    grub_uint16_t section_entries;///< Number of section entries following this header
    grub_uint8_t  id[28];
  } section;
} eltorito_catalog_t;

#pragma pack()

#endif
