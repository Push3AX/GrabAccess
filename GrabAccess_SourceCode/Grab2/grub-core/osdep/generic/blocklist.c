/* grub-setup.c - make GRUB usable */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2004,2005,2006,2007,2008,2009,2010,2011  Free Software Foundation, Inc.
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

#include <grub/disk.h>
#include <grub/file.h>
#include <grub/partition.h>
#include <grub/util/misc.h>
#include <grub/util/install.h>
#include <grub/emu/hostdisk.h>

#include <string.h>

#define MAX_TRIES	5

void
grub_install_get_blocklist (grub_device_t root_dev,
			    const char *core_path, const char *core_img,
			    size_t core_size,
			    void (*callback) (grub_disk_addr_t sector,
					      unsigned offset,
					      unsigned length,
					      void *data),
			    void *hook_data)
{
  int i;
  char *tmp_img;
  char *core_path_dev;

  core_path_dev = grub_make_system_path_relative_to_its_root (core_path);

  /* Make sure that GRUB reads the identical image as the OS.  */
  tmp_img = xmalloc (core_size);

  for (i = 0; i < MAX_TRIES; i++)
    {
      grub_file_t file;

      grub_util_info ((i == 0) ? _("attempting to read the core image `%s' from GRUB")
		      : _("attempting to read the core image `%s' from GRUB again"),
		      core_path_dev);

      grub_disk_cache_invalidate_all ();

      file = grub_file_open (core_path_dev, GRUB_FILE_TYPE_NONE | GRUB_FILE_TYPE_NO_DECOMPRESS);
      if (file)
	{
	  if (grub_file_size (file) != core_size)
	    grub_util_info ("succeeded in opening the core image but the size is different (%d != %d)",
			    (int) grub_file_size (file), (int) core_size);
	  else if (grub_file_read (file, tmp_img, core_size)
		   != (grub_ssize_t) core_size)
	    grub_util_info ("succeeded in opening the core image but cannot read %d bytes",
			    (int) core_size);
	  else if (memcmp (core_img, tmp_img, core_size) != 0)
	    {
#if 0
	      FILE *dump;
	      FILE *dump2;

	      dump = fopen ("dump.img", "wb");
	      if (dump)
		{
		  fwrite (tmp_img, 1, core_size, dump);
		  fclose (dump);
		}

	      dump2 = fopen ("dump2.img", "wb");
	      if (dump2)
		{
		  fwrite (core_img, 1, core_size, dump2);
		  fclose (dump2);
		}

#endif
	      grub_util_info ("succeeded in opening the core image but the data is different");
	    }
	  else
	    {
	      grub_file_close (file);
	      break;
	    }

	  grub_file_close (file);
	}
      else
	grub_util_info ("couldn't open the core image");

      if (grub_errno)
	grub_util_info ("error message = %s", grub_errmsg);

      grub_errno = GRUB_ERR_NONE;
      grub_util_biosdisk_flush (root_dev->disk);
      sleep (1);
    }

  if (i == MAX_TRIES)
    grub_util_error (_("cannot read `%s' correctly"), core_path_dev);

  grub_file_t file;
  /* Now read the core image to determine where the sectors are.  */
  file = grub_file_open (core_path_dev, GRUB_FILE_TYPE_NONE | GRUB_FILE_TYPE_NO_DECOMPRESS);
  if (! file)
    grub_util_error ("%s", grub_errmsg);

  file->read_hook = callback;
  file->read_hook_data = hook_data;
  if (grub_file_read (file, tmp_img, core_size) != (grub_ssize_t) core_size)
    grub_util_error ("%s", _("failed to read the sectors of the core image"));

  grub_file_close (file);
  free (tmp_img);

  free (core_path_dev);
}
