/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2005,2006,2007,2008,2009 Free Software Foundation, Inc.
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

#include <config.h>
#include <grub/symbol.h>
#include <grub/machine/memory.h>
#include <grub/machine/boot.h>
#include <grub/machine/kernel.h>

	.text
	.code16
	.globl _start
_start:
	/* Disable interrupts.  */
	cli

	jmp	1f

	.org GRUB_BOOT_I386_QEMU_CORE_ENTRY_ADDR
VARIABLE(grub_core_entry_addr)
	.long	0
1:

	/* Set up %ds, %ss, and %es.  */
	xorw	%ax, %ax
	movw	%ax, %ds
	movw	%ax, %ss
	movw	%ax, %es

	/* Set up the real mode stack.  */
	movl	$GRUB_MEMORY_MACHINE_REAL_STACK, %esp

	/* Transition to protected mode.  We use pushl to force generation
	   of a flat return address.  */
	pushl	$1f
	jmp	real_to_prot
	.code32
1:
	/* Ensure A20 is enabled. We're in qemu, so control port A works
	   and there is no need to wait since there is no real logic, it's
	   all emulated.  */
	inb	$0x92
	andb	$(~0x03), %al
	orb	$0x02, %al
	outb	$0x92
	movl	EXT_C(grub_core_entry_addr), %edx
	jmp	*%edx

#include "../../../kern/i386/realmode.S"

	/* Intel, in its infinite wisdom, decided to put the i8086 entry point
	   *right here* and this is why we need this kludge.  */

	.org GRUB_BOOT_MACHINE_SIZE - 16

       .code16
       
	jmp	_start
	.org GRUB_BOOT_MACHINE_SIZE
