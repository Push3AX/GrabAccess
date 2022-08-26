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
/*
 *  Library used for sorting and comparison routines.
 *
 *  Copyright (c) 2009 - 2014, Intel Corporation. All rights reserved.
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __SORT_LIB_H__
#define __SORT_LIB_H__

#include <grub/types.h>

/**
  Prototype for comparison function for any two element types.

  @param[in] Buffer1                  The pointer to first buffer.
  @param[in] Buffer2                  The pointer to second buffer.

  @retval 0                           Buffer1 equal to Buffer2.
  @return <0                          Buffer1 is less than Buffer2.
  @return >0                          Buffer1 is greater than Buffer2.
**/
typedef
grub_ssize_t
(*sort_compare)(const void *buf1, const void *buf2);

/**
  Function to perform a Quick Sort on a buffer of comparable elements.

  Each element must be equally sized.

  If BufferToSort is NULL, then ASSERT.
  If CompareFunction is NULL, then ASSERT.

  If Count is < 2 , then perform no action.
  If Size is < 1 , then perform no action.

  @param[in, out] BufferToSort   On call, a Buffer of (possibly sorted) elements;
                                 on return, a buffer of sorted elements.
  @param[in]  Count              The number of elements in the buffer to sort.
  @param[in]  ElementSize        The size of an element in bytes.
  @param[in]  CompareFunction    The function to call to perform the comparison
                                 of any two elements.
**/
void
perform_quick_sort (void *buf_to_sort, const grub_size_t count,
                    const grub_size_t element_size,
                    sort_compare compare_function);

/**
  Function to compare 2 strings.

  @param[in] Buffer1            The pointer to String to compare (CHAR16**).
  @param[in] Buffer2            The pointer to second String to compare (CHAR16**).

  @retval 0                     Buffer1 equal to Buffer2.
  @return < 0                   Buffer1 is less than Buffer2.
  @return > 0                   Buffer1 is greater than Buffer2.
**/
grub_ssize_t
string_compare (const void *buf1, const void *buf2);

#endif //__SORT_LIB_H__
