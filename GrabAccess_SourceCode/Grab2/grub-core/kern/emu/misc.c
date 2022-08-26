/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2003,2005,2006,2007,2008,2009,2010  Free Software Foundation, Inc.
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

#ifndef GRUB_BUILD
#include <config-util.h>
#endif
#include <config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>

#include <grub/mm.h>
#include <grub/err.h>
#include <grub/env.h>
#include <grub/types.h>
#include <grub/misc.h>
#include <grub/i18n.h>
#include <grub/time.h>
#include <grub/emu/misc.h>

int verbosity;
int kexecute;

void
grub_util_warn (const char *fmt, ...)
{
  va_list ap;

  fprintf (stderr, _("%s: warning:"), program_name);
  fprintf (stderr, " ");
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fprintf (stderr, ".\n");
  fflush (stderr);
}

void
grub_util_info (const char *fmt, ...)
{
  if (verbosity > 0)
    {
      va_list ap;

      fprintf (stderr, _("%s: info:"), program_name);
      fprintf (stderr, " ");
      va_start (ap, fmt);
      vfprintf (stderr, fmt, ap);
      va_end (ap);
      fprintf (stderr, ".\n");
      fflush (stderr);
    }
}

void
grub_util_error (const char *fmt, ...)
{
  va_list ap;

  fprintf (stderr, _("%s: error:"), program_name);
  fprintf (stderr, " ");
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fprintf (stderr, ".\n");
  grub_exit (1);
}

void *
xcalloc (grub_size_t nmemb, grub_size_t size)
{
  void *p;

  p = calloc (nmemb, size);
  if (!p)
    grub_util_error ("%s", _("out of memory"));

  return p;
}

void *
xmalloc (grub_size_t size)
{
  void *p;

  p = malloc (size);
  if (! p)
    grub_util_error ("%s", _("out of memory"));

  return p;
}

void *
xrealloc (void *ptr, grub_size_t size)
{
  ptr = realloc (ptr, size);
  if (! ptr)
    grub_util_error ("%s", _("out of memory"));

  return ptr;
}

char *
xstrdup (const char *str)
{
  size_t len;
  char *newstr;

  len = strlen (str);
  newstr = (char *) xmalloc (len + 1);
  memcpy (newstr, str, len + 1);

  return newstr;
}

#if !defined (GRUB_MKFONT) && !defined (GRUB_BUILD)
char *
xasprintf (const char *fmt, ...)
{ 
  va_list ap;
  char *result;
  
  va_start (ap, fmt);
  result = grub_xvasprintf (fmt, ap);
  va_end (ap);
  if (!result)
    grub_util_error ("%s", _("out of memory"));
  
  return result;
}
#endif

#if !defined (GRUB_MACHINE_EMU) || defined (GRUB_UTIL)
void
__attribute__ ((noreturn))
grub_exit (int rc)
{
#if defined (GRUB_KERNEL)
  grub_reboot();
#endif
  exit (rc < 0 ? 1 : rc);
}
#endif

grub_uint64_t
grub_get_time_ms (void)
{
  struct timeval tv;

  gettimeofday (&tv, 0);

  return (tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

size_t
grub_util_get_image_size (const char *path)
{
  FILE *f;
  size_t ret;
  off_t sz;

  f = grub_util_fopen (path, "rb");

  if (!f)
    grub_util_error (_("cannot open `%s': %s"), path, strerror (errno));

  fseeko (f, 0, SEEK_END);
  
  sz = ftello (f);
  if (sz < 0)
    grub_util_error (_("cannot open `%s': %s"), path, strerror (errno));
  if (sz != (size_t) sz)
    grub_util_error (_("file `%s' is too big"), path);
  ret = (size_t) sz;

  fclose (f);

  return ret;
}

void
grub_util_load_image (const char *path, char *buf)
{
  FILE *fp;
  size_t size;

  grub_util_info ("reading %s", path);

  size = grub_util_get_image_size (path);

  fp = grub_util_fopen (path, "rb");
  if (! fp)
    grub_util_error (_("cannot open `%s': %s"), path,
		     strerror (errno));

  if (fread (buf, 1, size, fp) != size)
    grub_util_error (_("cannot read `%s': %s"), path,
		     strerror (errno));

  fclose (fp);
}

void
grub_util_set_kexecute(void)
{
  kexecute++;
}

int
grub_util_get_kexecute(void)
{
  return kexecute;
}
