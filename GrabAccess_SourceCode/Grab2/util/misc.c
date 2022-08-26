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

#include <config.h>

#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>

#include <grub/kernel.h>
#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/cache.h>
#include <grub/emu/misc.h>
#include <grub/util/misc.h>
#include <grub/mm.h>
#include <grub/term.h>
#include <grub/time.h>
#include <grub/i18n.h>
#include <grub/script_sh.h>
#include <grub/emu/hostfile.h>

#define ENABLE_RELOCATABLE 0
#ifdef GRUB_BUILD
const char *program_name = GRUB_BUILD_PROGRAM_NAME;
#else
#include "progname.h"
#endif

#ifdef GRUB_UTIL
int
grub_err_printf (const char *fmt, ...)
{
  va_list ap;
  int ret;

  va_start (ap, fmt);
  ret = vfprintf (stderr, fmt, ap);
  va_end (ap);

  return ret;
}
#endif

char *
grub_util_get_path (const char *dir, const char *file)
{
  char *path;

  path = (char *) xmalloc (strlen (dir) + 1 + strlen (file) + 1);
  sprintf (path, "%s/%s", dir, file);
  return path;
}

char *
grub_util_read_image (const char *path)
{
  char *img;
  FILE *fp;
  size_t size;

  grub_util_info ("reading %s", path);

  size = grub_util_get_image_size (path);
  img = (char *) xmalloc (size);

  fp = grub_util_fopen (path, "rb");
  if (! fp)
    grub_util_error (_("cannot open `%s': %s"), path,
		     strerror (errno));

  if (fread (img, 1, size, fp) != size)
    grub_util_error (_("cannot read `%s': %s"), path,
		     strerror (errno));

  fclose (fp);

  return img;
}

void
grub_util_write_image_at (const void *img, size_t size, off_t offset, FILE *out,
			  const char *name)
{
  grub_util_info ("writing 0x%" GRUB_HOST_PRIxLONG_LONG " bytes at offset 0x%"
		  GRUB_HOST_PRIxLONG_LONG,
		  (unsigned long long) size, (unsigned long long) offset);
  if (fseeko (out, offset, SEEK_SET) == -1)
    grub_util_error (_("cannot seek `%s': %s"),
		     name, strerror (errno));
  if (fwrite (img, 1, size, out) != size)
    grub_util_error (_("cannot write to `%s': %s"),
		     name, strerror (errno));
}

void
grub_util_write_image (const char *img, size_t size, FILE *out,
		       const char *name)
{
  grub_util_info ("writing 0x%" GRUB_HOST_PRIxLONG_LONG " bytes", (unsigned long long) size);
  if (fwrite (img, 1, size, out) != size)
    {
      if (!name)
	grub_util_error (_("cannot write to the stdout: %s"),
			 strerror (errno));
      else
	grub_util_error (_("cannot write to `%s': %s"),
			 name, strerror (errno));
    }
}

grub_err_t
grub_script_execute_cmdline (struct grub_script_cmd *cmd __attribute__ ((unused)))
{
  return 0;
}

grub_err_t
grub_script_execute_cmdlist (struct grub_script_cmd *cmd __attribute__ ((unused)))
{
  return 0;
}

grub_err_t
grub_script_execute_cmdif (struct grub_script_cmd *cmd __attribute__ ((unused)))
{
  return 0;
}

grub_err_t
grub_script_execute_cmdfor (struct grub_script_cmd *cmd __attribute__ ((unused)))
{
  return 0;
}

grub_err_t
grub_script_execute_cmdwhile (struct grub_script_cmd *cmd __attribute__ ((unused)))
{
  return 0;
}

grub_err_t
grub_script_execute (struct grub_script *script)
{
  if (script == 0 || script->cmd == 0)
    return 0;

  return script->cmd->exec (script->cmd);
}

int
grub_getkey (void)
{
  return -1;
}

void
grub_refresh (void)
{
  fflush (stdout);
}

static void
grub_xputs_real (const char *str)
{
  fputs (str, stdout);
}

void (*grub_xputs) (const char *str) = grub_xputs_real;

int
grub_dl_ref (grub_dl_t mod)
{
  (void) mod;
  return 0;
}

int
grub_dl_unref (grub_dl_t mod)
{
  (void) mod;
  return 0;
}

/* Some functions that we don't use.  */
void
grub_mm_init_region (void *addr __attribute__ ((unused)),
		     grub_size_t size __attribute__ ((unused)))
{
}

void
grub_register_exported_symbols (void)
{
}

/* Used in comparison of arrays of strings with qsort */
int
grub_qsort_strcmp (const void *p1, const void *p2)
{
  return strcmp(*(char *const *)p1, *(char *const *)p2);
}

