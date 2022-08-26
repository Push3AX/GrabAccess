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
#include <grub/machine/chainloader.h>
#include <grub/machine/kernel.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_dl_t my_mod;
static struct grub_relocator *rel;
static grub_uint32_t edx = 0xffffffff;

#define GRUB_NTLDR_SEGMENT         0x2000

static grub_err_t
grub_ntldr_boot (void)
{
  struct grub_relocator16_state state = { 
    .cs = GRUB_NTLDR_SEGMENT,
    .ip = 0,
    .ds = 0,
    .es = 0,
    .fs = 0,
    .gs = 0,
    .ss = 0,
    .sp = 0x7c00,
    .edx = edx,
    .a20 = 1
  };
  grub_video_set_mode ("text", 0, 0);

  return grub_relocator16_boot (rel, state);
}

static grub_err_t
grub_ntldr_unload (void)
{
  grub_relocator_unload (rel);
  rel = NULL;
  grub_dl_unref (my_mod);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_ntldr (grub_command_t cmd __attribute__ ((unused)),
		int argc, char *argv[])
{
  grub_file_t file = 0;
  grub_err_t err;
  void *bs, *ntldr;
  grub_size_t ntldrsize;
  grub_device_t dev;

  if (argc == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  grub_dl_ref (my_mod);

  rel = grub_relocator_new ();
  if (!rel)
    goto fail;

  file = grub_file_open (argv[0], GRUB_FILE_TYPE_NTLDR);
  if (! file)
    goto fail;

  {
    grub_relocator_chunk_t ch;
    err = grub_relocator_alloc_chunk_addr (rel, &ch, 0x7C00,
					   GRUB_DISK_SECTOR_SIZE);
    if (err)
      goto fail;
    bs = get_virtual_current_address (ch);
  }

  edx = grub_get_root_biosnumber ();
  dev = grub_device_open (0);

  if (dev && dev->disk)
    {
      err = grub_disk_read (dev->disk, 0, 0, GRUB_DISK_SECTOR_SIZE, bs);
      if (err)
	{
	  grub_device_close (dev);
	  goto fail;
	}
      grub_chainloader_patch_bpb (bs, dev, edx);
    }

  if (dev)
    grub_device_close (dev);

  ntldrsize = grub_file_size (file);
  {
    grub_relocator_chunk_t ch;
    err = grub_relocator_alloc_chunk_addr (rel, &ch, GRUB_NTLDR_SEGMENT << 4,
					   ntldrsize);
    if (err)
      goto fail;
    ntldr = get_virtual_current_address (ch);
  }

  if (grub_file_read (file, ntldr, ntldrsize)
      != (grub_ssize_t) ntldrsize)
    goto fail;
 
  grub_loader_set (grub_ntldr_boot, grub_ntldr_unload, 1);
  return GRUB_ERR_NONE;

 fail:

  if (file)
    grub_file_close (file);

  grub_ntldr_unload ();

  return grub_errno;
}

static grub_command_t cmd;

GRUB_MOD_INIT(ntldr)
{
#ifdef GRUB_MACHINE_MULTIBOOT
  if (!grub_mb_check_bios_int (0x13))
    return;
#endif
  cmd = grub_register_command ("ntldr", grub_cmd_ntldr,
			       0, N_("Load NTLDR or BootMGR."));
  my_mod = mod;
}

GRUB_MOD_FINI(ntldr)
{
#ifdef GRUB_MACHINE_MULTIBOOT
  if (!grub_mb_check_bios_int (0x13))
    return;
#endif
  grub_unregister_command (cmd);
}
