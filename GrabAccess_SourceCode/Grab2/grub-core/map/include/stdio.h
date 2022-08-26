/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009, 2010  Free Software Foundation, Inc.
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

#ifndef MAP_POSIX_STDIO_H
#define MAP_POSIX_STDIO_H	1

#include <grub/misc.h>
#include <grub/term.h>
#include <stdint.h>

static inline int
vprintf (const char *fmt, va_list args)
{
  return grub_vprintf (fmt, args);
};

static inline int
printf (const char *fmt, ...)
{
  va_list ap;
  int ret;

  va_start (ap, fmt);
  ret = grub_vprintf (fmt, ap);
  va_end (ap);

  return ret;
}

static inline int
snprintf (char *str, grub_size_t n, const char *fmt, ...)
{
  va_list ap;
  int ret;

  va_start (ap, fmt);
  ret = grub_vsnprintf (str, n, fmt, ap);
  va_end (ap);

  return ret;
}

static inline int
sprintf (char *str, const char *fmt, ...)
{
  va_list ap;
  int ret;

  va_start (ap, fmt);
  ret = grub_vsnprintf (str, GRUB_UINT_MAX, fmt, ap);
  va_end (ap);

  return ret;
}

#endif
