/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010,2012,2013 Free Software Foundation, Inc.
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

#include <grub/util/misc.h>
#include <grub/util/install.h>
#include <grub/i18n.h>
#include <grub/term.h>
#include <grub/macho.h>

#define _GNU_SOURCE	1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static void
write_fat (FILE *in32, FILE *in64, FILE *out, const char *out_filename,
	   const char *name32, const char *name64)
{
  struct grub_macho_fat_header head;
  struct grub_macho_fat_arch arch32, arch64;
  grub_uint32_t size32, size64;
  long size;
  char *buf;

  fseek (in32, 0, SEEK_END);
  size = ftell (in32);
  if (size < 0)
    grub_util_error ("cannot get end of input file '%s': %s",
		     name32, strerror (errno));
  size32 = (grub_uint32_t) size;
  fseek (in32, 0, SEEK_SET);

  fseek (in64, 0, SEEK_END);
  size = ftell (in64);
  if (size < 0)
    grub_util_error ("cannot get end of input file '%s': %s",
		     name64, strerror (errno));
  size64 = (grub_uint64_t) size;
  fseek (in64, 0, SEEK_SET);

  head.magic = grub_cpu_to_le32_compile_time (GRUB_MACHO_FAT_EFI_MAGIC);
  head.nfat_arch = grub_cpu_to_le32_compile_time (2);
  arch32.cputype = grub_cpu_to_le32_compile_time (GRUB_MACHO_CPUTYPE_IA32);
  arch32.cpusubtype = grub_cpu_to_le32_compile_time (3);
  arch32.offset = grub_cpu_to_le32_compile_time (sizeof (head)
						 + sizeof (arch32)
						 + sizeof (arch64));
  arch32.size = grub_cpu_to_le32 (size32);
  arch32.align = 0;

  arch64.cputype = grub_cpu_to_le32_compile_time (GRUB_MACHO_CPUTYPE_AMD64);
  arch64.cpusubtype = grub_cpu_to_le32_compile_time (3);
  arch64.offset = grub_cpu_to_le32 (sizeof (head) + sizeof (arch32)
				    + sizeof (arch64) + size32);
  arch64.size = grub_cpu_to_le32 (size64);
  arch64.align = 0;
  if (fwrite (&head, 1, sizeof (head), out) != sizeof (head)
      || fwrite (&arch32, 1, sizeof (arch32), out) != sizeof (arch32)
      || fwrite (&arch64, 1, sizeof (arch64), out) != sizeof (arch64))
    {
      if (out_filename)
	grub_util_error ("cannot write to `%s': %s",
			 out_filename, strerror (errno));
      else
	grub_util_error ("cannot write to the stdout: %s", strerror (errno));
    }

  buf = xmalloc (size32);
  if (fread (buf, 1, size32, in32) != size32)
    grub_util_error (_("cannot read `%s': %s"), name32,
                     strerror (errno));
  if (fwrite (buf, 1, size32, out) != size32)
    {
      if (out_filename)
	grub_util_error ("cannot write to `%s': %s", 
			 out_filename, strerror (errno));
      else
	grub_util_error ("cannot write to the stdout: %s", strerror (errno));
    }
  free (buf);

  buf = xmalloc (size64);
  if (fread (buf, 1, size64, in64) != size64)
    grub_util_error (_("cannot read `%s': %s"), name64,
                     strerror (errno));
  if (fwrite (buf, 1, size64, out) != size64)
    {
      if (out_filename)
	grub_util_error ("cannot write to `%s': %s",
			 out_filename, strerror (errno));
      else
	grub_util_error ("cannot write to the stdout: %s", strerror (errno));
    }
  free (buf);
}

void
grub_util_glue_efi (const char *file32, const char *file64, const char *outname)
{
  FILE *in32, *in64, *out;

  in32 = grub_util_fopen (file32, "r");

  if (!in32)
    grub_util_error (_("cannot open `%s': %s"), file32,
		     strerror (errno));

  in64 = grub_util_fopen (file64, "r");
  if (!in64)
    grub_util_error (_("cannot open `%s': %s"), file64,
		     strerror (errno));

  if (outname)
    out = grub_util_fopen (outname, "wb");
  else
    out = stdout;

  if (!out)
    {
      grub_util_error (_("cannot open `%s': %s"), outname ? : "stdout",
		       strerror (errno));
    }

  write_fat (in32, in64, out, outname,
	     file32, file64);

  fclose (in32);
  fclose (in64);

  if (out != stdout)
    fclose (out);
}

