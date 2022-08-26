/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1992-1999,2001,2003,2004,2005,2009,2010,2011,2012,2013 Free Software Foundation, Inc.
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

#include <config.h>

#include <grub/types.h>
#include <grub/crypto.h>
#include <grub/auth.h>
#include <grub/emu/misc.h>
#include <grub/util/misc.h>
#include <grub/i18n.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
grub_get_random (void *out, grub_size_t len)
{
  FILE *f;
  size_t rd;

  f = grub_util_fopen ("/dev/urandom", "rb");
  if (!f)
    return 1;
  rd = fread (out, 1, len, f);
  fclose (f);

  if (rd != len)
    return 1;
  return 0;
}
