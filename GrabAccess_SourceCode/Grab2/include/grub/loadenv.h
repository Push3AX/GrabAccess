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

#ifndef GRUB_LOADENV_HEADER
#define GRUB_LOADENV_HEADER	1

#include <grub/symbol.h>
#include <grub/err.h>
#include <grub/types.h>
#include <grub/env.h>
#include <grub/file.h>
#include <grub/menu.h>
#include <grub/lib/envblk.h>

struct grub_env_whitelist
{
  grub_size_t len;
  char **list;
};
typedef struct grub_env_whitelist grub_env_whitelist_t;

grub_envblk_t
read_envblk_file (grub_file_t file);

int
test_whitelist_membership (const char* name,
                           const grub_env_whitelist_t* whitelist);

int
set_var (const char *name, const char *value, void *whitelist);


#endif /* ! GRUB_LOADENV_HEADER */
