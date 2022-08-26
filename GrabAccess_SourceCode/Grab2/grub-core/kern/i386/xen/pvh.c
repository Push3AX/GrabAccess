/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2018  Free Software Foundation, Inc.
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

#include <grub/kernel.h>
#include <grub/misc.h>
#include <grub/memory.h>
#include <grub/mm.h>
#include <grub/i386/cpuid.h>
#include <grub/i386/io.h>
#include <grub/xen.h>
#include <xen/hvm/start_info.h>
#include <grub/i386/linux.h>
#include <grub/machine/kernel.h>
#include <grub/machine/memory.h>
#include <xen/hvm/params.h>
#include <xen/memory.h>

#define XEN_MEMORY_MAP_SIZE   128

grub_uint64_t grub_rsdp_addr;

static char hypercall_page[GRUB_XEN_PAGE_SIZE]
  __attribute__ ((aligned (GRUB_XEN_PAGE_SIZE)));

static grub_uint32_t xen_cpuid_base;
static struct start_info grub_xen_start_page;
static struct grub_e820_mmap_entry map[XEN_MEMORY_MAP_SIZE];
static unsigned int nr_map_entries;

static void
grub_xen_cons_msg (const char *msg)
{
  const char *c;

  for (c = msg; *c; c++)
    grub_outb (*c, XEN_HVM_DEBUGCONS_IOPORT);
}

static void
grub_xen_panic (const char *msg)
{
  grub_xen_cons_msg (msg);
  grub_xen_cons_msg ("System halted!\n");

  asm volatile ("cli");

  while (1)
    {
      asm volatile ("hlt");
    }
}

static void
grub_xen_cpuid_base (void)
{
  grub_uint32_t base, eax, signature[3];

  for (base = 0x40000000; base < 0x40010000; base += 0x100)
    {
      grub_cpuid (base, eax, signature[0], signature[1], signature[2]);
      if (!grub_memcmp ("XenVMMXenVMM", signature, 12) && (eax - base) >= 2)
	{
	  xen_cpuid_base = base;
	  return;
	}
    }

  grub_xen_panic ("Found no Xen signature!\n");
}

static void
grub_xen_setup_hypercall_page (void)
{
  grub_uint32_t msr, addr, eax, ebx, ecx, edx;

  /* Get base address of Xen-specific MSRs. */
  grub_cpuid (xen_cpuid_base + 2, eax, ebx, ecx, edx);
  msr = ebx;
  addr = (grub_uint32_t) (&hypercall_page);

  /* Specify hypercall page address for Xen. */
  asm volatile ("wrmsr" : : "c" (msr), "a" (addr), "d" (0) : "memory");
}

int
grub_xen_hypercall (grub_uint32_t callno, grub_uint32_t a0,
		    grub_uint32_t a1, grub_uint32_t a2,
		    grub_uint32_t a3, grub_uint32_t a4,
		    grub_uint32_t a5 __attribute__ ((unused)))
{
  grub_uint32_t res;

  asm volatile ("call *%[callno]"
		: "=a" (res), "+b" (a0), "+c" (a1), "+d" (a2),
		  "+S" (a3), "+D" (a4)
		: [callno] "a" (&hypercall_page[callno * 32])
		: "memory");
  return res;
}

static grub_uint32_t
grub_xen_get_param (int idx)
{
  struct xen_hvm_param xhv;
  int r;

  xhv.domid = DOMID_SELF;
  xhv.index = idx;
  r = grub_xen_hypercall (__HYPERVISOR_hvm_op, HVMOP_get_param,
			  (grub_uint32_t) (&xhv), 0, 0, 0, 0);
  if (r < 0)
    grub_xen_panic ("Could not get parameter from Xen!\n");
  return xhv.value;
}

static void *
grub_xen_add_physmap (unsigned int space, void *addr)
{
  struct xen_add_to_physmap xatp;

  xatp.domid = DOMID_SELF;
  xatp.idx = 0;
  xatp.space = space;
  xatp.gpfn = (grub_addr_t) addr >> GRUB_XEN_LOG_PAGE_SIZE;
  if (grub_xen_hypercall (__HYPERVISOR_memory_op, XENMEM_add_to_physmap,
			  (grub_uint32_t) (&xatp), 0, 0, 0, 0))
    grub_xen_panic ("Memory_op hypercall failed!\n");
  return addr;
}

static void
grub_xen_sort_mmap (void)
{
  grub_uint64_t from, to;
  unsigned int i;
  struct grub_e820_mmap_entry tmp;

  /* Align map entries to page boundaries. */
  for (i = 0; i < nr_map_entries; i++)
    {
      from = map[i].addr;
      to = from + map[i].len;
      if (map[i].type == GRUB_MEMORY_AVAILABLE)
	{
	  from = ALIGN_UP (from, GRUB_XEN_PAGE_SIZE);
	  to = ALIGN_DOWN (to, GRUB_XEN_PAGE_SIZE);
	}
      else
	{
	  from = ALIGN_DOWN (from, GRUB_XEN_PAGE_SIZE);
	  to = ALIGN_UP (to, GRUB_XEN_PAGE_SIZE);
	}
      map[i].addr = from;
      map[i].len = to - from;
    }

 again:
  /* Sort entries by start address. */
  for (i = 1; i < nr_map_entries; i++)
    {
      if (map[i].addr >= map[i - 1].addr)
	continue;
      tmp = map[i];
      map[i] = map[i - 1];
      map[i - 1] = tmp;
      i = 0;
    }

  /* Detect overlapping areas. */
  for (i = 1; i < nr_map_entries; i++)
    {
      if (map[i].addr >= map[i - 1].addr + map[i - 1].len)
	continue;
      tmp = map[i - 1];
      map[i - 1].len = map[i].addr - map[i - 1].addr;
      if (map[i].addr + map[i].len >= tmp.addr + tmp.len)
	continue;
      if (nr_map_entries < ARRAY_SIZE (map))
	{
	  map[nr_map_entries].addr = map[i].addr + map[i].len;
	  map[nr_map_entries].len = tmp.addr + tmp.len - map[nr_map_entries].addr;
	  map[nr_map_entries].type = tmp.type;
	  nr_map_entries++;
	  goto again;
	}
    }

  /* Merge adjacent entries. */
  for (i = 1; i < nr_map_entries; i++)
    {
      if (map[i].type == map[i - 1].type &&
	  map[i].addr == map[i - 1].addr + map[i - 1].len)
	{
	  map[i - 1].len += map[i].len;
	  map[i] = map[nr_map_entries - 1];
	  nr_map_entries--;
	  goto again;
	}
    }
}

static void
grub_xen_get_mmap (void)
{
  struct xen_memory_map memmap;

  memmap.nr_entries = ARRAY_SIZE (map);
  set_xen_guest_handle (memmap.buffer, map);
  if (grub_xen_hypercall (__HYPERVISOR_memory_op, XENMEM_memory_map,
			  (grub_uint32_t) (&memmap), 0, 0, 0, 0))
    grub_xen_panic ("Could not get memory map from Xen!\n");
  nr_map_entries = memmap.nr_entries;

  grub_xen_sort_mmap ();
}

static void
grub_xen_set_mmap (void)
{
  struct xen_foreign_memory_map memmap;

  memmap.domid = DOMID_SELF;
  memmap.map.nr_entries = nr_map_entries;
  set_xen_guest_handle (memmap.map.buffer, map);
  grub_xen_hypercall (__HYPERVISOR_memory_op, XENMEM_set_memory_map,
		      (grub_uint32_t) (&memmap), 0, 0, 0, 0);
}

static void
grub_xen_mm_init_regions (void)
{
  grub_uint64_t modend, from, to;
  unsigned int i;

  modend = grub_modules_get_end ();

  for (i = 0; i < nr_map_entries; i++)
    {
      if (map[i].type != GRUB_MEMORY_AVAILABLE)
        continue;
      from = map[i].addr;
      to = from + map[i].len;
      if (from < modend)
        from = modend;
      if (from >= to || from >= (1ULL << 32))
        continue;
      if (to > (1ULL << 32))
        to = 1ULL << 32;
      grub_mm_init_region ((void *) (grub_addr_t) from, to - from);
    }
}

static grub_uint64_t
grub_xen_find_page (grub_uint64_t start)
{
  unsigned int i, j;
  grub_uint64_t last = start;

  /*
   * Try to find a e820 map hole below 4G.
   * Relies on page-aligned entries (addr and len) and input (start).
   */

  for (i = 0; i < nr_map_entries; i++)
    {
      if (last > map[i].addr + map[i].len)
	continue;
      if (last < map[i].addr)
	return last;
      if ((map[i].addr >> 32) || ((map[i].addr + map[i].len) >> 32))
	break;
      last = map[i].addr + map[i].len;
    }
    if (i == nr_map_entries)
      return last;

  /* No hole found, use the highest RAM page below 4G and reserve it. */
  if (nr_map_entries == ARRAY_SIZE (map))
    grub_xen_panic ("Memory map size limit reached!\n");
  for (i = 0, j = 0; i < nr_map_entries; i++)
    {
      if (map[i].type != GRUB_MEMORY_AVAILABLE)
	continue;
      if (map[i].addr >> 32)
	break;
      j = i;
      if ((map[i].addr + map[i].len) >> 32)
	break;
    }
  if (map[j].type != GRUB_MEMORY_AVAILABLE)
    grub_xen_panic ("No free memory page found!\n");
  if ((map[j].addr + map[j].len) >> 32)
    last = (1ULL << 32) - GRUB_XEN_PAGE_SIZE;
  else
    last = map[j].addr + map[j].len - GRUB_XEN_PAGE_SIZE;
  map[nr_map_entries].addr = last;
  map[nr_map_entries].len = GRUB_XEN_PAGE_SIZE;
  map[nr_map_entries].type = GRUB_MEMORY_RESERVED;
  nr_map_entries++;
  grub_xen_sort_mmap ();

  return last;
}

void
grub_xen_setup_pvh (void)
{
  grub_addr_t par;

  grub_xen_cpuid_base ();
  grub_xen_setup_hypercall_page ();
  grub_xen_get_mmap ();

  /* Setup Xen data. */
  grub_xen_start_page_addr = &grub_xen_start_page;

  par = grub_xen_get_param (HVM_PARAM_CONSOLE_PFN);
  grub_xen_start_page_addr->console.domU.mfn = par;
  grub_xen_xcons = (void *) (grub_addr_t) (par << GRUB_XEN_LOG_PAGE_SIZE);
  par = grub_xen_get_param (HVM_PARAM_CONSOLE_EVTCHN);
  grub_xen_start_page_addr->console.domU.evtchn = par;

  par = grub_xen_get_param (HVM_PARAM_STORE_PFN);
  grub_xen_start_page_addr->store_mfn = par;
  grub_xen_xenstore = (void *) (grub_addr_t) (par << GRUB_XEN_LOG_PAGE_SIZE);
  par = grub_xen_get_param (HVM_PARAM_STORE_EVTCHN);
  grub_xen_start_page_addr->store_evtchn = par;

  par = grub_xen_find_page (0);
  grub_xen_grant_table = grub_xen_add_physmap (XENMAPSPACE_grant_table,
					       (void *) par);
  par = grub_xen_find_page (par + GRUB_XEN_PAGE_SIZE);
  grub_xen_shared_info = grub_xen_add_physmap (XENMAPSPACE_shared_info,
					       (void *) par);
  grub_xen_set_mmap ();

  grub_xen_mm_init_regions ();

  grub_rsdp_addr = pvh_start_info->rsdp_paddr;
}

grub_err_t
grub_machine_mmap_iterate (grub_memory_hook_t hook, void *hook_data)
{
  unsigned int i;

  for (i = 0; i < nr_map_entries; i++)
    {
      if (map[i].len && hook (map[i].addr, map[i].len, map[i].type, hook_data))
        break;
    }

  return GRUB_ERR_NONE;
}
