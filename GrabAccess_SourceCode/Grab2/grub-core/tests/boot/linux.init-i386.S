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

#define SYSCALL_WRITE 4
#define SYSCALL_RESET 88
#define SYSCALL_IOPL 110
#define SYSCALL_EXIT 1
#define SYSCALL_INT 0x80

#define STDOUT 1
#define SHUTDOWN_MAGIC1 0xfee1dead
#define SHUTDOWN_MAGIC2 0x28121969
#define SHUTDOWN_MAGIC3 0x4321fedc

	.text
	.global start, _start
_start:
start:
	/* write.  */
	movl $SYSCALL_WRITE, %eax
	movl $STDOUT, %ebx
	leal message, %ecx
	movl $(messageend-message), %edx
	int $SYSCALL_INT

	movl $SYSCALL_IOPL, %eax
	movl $3, %ebx
	int $SYSCALL_INT

#include "qemu-shutdown-x86.S"
	
	/* shutdown.  */
	movl $SYSCALL_RESET, %eax
	movl $SHUTDOWN_MAGIC1, %ebx
	movl $SHUTDOWN_MAGIC2, %ecx
	movl $SHUTDOWN_MAGIC3, %edx
	int $SYSCALL_INT

	/* exit (1). Shouldn't be reached.  */
	movl $SYSCALL_EXIT, %eax
	movl $1, %ebx
	int $SYSCALL_INT
	.data
message:
	.ascii "Boot Test Passed Successfully\n" SUCCESSFUL_BOOT_STRING "\n"
messageend:
