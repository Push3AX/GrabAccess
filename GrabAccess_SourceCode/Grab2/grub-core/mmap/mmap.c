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

#include <grub/memory.h>
#include <grub/machine/memory.h>
#include <grub/err.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/command.h>
#include <grub/dl.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

#ifndef GRUB_MMAP_REGISTER_BY_FIRMWARE

struct grub_mmap_region *grub_mmap_overlays = 0;
static int curhandle = 1;

#endif

static int current_priority = 1;

/* Scanline events. */
struct grub_mmap_scan
{
  /* At which memory address. */
  grub_uint64_t pos;
  /* 0 = region starts, 1 = region ends. */
  int type;
  /* Which type of memory region? */
  grub_memory_type_t memtype;
  /* Priority. 0 means coming from firmware.  */
  int priority;
};

/* Context for grub_mmap_iterate.  */
struct grub_mmap_iterate_ctx
{
  struct grub_mmap_scan *scanline_events;
  int i;
};

/* Helper for grub_mmap_iterate.  */
static int
count_hook (grub_uint64_t addr __attribute__ ((unused)),
	    grub_uint64_t size __attribute__ ((unused)),
	    grub_memory_type_t type __attribute__ ((unused)), void *data)
{
  int *mmap_num = data;

  (*mmap_num)++;
  return 0;
}

/* Helper for grub_mmap_iterate.  */
static int
fill_hook (grub_uint64_t addr, grub_uint64_t size, grub_memory_type_t type,
	   void *data)
{
  struct grub_mmap_iterate_ctx *ctx = data;

  if (type == GRUB_MEMORY_HOLE)
    {
      grub_dprintf ("mmap", "Unknown memory type %d. Assuming unusable\n",
		    type);
      type = GRUB_MEMORY_RESERVED;
    }

  ctx->scanline_events[ctx->i].pos = addr;
  ctx->scanline_events[ctx->i].type = 0;
  ctx->scanline_events[ctx->i].memtype = type;
  ctx->scanline_events[ctx->i].priority = 0;

  ctx->i++;

  ctx->scanline_events[ctx->i].pos = addr + size;
  ctx->scanline_events[ctx->i].type = 1;
  ctx->scanline_events[ctx->i].memtype = type;
  ctx->scanline_events[ctx->i].priority = 0;
  ctx->i++;

  return 0;
}

struct mm_list
{
  struct mm_list *next;
  grub_memory_type_t val;
  int present;
};

grub_err_t
grub_mmap_iterate (grub_memory_hook_t hook, void *hook_data)
{
  /* This function resolves overlapping regions and sorts the memory map.
     It uses scanline (sweeping) algorithm.
  */
  struct grub_mmap_iterate_ctx ctx;
  int i, done;

  struct grub_mmap_scan t;

  /* Previous scanline event. */
  grub_uint64_t lastaddr;
  int lasttype;
  /* Current scanline event. */
  int curtype;
  /* How many regions of given type/priority overlap at current location? */
  /* Normally there shouldn't be more than one region per priority but be robust.  */
  struct mm_list *present;
  /* Number of mmap chunks. */
  int mmap_num;

#ifndef GRUB_MMAP_REGISTER_BY_FIRMWARE
  struct grub_mmap_region *cur;
#endif

  mmap_num = 0;

#ifndef GRUB_MMAP_REGISTER_BY_FIRMWARE
  for (cur = grub_mmap_overlays; cur; cur = cur->next)
    mmap_num++;
#endif

  grub_machine_mmap_iterate (count_hook, &mmap_num);

  /* Initialize variables. */
  ctx.scanline_events = (struct grub_mmap_scan *)
    grub_calloc (mmap_num, sizeof (struct grub_mmap_scan) * 2);

  present = grub_calloc (current_priority, sizeof (present[0]));

  if (! ctx.scanline_events || !present)
    {
      grub_free (ctx.scanline_events);
      grub_free (present);
      return grub_errno;
    }

  ctx.i = 0;
#ifndef GRUB_MMAP_REGISTER_BY_FIRMWARE
  /* Register scanline events. */
  for (cur = grub_mmap_overlays; cur; cur = cur->next)
    {
      ctx.scanline_events[ctx.i].pos = cur->start;
      ctx.scanline_events[ctx.i].type = 0;
      ctx.scanline_events[ctx.i].memtype = cur->type;
      ctx.scanline_events[ctx.i].priority = cur->priority;
      ctx.i++;

      ctx.scanline_events[ctx.i].pos = cur->end;
      ctx.scanline_events[ctx.i].type = 1;
      ctx.scanline_events[ctx.i].memtype = cur->type;
      ctx.scanline_events[ctx.i].priority = cur->priority;
      ctx.i++;
    }
#endif /* ! GRUB_MMAP_REGISTER_BY_FIRMWARE */

  grub_machine_mmap_iterate (fill_hook, &ctx);

  /* Primitive bubble sort. It has complexity O(n^2) but since we're
     unlikely to have more than 100 chunks it's probably one of the
     fastest for one purpose. */
  done = 1;
  while (done)
    {
      done = 0;
      for (i = 0; i < 2 * mmap_num - 1; i++)
	if (ctx.scanline_events[i + 1].pos < ctx.scanline_events[i].pos
	    || (ctx.scanline_events[i + 1].pos == ctx.scanline_events[i].pos
		&& ctx.scanline_events[i + 1].type == 0
		&& ctx.scanline_events[i].type == 1))
	  {
	    t = ctx.scanline_events[i + 1];
	    ctx.scanline_events[i + 1] = ctx.scanline_events[i];
	    ctx.scanline_events[i] = t;
	    done = 1;
	  }
    }

  lastaddr = ctx.scanline_events[0].pos;
  lasttype = ctx.scanline_events[0].memtype;
  for (i = 0; i < 2 * mmap_num; i++)
    {
      /* Process event. */
      if (ctx.scanline_events[i].type)
	{
	  if (present[ctx.scanline_events[i].priority].present)
	    {
	      if (present[ctx.scanline_events[i].priority].val == ctx.scanline_events[i].memtype)
		{
		  if (present[ctx.scanline_events[i].priority].next)
		    {
		      struct mm_list *p = present[ctx.scanline_events[i].priority].next;
		      present[ctx.scanline_events[i].priority] = *p;
		      grub_free (p);
		    }
		  else
		    {
		      present[ctx.scanline_events[i].priority].present = 0;
		    }
		}
	      else
		{
		  struct mm_list **q = &(present[ctx.scanline_events[i].priority].next), *p;
		  for (; *q; q = &((*q)->next))
		    if ((*q)->val == ctx.scanline_events[i].memtype)
		      {
			p = *q;
			*q = p->next;
			grub_free (p);
			break;
		      }
		}
	    }
	}
      else
	{
	  if (!present[ctx.scanline_events[i].priority].present)
	    {
	      present[ctx.scanline_events[i].priority].present = 1;
	      present[ctx.scanline_events[i].priority].val = ctx.scanline_events[i].memtype;
	    }
	  else
	    {
	      struct mm_list *n = grub_malloc (sizeof (*n));
	      n->val = ctx.scanline_events[i].memtype;
	      n->present = 1;
	      n->next = present[ctx.scanline_events[i].priority].next;
	      present[ctx.scanline_events[i].priority].next = n;
	    }
	}

      /* Determine current region type. */
      curtype = -1;
      {
	int k;
	for (k = current_priority - 1; k >= 0; k--)
	  if (present[k].present)
	    {
	      curtype = present[k].val;
	      break;
	    }
      }

      /* Announce region to the hook if necessary. */
      if ((curtype == -1 || curtype != lasttype)
	  && lastaddr != ctx.scanline_events[i].pos
	  && lasttype != -1
	  && lasttype != GRUB_MEMORY_HOLE
	  && hook (lastaddr, ctx.scanline_events[i].pos - lastaddr, lasttype,
		   hook_data))
	{
	  grub_free (ctx.scanline_events);
	  grub_free (present);
	  return GRUB_ERR_NONE;
	}

      /* Update last values if necessary. */
      if (curtype == -1 || curtype != lasttype)
	{
	  lasttype = curtype;
	  lastaddr = ctx.scanline_events[i].pos;
	}
    }

  grub_free (ctx.scanline_events);
  grub_free (present);
  return GRUB_ERR_NONE;
}

#ifndef GRUB_MMAP_REGISTER_BY_FIRMWARE
int
grub_mmap_register (grub_uint64_t start, grub_uint64_t size, int type)
{
  struct grub_mmap_region *cur;

  grub_dprintf ("mmap", "registering\n");

  cur = (struct grub_mmap_region *)
    grub_malloc (sizeof (struct grub_mmap_region));
  if (! cur)
    return 0;

  cur->next = grub_mmap_overlays;
  cur->start = start;
  cur->end = start + size;
  cur->type = type;
  cur->handle = curhandle++;
  cur->priority = current_priority++;
  grub_mmap_overlays = cur;

  if (grub_machine_mmap_register (start, size, type, curhandle))
    {
      grub_mmap_overlays = cur->next;
      grub_free (cur);
      return 0;
    }

  return cur->handle;
}

grub_err_t
grub_mmap_unregister (int handle)
{
  struct grub_mmap_region *cur, *prev;

  for (cur = grub_mmap_overlays, prev = 0; cur; prev = cur, cur = cur->next)
    if (handle == cur->handle)
      {
	grub_err_t err;
	err = grub_machine_mmap_unregister (handle);
	if (err)
	  return err;

	if (prev)
	  prev->next = cur->next;
	else
	  grub_mmap_overlays = cur->next;
	grub_free (cur);
	return GRUB_ERR_NONE;
      }
  return grub_error (GRUB_ERR_BUG, "mmap overlay not found");
}

#endif /* ! GRUB_MMAP_REGISTER_BY_FIRMWARE */

#define CHUNK_SIZE	0x400

struct badram_entry {
  grub_uint64_t addr, mask;
};

static inline grub_uint64_t
fill_mask (struct badram_entry *entry, grub_uint64_t iterator)
{
  int i, j;
  grub_uint64_t ret = (entry->addr & entry->mask);

  /* Find first fixed bit. */
  for (i = 0; i < 64; i++)
    if ((entry->mask & (1ULL << i)) != 0)
      break;
  j = 0;
  for (; i < 64; i++)
    if ((entry->mask & (1ULL << i)) == 0)
      {
	if ((iterator & (1ULL << j)) != 0)
	  ret |= 1ULL << i;
	j++;
      }
  return ret;
}

/* Helper for grub_cmd_badram.  */
static int
badram_iter (grub_uint64_t addr, grub_uint64_t size,
	     grub_memory_type_t type __attribute__ ((unused)), void *data)
{
  struct badram_entry *entry = data;
  grub_uint64_t iterator, low, high, cur;
  int tail, var;
  int i;
  grub_dprintf ("badram", "hook %llx+%llx\n", (unsigned long long) addr,
		(unsigned long long) size);

  /* How many trailing zeros? */
  for (tail = 0; ! (entry->mask & (1ULL << tail)); tail++);

  /* How many zeros in mask? */
  var = 0;
  for (i = 0; i < 64; i++)
    if (! (entry->mask & (1ULL << i)))
      var++;

  if (fill_mask (entry, 0) >= addr)
    iterator = 0;
  else
    {
      low = 0;
      high = ~0ULL;
      /* Find starting value. Keep low and high such that
	 fill_mask (low) < addr and fill_mask (high) >= addr;
      */
      while (high - low > 1)
	{
	  cur = (low + high) / 2;
	  if (fill_mask (entry, cur) >= addr)
	    high = cur;
	  else
	    low = cur;
	}
      iterator = high;
    }

  for (; iterator < (1ULL << (var - tail))
	 && (cur = fill_mask (entry, iterator)) < addr + size;
       iterator++)
    {
      grub_dprintf ("badram", "%llx (size %llx) is a badram range\n",
		    (unsigned long long) cur, (1ULL << tail));
      grub_mmap_register (cur, (1ULL << tail), GRUB_MEMORY_HOLE);
    }
  return 0;
}

static grub_err_t
grub_cmd_badram (grub_command_t cmd __attribute__ ((unused)),
		 int argc, char **args)
{
  const char *str;
  struct badram_entry entry;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("one argument expected"));

  grub_dprintf ("badram", "executing badram\n");

  str = args[0];

  while (1)
    {
      /* Parse address and mask.  */
      entry.addr = grub_strtoull (str, &str, 16);
      if (*str == ',')
	str++;
      entry.mask = grub_strtoull (str, &str, 16);
      if (*str == ',')
	str++;

      if (grub_errno == GRUB_ERR_BAD_NUMBER)
	{
	  grub_errno = 0;
	  return GRUB_ERR_NONE;
	}

      /* When part of a page is tainted, we discard the whole of it.  There's
	 no point in providing sub-page chunks.  */
      entry.mask &= ~(CHUNK_SIZE - 1);

      grub_dprintf ("badram", "badram %llx:%llx\n",
		    (unsigned long long) entry.addr,
		    (unsigned long long) entry.mask);

      grub_mmap_iterate (badram_iter, &entry);
    }
}

static grub_uint64_t
parsemem (const char *str)
{
  grub_uint64_t ret;
  const char *ptr;

  ret = grub_strtoul (str, &ptr, 0);

  switch (*ptr)
    {
    case 'K':
      return ret << 10;
    case 'M':
      return ret << 20;
    case 'G':
      return ret << 30;
    case 'T':
      return ret << 40;
    }
  return ret;
}

struct cutmem_range {
  grub_uint64_t from, to;
};

/* Helper for grub_cmd_cutmem.  */
static int
cutmem_iter (grub_uint64_t addr, grub_uint64_t size,
	     grub_memory_type_t type __attribute__ ((unused)), void *data)
{
  struct cutmem_range *range = data;
  grub_uint64_t end = addr + size;

  if (addr <= range->from)
    addr = range->from;
  if (end >= range->to)
    end = range->to;

  if (end <= addr)
    return 0;

  grub_mmap_register (addr, end - addr, GRUB_MEMORY_HOLE);
  return 0;
}

static grub_err_t
grub_cmd_cutmem (grub_command_t cmd __attribute__ ((unused)),
		 int argc, char **args)
{
  struct cutmem_range range;

  if (argc != 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("two arguments expected"));

  range.from = parsemem (args[0]);
  if (grub_errno)
    return grub_errno;

  range.to = parsemem (args[1]);
  if (grub_errno)
    return grub_errno;

  grub_mmap_iterate (cutmem_iter, &range);

  return GRUB_ERR_NONE;
}

static grub_command_t cmd, cmd_cut;

static grub_uint64_t total_mem = 0;

#ifndef GRUB_MACHINE_EMU
static int
totalmem_hook (grub_uint64_t addr __attribute__ ((unused)),
               grub_uint64_t size,
               grub_memory_type_t type,
               void *data __attribute__ ((unused)))
{
  if (type != GRUB_MEMORY_RESERVED)
  total_mem += size;
  return 0;
}
#endif

grub_uint64_t
grub_get_total_mem_size (void)
{
  total_mem = 0;
#ifndef GRUB_MACHINE_EMU
  grub_machine_mmap_iterate (totalmem_hook, NULL);
#endif
  return total_mem;
}


GRUB_MOD_INIT(mmap)
{
  cmd = grub_register_command ("badram", grub_cmd_badram,
			       N_("ADDR1,MASK1[,ADDR2,MASK2[,...]]"),
			       N_("Declare memory regions as faulty (badram)."));
  cmd_cut = grub_register_command ("cutmem", grub_cmd_cutmem,
				   N_("FROM[K|M|G] TO[K|M|G]"),
				   N_("Remove any memory regions in specified range."));

}

GRUB_MOD_FINI(mmap)
{
  grub_unregister_command (cmd);
  grub_unregister_command (cmd_cut);
}

