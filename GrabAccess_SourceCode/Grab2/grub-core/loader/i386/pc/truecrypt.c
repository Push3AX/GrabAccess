/* chainloader.c - boot another boot loader */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2004,2007,2009,2010  Free Software Foundation, Inc.
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

#include <grub/loader.h>
#include <grub/file.h>
#include <grub/err.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/misc.h>
#include <grub/types.h>
#include <grub/partition.h>
#include <grub/dl.h>
#include <grub/command.h>
#include <grub/machine/biosnum.h>
#include <grub/i18n.h>
#include <grub/video.h>
#include <grub/mm.h>
#include <grub/cpu/relocator.h>
#include <grub/deflate.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_dl_t my_mod;
static struct grub_relocator *rel;
static grub_uint32_t edx = 0xffffffff;
static grub_uint16_t sp;
static grub_uint32_t destaddr;

#define GRUB_TRUECRYPT_SEGMENT         0x2000

static grub_err_t
grub_truecrypt_boot (void)
{
  grub_uint16_t segment = destaddr >> 4;
  struct grub_relocator16_state state = { 
    .cs = segment,
    .ds = segment,
    .es = segment,
    .fs = segment,
    .gs = segment,
    .ss = segment,
    .ip = 0x100,
    .sp = sp,
    .edx = edx,
    .a20 = 1
  };
  grub_video_set_mode ("text", 0, 0);

  return grub_relocator16_boot (rel, state);
}

static grub_err_t
grub_truecrypt_unload (void)
{
  grub_relocator_unload (rel);
  rel = NULL;
  grub_dl_unref (my_mod);
  return GRUB_ERR_NONE;
}

/* Information on protocol supplied by Attila Lendvai.  */
#define MAGIC "\0CD001\1EL TORITO SPECIFICATION"

static grub_err_t
grub_cmd_truecrypt (grub_command_t cmd __attribute__ ((unused)),
		int argc, char *argv[])
{
  grub_file_t file = 0;
  grub_err_t err;
  void *truecrypt;
  grub_size_t truecryptsize;
  const grub_size_t truecryptmemsize = 42 * 1024;
  grub_uint8_t dh;
  grub_uint32_t catalog, rba;
  grub_uint8_t buf[128];
  char *compressed = NULL;
  char *uncompressed = NULL;

  if (argc == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  rel = NULL;

  grub_dl_ref (my_mod);

  file = grub_file_open (argv[0], GRUB_FILE_TYPE_TRUECRYPT);
  if (! file)
    goto fail;

  if (grub_file_seek (file, 17 * 2048) == (grub_size_t) -1)
    goto fail;

  if (grub_file_read (file, buf, sizeof (buf))
      != sizeof (buf))
    goto fail;

  if (grub_memcmp (buf, MAGIC, sizeof (MAGIC)) != 0)
    {
      grub_error (GRUB_ERR_BAD_OS, "invalid eltorito signature");
      goto fail;
    }

  catalog = grub_get_unaligned32 (buf + 0x47);

  if (grub_file_seek (file, catalog * 2048) == (grub_size_t)-1)
    goto fail;

  if (grub_file_read (file, buf, sizeof (buf))
      != sizeof (buf))
    goto fail;

  if (buf[0] != 1 || buf[1] != 0 || buf[0x1e] != 0x55
      || buf[0x1f] != 0xaa || buf[0x20] != 0x88
      || buf[0x26] != 1 || buf[0x27] != 0)
    {
      grub_error (GRUB_ERR_BAD_OS, "invalid eltorito catalog");
      goto fail;
    }

  rba = grub_get_unaligned32 (buf + 0x28);

  if (grub_file_seek (file, rba * 2048 + 0x1b7) == (grub_size_t) -1)
    goto fail;

  if (grub_file_read (file, &dh, 1)
      != 1)
    goto fail;

  if (grub_file_seek (file, rba * 2048 + 512 + 2048) == (grub_size_t) -1)
    goto fail;

  compressed = grub_malloc (57 * 512);
  if (!compressed)
    goto fail;

  if (grub_file_read (file, compressed, 57 * 512)
      != 57 * 512)
    goto fail;

  uncompressed = grub_malloc (truecryptmemsize);
  if (!uncompressed)
    goto fail;

  /* It's actually gzip but our gzip decompressor isn't able to handle
     trailing garbage, hence let's use raw decompressor.  */
  truecryptsize = grub_deflate_decompress (compressed + 10, 57 * 512 - 10,
					   0, uncompressed, truecryptmemsize);
  if ((grub_ssize_t) truecryptsize < 0)
    goto fail;

  if (truecryptmemsize <= truecryptsize + 0x100)
    {
      grub_error (GRUB_ERR_BAD_OS, "file is too big");
      goto fail;
    }

  rel = grub_relocator_new ();
  if (!rel)
    goto fail;

  edx = (dh << 8) | grub_get_root_biosnumber ();

  destaddr = ALIGN_DOWN (grub_min (0x90000, grub_mmap_get_lower ())
			 - truecryptmemsize, 64 * 1024);

  {
    grub_relocator_chunk_t ch;
    err = grub_relocator_alloc_chunk_addr (rel, &ch, destaddr,
					   truecryptmemsize);
    if (err)
      goto fail;
    truecrypt = get_virtual_current_address (ch);
  }

  grub_memset (truecrypt, 0, 0x100);
  grub_memcpy ((char *) truecrypt + 0x100, uncompressed, truecryptsize);

  grub_memset ((char *) truecrypt + truecryptsize + 0x100,
	       0, truecryptmemsize - truecryptsize - 0x100);
  sp = truecryptmemsize - 4;
 
  grub_loader_set (grub_truecrypt_boot, grub_truecrypt_unload, 1);

  grub_free (uncompressed);
  grub_free (compressed);

  return GRUB_ERR_NONE;

 fail:

  if (!grub_errno)
    grub_error (GRUB_ERR_BAD_OS, "bad truecrypt ISO");

  if (file)
    grub_file_close (file);

  grub_truecrypt_unload ();

  grub_free (uncompressed);
  grub_free (compressed);

  return grub_errno;
}

static grub_command_t cmd;

GRUB_MOD_INIT(truecrypt)
{
  cmd = grub_register_command ("truecrypt", grub_cmd_truecrypt,
			       0, N_("Load Truecrypt ISO."));
  my_mod = mod;
}

GRUB_MOD_FINI(truecrypt)
{
  grub_unregister_command (cmd);
}
