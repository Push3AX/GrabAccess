 /*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019  Free Software Foundation, Inc.
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
 *  Library used for sorting and comparison routines.
 *
 *  Copyright (c) 2009 - 2014, Intel Corporation. All rights reserved.
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <grub/dl.h>
#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>

#include <grub/lib/sortlib.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define assert(x) assert_real(__FILE__, __LINE__, x)

static void
assert_real (const char *file, int line, int cond)
{
  if (!cond)
    grub_printf ("Assertion failed at %s:%d\n", file, line);
}

/**
  Worker function for QuickSorting.  This function is identical to PerformQuickSort,
  except that is uses the pre-allocated buffer so the in place sorting does not need to
  allocate and free buffers constantly.

  Each element must be equal sized.

  if buf_to_sort is NULL, then assert.
  if compare_function is NULL, then assert.
  if buf is NULL, then assert.

  if count is < 2 then perform no action.
  if Size is < 1 then perform no action.

  @param[in, out] buf_to_sort   on call a buf of (possibly sorted) elements
                                 on return a buffer of sorted elements
  @param[in] count               the number of elements in the buffer to sort
  @param[in] element_size         Size of an element in bytes
  @param[in] compare_function     The function to call to perform the comparison
                                 of any 2 elements
  @param[in] buf              buf of size element_size for use in swapping
**/
static void
quick_sort_worker (void *buf_to_sort, const grub_size_t count,
                   const grub_size_t element_size,
                   sort_compare compare_function, void *buf)
{
  void *pivot;
  grub_size_t loopcount;
  grub_size_t next_swap_location;

  assert(buf_to_sort != NULL);
  assert(compare_function != NULL);
  assert(buf != NULL);

  if (count < 2 || element_size  < 1)
    return;

  next_swap_location = 0;
  //
  // pick a pivot (we choose last element)
  //
  pivot = ((grub_uint8_t*)buf_to_sort + ((count - 1) * element_size));

  //
  // Now get the pivot such that all on "left" are below it
  // and everything "right" are above it
  //
  for (loopcount = 0; loopcount < count - 1; loopcount++)
  {
    //
    // if the element is less than the pivot
    //
    if (compare_function ((void*)((grub_uint8_t*)buf_to_sort +
        ((loopcount) * element_size)), pivot) <= 0)
    {
      // swap
      grub_memcpy (buf, (grub_uint8_t*)buf_to_sort +
                   (next_swap_location*element_size), element_size);
      grub_memcpy ((grub_uint8_t*)buf_to_sort + (next_swap_location*element_size),
                   (grub_uint8_t*)buf_to_sort + ((loopcount) * element_size),
                   element_size);
      grub_memcpy ((grub_uint8_t*)buf_to_sort + ((loopcount) * element_size), buf,
                   element_size);
      // increment next_swap_location
      next_swap_location++;
    }
  }
  //
  // swap pivot to it's final position (NextSwapLocaiton)
  //
  grub_memcpy (buf, pivot, element_size);
  grub_memcpy (pivot,
               (grub_uint8_t*)buf_to_sort + (next_swap_location*element_size),
               element_size);
  grub_memcpy ((grub_uint8_t*)buf_to_sort + (next_swap_location*element_size), buf,
               element_size);
  //
  // Now recurse on 2 paritial lists.
  // Neither of these will have the 'pivot' element
  // IE list is sorted left half, pivot element, sorted right half...
  //
  if (next_swap_location >= 2) 
  {
    quick_sort_worker(buf_to_sort, next_swap_location,
                      element_size, compare_function, buf);
  }

  if ((count - next_swap_location - 1) >= 2)
  {
    quick_sort_worker(
      (grub_uint8_t *)buf_to_sort + (next_swap_location+1) * element_size,
      count - next_swap_location - 1, element_size,
      compare_function, buf);
  }
  return;
}

/**
  Function to perform a Quick Sort alogrithm on a buffer of comparable elements.

  Each element must be equal sized.

  if buf_to_sort is NULL, then assert.
  if compare_function is NULL, then assert.

  if count is < 2 then perform no action.
  if Size is < 1 then perform no action.

  @param[in, out] buf_to_sort   on call a buf of (possibly sorted) elements
                                 on return a buffer of sorted elements
  @param[in] count               the number of elements in the buffer to sort
  @param[in] element_size         Size of an element in bytes
  @param[in] compare_function     The function to call to perform the comparison
                                 of any 2 elements
**/
void
perform_quick_sort (void *buf_to_sort, const grub_size_t count,
                    const grub_size_t element_size,
                    sort_compare compare_function)
{
  void *buf;
  assert (buf_to_sort != NULL);
  assert (compare_function != NULL);
  buf = grub_zalloc (element_size);
  assert (buf != NULL);
  quick_sort_worker (buf_to_sort, count, element_size, compare_function, buf);
  grub_free (buf);
  return;
}

/**
  Function to compare 2 strings.

  @param[in] buf1            Pointer to String to compare (CHAR16**).
  @param[in] buf2            Pointer to second String to compare (CHAR16**).

  @retval 0                     buf1 equal to buf2.
  @retval <0                    buf1 is less than buf2.
  @retval >0                    buf1 is greater than buf2.
**/
grub_ssize_t
string_compare (const void *buf1, const void *buf2)
{
  return (grub_strcmp((const char *)buf1, (const char *)buf2));
}
