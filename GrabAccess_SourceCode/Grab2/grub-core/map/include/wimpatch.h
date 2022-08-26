#ifndef _WIMPATCH_H
#define _WIMPATCH_H

/*
 * Copyright (C) 2014 Michael Brown <mbrown@fensystems.co.uk>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * @file
 *
 * WIM dynamic patching
 *
 */

#include <vfat.h>
#include <stdint.h>
#include <wimboot.h>

extern void
patch_wim (struct vfat_file *file, void *data, size_t offset, size_t len);

extern void set_wim_patch (struct wimboot_cmdline *cmd);

#endif /* _WIMPATCH_H */
