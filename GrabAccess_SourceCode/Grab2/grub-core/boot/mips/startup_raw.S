/* startup.S - Startup code for the MIPS.  */
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

#include <grub/symbol.h>
#include <grub/offsets.h>
#include <grub/machine/memory.h>
#include <grub/machine/kernel.h>
#include <grub/offsets.h>
#include <grub/mips/asm.h>

#define BASE_ADDR 8

.extern __bss_start
.extern _end
.extern _edata

	.globl __start, _start, start
	.set noreorder
	.set nomacro
__start:
_start:
start:		

	bal codestart
	 nop
base:	
	.org GRUB_DECOMPRESSOR_MACHINE_COMPRESSED_SIZE
compressed_size:
	.long 0
	.org GRUB_DECOMPRESSOR_MACHINE_UNCOMPRESSED_SIZE
uncompressed_size:
	.long 0
	.org GRUB_DECOMPRESSOR_MACHINE_UNCOMPRESSED_ADDR
uncompressed_addr:
	.long 0
codestart:
	/* Save our base.  */
	move $s0, $ra

	/* Parse arguments. Has to be done before relocation.
	So need to do it in asm.  */
#ifdef GRUB_MACHINE_MIPS_QEMU_MIPS
	lui $t0, %hi (((16 << 20) - 264 + 4) | 0x80000000)
	lw $t1, %lo (((16 << 20) - 264 + 4) | 0x80000000) ($t0)

	lui $t2, 0x1234
	ori $t2, 0x5678

	bne $t1, $t2, 1f
	 nop

	lui $t0, %hi (((16 << 20) - 264) | 0x80000000)
	b 2f
	 lw $s4, %lo (((16 << 20) - 264) | 0x80000000) ($t0)
	
1:
	li $s4, 0
2:	
#endif

#ifdef GRUB_MACHINE_MIPS_LOONGSON
	move $s2, $zero
	move $s3, $zero
	move $s4, $zero
	move $s5, $zero
	move $s7, $zero

	/* $a2 has the environment.  */
	addiu $t0, $zero, -0x10
	and $t1, $a2, $t0
	beq $t0, $t1, argfw
	 nop
	move $t0, $a2	
argcont:	
	lw $t1, 0($t0)
	beq $t1, $zero, argdone
	 nop
#define DO_PARSE(str, reg) \
	addiu $t2, $s0, (str-base);\
	bal parsestr;\
	 nop	    ;\
	beq $v0, $zero, 1f;\
	 nop		  ;\
	b 2f;\
	 move reg, $v0;	 \
1:
#define DO_CHECKT1(str, val)				 \
	move $t6, $t1					;\
	addiu $t7, $s0, (str - base);\
	bal do_check				       ;\
	 li $t2, val

	DO_PARSE (busclockstr, $s2)
	DO_PARSE (cpuclockstr, $s3)
	DO_PARSE (memsizestr, $s4)
	DO_PARSE (highmemsizestr, $s5)
	DO_CHECKT1 (pmon_yeeloong_verstr, GRUB_ARCH_MACHINE_YEELOONG)
	DO_CHECKT1 (pmon_fuloong2f_verstr, GRUB_ARCH_MACHINE_FULOONG2F)
2:	
	b argcont
	 addiu $t0, $t0, 4	 
parsestr:
	move $v0, $zero
	move $t3, $t1
3:	
	lb GRUB_ASM_T4, 0($t2)
	lb GRUB_ASM_T5, 0($t3)
	addiu $t2, $t2, 1
	addiu $t3, $t3, 1
	beq GRUB_ASM_T5, $zero, 1f
	 nop
	beq GRUB_ASM_T5, GRUB_ASM_T4, 3b
	 nop
	bne GRUB_ASM_T4, $zero, 1f
	 nop

	addiu $t3, $t3, 0xffff
digcont:	
	lb GRUB_ASM_T5, 0($t3)
	/* Substract '0' from digit.  */
	addiu GRUB_ASM_T5, GRUB_ASM_T5, 0xffd0
	bltz GRUB_ASM_T5, 1f
	 nop
	addiu GRUB_ASM_T4, GRUB_ASM_T5, 0xfff7
	bgtz GRUB_ASM_T4, 1f
	 nop
	/* Multiply $v0 by 10 with bitshifts. */
	sll $v0, $v0, 1
	sll GRUB_ASM_T4, $v0, 2
	addu $v0, $v0, GRUB_ASM_T4
	addu $v0, $v0, GRUB_ASM_T5
	addiu $t3, $t3, 1
	b digcont
	 nop
1:
	jr $ra
	 nop
busclockstr:	.asciz "busclock="
cpuclockstr:	.asciz "cpuclock="
memsizestr:	.asciz "memsize="
highmemsizestr:	.asciz "highmemsize="
machtype_yeeloong_str1:	.asciz "machtype=8.9"
machtype_yeeloong_str2:	.asciz "machtype=lemote-yeeloong-"
machtype_fuloong2f_str:	.asciz "machtype=lemote-fuloong-2f"
machtype_fuloong2e_str:	.asciz "machtype=lemote-fuloong-2e"
pmon_yeeloong_str:	.asciz "PMON_VER=LM8"
pmon_fuloong2f_str:	.asciz "PMON_VER=LM6"
pmon_yeeloong_verstr:	.asciz "Version=LM8"
pmon_fuloong2f_verstr:	.asciz "Version=LM6"
	.p2align 2

argdone:
	beq $a0, $zero, cmdlinedone
	 nop
#define DO_CHECKA1(str, val)				 \
	lw $t6, 0($a1)					;\
	addiu $t7, $s0, (str - base);\
	bal do_check				       ;\
	 li $t2, val
	DO_CHECKA1 (machtype_yeeloong_str1, GRUB_ARCH_MACHINE_YEELOONG)
	DO_CHECKA1 (machtype_yeeloong_str2, GRUB_ARCH_MACHINE_YEELOONG)
	DO_CHECKA1 (pmon_yeeloong_str, GRUB_ARCH_MACHINE_YEELOONG)
	DO_CHECKA1 (machtype_fuloong2f_str, GRUB_ARCH_MACHINE_FULOONG2F)
	DO_CHECKA1 (machtype_fuloong2e_str, GRUB_ARCH_MACHINE_FULOONG2E)
	DO_CHECKA1 (pmon_fuloong2f_str, GRUB_ARCH_MACHINE_FULOONG2F)
	addiu $a0, $a0, -1
	b argdone
  	 addiu $a1, $a1, 4
do_check:
	lb GRUB_ASM_T4, 0($t7)
	beq GRUB_ASM_T4, $zero, 1f
	 lb $t3, 0($t6)
	bne $t3, GRUB_ASM_T4, 2f
	 addiu $t6, $t6, 1
	b do_check
	 addiu $t7, $t7, 1
1:
	move $s7, $t2
2:
	jr $ra
	 nop
argfw:
	not $s7, $a2
cmdlinedone:	
#endif
#ifdef GRUB_MACHINE_ARC
	lui $t0, %hi(_start - 256)
	addiu $t0, $t0, %lo(_start - 256)
	addiu $t3, $t0, 255
	lw $t1, 0($a1)
1:	
	bne $t0, $t3, 2f
	 lb $t2, 0($t1)
	move $t2, $zero
2:
	sb $t2, 0($t0)
	addiu $t0, $t0, 1
	bnez $t2, 1b
	 addiu $t1, $t1, 1
#endif
	/* Copy the decompressor. */
	lui $t1, %hi(base)
	addiu $t1, $t1, %lo(base)
	lui $t3, %hi(__bss_start)
	addiu $t3, $t3, %lo(__bss_start)
	move $t2, $s0

1:
	beq $t1, $t3, 2f
	 lb GRUB_ASM_T4, 0($t2)
	sb GRUB_ASM_T4, 0($t1)
	addiu $t1, $t1, 1
	b 1b
	 addiu $t2, $t2, 1
2:
	/* Clean out its BSS.  */
	lui $t1, %hi(__bss_start)
	addiu $t1, $t1, %lo(__bss_start)
	lui $t2, %hi(_end)
	addiu $t2, $t2, %lo(_end)
1:
	beq $t1, $t2, 2f
	 nop
	sb $zero, 0($t1)
	b 1b
	 addiu $t1, $t1, 1
2:
	lui $a0, %hi(base)
	addiu $a0, $a0, %lo(base)
	lui $a1, %hi(_end)
	addiu $a1, %lo(_end)
	subu $a1,$a1,$a0

#include "../../kern/mips/cache_flush.S"

	/* Decompress the payload. */
	lui $a0, %hi(_edata)
	addiu $a0, $a0, %lo(_edata)

	lui $t0, %hi(base)
	addiu $t0, $t0, %lo(base)
	subu $a0, $a0, $t0
	addu $a0, $a0, $s0

	lw $a1, (GRUB_DECOMPRESSOR_MACHINE_UNCOMPRESSED_ADDR - BASE_ADDR)($s0)
	lw $a2, (GRUB_DECOMPRESSOR_MACHINE_COMPRESSED_SIZE - BASE_ADDR)($s0)
	lw $a3, (GRUB_DECOMPRESSOR_MACHINE_UNCOMPRESSED_SIZE - BASE_ADDR)($s0)
	move $s1, $a1

	/* $a0 contains source compressed address, $a1 is destination,
	   $a2 is compressed size, $a3 is uncompressed size. 
	*/
	move $s6, $a3

	lui $t9, %hi(EXT_C(grub_decompress_core))
	addiu $t9, $t9, %lo(EXT_C(grub_decompress_core))

#ifdef GRUB_MACHINE_ARC
	lui $sp, %hi(_start - 512)
	jalr $t9
	 addiu $sp, $sp, %lo(_start - 512)
#else
	lui $sp, %hi(_start - 256)
	jalr $t9
	 addiu $sp, $sp, %lo(_start - 256)
#endif	
	move $a0, $s1
	move $a1, $s6

#include "../../kern/mips/cache_flush.S"

	lui $t1, %hi(GRUB_MACHINE_LINK_ADDR)
	addiu $t1, %lo(GRUB_MACHINE_LINK_ADDR)

	jr $t1
	 nop
	/* Ensure that .data section is created. In code we suppose that _edata
	   is first location not in decompressor image. Strictly speaking it's
	   _edata only when .data is present and _etext otherwise. But checking
	   for .data presence would cost more in code than it is to ensure that
	   .data is created.
	*/
	.data
	.long 0
