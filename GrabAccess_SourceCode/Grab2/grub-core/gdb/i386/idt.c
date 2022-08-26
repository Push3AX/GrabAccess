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

static struct grub_cpu_interrupt_gate grub_gdb_idt[GRUB_GDB_LAST_TRAP + 1]
  __attribute__ ((aligned(16)));

/* Sets up a gate descriptor in the IDT table.  */
static void
grub_idt_gate (struct grub_cpu_interrupt_gate *gate, void (*offset) (void),
	       grub_uint16_t selector, grub_uint8_t type, grub_uint8_t dpl)
{
  gate->offset_lo = (int) offset & 0xffff;
  gate->selector = selector & 0xffff;
  gate->unused = 0;
  gate->gate = (type & 0x1f) | ((dpl & 0x3) << 5) | 0x80;
  gate->offset_hi = ((int) offset >> 16) & 0xffff;
}

static struct grub_cpu_idt_descriptor grub_gdb_orig_idt_desc
  __attribute__ ((aligned(16)));
static struct grub_cpu_idt_descriptor grub_gdb_idt_desc
  __attribute__ ((aligned(16)));

/* Set up interrupt and trap handler descriptors in IDT.  */
void
grub_gdb_idtinit (void)
{
  int i;
  grub_uint16_t seg;

  asm volatile ("xorl %%eax, %%eax\n"
		"mov %%cs, %%ax\n" :"=a" (seg));

  for (i = 0; i <= GRUB_GDB_LAST_TRAP; i++)
    {
      grub_idt_gate (&grub_gdb_idt[i],
		     grub_gdb_trapvec[i], seg,
                     GRUB_CPU_TRAP_GATE, 0);
    }

  grub_gdb_idt_desc.base = (grub_addr_t) grub_gdb_idt;
  grub_gdb_idt_desc.limit = sizeof (grub_gdb_idt) - 1;
  asm volatile ("sidt %0" : : "m" (grub_gdb_orig_idt_desc));
  asm volatile ("lidt %0" : : "m" (grub_gdb_idt_desc));
}

void
grub_gdb_idtrestore (void)
{
  asm volatile ("lidt %0" : : "m" (grub_gdb_orig_idt_desc));
}

void
grub_gdb_breakpoint (void)
{
  asm volatile ("int $3");
}
