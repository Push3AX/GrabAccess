/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2016  Free Software Foundation, Inc.
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

	.file	"coreboot.S"
	.text
	.syntax	unified
#if !defined (__thumb2__)
	.arch	armv7a
	.arm
#else
	.arch	armv7
	.thumb
#endif

FUNCTION(grub_arm_pfr1)
	mrc p15, 0, r0, c0, c1, 1
	bx	lr

FUNCTION(grub_armv7_get_timer_value)
	isb
	mrrc p15, 1, r0, r1, c14
	bx	lr

FUNCTION(grub_armv7_get_timer_frequency)
	mrc p15, 0, r0, c14, c0, 0
	bx	lr

