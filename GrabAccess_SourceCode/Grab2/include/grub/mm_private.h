/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010  Free Software Foundation, Inc.
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

#ifndef GRUB_MM_PRIVATE_H
#define GRUB_MM_PRIVATE_H	1

#include <grub/mm.h>

/* For context, see kern/mm.c */

/* Magic words.  */
#define GRUB_MM_FREE_MAGIC	0x2d3c2808
#define GRUB_MM_ALLOC_MAGIC	0x6db08fa4

/* A header describing a block of memory - either allocated or free */
typedef struct grub_mm_header
{
  /*
   * The 'next' free block in this region's circular free list.
   * Only meaningful if the block is free.
   */
  struct grub_mm_header *next;
  /* The block size, not in bytes but the number of cells of
   * GRUB_MM_ALIGN bytes. Includes the header cell.
   */
  grub_size_t size;
  /* either free or alloc magic, depending on the block type. */
  grub_size_t magic;
  /* pad to cell size: see the top of kern/mm.c. */
#if GRUB_CPU_SIZEOF_VOID_P == 4
  char padding[4];
#elif GRUB_CPU_SIZEOF_VOID_P == 8
  char padding[8];
#else
# error "unknown word size"
#endif
}
*grub_mm_header_t;

#if GRUB_CPU_SIZEOF_VOID_P == 4
# define GRUB_MM_ALIGN_LOG2	4
#elif GRUB_CPU_SIZEOF_VOID_P == 8
# define GRUB_MM_ALIGN_LOG2	5
#endif

#define GRUB_MM_ALIGN	(1 << GRUB_MM_ALIGN_LOG2)

/* A region from which we can make allocations. */
typedef struct grub_mm_region
{
  /* The first free block in this region. */
  struct grub_mm_header *first;

  /*
   * The next region in the linked list of regions. Regions are initially
   * sorted in order of increasing size, but can grow, in which case the
   * ordering may not be preserved.
   */
  struct grub_mm_region *next;

  /*
   * A grub_mm_region will always be aligned to cell size. The pre-size is
   * the number of bytes we were given but had to skip in order to get that
   * alignment.
   */
  grub_size_t pre_size;

  /* How many bytes are in this region? (free and allocated) */
  grub_size_t size;
}
*grub_mm_region_t;

#ifndef GRUB_MACHINE_EMU
extern grub_mm_region_t EXPORT_VAR (grub_mm_base);
#endif

#endif
