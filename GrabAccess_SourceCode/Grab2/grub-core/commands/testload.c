/* testload.c - load the same file in multiple ways */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2003,2005,2006,2007,2009,2010  Free Software Foundation, Inc.
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

#include <grub/dl.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/env.h>
#include <grub/misc.h>
#include <grub/file.h>
#include <grub/disk.h>
#include <grub/term.h>
#include <grub/loader.h>
#include <grub/command.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

/* Helper for grub_cmd_testload.  */
static void
read_progress (grub_disk_addr_t sector __attribute__ ((unused)),
	       unsigned offset __attribute__ ((unused)),
	       unsigned len,
	       void *data __attribute__ ((unused)))
{
  for (; len >= GRUB_DISK_SECTOR_SIZE; len -= GRUB_DISK_SECTOR_SIZE)
    grub_xputs (".");
  if (len)
    grub_xputs (".");
  grub_refresh ();
}

static grub_err_t
grub_cmd_testload (struct grub_command *cmd __attribute__ ((unused)),
		   int argc, char *argv[])
{
  grub_file_t file;
  char *buf;
  grub_size_t size;
  grub_off_t pos;

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  file = grub_file_open (argv[0], GRUB_FILE_TYPE_TESTLOAD);
  if (! file)
    return grub_errno;

  size = grub_file_size (file) & ~(GRUB_DISK_SECTOR_SIZE - 1);
  if (size == 0)
    {
      grub_file_close (file);
      return GRUB_ERR_NONE;
    }

  buf = grub_malloc (size);
  if (! buf)
    goto fail;

  grub_printf ("Reading %s sequentially", argv[0]);
  file->read_hook = read_progress;
  if (grub_file_read (file, buf, size) != (grub_ssize_t) size)
    goto fail;
  grub_printf (" Done.\n");

  /* Read sequentially again.  */
  grub_printf ("Reading %s sequentially again", argv[0]);
  grub_file_seek (file, 0);

  for (pos = 0; pos < size;)
    {
      char sector[GRUB_DISK_SECTOR_SIZE];
      grub_size_t curlen = GRUB_DISK_SECTOR_SIZE;

      if (curlen > size - pos)
	curlen = size - pos;

      if (grub_file_read (file, sector, curlen)
	  != (grub_ssize_t) curlen)
	goto fail;

      if (grub_memcmp (sector, buf + pos, curlen) != 0)
	{
	  grub_printf ("\nDiffers in %lld\n", (unsigned long long) pos);
	  goto fail;
	}
      pos += curlen;
    }
  grub_printf (" Done.\n");

  /* Read backwards and compare.  */
  grub_printf ("Reading %s backwards", argv[0]);
  pos = size;
  while (pos > 0)
    {
      char sector[GRUB_DISK_SECTOR_SIZE];

      if (pos >= GRUB_DISK_SECTOR_SIZE)
	pos -= GRUB_DISK_SECTOR_SIZE;
      else
	pos = 0;

      grub_file_seek (file, pos);

      if (grub_file_read (file, sector, GRUB_DISK_SECTOR_SIZE)
	  != GRUB_DISK_SECTOR_SIZE)
	goto fail;

      if (grub_memcmp (sector, buf + pos, GRUB_DISK_SECTOR_SIZE) != 0)
	{
	  int i;

	  grub_printf ("\nDiffers in %lld\n", (unsigned long long) pos);

	  for (i = 0; i < GRUB_DISK_SECTOR_SIZE; i++)
	    {
	      grub_printf ("%02x ", buf[pos + i]);
	      if ((i & 15) == 15)
		grub_printf ("\n");
	    }

	  if (i)
	    grub_refresh ();

	  goto fail;
	}
    }
  grub_printf (" Done.\n");

  return GRUB_ERR_NONE;

 fail:

  grub_file_close (file);
  grub_free (buf);

  if (!grub_errno)
    grub_error (GRUB_ERR_IO, "bad read");
  return grub_errno;
}

static grub_command_t cmd;

GRUB_MOD_INIT(testload)
{
  cmd =
    grub_register_command ("testload", grub_cmd_testload,
			   N_("FILE"),
			   N_("Load the same file in multiple ways."));
}

GRUB_MOD_FINI(testload)
{
  grub_unregister_command (cmd);
}
