/* -*-Asm-*- */
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

#include <grub/machine/boot.h>
#include <grub/offsets.h>

	.text
	.align	4
	.globl	_start
_start:
	/* First stage boot block jumps to us here.  */
pic_base:
	call	after_info_block
	 mov	%o7, PIC_REG

prom_write_name:		.asciz "write"
prom_seek_name:			.asciz "seek"
prom_read_name:			.asciz "read"
prom_close_name:		.asciz "close"

notification_string:		.asciz "Loading kernel"
#define NOTIFICATION_STRING_LEN	14

notification_step:		.asciz "."
#define NOTIFICATION_STEP_LEN	1

notification_done:		.asciz "\r\n"
#define NOTIFICATION_DONE_LEN	2

	.align	4

	/* %o2: message string
	 * %o3: message length
	 */
console_write:
	GET_ABS(prom_write_name, %o0)
	mov	STDOUT_NODE_REG, %o1
	/* fallthru */

	/* %o0: OF call name
	 * %o1: input arg 1
	 * %o2: input arg 2
	 * %o3: input arg 3
	 */
prom_call_3_1:
	mov	3, %g1
	mov	1, %o5
	/* fallthru */

	/* %o0: OF call name
	 * %g1: num inputs
	 * %o5: num outputs
	 * %o1-%o4: inputs
	 */
prom_call:
	stx	%o0, [%l1 + 0x00]
	stx	%g1, [%l1 + 0x08]
	stx	%o5, [%l1 + 0x10]
	stx	%o1, [%l1 + 0x18]
	stx	%o2, [%l1 + 0x20]
	stx	%o3, [%l1 + 0x28]
	stx	%o4, [%l1 + 0x30]
	jmpl	CIF_REG, %g0
	 mov	%l1, %o0


after_info_block:
	sethi	%hi(SCRATCH_PAD_DISKBOOT), %l1	/* OF argument slots */

	GET_ABS(notification_string, %o2)
	call	console_write
	 mov	NOTIFICATION_STRING_LEN, %o3

	GET_ABS(firstlist - GRUB_BOOT_SPARC64_IEEE1275_LIST_SIZE, %l2)
	set	GRUB_BOOT_SPARC64_IEEE1275_IMAGE_ADDRESS, %l3
bootloop:
	lduw	[%l2 + 0x08], %o0
	brz	%o0, bootit
	 lduw	[%l2 + 0x00], %o3
	sllx	%o3, 32, %o3
	lduw	[%l2 + 0x04], %o4
	or	%o3, %o4, %o3
	GET_ABS(prom_seek_name, %o0)
	mov	BOOTDEV_REG, %o1
	clr	%o2
	call	prom_call_3_1
	 sllx	%o3, 9, %o3

	GET_ABS(prom_read_name, %o0)
	mov	BOOTDEV_REG, %o1
	lduw	[%l2 + 0x08], %o3
	sllx	%o3, 9, %o3
	mov	%l3, %o2
	call	prom_call_3_1
	 add	%l3, %o3, %l3

	GET_ABS(notification_step, %o2)
	call	console_write
	 mov	NOTIFICATION_STEP_LEN, %o3

	ba	bootloop
	 sub	%l2, GRUB_BOOT_SPARC64_IEEE1275_LIST_SIZE, %l2

bootit:
	GET_ABS(prom_close_name, %o0)
	mov	1, %g1
	mov	0, %o5
	call	prom_call
	 mov	BOOTDEV_REG, %o1

	GET_ABS(notification_done, %o2)
	call	console_write
	 mov	NOTIFICATION_DONE_LEN, %o3
	sethi	%hi(GRUB_BOOT_SPARC64_IEEE1275_IMAGE_ADDRESS), %o2
	jmpl	%o2 + %lo(GRUB_BOOT_SPARC64_IEEE1275_IMAGE_ADDRESS), %o7
	 mov	CIF_REG, %o4
1:	ba,a	1b

lastlist:
	.word	0
	.word	0

	.org (0x200 - GRUB_BOOT_SPARC64_IEEE1275_LIST_SIZE)
blocklist_default_start:
	.word	0
	.word	2
blocklist_default_len:
	.word	0
firstlist:
