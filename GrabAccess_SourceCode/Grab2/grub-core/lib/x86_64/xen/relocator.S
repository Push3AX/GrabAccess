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

#include <grub/x86_64/memory.h>
#include <grub/x86_64/types.h>
#include <grub/symbol.h>
#include <grub/xen.h>

/* Macro to load an imm64 value stored by the C-part into %rax: */
#define MOV_IMM64_RAX(var) .byte 0x48, 0xb8; VARIABLE(var); .quad 0

	.p2align	4	/* force 16-byte alignment */

VARIABLE(grub_relocator_xen_remap_start)
LOCAL(base):
	/* Remap the remapper to it's new address. */
	MOV_IMM64_RAX(grub_relocator_xen_remapper_virt)

	movq    %rax, %rdi	/* %rdi: new virtual address of remapper */
	movq    %rax, %rbx	/* Remember new virtual address */

	MOV_IMM64_RAX(grub_relocator_xen_remapper_map)

	movq    %rax, %rsi	/* %rsi: page table entry */

	movq    $UVMF_INVLPG, %rdx	/* %rdx: flags (inv. single entry) */
	movq    $__HYPERVISOR_update_va_mapping, %rax
	syscall			/* Do the remap operation */

	addq   $(LOCAL(cont) - LOCAL(base)), %rbx

	jmp *%rbx		/* Continue with new virtual address */

LOCAL(cont):
	/* Modify mappings of new page tables to be read-only. */
	MOV_IMM64_RAX(grub_relocator_xen_mfn_list)

	movq	%rax, %rbx	/* %rbx is the base of the p2m list */
	leaq	EXT_C(grub_relocator_xen_paging_areas) (%rip), %r8

1:
	movq	0(%r8), %r12	/* Get start pfn of the current area */
	movq	GRUB_TARGET_SIZEOF_LONG(%r8), %rcx	/* Get # of pg tables */
	testq	%rcx, %rcx	/* 0 -> last area reached */
	jz	3f
2:
	movq	%r12, %rdi
	shlq	$PAGE_SHIFT, %rdi	/* virtual address (1:1 mapping) */
	movq    (%rbx, %r12, 8), %rsi	/* mfn */
	shlq    $PAGE_SHIFT,  %rsi
	orq     $(GRUB_PAGE_PRESENT | GRUB_PAGE_USER), %rsi	/* Build pte */
	movq    $UVMF_INVLPG, %rdx
	movq    %rcx, %r9	/* %rcx clobbered by hypercall */
	movq    $__HYPERVISOR_update_va_mapping, %rax
	syscall

	movq    %r9, %rcx
	incq	%r12		/* next pfn */

	loop 2b

	addq	$(2 * GRUB_TARGET_SIZEOF_LONG), %r8	/* next pg table area */
	jmp	1b

3:
	/* Switch page tables: pin new L4 pt, load cr3, unpin old L4. */
	leaq   EXT_C(grub_relocator_xen_mmu_op) (%rip), %rdi
	movq   $3, %rsi		/* 3 mmu ops */
	movq   $0, %rdx		/* pdone (not used) */
	movq   $DOMID_SELF, %r10
	movq   $__HYPERVISOR_mmuext_op, %rax
	syscall

	/* Continue in virtual kernel mapping. */
	MOV_IMM64_RAX(grub_relocator_xen_remap_continue)

	jmp *%rax

VARIABLE(grub_relocator_xen_paging_areas)
	/* array of start, size pairs, size 0 is end marker */
	.quad	0, 0, 0, 0, 0, 0, 0, 0

VARIABLE(grub_relocator_xen_mmu_op)
	.space 256

VARIABLE(grub_relocator_xen_remap_end)


VARIABLE(grub_relocator_xen_start)
	/* Unmap old remapper area. */
	MOV_IMM64_RAX(grub_relocator_xen_remapper_virt2)

	movq    %rax, %rdi

	xorq    %rax, %rax	/* Invalid pte */
	movq    %rax, %rsi

	movq    $UVMF_INVLPG, %rdx
	movq    $__HYPERVISOR_update_va_mapping, %rax
	syscall

	/* Prepare registers for starting kernel. */
	MOV_IMM64_RAX(grub_relocator_xen_stack)

	movq	%rax, %rsp

	MOV_IMM64_RAX(grub_relocator_xen_start_info)

	movq	%rax, %rsi

	cld

	MOV_IMM64_RAX(grub_relocator_xen_entry_point)

	/* Now start the new kernel. */
	jmp *%rax

VARIABLE(grub_relocator_xen_end)
