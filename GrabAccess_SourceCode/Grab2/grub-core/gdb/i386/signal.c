/* idt.c - routines for constructing IDT fot the GDB stub */
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

#include <grub/machine/memory.h>
#include <grub/misc.h>
#include <grub/cpu/gdb.h>
#include <grub/gdb.h>

/* Converting CPU trap number to UNIX signal number as
   described in System V ABI i386 Processor Supplement, 3-25.  */
unsigned int
grub_gdb_trap2sig (int trap_no)
{
  const int signals[] = {
    SIGFPE,			/* 0:   Divide error fault              */
    SIGTRAP,			/* 1:   Single step trap fault          */
    SIGABRT,			/* 2:   # Nonmaskable interrupt         */
    SIGTRAP,			/* 3:   Breakpoint trap                 */
    SIGSEGV,			/* 4:   Overflow trap                   */
    SIGSEGV,			/* 5:   Bounds check fault              */
    SIGILL,			/* 6:   Invalid opcode fault            */
    SIGFPE,			/* 7:   No coprocessor fault            */
    SIGABRT,			/* 8:   # Double fault abort            */
    SIGSEGV,			/* 9:   Coprocessor overrun abort       */
    SIGSEGV,			/* 10:  Invalid TSS fault               */
    SIGSEGV,			/* 11:  Segment not present fault       */
    SIGSEGV,			/* 12:  Stack exception fault           */
    SIGSEGV,			/* 13:  General protection fault abort  */
    SIGSEGV,			/* 14:  Page fault                      */
    SIGABRT,			/* 15:  (reserved)                      */
    SIGFPE,			/* 16:  Coprocessor error fault         */
    SIGUSR1			/* other                                */
  };

  return signals[trap_no < 17 ? trap_no : 17];
}

