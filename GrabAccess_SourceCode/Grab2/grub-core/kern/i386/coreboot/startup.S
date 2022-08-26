/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2005,2006,2007,2008 Free Software Foundation, Inc.
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
#include <grub/machine/memory.h>
#include <grub/offsets.h>
#include <multiboot.h>
#include <multiboot2.h>

/*
 * Note: GRUB is compiled with the options -mrtd and -mregparm=3.
 *       So the first three arguments are passed in %eax, %edx, and %ecx,
 *       respectively, and if a function has a fixed number of arguments
 *       and the number if greater than three, the function must return
 *       with "ret $N" where N is ((the number of arguments) - 3) * 4.
 */

    .file    "startup.S"
    .text
    .globl   start, _start
start:
_start:
    /* initialize the stack */
    movl  $GRUB_MEMORY_MACHINE_PROT_STACK, %esp

    /* jump to the main body of C code */
    jmp   EXT_C(grub_main)

/*
 *  Support for booting GRUB from a Multiboot boot loader (e.g. GRUB itself).
 */
#define MULTIBOOT_FLAGS MULTIBOOT_MEMORY_INFO
    .p2align  2 /* force 4-byte alignment */
multiboot_header:
    /* magic */
    .long   MULTIBOOT_HEADER_MAGIC
    /* flags */
    .long   MULTIBOOT_FLAGS
    /* checksum */
    .long   -(MULTIBOOT_HEADER_MAGIC+MULTIBOOT_FLAGS) /* checksum */
