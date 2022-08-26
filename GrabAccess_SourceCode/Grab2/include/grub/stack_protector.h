/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2021  Free Software Foundation, Inc.
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

#ifndef GRUB_STACK_PROTECTOR_H
#define GRUB_STACK_PROTECTOR_H	1

#include <grub/symbol.h>
#include <grub/types.h>

#ifdef GRUB_STACK_PROTECTOR
extern grub_addr_t EXPORT_VAR (__stack_chk_guard);
extern void __attribute__ ((noreturn)) EXPORT_FUNC (__stack_chk_fail) (void);
#endif

#endif /* GRUB_STACK_PROTECTOR_H */
