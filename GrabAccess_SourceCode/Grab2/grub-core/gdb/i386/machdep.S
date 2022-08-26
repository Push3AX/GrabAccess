/* machdep.S - machine dependent assembly routines for the GDB stub */
/*
 *  Copyright (C) 2006  Lubomir Kundrak
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <grub/cpu/gdb.h>
#include <grub/symbol.h>

#define EC_PRESENT	1
#define EC_ABSENT	0

#define GRUB_GDB_STACKSIZE	40000

#define SEP ,

#ifdef __APPLE__
	.zerofill __DATA, __bss, LOCAL(grub_gdb_stack_end), GRUB_GDB_STACKSIZE, 4
	LOCAL(grub_gdb_stack) = LOCAL(grub_gdb_stack_end)
#else
/*
 * The .data index for the address vector.
 */

#define	VECTOR		1

	.bss
	.space GRUB_GDB_STACKSIZE
VARIABLE(grub_gdb_stack)
#endif

/*
 * Supplemental macros for register saving/restoration
 * on exception handler entry/leave.
 */

#ifdef __APPLE__
.macro save32
#define REG $0
#define NDX $1
#else
.macro save32 reg ndx
#define REG \reg
#define NDX \ndx
#endif
	movl REG, EXT_C(grub_gdb_regs)+(NDX * 4)
.endm

#undef REG
#undef NDX

#ifdef __APPLE__
.macro save16
#define REG $0
#define NDX $1
#else
.macro save16 reg ndx
#define REG \reg
#define NDX \ndx
#endif
	xorl %eax, %eax
	movw REG, EXT_C(grub_gdb_regs)+(NDX * 4)
	movw %ax, EXT_C(grub_gdb_regs)+(NDX * 4 + 2)
	movl EXT_C(grub_gdb_regs)+(EAX * 4), %eax
.endm

#undef REG
#undef NDX

#ifdef __APPLE__
.macro load32
#define NDX $0
#define REG $1
#else
.macro load32 ndx reg
#define REG \reg
#define NDX \ndx
#endif
	movl EXT_C(grub_gdb_regs)+(NDX * 4), REG
.endm

#undef REG
#undef NDX

#ifdef __APPLE__
.macro load16
#define NDX $0
#define REG $1
#else
.macro load16 ndx reg 
#define NDX \ndx
#define REG \reg
#endif
	movw EXT_C(grub_gdb_regs)+(NDX * 4), REG
.endm

#undef REG
#undef NDX

.macro save_context
	save32 %eax, EAX

	save32 %ecx, ECX
	save32 %edx, EDX
	save32 %ebx, EBX
	save32 %ebp, EBP
	save32 %esi, ESI
	save32 %edi, EDI

	popl %ebx
	save32 %ebx, EIP
	popl %ebx
	save32 %ebx, CS
	popl %ebx
	save32 %ebx, EFLAGS

	save32 %esp, ESP

	save16 %ds, DS
	save16 %es, ES
	save16 %fs, FS
	save16 %gs, GS
	save16 %ss, SS
.endm

.macro load_context
	load16 SS, %ss
	load32 ESP, %esp

	load32 EBP, %ebp
	load32 ESI, %esi
	load32 EDI, %edi

	load16 DS, %ds
	load16 ES, %es
	load16 FS, %fs
	load16 GS, %gs

	load32 EFLAGS, %eax
	pushl %eax
	load32 CS, %eax
	pushl %eax
	load32 EIP, %eax
	pushl %eax

	load32 EBX, %ebx
	load32 EDX, %edx
	load32 ECX, %ecx
	load32 EAX, %eax
.endm

/*
 * This macro creates handlers for a given range of exception numbers
 * and adds their addresses to the grub_gdb_trapvec array.
 */

#ifdef __APPLE__
.macro ent
#define EC $0
#define BEG $1
#define END $2
#else
.macro ent ec beg end=0
#define EC \ec
#define BEG \beg
#define END \end
#endif

	/*
	 * Wrapper body itself.
	 */

	.text
1:	
	.if EC
		add MACRO_DOLLAR(4), %esp
	.endif

	save_context
#ifdef __APPLE__
	mov	$LOCAL(grub_gdb_stack), %esp
#else
	mov	$EXT_C(grub_gdb_stack), %esp
#endif
	mov	$(BEG), %eax	/* trap number */
	call	EXT_C(grub_gdb_trap)
	load_context
	iret

	/*
	 * Address entry in trapvec array.
	 */

#ifdef __APPLE__
	.section __DATA, VECTOR
#else
       .data VECTOR
#endif
	.long 1b

	/*
	 * Next... (recursion).
	 */

	.if END-BEG > 0
#ifdef __APPLE__
		ent EC, (BEG+1), END
#else
		ent \ec "(\beg+1)" \end
#endif
	.endif
.endm

/*
 * Here does the actual construction of the address array and handlers
 * take place.
 */
#ifdef __APPLE__
	.section __DATA, VECTOR
#else
       .data VECTOR
#endif
VARIABLE(grub_gdb_trapvec)
	ent EC_ABSENT,	0, 7
	ent EC_PRESENT,	8
	ent EC_ABSENT,	9
	ent EC_PRESENT,	10, 14
	/*
	 * You may have to split this further or as(1)
	 * will complain about nesting being too deep.
	 */
	ent EC_ABSENT,	15, GRUB_GDB_LAST_TRAP
