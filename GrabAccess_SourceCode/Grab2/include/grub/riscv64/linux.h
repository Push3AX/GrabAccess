/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2018  Free Software Foundation, Inc.
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

#ifndef GRUB_RISCV64_LINUX_HEADER
#define GRUB_RISCV64_LINUX_HEADER 1

#define GRUB_LINUX_RISCV_MAGIC_SIGNATURE 0x52534356 /* 'RSCV' */

#define GRUB_EFI_PE_MAGIC	0x5A4D

/* From linux/Documentation/riscv/booting.txt */
struct linux_riscv_kernel_header
{
  grub_uint32_t code0;		/* Executable code */
  grub_uint32_t code1;		/* Executable code */
  grub_uint64_t text_offset;	/* Image load offset */
  grub_uint64_t res0;		/* reserved */
  grub_uint64_t res1;		/* reserved */
  grub_uint64_t res2;		/* reserved */
  grub_uint64_t res3;		/* reserved */
  grub_uint64_t res4;		/* reserved */
  grub_uint32_t magic;		/* Magic number, little endian, "RSCV" */
  grub_uint32_t hdr_offset;	/* Offset of PE/COFF header */
};

#define linux_arch_kernel_header linux_riscv_kernel_header

#endif /* ! GRUB_RISCV64_LINUX_HEADER */
