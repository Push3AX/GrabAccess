/* -*-Asm-*- */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2007,2008,2009  Free Software Foundation, Inc.
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
#include <grub/machine/boot.h>
#include <grub/machine/kernel.h>
#include <multiboot.h>

        .file   "lnxboot.S"

#define CODE_ADDR	0x6000
#define CODE_SECTORS	1
#define DATA_ADDR	((GRUB_BOOT_MACHINE_KERNEL_ADDR) + 0x200)

#define BLCK_LENG	0x4000

	.text

        .code16

        .globl  start, _start

data_start:
	xorl	%ebp, %ebp
	jmp	LOCAL(linux_next)

	.org 0x1F1

setup_sects:
	.byte	CODE_SECTORS
root_flags:
	.word	0
syssize:
	.word	0
swap_dev:
	.word	0
ram_size:
	.word	0
vid_mode:
	.word	0
root_dev:
	.word	0
boot_flag:
	.word	0xAA55

start:
_start:

	jmp LOCAL(linux_init)

	.ascii	"HdrS"			/* Header signature.  */
	.word	0x0203			/* Header version number.  */

realmode_swtch:
	.word	0, 0			/* default_switch, SETUPSEG.  */
start_sys_seg:
	.word	0x1000			/* Obsolete.  */
version_ptr:
	.word	0			/* Version string ptr.  */
type_of_loader:
	.byte	0			/* Filled in by boot loader.  */
loadflags:
	.byte	1			/* Please load high.  */
setup_move_size:
	.word	0			/* Unused.  */
code32_start:
	.long	0x100000		/* 32-bit start address.  */
ramdisk_image:
	.long	0			/* Loaded ramdisk image address.  */
ramdisk_size:
	.long	0			/* Size of loaded ramdisk.  */
bootsect_kludge:
	.word	0, 0
heap_end_ptr:
	.word	0
pad1:
	.word	0
cmd_line_ptr:
	.long	0			/* Command line.  */
ramdisk_max:
	.long	0xffffffff		/* Highest allowed ramdisk address.  */

gdt:
	.long	0, 0, 0, 0		/* Must be zero.  */
	.word	0xffff			/* 64 K segment size.  */
gdt_src1:
	.byte	0, 0 ,0			/* Low 24 bits of source address.  */
	.byte	0x93			/* Access rights.  */
	.byte	0			/* Extended access rights.  */
gdt_src2:
	.byte	0			/* High 8 bits of source address.  */
	.word	0xffff			/* 64 K segment size.  */
gdt_dst1:
	.byte	0, 0, 0			/* Low 24 bits of target address.  */
	.byte	0x93			/* Access rights.  */
	.byte	0			/* Extended access rights.  */
gdt_dst2:
	.byte	0			/* High 8 bits of source address.  */
	.long	0, 0, 0, 0		/* More space for the BIOS.  */

reg_edx:
	.byte	0x80, 0, 0xFF, 0xFF

data_leng:
	.long	0

LOCAL(linux_init):
	movw	%cs:(reg_edx - start), %dx
	movl	%cs:(code32_start - start), %ebp

LOCAL(linux_next):

	call	LOCAL(normalize)

LOCAL(normalize):
	popw	%bx
	subw	$(LOCAL(normalize) - start), %bx
	shrw	$4, %bx
	movw	%cs, %ax
	addw	%bx, %ax
	pushw	%ax
	pushw	$(real_code - start)
	lret				/* Jump to real_code.  */

real_code:
	subw	$0x20, %ax
	movw	%ax, %ds
	movw	(setup_sects - data_start), %cx
	shlw	$7, %cx

	/* Setup stack.  */

	xorw	%si, %si
	movw	%si, %ss
	movw	$(CODE_ADDR), %sp

	/* Move itself to 0:CODE_ADDR.  */

	cld
	movw	%cs, %ax
	movw	%ax, %ds
	movw	$(CODE_ADDR >> 4), %ax
	movw	%ax, %es
	movw	%si, %di

	rep
	movsl
	ljmp	$(CODE_ADDR >> 4), $(real_code_2  - start)

real_code_2:

	xchgl	%ebp, %esi
	orl	%esi, %esi
	jnz	1f
	movw	%ds, %si
	shll	$4, %esi
	addl	%ebp, %esi
1:

	pushw	%es
	popw	%ds

	movl	$0x1000, %ecx
	addl	$0x200, %esi
	movl	$DATA_ADDR, %edi

	call	LOCAL(move_memory)

	/* Check for multiboot signature.  */
	movl	$DATA_ADDR, %edi
3:
	movl	%ss:(%edi), %eax
	cmpl	$MULTIBOOT_HEADER_MAGIC, %eax
	jz	1f
	addl	$4, %edi
	cmpl	$(DATA_ADDR + 0x1000), %edi
	jne     3b

	movl	(ramdisk_image - start), %esi
	movl	(ramdisk_size - start), %ecx
	movl	$(DATA_ADDR - 0x200), %edi
	jmp	2f

1:

	movl	$(DATA_ADDR + 0x1000), %edi
	movl	%ss:(DATA_ADDR + GRUB_DECOMPRESSOR_MACHINE_COMPRESSED_SIZE), %ecx
	addl	$GRUB_DECOMPRESSOR_I386_PC_MAX_DECOMPRESSOR_SIZE, %ecx

2:
	call	LOCAL(move_memory)

	movb	%dh, %ss:(DATA_ADDR + GRUB_DECOMPRESSOR_I386_PC_BOOT_DEVICE + 2)
	movb	(reg_edx + 2 - start), %dh
	movb	%dh, %ss:(DATA_ADDR + GRUB_DECOMPRESSOR_I386_PC_BOOT_DEVICE + 1)

	movb	$0xFF, %dh

	ljmp	$(DATA_ADDR >> 4), $0

/*
 * Parameters:
 *   esi: source address
 *   edi: target address
 *   ecx: number of bytes
 */

LOCAL(move_memory):
	incl	%ecx
	andb	$0xFE, %cl
	pushw	%dx
1:
	pushl	%esi
	pushl	%edi
	pushl	%ecx
	cmpl	$BLCK_LENG, %ecx
	jbe	2f
	movl	$BLCK_LENG, %ecx
2:
	pushl	%ecx

	movl	%esi, %eax
	movw	%si, (gdt_src1 - start)
	shrl	$16, %eax
	movb	%al, (gdt_src1 + 2 - start)
	movb	%ah, (gdt_src2 - start)

	movl	%edi, %eax
	movw	%di, (gdt_dst1 - start)
	shrl	$16, %eax
	movb	%al, (gdt_dst1 + 2 - start)
	movb	%ah, (gdt_dst2 - start)

	movw	$(gdt - start), %si
	movb	$0x87, %ah
	shrw	$1, %cx

	int	$0x15

	popl	%eax
	popl	%ecx
	popl	%edi
	popl	%esi

	jnc	2f
	movw	$(err_int15_msg - start), %si
	jmp	LOCAL(fail)

2:

	addl	%eax, %esi
	addl	%eax, %edi
	subl	%eax, %ecx
	jnz	1b


	popw	%dx
	ret

/*
 * Parameters:
 *   si: message
 */

LOCAL(fail):
	movb	$0x0e, %ah
	xorw	%bx, %bx
1:
	lodsb	(%si), %al
	int	$0x10
	cmpb	$0, %al
	jne	1b
1:	jmp	1b

err_int15_msg:
	.ascii	"move memory fails\0"

	.org (CODE_SECTORS * 512 + 512)
