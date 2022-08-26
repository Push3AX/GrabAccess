/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2012  Free Software Foundation, Inc.
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
#define SYSCALL_EXIT 1

#define STDOUT 1
#define SHUTDOWN_MAGIC1 0xfee1dead
#define SHUTDOWN_MAGIC2 0x28121969
#define SHUTDOWN_MAGIC3 0x4321fedc

	.text
	.global start, _start, __start
__start:	
_start:
start:
	/* write.  */
	li %r0, SYSCALL_WRITE
	li %r3, STDOUT
	lis %r4, message@h
	ori %r4, %r4, message@l
	lis %r5, messageend@h
	ori %r5, %r5, messageend@l
	sub %r5, %r5, %r4
	sc

	/* shutdown.  */
	li %r0, SYSCALL_RESET
	lis %r3, SHUTDOWN_MAGIC1@h
	ori %r3, %r3, SHUTDOWN_MAGIC1@l
	lis %r4, SHUTDOWN_MAGIC2@h
	ori %r4, %r4, SHUTDOWN_MAGIC2@l
	lis %r5, SHUTDOWN_MAGIC3@h
	ori %r5, %r5, SHUTDOWN_MAGIC3@l
	sc

	/* exit(1). Shouldn't be reached.  */
	li %r0, SYSCALL_EXIT
	li %r3, 1
	sc

	.data
message:
	.ascii "Boot Test Passed Successfully\n" SUCCESSFUL_BOOT_STRING "\n"
messageend:
