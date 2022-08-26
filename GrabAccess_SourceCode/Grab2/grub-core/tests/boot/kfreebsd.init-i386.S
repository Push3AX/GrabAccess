/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010  Free Software Foundation, Inc.
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

#define MODE_RDRW 2
#define FLAGS_NONE 0
#define SYSCALL_OPEN 5
#define SYSCALL_WRITE 4
#define SYSCALL_RESET 55
#define SYSCALL_FSYNC 95
#define SYSCALL_ARCH 165
#define SYSCALL_EXIT 1
#define SYSCALL_ARCH_IOPERM	4
#define SYSCALL_INT 0x80

#define RESET_NOSYNC 0x4
#define RESET_HALT 0x8
#define RESET_POWEROFF 0x4000

	.section ".init", "ax"
	.global start,_start
start:
_start:
	/* open.  */
	movl $SYSCALL_OPEN, %eax
	pushl $FLAGS_NONE
	pushl $MODE_RDRW
	leal device, %ebx
	pushl %ebx
	pushl $0
	int $SYSCALL_INT
	addl $16, %esp
	movl %eax, %ecx

	/* write.  */
	movl $SYSCALL_WRITE, %eax
	pushl $(messageend-message)
	leal message, %ebx
	pushl %ebx
	pushl %ecx
	pushl $0
	int $SYSCALL_INT
	addl $16, %esp

	/* fsync.  */
	movl $SYSCALL_FSYNC, %eax
	pushl %ecx
	pushl $0
	int $SYSCALL_INT
	addl $8, %esp

	/* IOPERM.  */
	movl $SYSCALL_ARCH, %eax
	pushl $ioperm_arg1
	pushl $SYSCALL_ARCH_IOPERM
	pushl $0
	int $SYSCALL_INT
	addl $12, %esp

	/* IOPERM.  */
	movl $SYSCALL_ARCH, %eax
	pushl $ioperm_arg2
	pushl $SYSCALL_ARCH_IOPERM
	pushl $0
	int $SYSCALL_INT
	addl $12, %esp

#include "qemu-shutdown-x86.S"
	
	/* shutdown.  */
	movl $SYSCALL_RESET, %eax
	pushl $(RESET_POWEROFF|RESET_HALT|RESET_NOSYNC)
	pushl $0
	int $SYSCALL_INT
	addl $8, %esp

	/* exit (1). Shouldn't be reached.  */
	movl $SYSCALL_EXIT, %eax
	pushl $1
	pushl $0
	int $SYSCALL_INT
device:
	.ascii "/dev/console"
	.byte 0
message:
	.ascii "Boot Test Passed Successfully\n" SUCCESSFUL_BOOT_STRING "\n"
messageend:
ioperm_arg1:
	.long 0xcf8
	.long 8
	.long 1
ioperm_arg2:
	.long 0x7000
	.long 8
	.long 1
