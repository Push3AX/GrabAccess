/* mm.c - functions for memory manager */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2005,2007,2008,2009  Free Software Foundation, Inc.
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

/*
  The design of this memory manager.

  This is a simple implementation of malloc with a few extensions. These are
  the extensions:

  - memalign is implemented efficiently.

  - multiple regions may be used as free space. They may not be
  contiguous.

  Regions are managed by a singly linked list, and the meta information is
  stored in the beginning of each region. Space after the meta information
  is used to allocate memory.

  The memory space is used as cells instead of bytes for simplicity. This
  is important for some CPUs which may not access multiple bytes at a time
  when the first byte is not aligned at a certain boundary (typically,
  4-byte or 8-byte). The size of each cell is equal to the size of struct
  grub_mm_header, so the header of each allocated/free block fits into one
  cell precisely. One cell is 16 bytes on 32-bit platforms and 32 bytes
  on 64-bit platforms.

  There are two types of blocks: allocated blocks and free blocks.

  In allocated blocks, the header of each block has only its size. Note that
  this size is based on cells but not on bytes. The header is located right
  before the returned pointer, that is, the header resides at the previous
  cell.

  Free blocks constitutes a ring, using a singly linked list. The first free
  block is pointed to by the meta information of a region. The allocator
  attempts to pick up the second block instead of the first one. This is
  a typical optimization against defragmentation, and makes the
  implementation a bit easier.

  For safety, both allocated blocks and free ones are marked by magic
  numbers. Whenever anything unexpected is detected, GRUB aborts the
  operation.
 */

#include <config.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/err.h>
#include <grub/types.h>
#include <grub/disk.h>
#include <grub/dl.h>
#include <grub/i18n.h>
#include <grub/mm_private.h>
#include <grub/safemath.h>

#ifdef MM_DEBUG
# undef grub_calloc
# undef grub_malloc
# undef grub_zalloc
# undef grub_realloc
# undef grub_free
# undef grub_memalign
#endif



grub_mm_region_t grub_mm_base;

/* Get a header from the pointer PTR, and set *P and *R to a pointer
   to the header and a pointer to its region, respectively. PTR must
   be allocated.  */
static void
get_header_from_pointer (void *ptr, grub_mm_header_t *p, grub_mm_region_t *r)
{
  if ((grub_addr_t) ptr & (GRUB_MM_ALIGN - 1))
    grub_fatal ("unaligned pointer %p", ptr);

  for (*r = grub_mm_base; *r; *r = (*r)->next)
    if ((grub_addr_t) ptr > (grub_addr_t) ((*r) + 1)
	&& (grub_addr_t) ptr <= (grub_addr_t) ((*r) + 1) + (*r)->size)
      break;

  if (! *r)
    grub_fatal ("out of range pointer %p", ptr);

  *p = (grub_mm_header_t) ptr - 1;
  if ((*p)->magic == GRUB_MM_FREE_MAGIC)
    grub_fatal ("double free at %p", *p);
  if ((*p)->magic != GRUB_MM_ALLOC_MAGIC)
    grub_fatal ("alloc magic is broken at %p: %lx", *p,
		(unsigned long) (*p)->magic);
}

/* Initialize a region starting from ADDR and whose size is SIZE,
   to use it as free space.  */
void
grub_mm_init_region (void *addr, grub_size_t size)
{
  grub_mm_header_t h;
  grub_mm_region_t r, *p, q;

#if 0
  grub_printf ("Using memory for heap: start=%p, end=%p\n", addr, addr + (unsigned int) size);
#endif

  /* Exclude last 4K to avoid overflows. */
  /* If addr + 0x1000 overflows then whole region is in excluded zone.  */
  if ((grub_addr_t) addr > ~((grub_addr_t) 0x1000))
    return;

  /* If addr + 0x1000 + size overflows then decrease size.  */
  if (((grub_addr_t) addr + 0x1000) > ~(grub_addr_t) size)
    size = ((grub_addr_t) -0x1000) - (grub_addr_t) addr;

  /* Attempt to merge this region with every existing region */
  for (p = &grub_mm_base, q = *p; q; p = &(q->next), q = *p)
    /*
     * Is the new region immediately below an existing region? That
     * is, is the address of the memory we're adding now (addr) + size
     * of the memory we're adding (size) + the bytes we couldn't use
     * at the start of the region we're considering (q->pre_size)
     * equal to the address of q? In other words, does the memory
     * looks like this?
     *
     * addr                          q
     *   |----size-----|-q->pre_size-|<q region>|
     */
    if ((grub_uint8_t *) addr + size + q->pre_size == (grub_uint8_t *) q)
      {
	/*
	 * Yes, we can merge the memory starting at addr into the
	 * existing region from below. Align up addr to GRUB_MM_ALIGN
	 * so that our new region has proper alignment.
	 */
	r = (grub_mm_region_t) ALIGN_UP ((grub_addr_t) addr, GRUB_MM_ALIGN);
	/* Copy the region data across */
	*r = *q;
	/* Consider all the new size as pre-size */
	r->pre_size += size;

	/*
	 * If we have enough pre-size to create a block, create a
	 * block with it. Mark it as allocated and pass it to
	 * grub_free (), which will sort out getting it into the free
	 * list.
	 */
	if (r->pre_size >> GRUB_MM_ALIGN_LOG2)
	  {
	    h = (grub_mm_header_t) (r + 1);
	    /* block size is pre-size converted to cells */
	    h->size = (r->pre_size >> GRUB_MM_ALIGN_LOG2);
	    h->magic = GRUB_MM_ALLOC_MAGIC;
	    /* region size grows by block size converted back to bytes */
	    r->size += h->size << GRUB_MM_ALIGN_LOG2;
	    /* adjust pre_size to be accurate */
	    r->pre_size &= (GRUB_MM_ALIGN - 1);
	    *p = r;
	    grub_free (h + 1);
	  }
	/* Replace the old region with the new region */
	*p = r;
	return;
      }

  /* Allocate a region from the head.  */
  r = (grub_mm_region_t) ALIGN_UP ((grub_addr_t) addr, GRUB_MM_ALIGN);

  /* If this region is too small, ignore it.  */
  if (size < GRUB_MM_ALIGN + (char *) r - (char *) addr + sizeof (*r))
    return;

  size -= (char *) r - (char *) addr + sizeof (*r);

  h = (grub_mm_header_t) (r + 1);
  h->next = h;
  h->magic = GRUB_MM_FREE_MAGIC;
  h->size = (size >> GRUB_MM_ALIGN_LOG2);

  r->first = h;
  r->pre_size = (grub_addr_t) r - (grub_addr_t) addr;
  r->size = (h->size << GRUB_MM_ALIGN_LOG2);

  /* Find where to insert this region. Put a smaller one before bigger ones,
     to prevent fragmentation.  */
  for (p = &grub_mm_base, q = *p; q; p = &(q->next), q = *p)
    if (q->size > r->size)
      break;

  *p = r;
  r->next = q;
}

/* Allocate the number of units N with the alignment ALIGN from the ring
 * buffer given in *FIRST.  ALIGN must be a power of two. Both N and
 * ALIGN are in units of GRUB_MM_ALIGN.  Return a non-NULL if successful,
 * otherwise return NULL.
 *
 * Note: because in certain circumstances we need to adjust the ->next
 * pointer of the previous block, we iterate over the singly linked
 * list with the pair (prev, cur). *FIRST is our initial previous, and
 * *FIRST->next is our initial current pointer. So we will actually
 * allocate from *FIRST->next first and *FIRST itself last.
 */
static void *
grub_real_malloc (grub_mm_header_t *first, grub_size_t n, grub_size_t align)
{
  grub_mm_header_t cur, prev;

  /* When everything is allocated side effect is that *first will have alloc
     magic marked, meaning that there is no room in this region.  */
  if ((*first)->magic == GRUB_MM_ALLOC_MAGIC)
    return 0;

  /* Try to search free slot for allocation in this memory region.  */
  for (prev = *first, cur = prev->next; ; prev = cur, cur = cur->next)
    {
      grub_off_t extra;

      extra = ((grub_addr_t) (cur + 1) >> GRUB_MM_ALIGN_LOG2) & (align - 1);
      if (extra)
	extra = align - extra;

      if (! cur)
	grub_fatal ("null in the ring");

      if (cur->magic != GRUB_MM_FREE_MAGIC)
	grub_fatal ("free magic is broken at %p: 0x%x", cur, cur->magic);

      if (cur->size >= n + extra)
	{
	  extra += (cur->size - extra - n) & (~(align - 1));
	  if (extra == 0 && cur->size == n)
	    {
	      /* There is no special alignment requirement and memory block
	         is complete match.

	         1. Just mark memory block as allocated and remove it from
	            free list.

	         Result:
	         +---------------+ previous block's next
	         | alloc, size=n |          |
	         +---------------+          v
	       */
	      prev->next = cur->next;
	    }
	  else if (align == 1 || cur->size == n + extra)
	    {
	      /* There might be alignment requirement, when taking it into
	         account memory block fits in.

	         1. Allocate new area at end of memory block.
	         2. Reduce size of available blocks from original node.
	         3. Mark new area as allocated and "remove" it from free
	            list.

	         Result:
	         +---------------+
	         | free, size-=n | next --+
	         +---------------+        |
	         | alloc, size=n |        |
	         +---------------+        v
	       */
	      cur->size -= n;
	      cur += cur->size;
	    }
	  else if (extra == 0)
	    {
	      grub_mm_header_t r;
	      
	      r = cur + extra + n;
	      r->magic = GRUB_MM_FREE_MAGIC;
	      r->size = cur->size - extra - n;
	      r->next = cur->next;
	      prev->next = r;

	      if (prev == cur)
		{
		  prev = r;
		  r->next = r;
		}
	    }
	  else
	    {
	      /* There is alignment requirement and there is room in memory
	         block.  Split memory block to three pieces.

	         1. Create new memory block right after section being
	            allocated.  Mark it as free.
	         2. Add new memory block to free chain.
	         3. Mark current memory block having only extra blocks.
	         4. Advance to aligned block and mark that as allocated and
	            "remove" it from free list.

	         Result:
	         +------------------------------+
	         | free, size=extra             | next --+
	         +------------------------------+        |
	         | alloc, size=n                |        |
	         +------------------------------+        |
	         | free, size=orig.size-extra-n | <------+, next --+
	         +------------------------------+                  v
	       */
	      grub_mm_header_t r;

	      r = cur + extra + n;
	      r->magic = GRUB_MM_FREE_MAGIC;
	      r->size = cur->size - extra - n;
	      r->next = cur;

	      cur->size = extra;
	      prev->next = r;
	      cur += extra;
	    }

	  cur->magic = GRUB_MM_ALLOC_MAGIC;
	  cur->size = n;

	  /* Mark find as a start marker for next allocation to fasten it.
	     This will have side effect of fragmenting memory as small
	     pieces before this will be un-used.  */
	  /* So do it only for chunks under 32K.  */
	  if (n < (0x8000 >> GRUB_MM_ALIGN_LOG2)
	      || *first == cur)
	    *first = prev;

	  return cur + 1;
	}

      /* Search was completed without result.  */
      if (cur == *first)
	break;
    }

  return 0;
}

/* Allocate SIZE bytes with the alignment ALIGN and return the pointer.  */
void *
grub_memalign (grub_size_t align, grub_size_t size)
{
  grub_mm_region_t r;
  grub_size_t n = ((size + GRUB_MM_ALIGN - 1) >> GRUB_MM_ALIGN_LOG2) + 1;
  int count = 0;

  if (!grub_mm_base)
    goto fail;

  if (size > ~(grub_size_t) align)
    goto fail;

  /* We currently assume at least a 32-bit grub_size_t,
     so limiting allocations to <adress space size> - 1MiB
     in name of sanity is beneficial. */
  if ((size + align) > ~(grub_size_t) 0x100000)
    goto fail;

  align = (align >> GRUB_MM_ALIGN_LOG2);
  if (align == 0)
    align = 1;

 again:

  for (r = grub_mm_base; r; r = r->next)
    {
      void *p;

      p = grub_real_malloc (&(r->first), n, align);
      if (p)
	return p;
    }

  /* If failed, increase free memory somehow.  */
  switch (count)
    {
    case 0:
      /* Invalidate disk caches.  */
      grub_disk_cache_invalidate_all ();
      count++;
      goto again;

#if 0
    case 1:
      /* Unload unneeded modules.  */
      grub_dl_unload_unneeded ();
      count++;
      goto again;
#endif

    default:
      break;
    }

 fail:
  grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
  return 0;
}

/*
 * Allocate NMEMB instances of SIZE bytes and return the pointer, or error on
 * integer overflow.
 */
void *
grub_calloc (grub_size_t nmemb, grub_size_t size)
{
  void *ret;
  grub_size_t sz = 0;

  if (grub_mul (nmemb, size, &sz))
    {
      grub_error (GRUB_ERR_OUT_OF_RANGE, N_("overflow is detected"));
      return NULL;
    }

  ret = grub_memalign (0, sz);
  if (!ret)
    return NULL;

  grub_memset (ret, 0, sz);
  return ret;
}

/* Allocate SIZE bytes and return the pointer.  */
void *
grub_malloc (grub_size_t size)
{
  return grub_memalign (0, size);
}

/* Allocate SIZE bytes, clear them and return the pointer.  */
void *
grub_zalloc (grub_size_t size)
{
  void *ret;

  ret = grub_memalign (0, size);
  if (ret)
    grub_memset (ret, 0, size);

  return ret;
}

/* Deallocate the pointer PTR.  */
void
grub_free (void *ptr)
{
  grub_mm_header_t p;
  grub_mm_region_t r;

  if (! ptr)
    return;

  get_header_from_pointer (ptr, &p, &r);

  if (r->first->magic == GRUB_MM_ALLOC_MAGIC)
    {
      p->magic = GRUB_MM_FREE_MAGIC;
      r->first = p->next = p;
    }
  else
    {
      grub_mm_header_t cur, prev;

#if 0
      cur = r->first;
      do
	{
	  grub_printf ("%s:%d: q=%p, q->size=0x%x, q->magic=0x%x\n",
		       GRUB_FILE, __LINE__, cur, cur->size, cur->magic);
	  cur = cur->next;
	}
      while (cur != r->first);
#endif
      /* Iterate over all blocks in the free ring.
       *
       * The free ring is arranged from high addresses to low
       * addresses, modulo wraparound.
       *
       * We are looking for a block with a higher address than p or
       * whose next address is lower than p.
       */
      for (prev = r->first, cur = prev->next; cur <= p || cur->next >= p;
	   prev = cur, cur = prev->next)
	{
	  if (cur->magic != GRUB_MM_FREE_MAGIC)
	    grub_fatal ("free magic is broken at %p: 0x%x", cur, cur->magic);

	  /* Deal with wrap-around */
	  if (cur <= cur->next && (cur > p || cur->next < p))
	    break;
	}

      /* mark p as free and insert it between cur and cur->next */
      p->magic = GRUB_MM_FREE_MAGIC;
      p->next = cur->next;
      cur->next = p;

      /*
       * If the block we are freeing can be merged with the next
       * free block, do that.
       */
      if (p->next + p->next->size == p)
	{
	  p->magic = 0;

	  p->next->size += p->size;
	  cur->next = p->next;
	  p = p->next;
	}

      r->first = cur;

      /* Likewise if can be merged with the preceeding free block */
      if (cur == p + p->size)
	{
	  cur->magic = 0;
	  p->size += cur->size;
	  if (cur == prev)
	    prev = p;
	  prev->next = p;
	  cur = prev;
	}

      /*
       * Set r->first such that the just free()d block is tried first.
       * (An allocation is tried from *first->next, and cur->next == p.)
       */
      r->first = cur;
    }
}

/* Reallocate SIZE bytes and return the pointer. The contents will be
   the same as that of PTR.  */
void *
grub_realloc (void *ptr, grub_size_t size)
{
  grub_mm_header_t p;
  grub_mm_region_t r;
  void *q;
  grub_size_t n;

  if (! ptr)
    return grub_malloc (size);

  if (! size)
    {
      grub_free (ptr);
      return 0;
    }

  /* FIXME: Not optimal.  */
  n = ((size + GRUB_MM_ALIGN - 1) >> GRUB_MM_ALIGN_LOG2) + 1;
  get_header_from_pointer (ptr, &p, &r);

  if (p->size >= n)
    return ptr;

  q = grub_malloc (size);
  if (! q)
    return q;

  /* We've already checked that p->size < n.  */
  grub_memcpy (q, ptr, p->size << GRUB_MM_ALIGN_LOG2);
  grub_free (ptr);
  return q;
}

#ifdef MM_DEBUG
int grub_mm_debug = 0;

void
grub_mm_dump_free (void)
{
  grub_mm_region_t r;

  for (r = grub_mm_base; r; r = r->next)
    {
      grub_mm_header_t p;

      /* Follow the free list.  */
      p = r->first;
      do
	{
	  if (p->magic != GRUB_MM_FREE_MAGIC)
	    grub_fatal ("free magic is broken at %p: 0x%x", p, p->magic);

	  grub_printf ("F:%p:%u:%p\n",
		       p, (unsigned int) p->size << GRUB_MM_ALIGN_LOG2, p->next);
	  p = p->next;
	}
      while (p != r->first);
    }

  grub_printf ("\n");
}

void
grub_mm_dump (unsigned lineno)
{
  grub_mm_region_t r;

  grub_printf ("called at line %u\n", lineno);
  for (r = grub_mm_base; r; r = r->next)
    {
      grub_mm_header_t p;

      for (p = (grub_mm_header_t) ALIGN_UP ((grub_addr_t) (r + 1),
					    GRUB_MM_ALIGN);
	   (grub_addr_t) p < (grub_addr_t) (r+1) + r->size;
	   p++)
	{
	  switch (p->magic)
	    {
	    case GRUB_MM_FREE_MAGIC:
	      grub_printf ("F:%p:%u:%p\n",
			   p, (unsigned int) p->size << GRUB_MM_ALIGN_LOG2, p->next);
	      break;
	    case GRUB_MM_ALLOC_MAGIC:
	      grub_printf ("A:%p:%u\n", p, (unsigned int) p->size << GRUB_MM_ALIGN_LOG2);
	      break;
	    }
	}
    }

  grub_printf ("\n");
}

void *
grub_debug_calloc (const char *file, int line, grub_size_t nmemb, grub_size_t size)
{
  void *ptr;

  if (grub_mm_debug)
    grub_printf ("%s:%d: calloc (0x%" PRIxGRUB_SIZE ", 0x%" PRIxGRUB_SIZE ") = ",
		 file, line, nmemb, size);
  ptr = grub_calloc (nmemb, size);
  if (grub_mm_debug)
    grub_printf ("%p\n", ptr);
  return ptr;
}

void *
grub_debug_malloc (const char *file, int line, grub_size_t size)
{
  void *ptr;

  if (grub_mm_debug)
    grub_printf ("%s:%d: malloc (0x%" PRIxGRUB_SIZE ") = ", file, line, size);
  ptr = grub_malloc (size);
  if (grub_mm_debug)
    grub_printf ("%p\n", ptr);
  return ptr;
}

void *
grub_debug_zalloc (const char *file, int line, grub_size_t size)
{
  void *ptr;

  if (grub_mm_debug)
    grub_printf ("%s:%d: zalloc (0x%" PRIxGRUB_SIZE ") = ", file, line, size);
  ptr = grub_zalloc (size);
  if (grub_mm_debug)
    grub_printf ("%p\n", ptr);
  return ptr;
}

void
grub_debug_free (const char *file, int line, void *ptr)
{
  if (grub_mm_debug)
    grub_printf ("%s:%d: free (%p)\n", file, line, ptr);
  grub_free (ptr);
}

void *
grub_debug_realloc (const char *file, int line, void *ptr, grub_size_t size)
{
  if (grub_mm_debug)
    grub_printf ("%s:%d: realloc (%p, 0x%" PRIxGRUB_SIZE ") = ", file, line, ptr, size);
  ptr = grub_realloc (ptr, size);
  if (grub_mm_debug)
    grub_printf ("%p\n", ptr);
  return ptr;
}

void *
grub_debug_memalign (const char *file, int line, grub_size_t align,
		    grub_size_t size)
{
  void *ptr;

  if (grub_mm_debug)
    grub_printf ("%s:%d: memalign (0x%" PRIxGRUB_SIZE  ", 0x%" PRIxGRUB_SIZE  
		 ") = ", file, line, align, size);
  ptr = grub_memalign (align, size);
  if (grub_mm_debug)
    grub_printf ("%p\n", ptr);
  return ptr;
}

#endif /* MM_DEBUG */
