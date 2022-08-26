/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2005,2006,2007,2008,2009,2011 Free Software Foundation, Inc.
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
#include <grub/offsets.h>
#include <grub/machine/boot.h>
#include <grub/machine/memory.h>
#include <grub/machine/kernel.h>

#define ABS(x)	((x) - LOCAL (base) + GRUB_BOOT_MACHINE_KERNEL_ADDR + 0x200)

	.file	"startup_raw.S"

	.text

	/* Tell GAS to generate 16-bit instructions so that this code works
	   in real mode. */
	.code16

	.globl	start, _start
start:
_start:
LOCAL (base):
	/*
	 *  Guarantee that "main" is loaded at 0x0:0x8200.
	 */
#ifdef __APPLE__
	ljmp $0, $(ABS(LOCAL (codestart)) - 0x10000)
#else
	ljmp $0, $ABS(LOCAL (codestart))
#endif

	/*
	 *  This is a special data area.
	 */

	.org GRUB_DECOMPRESSOR_MACHINE_COMPRESSED_SIZE
LOCAL(compressed_size):
	.long 0
	.org GRUB_DECOMPRESSOR_MACHINE_UNCOMPRESSED_SIZE
LOCAL(uncompressed_size):
	.long 0

	.org GRUB_KERNEL_I386_PC_REED_SOLOMON_REDUNDANCY
reed_solomon_redundancy:
	.long	0
	.org GRUB_KERNEL_I386_PC_NO_REED_SOLOMON_LENGTH
	.short	(LOCAL(reed_solomon_part) - _start)

/*
 *  This is the area for all of the special variables.
 */
	.org GRUB_DECOMPRESSOR_I386_PC_BOOT_DEVICE
LOCAL(boot_dev):
	.byte	0xFF, 0xFF, 0xFF
LOCAL(boot_drive):
	.byte	0x00

/* the real mode code continues... */
LOCAL (codestart):
	cli		/* we're not safe here! */

	/* set up %ds, %ss, and %es */
	xorw	%ax, %ax
	movw	%ax, %ds
	movw	%ax, %ss
	movw	%ax, %es

	/* set up the real mode/BIOS stack */
	movl	$GRUB_MEMORY_MACHINE_REAL_STACK, %ebp
	movl	%ebp, %esp

	sti		/* we're safe again */

	/* save the boot drive */
	movb	%dl, LOCAL(boot_drive)

	/* reset disk system (%ah = 0) */
	int	$0x13

	/* transition to protected mode */
	calll	real_to_prot

	/* The ".code32" directive takes GAS out of 16-bit mode. */
	.code32

	cld
	call	grub_gate_a20

	movl	LOCAL(compressed_size), %edx
#ifdef __APPLE__
	addl    $decompressor_end, %edx
	subl    $(LOCAL(reed_solomon_part)), %edx
#else
	addl    $(LOCAL(decompressor_end) - LOCAL(reed_solomon_part)), %edx
#endif
	movl    reed_solomon_redundancy, %ecx
	leal    LOCAL(reed_solomon_part), %eax
	cld
	call    EXT_C (grub_reed_solomon_recover)
	jmp	post_reed_solomon

#include "../../../kern/i386/realmode.S"

/*
 *
 * This is a workaround for clang adding a section containing only .addrsig
 * Since clang itself is unable to assemble this pseudo-opcode, just replace
 * it with .text
 *
 */
#define addrsig text
#include <rs_decoder.h>
#undef addrsig

	.text

/*
 * grub_gate_a20(void)
 *
 * Gate address-line 20 for high memory.
 *
 * This routine is probably overconservative in what it does, but so what?
 *
 * It also eats any keystrokes in the keyboard buffer.  :-(
 */

grub_gate_a20:	
gate_a20_test_current_state:
	/* first of all, test if already in a good state */
	call	gate_a20_check_state
	testb	%al, %al
	jnz	gate_a20_try_bios
	ret

gate_a20_try_bios:
	/* second, try a BIOS call */
	pushl	%ebp
	call	prot_to_real

	.code16
	movw	$0x2401, %ax
	int	$0x15

	calll	real_to_prot
	.code32

	popl	%ebp
	call	gate_a20_check_state
	testb	%al, %al
	jnz	gate_a20_try_system_control_port_a
	ret

gate_a20_try_system_control_port_a:
	/*
	 * In macbook, the keyboard test would hang the machine, so we move
	 * this forward.
	 */
	/* fourth, try the system control port A */
	inb	$0x92
	andb	$(~0x03), %al
	orb	$0x02, %al
	outb	$0x92

	call	gate_a20_check_state
	testb	%al, %al
	jnz	gate_a20_try_keyboard_controller
	ret

gate_a20_flush_keyboard_buffer:
	inb	$0x64
	andb	$0x02, %al
	jnz	gate_a20_flush_keyboard_buffer
2:
	inb	$0x64
	andb	$0x01, %al
	jz	3f
	inb	$0x60
	jmp	2b
3:
	ret

gate_a20_try_keyboard_controller:
	/* third, try the keyboard controller */
	call    gate_a20_flush_keyboard_buffer

	movb	$0xd1, %al
	outb	$0x64
4:
	inb	$0x64
	andb	$0x02, %al
	jnz	4b

	movb	$0xdf, %al
	outb	$0x60
	call    gate_a20_flush_keyboard_buffer

	/* output a dummy command (USB keyboard hack) */
	movb	$0xff, %al
	outb	$0x64
	call    gate_a20_flush_keyboard_buffer

	call	gate_a20_check_state
	testb	%al, %al
	/* everything failed, so restart from the beginning */
	jnz	gate_a20_try_bios
	ret

gate_a20_check_state:
	/* iterate the checking for a while */
	movl	$100, %ecx
1:
	call	3f
	testb	%al, %al
	jz	2f
	loop	1b
2:
	ret
3:
	pushl	%ebx
	pushl	%ecx
	xorl	%eax, %eax
	/* compare the byte at 0x8000 with that at 0x108000 */
	movl	$GRUB_BOOT_MACHINE_KERNEL_ADDR, %ebx
	pushl	%ebx
	/* save the original byte in CL */
	movb	(%ebx), %cl
	/* store the value at 0x108000 in AL */
	addl	$0x100000, %ebx
	movb	(%ebx), %al
	/* try to set one less value at 0x8000 */
	popl	%ebx
	movb	%al, %ch
	decb	%ch
	movb	%ch, (%ebx)
	/* serialize */
	outb	%al, $0x80
	outb	%al, $0x80
	/* obtain the value at 0x108000 in CH */
	pushl	%ebx
	addl	$0x100000, %ebx
	movb	(%ebx), %ch
	/* this result is 0 if A20 is on or 1 if it is off */
	subb	%ch, %al
	/* restore the original */
	popl	%ebx
	movb	%cl, (%ebx)
	popl	%ecx
	popl	%ebx
	ret

LOCAL(reed_solomon_part):

/*
 * Support for booting GRUB from a Multiboot boot loader (e.g. GRUB itself).
 * This uses the a.out kludge to load raw binary to the area starting at 1MB,
 * and relocates itself after loaded.
 */
	.p2align	2	/* force 4-byte alignment */
multiboot_header:
	/* magic */
	.long	0x1BADB002
	/* flags */
	.long	(1 << 16)
	/* checksum */
	.long	-0x1BADB002 - (1 << 16)
	/* header addr */
	.long	multiboot_header - _start + 0x100000 + 0x200
	/* load addr */
	.long	0x100000
	/* load end addr */
	.long	0
	/* bss end addr */
	.long	0
	/* entry addr */
	.long	multiboot_entry - _start + 0x100000 + 0x200

multiboot_entry:
	.code32
	/* obtain the boot device */
	movl	12(%ebx), %edx

	movl	$GRUB_MEMORY_MACHINE_PROT_STACK, %ebp
	movl	%ebp, %esp

	/* relocate the code */
#ifdef __APPLE__
	LOCAL(compressed_size_offset) = LOCAL(compressed_size) - LOCAL(base)
	movl	$0x200, %ecx
	addl    $decompressor_end, %ecx
	subl    $LOCAL(base), %ecx
	addl	LOCAL(compressed_size_offset) + 0x100000 + 0x200, %ecx
#else
	movl	$(LOCAL(decompressor_end) - _start + 0x200), %ecx
	addl	LOCAL(compressed_size) - _start + 0x100000 + 0x200, %ecx
#endif
	movl	$0x100000, %esi
	movl	$GRUB_BOOT_MACHINE_KERNEL_ADDR, %edi
	cld
	rep
	movsb
	/* jump to the real address */
	movl	$multiboot_trampoline, %eax
	jmp	*%eax

multiboot_trampoline:
	/* fill the boot information */
	movl	%edx, LOCAL(boot_dev)
	shrl	$24, %edx
	/* enter the usual booting */
	call	prot_to_real
	.code16
	jmp     LOCAL (codestart)
	.code32

post_reed_solomon:

#ifdef ENABLE_LZMA
	movl	$GRUB_MEMORY_MACHINE_DECOMPRESSION_ADDR, %edi
#ifdef __APPLE__
	movl	$decompressor_end, %esi
#else
	movl	$LOCAL(decompressor_end), %esi
#endif
	pushl	%edi
	movl	LOCAL (uncompressed_size), %ecx
	leal	(%edi, %ecx), %ebx
	/* Don't remove this push: it's an argument.  */
	push 	%ecx
	call	_LzmaDecodeA
	pop	%ecx
	/* _LzmaDecodeA clears DF, so no need to run cld */
	popl	%esi
#endif

	movl	LOCAL(boot_dev), %edx
	movl	$prot_to_real, %edi
	movl	$real_to_prot, %ecx
	movl	$LOCAL(realidt), %eax
	jmp	*%esi

#ifdef ENABLE_LZMA
#include "lzma_decode.S"
#endif

	.p2align 4

#ifdef __APPLE__
	.zerofill __DATA, __aa_before_bss, decompressor_end, 10, 0
#else
	.bss
LOCAL(decompressor_end):
#endif
