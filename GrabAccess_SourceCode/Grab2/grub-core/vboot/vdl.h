/* vdl.h - types and prototypes for loadable module support */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2004,2005,2007,2008,2009  Free Software Foundation, Inc.
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

#ifndef GRUB_VDL_H
#define GRUB_VDL_H	1

/* .text section address */
extern void *p_text_section_addr;

grub_err_t vboot_arch_dl_check_header (void *ehdr);
grub_err_t vboot_arch_dl_relocate_symbols (grub_dl_t mod, void *ehdr);

grub_dl_t EXPORT_FUNC(vboot_dl_load_file) (const char *filename);
grub_dl_t EXPORT_FUNC(vboot_dl_load) (const char *name);
int EXPORT_FUNC(vboot_dl_ref) (grub_dl_t mod);

#endif /* ! GRUB_VDL_H */
