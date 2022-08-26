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

#include <grub/i386/memory.h>
#include <grub/i386/types.h>
#include <grub/symbol.h>
#include <grub/xen.h>

	.p2align	4	/* force 16-byte alignment */

VARIABLE(grub_relocator_xen_remap_start)
LOCAL(base):
	/* Remap the remapper to it's new address. */
	/* mov imm32, %ebx - %ebx: new virtual address of remapper */
	.byte	0xbb
VARIABLE(grub_relocator_xen_remapper_virt)
	.long	0

	/* mov imm32, %ecx - %ecx: low part of page table entry */
	.byte	0xb9
VARIABLE(grub_relocator_xen_remapper_map)
	.long	0

	/* mov imm32, %edx  - %edx: high part of page table entry */
	.byte	0xba
VARIABLE(grub_relocator_xen_remapper_map_high)
	.long	0

	movl	%ebx, %ebp	/* %ebx is clobbered by hypercall */

	movl    $UVMF_INVLPG, %esi	/* esi: flags (inv. single entry) */
	movl    $__HYPERVISOR_update_va_mapping, %eax
	int     $0x82

	movl	%ebp, %ebx
	addl   $(LOCAL(cont) - LOCAL(base)), %ebx

	jmp *%ebx		/* Continue with new virtual address */

LOCAL(cont):
	/* Modify mappings of new page tables to be read-only. */
	/* mov imm32, %eax */
	.byte	0xb8
VARIABLE(grub_relocator_xen_paging_areas_addr)
	.long	0
	movl	%eax, %ebx
1:
	movl	0(%ebx), %ebp	/* Get start pfn of the current area */
	movl	GRUB_TARGET_SIZEOF_LONG(%ebx), %ecx	/* Get # of pg tables */
	testl	%ecx, %ecx	/* 0 -> last area reached */
	jz	3f
	addl	$(2 * GRUB_TARGET_SIZEOF_LONG), %ebx
	movl	%ebx, %esp	/* Save current area pointer */

2:
	movl	%ecx, %edi
	/* mov imm32, %eax */
	.byte	0xb8
VARIABLE(grub_relocator_xen_mfn_list)
	.long	0
	movl    0(%eax, %ebp, 4), %ecx	/* mfn */
	movl	%ebp, %ebx
	shll	$PAGE_SHIFT, %ebx	/* virtual address (1:1 mapping) */
	movl    %ecx, %edx
	shll    $PAGE_SHIFT,  %ecx	/* prepare pte low part */
	shrl    $(32 - PAGE_SHIFT),  %edx	/* pte high part */
	orl     $(GRUB_PAGE_PRESENT | GRUB_PAGE_USER), %ecx	/* pte low */
	movl    $UVMF_INVLPG, %esi
	movl    $__HYPERVISOR_update_va_mapping, %eax
	int     $0x82		/* parameters: eax, ebx, ecx, edx, esi */

	incl	%ebp		/* next pfn */
	movl	%edi, %ecx

	loop	2b

	mov	%esp, %ebx	/* restore area poniter */
	jmp	1b

3:
	/* Switch page tables: pin new L3 pt, load cr3, unpin old L3. */
	/* mov imm32, %ebx */
	.byte	0xbb
VARIABLE(grub_relocator_xen_mmu_op_addr)
	.long  0
	movl   $3, %ecx		/* 3 mmu ops */
	movl   $0, %edx		/* pdone (not used) */
	movl   $DOMID_SELF, %esi
	movl   $__HYPERVISOR_mmuext_op, %eax
	int     $0x82

	/* Continue in virtual kernel mapping. */
	/* mov imm32, %eax */
	.byte	0xb8
VARIABLE(grub_relocator_xen_remap_continue)
	.long	0

	jmp *%eax

VARIABLE(grub_relocator_xen_paging_areas)
	.long	0, 0, 0, 0, 0, 0, 0, 0

VARIABLE(grub_relocator_xen_mmu_op)
	.space 256

VARIABLE(grub_relocator_xen_remap_end)


VARIABLE(grub_relocator_xen_start)
	/* Unmap old remapper area. */
	/* mov imm32, %eax */
	.byte	0xb8
VARIABLE(grub_relocator_xen_remapper_virt2)
	.long	0

	movl    %eax, %ebx

	xorl    %ecx, %ecx	/* Invalid pte */
	xorl    %edx, %edx

	movl    $UVMF_INVLPG, %esi
	movl    $__HYPERVISOR_update_va_mapping, %eax
	int     $0x82

	/* Prepare registers for starting kernel. */
	/* mov imm32, %eax */
	.byte	0xb8
VARIABLE(grub_relocator_xen_stack)
	.long	0

	movl	%eax, %esp

	/* mov imm32, %eax */
	.byte	0xb8
VARIABLE(grub_relocator_xen_start_info)
	.long	0

	movl	%eax, %esi

	cld

	/* mov imm32, %eax */
	.byte	0xb8
VARIABLE(grub_relocator_xen_entry_point)
	.long	0

	/* Now start the new kernel. */
	jmp *%eax

VARIABLE(grub_relocator_xen_end)
