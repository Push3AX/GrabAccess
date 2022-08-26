/* macho.c - load Mach-O files. */
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

/* This Mach-O loader is incomplete and can load only non-relocatable segments.
   This is however enough to boot xnu (otool -l and Mach-O specs for more info).
*/

#include <grub/err.h>
#include <grub/macho.h>
#include <grub/machoload.h>
#include <grub/file.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/i18n.h>
#include <grub/dl.h>

GRUB_MOD_LICENSE ("GPLv3+");

grub_err_t
grub_macho_close (grub_macho_t macho)
{
  grub_file_t file = macho->file;

  if (!macho->uncompressed32)
    grub_free (macho->cmds32);
  grub_free (macho->uncompressed32);
  if (!macho->uncompressed64)
    grub_free (macho->cmds64);
  grub_free (macho->uncompressed64);

  grub_free (macho);

  if (file)
    grub_file_close (file);

  return grub_errno;
}

grub_macho_t
grub_macho_file (grub_file_t file, const char *filename, int is_64bit)
{
  grub_macho_t macho;
  union grub_macho_filestart filestart;

  macho = grub_malloc (sizeof (*macho));
  if (! macho)
    return 0;

  macho->file = file;
  macho->offset32 = -1;
  macho->offset64 = -1;
  macho->end32 = -1;
  macho->end64 = -1;
  macho->cmds32 = 0;
  macho->cmds64 = 0;
  macho->uncompressed32 = 0;
  macho->uncompressed64 = 0;
  macho->compressed32 = 0;
  macho->compressed64 = 0;

  if (grub_file_seek (macho->file, 0) == (grub_off_t) -1)
    goto fail;

  if (grub_file_read (macho->file, &filestart, sizeof (filestart))
      != sizeof (filestart))
    {
      if (!grub_errno)
	grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
		    filename);
      goto fail;
    }

  /* Is it a fat file? */
  if (filestart.fat.magic == grub_cpu_to_be32_compile_time (GRUB_MACHO_FAT_MAGIC))
    {
      struct grub_macho_fat_arch *archs;
      int i, narchs;

      /* Load architecture description. */
      narchs = grub_be_to_cpu32 (filestart.fat.nfat_arch);
      if (grub_file_seek (macho->file, sizeof (struct grub_macho_fat_header))
	  == (grub_off_t) -1)
	goto fail;
      archs = grub_calloc (narchs, sizeof (struct grub_macho_fat_arch));
      if (!archs)
	goto fail;
      if (grub_file_read (macho->file, archs,
			  sizeof (struct grub_macho_fat_arch) * narchs)
	  != (grub_ssize_t) sizeof(struct grub_macho_fat_arch) * narchs)
	{
	  grub_free (archs);
	  if (!grub_errno)
	    grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
			filename);
	  goto fail;
	}

      for (i = 0; i < narchs; i++)
	{
	  if ((archs[i].cputype
	       == grub_cpu_to_be32_compile_time (GRUB_MACHO_CPUTYPE_IA32))
	      && !is_64bit)
	    {
	      macho->offset32 = grub_be_to_cpu32 (archs[i].offset);
	      macho->end32 = grub_be_to_cpu32 (archs[i].offset)
		+ grub_be_to_cpu32 (archs[i].size);
	    }
	  if ((archs[i].cputype
	       == grub_cpu_to_be32_compile_time (GRUB_MACHO_CPUTYPE_AMD64))
	      && is_64bit)
	    {
	      macho->offset64 = grub_be_to_cpu32 (archs[i].offset);
	      macho->end64 = grub_be_to_cpu32 (archs[i].offset)
		+ grub_be_to_cpu32 (archs[i].size);
	    }
	}
      grub_free (archs);
    }

  /* Is it a thin 32-bit file? */
  if (filestart.thin32.magic == GRUB_MACHO_MAGIC32 && !is_64bit)
    {
      macho->offset32 = 0;
      macho->end32 = grub_file_size (file);
    }

  /* Is it a thin 64-bit file? */
  if (filestart.thin64.magic == GRUB_MACHO_MAGIC64 && is_64bit)
    {
      macho->offset64 = 0;
      macho->end64 = grub_file_size (file);
    }

  if (grub_memcmp (filestart.lzss.magic, GRUB_MACHO_LZSS_MAGIC,
		   sizeof (filestart.lzss.magic)) == 0 && !is_64bit)
    {
      macho->offset32 = 0;
      macho->end32 = grub_file_size (file);
    }

  /* Is it a thin 64-bit file? */
  if (grub_memcmp (filestart.lzss.magic, GRUB_MACHO_LZSS_MAGIC,
		   sizeof (filestart.lzss.magic)) == 0 && is_64bit)
    {
      macho->offset64 = 0;
      macho->end64 = grub_file_size (file);
    }

  grub_macho_parse32 (macho, filename);
  grub_macho_parse64 (macho, filename);

  if (macho->offset32 == -1 && !is_64bit)
    {
      grub_error (GRUB_ERR_BAD_OS,
		  "Mach-O doesn't contain suitable 32-bit architecture");
      goto fail;
    }

  if (macho->offset64 == -1 && is_64bit)
    {
      grub_error (GRUB_ERR_BAD_OS,
		  "Mach-O doesn't contain suitable 64-bit architecture");
      goto fail;
    }

  return macho;

fail:
  macho->file = 0;
  grub_macho_close (macho);
  return 0;
}

grub_macho_t
grub_macho_open (const char *name, enum grub_file_type type, int is_64bit)
{
  grub_file_t file;
  grub_macho_t macho;

  file = grub_file_open (name, type);
  if (! file)
    return 0;

  macho = grub_macho_file (file, name, is_64bit);
  if (! macho)
    grub_file_close (file);

  return macho;
}
