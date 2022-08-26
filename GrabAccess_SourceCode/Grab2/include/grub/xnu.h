/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009  Free Software Foundation, Inc.
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

#ifndef GRUB_XNU_H
#define GRUB_XNU_H 1

#include <grub/bitmap.h>

/* Header of a hibernation image. */
struct grub_xnu_hibernate_header
{
  /* Size of the image. Notice that file containing image is usually bigger. */
  grub_uint64_t image_size;
  grub_uint8_t unknown1[8];
  /* Where to copy launchcode? */
  grub_uint32_t launchcode_target_page;
  /* How many pages of launchcode? */
  grub_uint32_t launchcode_numpages;
  /* Where to jump? */
  grub_uint32_t entry_point;
  /* %esp at start. */
  grub_uint32_t stack;
  grub_uint8_t unknown2[44];
#define GRUB_XNU_HIBERNATE_MAGIC 0x73696d65
  grub_uint32_t magic;
  grub_uint8_t unknown3[28];
  /* This value is non-zero if page is encrypted. Unsupported. */
  grub_uint64_t encoffset;
  grub_uint8_t unknown4[360];
  /* The size of additional header used to locate image without parsing FS.
     Used only to skip it.
   */
  grub_uint32_t extmapsize;
} GRUB_PACKED;

/* In-memory structure for temporary keeping device tree. */
struct grub_xnu_devtree_key
{
  char *name;
  int datasize; /* -1 for not leaves. */
  union
  {
    struct grub_xnu_devtree_key *first_child;
    void *data;
  };
  struct grub_xnu_devtree_key *next;
};

/* A structure used in memory-map values. */
struct
grub_xnu_extdesc
{
  grub_uint32_t addr;
  grub_uint32_t size;
} GRUB_PACKED;

/* Header describing extension in the memory. */
struct grub_xnu_extheader
{
  grub_uint32_t infoplistaddr;
  grub_uint32_t infoplistsize;
  grub_uint32_t binaryaddr;
  grub_uint32_t binarysize;
  grub_uint32_t nameaddr;
  grub_uint32_t namesize;
} GRUB_PACKED;

#endif
