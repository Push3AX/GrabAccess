/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2007,2008,2013  Free Software Foundation, Inc.
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

#include <grub/i386/coreboot/memory.h>
#include <grub/coreboot/lbio.h>
#include <grub/types.h>
#include <grub/err.h>
#include <grub/misc.h>
#include <grub/dl.h>

#pragma GCC diagnostic ignored "-Wcast-align"

/* Helper for grub_linuxbios_table_iterate.  */
int
grub_linuxbios_check_signature (grub_linuxbios_table_header_t tbl_header)
{
  if (! grub_memcmp (tbl_header->signature, "LBIO", 4))
    return 1;

  return 0;
}

grub_err_t
grub_linuxbios_table_iterate (int (*hook) (grub_linuxbios_table_item_t,
					   void *),
			      void *hook_data)
{
  grub_linuxbios_table_header_t table_header = grub_linuxbios_get_tables ();
  grub_linuxbios_table_item_t table_item;

  if (!table_header)
    return 0;

signature_found:

  table_item =
    (grub_linuxbios_table_item_t) ((char *) table_header +
				   table_header->header_size);
  for (; table_item < (grub_linuxbios_table_item_t) ((char *) table_header
						     + table_header->header_size
						     + table_header->table_size);
       table_item = (grub_linuxbios_table_item_t) ((char *) table_item + table_item->size))
    {
      if (table_item->tag == GRUB_LINUXBIOS_MEMBER_LINK
         && grub_linuxbios_check_signature ((grub_linuxbios_table_header_t) (grub_addr_t)
                             *(grub_uint64_t *) (table_item + 1)))
       {
         table_header = (grub_linuxbios_table_header_t) (grub_addr_t)
           *(grub_uint64_t *) (table_item + 1);
         goto signature_found;   
       }
      if (hook (table_item, hook_data))
       return 1;
    }

  return 0;
}
