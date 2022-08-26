/* startup.S - bootstrap GRUB itself */
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

#include <config.h>
#include <grub/symbol.h>
#include <grub/machine/memory.h>

	.file	"startup_pvh.S"
	.text
	.globl	start, _start
	.code32

start:
_start:
	cld
	lgdt	gdtdesc
	ljmp	$GRUB_MEMORY_MACHINE_PROT_MODE_CSEG, $1f
1:
	movl	$GRUB_MEMORY_MACHINE_PROT_MODE_DSEG, %eax
	mov	%eax, %ds
	mov	%eax, %es
	mov	%eax, %fs
	mov	%eax, %gs
	mov	%eax, %ss
	leal	LOCAL(stack_end), %esp

	/* Save address of start info structure. */
	mov	%ebx, pvh_start_info
	call	EXT_C(grub_main)
	/* Doesn't return. */

	.p2align	3
gdt:
	.word	0, 0
	.byte	0, 0, 0, 0

	/* -- code segment --
	 * base = 0x00000000, limit = 0xFFFFF (4 KiB Granularity), present
	 * type = 32bit code execute/read, DPL = 0
	 */
	.word	0xFFFF, 0
	.byte	0, 0x9A, 0xCF, 0

	/* -- data segment --
	 * base = 0x00000000, limit 0xFFFFF (4 KiB Granularity), present
	 * type = 32 bit data read/write, DPL = 0
	 */
	.word	0xFFFF, 0
	.byte	0, 0x92, 0xCF, 0

	.p2align	3
/* this is the GDT descriptor */
gdtdesc:
	.word	0x17		/* limit */
	.long	gdt		/* addr */

	.p2align	2
/* Saved pointer to start info structure. */
	.globl	pvh_start_info
pvh_start_info:
	.long	0

	.bss
	.space	GRUB_MEMORY_MACHINE_PROT_STACK_SIZE
LOCAL(stack_end):
