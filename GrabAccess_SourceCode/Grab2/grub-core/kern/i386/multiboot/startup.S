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
    movl    %eax, EXT_C(kern_multiboot_magic)
    movl    %ebx, EXT_C(kern_multiboot_info)

    /* initialize the stack */
    movl  $GRUB_MEMORY_MACHINE_PROT_STACK, %esp

    /* jump to the main body of C code */
    jmp   EXT_C(grub_main)

/*
 *  Support for booting GRUB from a Multiboot boot loader (e.g. GRUB itself).
 */
#define MULTIBOOT_FLAGS (MULTIBOOT_MEMORY_INFO | \
                         MULTIBOOT_VIDEO_MODE)
    .p2align  2 /* force 4-byte alignment */
multiboot_header:
    /* magic */
    .long   MULTIBOOT_HEADER_MAGIC
    /* flags */
    .long   MULTIBOOT_FLAGS
    /* checksum */
    .long   -(MULTIBOOT_HEADER_MAGIC+MULTIBOOT_FLAGS) /* checksum */
    .long   0
    .long   0
    .long   0
    .long   0
    .long   0

    .long   1 /* mode_type */
    .long   1024 /* cols */
    .long   768 /* rows */
    .long   32 /* color depth */

/*
 *  Support for booting GRUB from a Multiboot2 boot loader (e.g. GRUB itself).
 */
#define MULTIBOOT2_ARCH MULTIBOOT2_ARCHITECTURE_I386
    .align  8
mb2_hdr:
    /* magic */
    .long   MULTIBOOT2_HEADER_MAGIC
    /* architecture */
    .long   MULTIBOOT2_ARCH
    /* header length */
    .long   mb2_hdr_end - mb2_hdr
    /* checksum */
    .long   -(MULTIBOOT2_HEADER_MAGIC + MULTIBOOT2_ARCH + (mb2_hdr_end - mb2_hdr))
    .align  8
inf_tag_start:
    .short  MULTIBOOT2_HEADER_TAG_INFORMATION_REQUEST
    .short  MULTIBOOT2_HEADER_TAG_REQUIRED
    .long   inf_tag_end - inf_tag_start
    .long   MULTIBOOT2_TAG_TYPE_BASIC_MEMINFO
    .long   MULTIBOOT2_TAG_TYPE_MMAP
inf_tag_end:
    .align  8
align_tag_start:
    .short  MULTIBOOT2_HEADER_TAG_MODULE_ALIGN
    .short  MULTIBOOT2_HEADER_TAG_REQUIRED
    .long   align_tag_end - align_tag_start
align_tag_end:
    .align  8
console_tag_start:
    .short  MULTIBOOT2_HEADER_TAG_CONSOLE_FLAGS
    .short  MULTIBOOT2_HEADER_TAG_REQUIRED
    .long   console_tag_end - console_tag_start
    .long   MULTIBOOT2_CONSOLE_FLAGS_EGA_TEXT_SUPPORTED
console_tag_end:
    .align  8
fb_tag_start:
    .short  MULTIBOOT2_HEADER_TAG_FRAMEBUFFER
    .short  MULTIBOOT2_HEADER_TAG_OPTIONAL
    .long   fb_tag_end - fb_tag_start
    .long   1024
    .long   768
    .long   32
fb_tag_end:
    .align  8
tag_end:
    .short  MULTIBOOT2_HEADER_TAG_END
    .short  MULTIBOOT2_HEADER_TAG_REQUIRED
    .long   tag_end_end - tag_end
tag_end_end:
mb2_hdr_end:

#include "../realmode.S"
#include "../int.S"
#include "../reboot.S"
