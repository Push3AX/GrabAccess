/* Mmap management. */
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

#include <grub/machine/memory.h>
#include <grub/memory.h>
#include <grub/err.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/mm.h>
#include <grub/misc.h>

#define NEXT_MEMORY_DESCRIPTOR(desc, size)      \
  ((grub_efi_memory_descriptor_t *) ((char *) (desc) + (size)))

grub_err_t
grub_efi_mmap_iterate (grub_memory_hook_t hook, void *hook_data,
		       int avoid_efi_boot_services)
{
  grub_efi_uintn_t mmap_size = 0;
  grub_efi_memory_descriptor_t *map_buf = 0;
  grub_efi_uintn_t map_key = 0;
  grub_efi_uintn_t desc_size = 0;
  grub_efi_uint32_t desc_version = 0;
  grub_efi_memory_descriptor_t *desc;

  if (grub_efi_get_memory_map (&mmap_size, map_buf,
			       &map_key, &desc_size,
			       &desc_version) < 0)
    return grub_errno;

  map_buf = grub_malloc (mmap_size);
  if (! map_buf)
    return grub_errno;

  if (grub_efi_get_memory_map (&mmap_size, map_buf,
			       &map_key, &desc_size,
			       &desc_version) <= 0)
    {
      grub_free (map_buf);
      return grub_errno;
    }

  for (desc = map_buf;
       desc < NEXT_MEMORY_DESCRIPTOR (map_buf, mmap_size);
       desc = NEXT_MEMORY_DESCRIPTOR (desc, desc_size))
    {
      grub_dprintf ("mmap", "EFI memory region 0x%llx-0x%llx: %d\n",
		    (unsigned long long) desc->physical_start,
		    (unsigned long long) desc->physical_start
		    + desc->num_pages * 4096, desc->type);
      switch (desc->type)
	{
	case GRUB_EFI_BOOT_SERVICES_CODE:
	  if (!avoid_efi_boot_services)
	    {
	      hook (desc->physical_start, desc->num_pages * 4096,
		    GRUB_MEMORY_AVAILABLE, hook_data);
	      break;
	    }
	  /* FALLTHROUGH */
	case GRUB_EFI_RUNTIME_SERVICES_CODE:
	  hook (desc->physical_start, desc->num_pages * 4096,
		GRUB_MEMORY_CODE, hook_data);
	  break;

	case GRUB_EFI_UNUSABLE_MEMORY:
	  hook (desc->physical_start, desc->num_pages * 4096,
		GRUB_MEMORY_BADRAM, hook_data);
	  break;

	case GRUB_EFI_BOOT_SERVICES_DATA:
	  if (!avoid_efi_boot_services)
	    {
	      hook (desc->physical_start, desc->num_pages * 4096,
		    GRUB_MEMORY_AVAILABLE, hook_data);
	      break;
	    }
	  /* FALLTHROUGH */
	case GRUB_EFI_RESERVED_MEMORY_TYPE:
	case GRUB_EFI_RUNTIME_SERVICES_DATA:
	case GRUB_EFI_MEMORY_MAPPED_IO:
	case GRUB_EFI_MEMORY_MAPPED_IO_PORT_SPACE:
	case GRUB_EFI_PAL_CODE:
	  hook (desc->physical_start, desc->num_pages * 4096,
		GRUB_MEMORY_RESERVED, hook_data);
	  break;

	case GRUB_EFI_LOADER_CODE:
	case GRUB_EFI_LOADER_DATA:
	case GRUB_EFI_CONVENTIONAL_MEMORY:
	  hook (desc->physical_start, desc->num_pages * 4096,
		GRUB_MEMORY_AVAILABLE, hook_data);
	  break;

	case GRUB_EFI_ACPI_RECLAIM_MEMORY:
	  hook (desc->physical_start, desc->num_pages * 4096,
		GRUB_MEMORY_ACPI, hook_data);
	  break;

	case GRUB_EFI_ACPI_MEMORY_NVS:
	  hook (desc->physical_start, desc->num_pages * 4096,
		GRUB_MEMORY_NVS, hook_data);
	  break;

	case GRUB_EFI_PERSISTENT_MEMORY:
	  hook (desc->physical_start, desc->num_pages * 4096,
		GRUB_MEMORY_PERSISTENT, hook_data);
	break;

	default:
	  grub_printf ("Unknown memory type %d, considering reserved\n",
		       desc->type);
	  hook (desc->physical_start, desc->num_pages * 4096,
		GRUB_MEMORY_RESERVED, hook_data);
	  break;
	}
    }

  return GRUB_ERR_NONE;
}

grub_err_t
grub_machine_mmap_iterate (grub_memory_hook_t hook, void *hook_data)
{
  return grub_efi_mmap_iterate (hook, hook_data, 0);
}

static inline grub_efi_memory_type_t
make_efi_memtype (int type)
{
  switch (type)
    {
    case GRUB_MEMORY_CODE:
      return GRUB_EFI_RUNTIME_SERVICES_CODE;

      /* No way to remove a chunk of memory from EFI mmap.
	 So mark it as unusable. */
    case GRUB_MEMORY_HOLE:
    /*
     * AllocatePages() does not support GRUB_EFI_PERSISTENT_MEMORY,
     * so no translation for GRUB_MEMORY_PERSISTENT or
     * GRUB_MEMORY_PERSISTENT_LEGACY.
     */
    case GRUB_MEMORY_PERSISTENT:
    case GRUB_MEMORY_PERSISTENT_LEGACY:
    case GRUB_MEMORY_RESERVED:
      return GRUB_EFI_UNUSABLE_MEMORY;

    case GRUB_MEMORY_AVAILABLE:
      return GRUB_EFI_CONVENTIONAL_MEMORY;

    case GRUB_MEMORY_ACPI:
      return GRUB_EFI_ACPI_RECLAIM_MEMORY;

    case GRUB_MEMORY_NVS:
      return GRUB_EFI_ACPI_MEMORY_NVS;
    }

  return GRUB_EFI_UNUSABLE_MEMORY;
}

struct overlay
{
  struct overlay *next;
  grub_efi_physical_address_t address;
  grub_efi_uintn_t pages;
  int handle;
};

static struct overlay *overlays = 0;
static int curhandle = 1;

int
grub_mmap_register (grub_uint64_t start, grub_uint64_t size, int type)
{
  grub_uint64_t end = start + size;
  grub_efi_physical_address_t address;
  grub_efi_boot_services_t *b;
  grub_efi_uintn_t pages;
  grub_efi_status_t status;
  struct overlay *curover;

  curover = (struct overlay *) grub_malloc (sizeof (struct overlay));
  if (! curover)
    return 0;

  b = grub_efi_system_table->boot_services;
  address = start & (~0xfffULL);
  pages = (end - address + 0xfff) >> 12;
  status = efi_call_2 (b->free_pages, address, pages);
  if (status != GRUB_EFI_SUCCESS && status != GRUB_EFI_NOT_FOUND)
    {
      grub_free (curover);
      return 0;
    }
  status = efi_call_4 (b->allocate_pages, GRUB_EFI_ALLOCATE_ADDRESS,
		       make_efi_memtype (type), pages, &address);
  if (status != GRUB_EFI_SUCCESS)
    {
      grub_free (curover);
      return 0;
    }
  curover->next = overlays;
  curover->handle = curhandle++;
  curover->address = address;
  curover->pages = pages;
  overlays = curover;

  return curover->handle;
}

grub_err_t
grub_mmap_unregister (int handle)
{
  struct overlay *curover, *prevover;
  grub_efi_boot_services_t *b;

  b = grub_efi_system_table->boot_services;


  for (curover = overlays, prevover = 0; curover;
       prevover = curover, curover = curover->next)
    {
      if (curover->handle == handle)
	{
	  efi_call_2 (b->free_pages, curover->address, curover->pages);
	  if (prevover != 0)
	    prevover->next = curover->next;
	  else
	    overlays = curover->next;
	  grub_free (curover);
	  return GRUB_ERR_NONE;
	}
    }
  return grub_error (GRUB_ERR_BUG, "handle %d not found", handle);
}

/* Result is always page-aligned. */
void *
grub_mmap_malign_and_register (grub_uint64_t align __attribute__ ((unused)),
			       grub_uint64_t size,
			       int *handle, int type,
			       int flags __attribute__ ((unused)))
{
  grub_efi_physical_address_t address;
  grub_efi_boot_services_t *b;
  grub_efi_uintn_t pages;
  grub_efi_status_t status;
  struct overlay *curover;
  grub_efi_allocate_type_t atype;

  curover = (struct overlay *) grub_malloc (sizeof (struct overlay));
  if (! curover)
    return 0;

  b = grub_efi_system_table->boot_services;

  address = 0xffffffff;

#if GRUB_TARGET_SIZEOF_VOID_P < 8
  /* Limit the memory access to less than 4GB for 32-bit platforms.  */
  atype = GRUB_EFI_ALLOCATE_MAX_ADDRESS;
#else
  atype = GRUB_EFI_ALLOCATE_ANY_PAGES;
#endif

  pages = (size + 0xfff) >> 12;
  status = efi_call_4 (b->allocate_pages, atype,
		       make_efi_memtype (type), pages, &address);
  if (status != GRUB_EFI_SUCCESS)
    {
      grub_free (curover);
      return 0;
    }

  if (address == 0)
    {
      /* Uggh, the address 0 was allocated... This is too annoying,
	 so reallocate another one.  */
      address = 0xffffffff;
      status = efi_call_4 (b->allocate_pages, atype,
			   make_efi_memtype (type), pages, &address);
      grub_efi_free_pages (0, pages);
      if (status != GRUB_EFI_SUCCESS)
	return 0;
    }

  curover->next = overlays;
  curover->handle = curhandle++;
  curover->address = address;
  curover->pages = pages;
  overlays = curover;
  *handle = curover->handle;

  return (void *) (grub_addr_t) curover->address;
}

void
grub_mmap_free_and_unregister (int handle)
{
  grub_mmap_unregister (handle);
}
