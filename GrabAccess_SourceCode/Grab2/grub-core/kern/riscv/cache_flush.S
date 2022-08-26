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

#include <grub/symbol.h>

	.file	"cache_flush.S"
	.text

/*
 * Simple cache maintenance functions
 */

/*
 * a0 - *beg (inclusive)
 * a1 - *end (exclusive)
 * a2 - line size
*/
FUNCTION(grub_arch_clean_dcache_range)
	/* TODO */
	ret

/*
 * a0 - *beg (inclusive)
 * a1 - *end (exclusive)
 * a2 - line size
 */
FUNCTION(grub_arch_invalidate_icache_range)
	fence.i
	ret
