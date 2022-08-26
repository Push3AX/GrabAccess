/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019  Free Software Foundation, Inc.
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

#ifndef GRUB_RDMSR_H
#define GRUB_RDMSR_H 1

/* TODO: Add a general protection exception handler.
         Accessing a reserved or unimplemented MSR address results in a GP#. */

static inline grub_uint64_t
grub_msr_read (grub_uint32_t msr_id)
{
    grub_uint32_t low, high;

    asm volatile ( "rdmsr"  : "=a"(low), "=d"(high) : "c"(msr_id) );

    return ((grub_uint64_t)high << 32) | low;
}

#endif