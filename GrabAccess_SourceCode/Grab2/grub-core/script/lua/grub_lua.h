/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009  Free Software Foundation, Inc.
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

#ifndef GRUB_LUA_HEADER
#define GRUB_LUA_HEADER 1

#include <grub/types.h>
#include <grub/mm.h>
#include <grub/env.h>
#include <grub/err.h>
#include <grub/misc.h>
#include <grub/setjmp.h>

#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>

#undef UNUSED
#define UNUSED		(void)

#define strtoul		grub_strtoul
#define strtod(s,e)	grub_strtoul(s,e,0)

#define exit(a)		grub_exit(a)
#define jmp_buf		grub_jmp_buf
#define setjmp		grub_setjmp
#define longjmp		grub_longjmp

#define fputs(s,f)	grub_printf("%s", s)

static inline const char *
getenv (const char *name)
{
  return grub_env_get (name);
}

#endif
