/* -*-Asm-*- */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008,2009  Free Software Foundation, Inc.
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
#include <grub/machine/boot.h>
#include <grub/machine/kernel.h>
#include <multiboot.h>

        .file   "cdboot.S"

#define CODE_ADDR	0x6000
#define DATA_ADDR	((GRUB_BOOT_MACHINE_KERNEL_ADDR) + 0x200)

#define CDSEC_SHIFT	11
#define CDBLK_LENG	16

	.text

        .code16

        .globl  start, _start

start:
_start:
	call	LOCAL(next)

LOCAL(next):
	jmp	1f

	.org 8

bi_pvd:
	.long 0		/* LBA of primary volume descriptor.  */
bi_file:
	.long 0		/* LBA of boot file. */
bi_length:
	.long 0		/* Length of boot file. */
bi_csum:
	.long 0		/* Checksum of boot file */
bi_reserved:
	.space (10*4)	/* Reserved */

1:
	popw	%bx

	/* Boot from CDROM.  */

	xorw	%ax, %ax
	movw	%ax, %ss
	movw	$(CODE_ADDR), %sp
	movw	%ax, %ds
	movw	%ax, %es

	movw	$(0x7C00 + err_noboot_msg - start), %si
	movl	%cs: bi_length - LOCAL(next)(%bx), %ecx
	orl	%ecx, %ecx
	jz	LOCAL(fail)

	addl	$((1 << CDSEC_SHIFT) - 1), %ecx
	shrl	$CDSEC_SHIFT, %ecx

	movl	%cs: bi_file - LOCAL(next)(%bx), %esi

	call	LOCAL(read_cdrom)

	ljmp	$(DATA_ADDR >> 4), $0

/*
 * Parameters:
 *   esi: start sector
 *   ecx: number of sectors
 */
LOCAL(read_cdrom):
	xorl	%eax, %eax

	/* Number of blocks to read.  */
	pushw	$CDBLK_LENG

	/* Block number.  */
	incl    %esi
	pushl	%eax
	pushl	%esi

	/* Buffer address.  */
	pushw	$((DATA_ADDR - 0x200)>> 4)
	pushl	%eax
	pushw	$0x10

	xorl	%edi, %edi
	movw	%sp, %si

1:
	movw	0x10(%si), %di
	cmpl	%ecx, %edi
	jbe	2f
	movl	%ecx, %edi

2:
	mov	%di, 2(%si)

	pushl	%ecx

	movb	$0x42, %ah
	int	$0x13

	jnc	3f

	movb	$0x42, %ah		/* Try again.  */
	int	$0x13

	jnc	3f

2:
	shrw	$1, %di			/* Reduce transfer size.  */
	jz	LOCAL(cdrom_fail)
	movw	%di, 0x10(%si)
	movw	%di, 2(%si)
	movb	$0x42, %ah
	int	$0x13
	jc	2b

3:

	movw	%di, %ax
	shlw	$(CDSEC_SHIFT - 4), %ax
	addw	%ax, 6(%si)
	addl	%edi, 8(%si)

	popl	%ecx
	subl	%edi, %ecx
	jnz	1b

	addw	$0x12, %sp
	ret

LOCAL(cdrom_fail):
	movw	$(0x7C00 + err_cdfail_msg - start), %si

LOCAL(fail):
	movb	$0x0e, %ah
	xorw	%bx, %bx
1:
	lodsb	(%si), %al
	int	$0x10
	cmpb	$0, %al
	jne	1b
1:	jmp	1b

err_noboot_msg:
	.ascii	"no boot info\0"

err_cdfail_msg:
	.ascii	"cdrom read fails\0"

	.org 0x7FF

	.byte	0
