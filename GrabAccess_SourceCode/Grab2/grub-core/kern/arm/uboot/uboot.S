/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013  Free Software Foundation, Inc.
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

#include <grub/offsets.h>
#include <grub/symbol.h>
#include <grub/machine/kernel.h>

	/*
	 * uboot_syscall():
	 *   This function is effectively a veneer, so it cannot
	 *   modify the stack or corrupt any registers other than
	 *   r12 (ip). Furthermore it needs to restore r8 for
	 *   U-Boot (Global Data Pointer) and preserve it for Grub.
	 */
FUNCTION(grub_uboot_syscall)
	str     r8, transition_space
	str     lr, transition_space + 4
	str     r9, transition_space + 8

	ldr	ip, saved_registers_ptr
	ldr	r8, [ip, #4 * 8]
	ldr	r9, [ip, #4 * 9]

	bl	do_syscall

	ldr     r8, transition_space
	ldr     lr, transition_space + 4
	ldr     r9, transition_space + 8

	bx	lr
do_syscall:

	ldr	ip, grub_uboot_syscall_ptr
	bx	ip
	
FUNCTION(grub_uboot_return)
	ldr	ip, saved_registers_ptr
	ldr	sp, [ip, #4 * 4]
	pop	{r4-r12, lr}
	mov	sp, r12
	bx	lr

	
	.align	3

@ GRUB context stack space
transition_space:	
	.long	0	@ r8
	.long	0	@ lr
	.long	0	@ r9

saved_registers_ptr:
	.long EXT_C(grub_arm_saved_registers)

VARIABLE(grub_uboot_syscall_ptr)
	.long	0	@

	END
