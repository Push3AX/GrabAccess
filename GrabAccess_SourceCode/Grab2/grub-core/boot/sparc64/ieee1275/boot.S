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

	.text
	.align	4
	/*
	 * We're writing the a.out header ourselves as newer
	 * upstream versions of binutils no longer support
	 * the a.out format on sparc64.
	 *
	 * The boot loader fits into 512 bytes with 32 bytes
	 * used for the a.out header, hence the text segment
	 * size is 512 - 32. There is no data segment and no
	 * code relocation, thus those fields remain zero.
	 */
	.word	0x1030107                        /* Magic number. */
	.word	512 - GRUB_BOOT_AOUT_HEADER_SIZE /* Size of text segment. */
	.word	0                                /* Size of initialized data. */
	.word	0                                /* Size of uninitialized data. */
	.word	0                                /* Size of symbol table || checksum. */
	.word	_start                           /* Entry point. */
	.word	0                                /* Size of text relocation. */
	.word	0                                /* Size of data relocation. */
	.globl	_start
_start:
	/* OF CIF entry point arrives in %o4 */
pic_base:
	call	boot_continue
	 mov	%o4, CIF_REG

#ifndef CDBOOT
	/* The offsets to these locations are defined by the
	 * GRUB_BOOT_MACHINE_foo macros in include/grub/sparc64/ieee1275/boot.h,
	 * and grub-setup uses this to patch these next three values as needed.
	 *
	 * The boot_path will be the OF device path of the partition where the
	 * rest of the GRUB kernel image resides.  kernel_sector will be set to
	 * the location of the first block of the GRUB kernel, and
	 * kernel_address is the location where we should load that first block.
	 *
	 * After loading in that block we will execute it by jumping to the
	 * load address plus the size of the prepended A.OUT header (32 bytes).
	 *
	 * Since this assembly code includes the 32 bytes long a.out header,
	 * we need to move the actual code entry point forward by the size
	 * of the a.out header, i.e. += GRUB_BOOT_AOUT_HEADER_SIZE.
	 */
	.org GRUB_BOOT_MACHINE_BOOT_DEVPATH + GRUB_BOOT_AOUT_HEADER_SIZE
boot_path:
	.org GRUB_BOOT_MACHINE_KERNEL_BYTE + GRUB_BOOT_AOUT_HEADER_SIZE
boot_path_end:
kernel_byte:		.xword (2 << 9)
kernel_address:		.word  GRUB_BOOT_MACHINE_KERNEL_ADDR
#else
#define boot_path (_start + 512 + SCRATCH_PAD_BOOT_SIZE)
#define boot_path_end (_start + 1024)
#include <grub/offsets.h>

	.org 8 + GRUB_BOOT_AOUT_HEADER_SIZE
kernel_byte:		.xword (2 << 9)
kernel_size:	        .word  512
kernel_address:		.word  GRUB_BOOT_SPARC64_IEEE1275_IMAGE_ADDRESS
#endif

prom_finddev_name:	.asciz "finddevice"
prom_chosen_path:	.asciz "/chosen"
prom_getprop_name:	.asciz "getprop"
prom_stdout_name:	.asciz "stdout"
prom_write_name:	.asciz "write"
prom_bootpath_name:	.asciz "bootpath"
prom_open_name:		.asciz "open"
prom_seek_name:		.asciz "seek"
prom_read_name:		.asciz "read"
prom_exit_name:		.asciz "exit"
grub_name:		.asciz "GRUB "
#ifdef CDBOOT
prom_close_name:	.asciz "close"
#endif

#define GRUB_NAME_LEN	5

	.align	4

prom_open_error:
	GET_ABS(prom_open_name, %o2)
	call	console_write
	 mov	4, %o3
	/* fallthru */

prom_error:
	GET_ABS(prom_exit_name, %o0)
	/* fallthru */

	/* %o0: OF call name
	 * %o1: input arg 1
	 */
prom_call_1_1_o2:
	clr	%o2
	ba	prom_call_x_1
	 mov	1, %g1

prom_call_getprop:
	mov	4, %g1
	stx	%g1, [%l1 + 256]
	mov	CHOSEN_NODE_REG, %o1
	ba	prom_call_x_1
	 GET_ABS(prom_getprop_name, %o0)

prom_call_3_1_o1:
	ba prom_call_3_1
	 mov	BOOTDEV_REG, %o1

	
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
prom_call_x_1:
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

boot_continue:
	mov	%o7, PIC_REG		/* PIC base */
#ifndef CDBOOT
	sethi	%hi(SCRATCH_PAD_BOOT), %l1	/* OF argument slots */
#else
	GET_ABS(_start + 512, %l1)	/* OF argument slots */
#endif

	/* Find the /chosen node so we can fetch the stdout handle,
	 * and thus perform console output.
	 *
	 * chosen_node = prom_finddevice("/chosen")
	 */
	GET_ABS(prom_finddev_name, %o0)
	call	prom_call_1_1_o2
	 GET_ABS(prom_chosen_path, %o1)

	ldx	[%l1 + 0x20], CHOSEN_NODE_REG
	brz	CHOSEN_NODE_REG, prom_error

	/* getprop(chosen_node, "stdout", &buffer, buffer_size) */
	 GET_ABS(prom_stdout_name, %o2)
	add	%l1, 256, %o3
	call	prom_call_getprop
	 mov	1024, %o4

	lduw	[%l1 + 256], STDOUT_NODE_REG
	brz,pn	STDOUT_NODE_REG, prom_error

	/* write(stdout_node, "GRUB ", strlen("GRUB ")) */
	 GET_ABS(grub_name, %o2)
	call	console_write
	 mov	GRUB_NAME_LEN, %o3

	GET_ABS(boot_path, %o3)
#ifndef CDBOOT
	ldub	[%o3], %o1
	brnz,pn	%o1, bootpath_known
#endif

	/* getprop(chosen_node, "bootpath", &buffer, buffer_size) */
	 GET_ABS(prom_bootpath_name, %o2)
	call	prom_call_getprop
	 mov	(boot_path_end - boot_path), %o4

bootpath_known:	

	/* Open up the boot_path, and use that handle to read the
	 * first block of the GRUB kernel image.
	 *
	 * bootdev_handle = open(boot_path)
	 */
	GET_ABS(prom_open_name, %o0)
	call	prom_call_1_1_o2
	 GET_ABS(boot_path, %o1)

	ldx	[%l1 + 0x20], BOOTDEV_REG
	brz,pn	BOOTDEV_REG, prom_open_error

	/* Since we have 64-bit cells, the high cell of the seek offset
	 * is zero and the low cell is the entire value.
	 *
	 * seek(bootdev, 0, *kernel_byte)
	 */
	 GET_ABS(prom_seek_name, %o0)
	clr	%o2
	call	prom_call_3_1_o1
	 LDX_ABS(kernel_byte, 0x00, %o3)

	/* read(bootdev, *kernel_address, 512) */
	GET_ABS(prom_read_name, %o0)
	LDUW_ABS(kernel_address, 0x00, %o2)
	call	prom_call_3_1_o1
#ifdef CDBOOT
	 LDUW_ABS(kernel_size, 0x00, %o3)

	GET_ABS(prom_close_name, %o0)
	mov	1, %g1
	mov	0, %o5
	call	prom_call
	 mov	BOOTDEV_REG, %o1
#else
	 mov	512, %o3
#endif

	LDUW_ABS(kernel_address, 0x00, %o2)
	jmpl	%o2, %o7
#ifdef CDBOOT
	 mov	CIF_REG, %o4
#else
	 nop
#endif
	.org GRUB_BOOT_MACHINE_CODE_END

/* the last 4 bytes in the sector 0 contain the signature */
	.word	GRUB_BOOT_MACHINE_SIGNATURE
