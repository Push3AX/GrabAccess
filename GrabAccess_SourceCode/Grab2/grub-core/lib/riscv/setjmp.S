/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2018  Free Software Foundation, Inc.
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
#include <grub/dl.h>

	.file	"setjmp.S"

GRUB_MOD_LICENSE "GPLv3+"

	.text

#if __riscv_xlen == 64
#define STORE_IDX(reg, idx)     sd reg, (idx*8)(a0)
#define LOAD_IDX(reg, idx)      ld reg, (idx*8)(a0)
#else
#define STORE_IDX(reg, idx)     sw reg, (idx*4)(a0)
#define LOAD_IDX(reg, idx)      lw reg, (idx*4)(a0)
#endif

/*
 * int grub_setjmp (grub_jmp_buf env)
 */
FUNCTION(grub_setjmp)
        /* Preserve all callee-saved registers and the SP */
        STORE_IDX(s0, 0)
        STORE_IDX(s1, 1)
        STORE_IDX(s2, 2)
        STORE_IDX(s3, 3)
        STORE_IDX(s4, 4)
        STORE_IDX(s5, 5)
        STORE_IDX(s6, 6)
        STORE_IDX(s7, 7)
        STORE_IDX(s8, 8)
        STORE_IDX(s9, 9)
        STORE_IDX(s10, 10)
        STORE_IDX(s11, 11)
        STORE_IDX(ra, 12)
        STORE_IDX(sp, 13)
        li  a0, 0
        ret

/*
 * int grub_longjmp (grub_jmp_buf env, int val)
 */
FUNCTION(grub_longjmp)
        LOAD_IDX(s0, 0)
        LOAD_IDX(s1, 1)
        LOAD_IDX(s2, 2)
        LOAD_IDX(s3, 3)
        LOAD_IDX(s4, 4)
        LOAD_IDX(s5, 5)
        LOAD_IDX(s6, 6)
        LOAD_IDX(s7, 7)
        LOAD_IDX(s8, 8)
        LOAD_IDX(s9, 9)
        LOAD_IDX(s10, 10)
        LOAD_IDX(s11, 11)
        LOAD_IDX(ra, 12)
        LOAD_IDX(sp, 13)

        /* Move the return value in place, but return 1 if passed 0. */
        beq a1, zero, longjmp_1
        mv a0, a1
        ret

        longjmp_1:
        li a0, 1
        ret
