/*
 * Special support for eabi and SVR4
 *
 *   Copyright (C) 1995-2014 Free Software Foundation, Inc.
 *   Written By Michael Meissner
 *   64-bit support written by David Edelsohn
 * 
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3, or (at your option) any
 * later version.
 * 
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * Under Section 7 of GPL version 3, you are granted additional
 * permissions described in the GCC Runtime Library Exception, version
 * 3.1, as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License and
 * a copy of the GCC Runtime Library Exception along with this program;
 * see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
 * <http://www.gnu.org/licenses/>.
 */ 

/* Do any initializations needed for the eabi environment */

#include <grub/symbol.h>
#include <grub/dl.h>

	.section ".text"

#define CFI_RESTORE(reg)		.cfi_restore reg
#define CFI_OFFSET(reg, off)		.cfi_offset reg, off
#define CFI_DEF_CFA_REGISTER(reg)	.cfi_def_cfa_register reg
#define CFI_STARTPROC			.cfi_startproc
#define CFI_ENDPROC			.cfi_endproc

/* Routines for restoring integer registers, called by the compiler.  */
/* Called with r11 pointing to the stack header word of the caller of the */
/* function, just beyond the end of the integer restore area.  */

CFI_STARTPROC
CFI_DEF_CFA_REGISTER (11)
CFI_OFFSET (65, 4)
CFI_OFFSET (14, -72)
CFI_OFFSET (15, -68)
CFI_OFFSET (16, -64)
CFI_OFFSET (17, -60)
CFI_OFFSET (18, -56)
CFI_OFFSET (19, -52)
CFI_OFFSET (20, -48)
CFI_OFFSET (21, -44)
CFI_OFFSET (22, -40)
CFI_OFFSET (23, -36)
CFI_OFFSET (24, -32)
CFI_OFFSET (25, -28)
CFI_OFFSET (26, -24)
CFI_OFFSET (27, -20)
CFI_OFFSET (28, -16)
CFI_OFFSET (29, -12)
CFI_OFFSET (30, -8)
CFI_OFFSET (31, -4)
FUNCTION(_restgpr_14_x)	lwz	14,-72(11)	/* restore gp registers */
CFI_RESTORE (14)
FUNCTION(_restgpr_15_x)	lwz	15,-68(11)
CFI_RESTORE (15)
FUNCTION(_restgpr_16_x)	lwz	16,-64(11)
CFI_RESTORE (16)
FUNCTION(_restgpr_17_x)	lwz	17,-60(11)
CFI_RESTORE (17)
FUNCTION(_restgpr_18_x)	lwz	18,-56(11)
CFI_RESTORE (18)
FUNCTION(_restgpr_19_x)	lwz	19,-52(11)
CFI_RESTORE (19)
FUNCTION(_restgpr_20_x)	lwz	20,-48(11)
CFI_RESTORE (20)
FUNCTION(_restgpr_21_x)	lwz	21,-44(11)
CFI_RESTORE (21)
FUNCTION(_restgpr_22_x)	lwz	22,-40(11)
CFI_RESTORE (22)
FUNCTION(_restgpr_23_x)	lwz	23,-36(11)
CFI_RESTORE (23)
FUNCTION(_restgpr_24_x)	lwz	24,-32(11)
CFI_RESTORE (24)
FUNCTION(_restgpr_25_x)	lwz	25,-28(11)
CFI_RESTORE (25)
FUNCTION(_restgpr_26_x)	lwz	26,-24(11)
CFI_RESTORE (26)
FUNCTION(_restgpr_27_x)	lwz	27,-20(11)
CFI_RESTORE (27)
FUNCTION(_restgpr_28_x)	lwz	28,-16(11)
CFI_RESTORE (28)
FUNCTION(_restgpr_29_x)	lwz	29,-12(11)
CFI_RESTORE (29)
FUNCTION(_restgpr_30_x)	lwz	30,-8(11)
CFI_RESTORE (30)
FUNCTION(_restgpr_31_x)	lwz	0,4(11)
				lwz	31,-4(11)
CFI_RESTORE (31)
				mtlr	0
CFI_RESTORE (65)
				mr	1,11
CFI_DEF_CFA_REGISTER (1)
				blr
CFI_ENDPROC

CFI_STARTPROC
FUNCTION(_savegpr_14)	stw	14,-72(11)	/* save gp registers */
FUNCTION(_savegpr_15)	stw	15,-68(11)
FUNCTION(_savegpr_16)	stw	16,-64(11)
FUNCTION(_savegpr_17)	stw	17,-60(11)
FUNCTION(_savegpr_18)	stw	18,-56(11)
FUNCTION(_savegpr_19)	stw	19,-52(11)
FUNCTION(_savegpr_20)	stw	20,-48(11)
FUNCTION(_savegpr_21)	stw	21,-44(11)
FUNCTION(_savegpr_22)	stw	22,-40(11)
FUNCTION(_savegpr_23)	stw	23,-36(11)
FUNCTION(_savegpr_24)	stw	24,-32(11)
FUNCTION(_savegpr_25)	stw	25,-28(11)
FUNCTION(_savegpr_26)	stw	26,-24(11)
FUNCTION(_savegpr_27)	stw	27,-20(11)
FUNCTION(_savegpr_28)	stw	28,-16(11)
FUNCTION(_savegpr_29)	stw	29,-12(11)
FUNCTION(_savegpr_30)	stw	30,-8(11)
FUNCTION(_savegpr_31)	stw	31,-4(11)
			blr
CFI_ENDPROC
