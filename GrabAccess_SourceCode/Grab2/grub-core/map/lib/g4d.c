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

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/disk.h>
#include <grub/device.h>
#include <grub/term.h>
#include <grub/partition.h>
#include <grub/file.h>
#include <grub/memory.h>

#ifdef GRUB_MACHINE_EFI
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#endif

#include <grub4dos.h>

static struct g4d_drive_map_slot *g4d_slot = 0;

static void
g4d_alloc_data (void)
{
  grub_uint8_t *g4d_data = NULL;

  if (g4d_slot)
    return;
#ifdef GRUB_MACHINE_EFI
  g4d_data = grub_efi_allocate_pages_real (G4D_MAX_ADDR + 0x1000, 1,
                GRUB_EFI_ALLOCATE_MAX_ADDRESS, GRUB_EFI_RUNTIME_SERVICES_DATA);
#else
  g4d_data = G4D_MAX_ADDR;
#endif
  if (!g4d_data)
  {
    grub_printf ("Can't allocate grub4dos drive map slot info.\n");
    return;
  }
  grub_printf ("write grub4dos drive map slot info to %p\n", g4d_data);

  grub_memcpy (g4d_data + 0xE0, "   $INT13SFGRUB4DOS", 19);
  g4d_slot = (void *)g4d_data;
  grub_memset (g4d_slot, 0, DRIVE_MAP_SLOT_SIZE * DRIVE_MAP_SIZE);
}

static void
read_block_start (grub_disk_addr_t sector,
                  unsigned offset __attribute ((unused)),
                  unsigned length, void *data)
{
  grub_disk_addr_t *start = data;
  *start = sector + 1 - (length >> GRUB_DISK_SECTOR_BITS);
}

void
g4d_add_drive (grub_file_t file, int is_cdrom)
{
  unsigned i;
  grub_disk_addr_t start = 0;
  char tmp[GRUB_DISK_SECTOR_SIZE];
  grub_file_t test = 0;
  g4d_alloc_data ();
  for (i = 0; i < DRIVE_MAP_SIZE; i++)
  {
    if (g4d_slot[i].from_drive != 0)
    {
      if (i == DRIVE_MAP_SIZE)
        grub_printf ("grub4dos drive map slot full.\n");
      continue;
    }

    g4d_slot[i].from_drive = 0x80;
    if (grub_ismemfile (file->name))
      g4d_slot[i].to_drive = 0xff;
    else
      g4d_slot[i].to_drive = 0x80;
    g4d_slot[i].max_head = 0xfe;
    g4d_slot[i].from_cdrom = is_cdrom ? 1 : 0;
    g4d_slot[i].to_sector = 0x02;
    if (grub_ismemfile (file->name))
      g4d_slot[i].start_sector = ((grub_addr_t) file->data) >> GRUB_DISK_SECTOR_BITS;
    else if (file->device->disk)
    {
      test = grub_file_open (file->name, GRUB_FILE_TYPE_CAT);
      if (!test)
        break;
      test->read_hook = read_block_start;
      test->read_hook_data = &start;
      grub_file_read (test, tmp, GRUB_DISK_SECTOR_SIZE);
      grub_file_close (test);
      g4d_slot[i].start_sector = start;
    }
    g4d_slot[i].sector_count = (file->size + 511) >> GRUB_DISK_SECTOR_BITS;
    break;
  }
}
