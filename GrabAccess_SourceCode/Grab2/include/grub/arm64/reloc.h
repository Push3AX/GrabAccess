/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013  Free Software Foundation, Inc.
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

#ifndef GRUB_ARM64_RELOC_H
#define GRUB_ARM64_RELOC_H 1

struct grub_arm64_trampoline
{
  grub_uint32_t ldr; /* ldr	x16, 8 */
  grub_uint32_t br; /* br x16 */
  grub_uint64_t addr;
};

int grub_arm_64_check_xxxx26_offset (grub_int64_t offset);
void
grub_arm64_set_xxxx26_offset (grub_uint32_t *place, grub_int64_t offset);
int
grub_arm64_check_hi21_signed (grub_int64_t offset);
void
grub_arm64_set_hi21 (grub_uint32_t *place, grub_int64_t offset);
void
grub_arm64_set_abs_lo12 (grub_uint32_t *place, grub_int64_t target);
void
grub_arm64_set_abs_lo12_ldst64 (grub_uint32_t *place, grub_int64_t target);

#endif
