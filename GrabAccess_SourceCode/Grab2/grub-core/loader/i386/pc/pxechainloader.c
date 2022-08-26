/* chainloader.c - boot another boot loader */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2004,2007,2009,2010,2012  Free Software Foundation, Inc.
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
#include <grub/machine/pxe.h>
#include <grub/net.h>

static grub_dl_t my_mod;
static struct grub_relocator *rel;
static grub_uint32_t edx = 0xffffffff;
static char boot_file[128];
static char server_name[64];

GRUB_MOD_LICENSE ("GPLv3+");

static grub_err_t
grub_pxechain_boot (void)
{
  struct grub_relocator16_state state = { 
    .cs = 0,
    .ip = 0x7c00,
    .ds = 0,
    .es = 0,
    .fs = 0,
    .gs = 0,
    .ss = 0,
    .sp = 0x7c00,
    .edx = edx
  };
  struct grub_net_bootp_packet *bp;

  bp = grub_pxe_get_cached (GRUB_PXENV_PACKET_TYPE_DHCP_ACK);

  grub_video_set_mode ("text", 0, 0);

  if (bp && boot_file[0])
    grub_memcpy (bp->boot_file, boot_file, sizeof (bp->boot_file));
  if (bp && server_name[0])
    grub_memcpy (bp->server_name, server_name, sizeof (bp->server_name));

  return grub_relocator16_boot (rel, state);
}

static grub_err_t
grub_pxechain_unload (void)
{
  grub_relocator_unload (rel);
  rel = NULL;
  grub_dl_unref (my_mod);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_pxechain (grub_command_t cmd __attribute__ ((unused)),
		int argc, char *argv[])
{
  grub_file_t file = 0;
  grub_err_t err;
  void *image;
  grub_size_t imagesize;
  char *fname;

  if (argc == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  grub_dl_ref (my_mod);

  rel = grub_relocator_new ();
  if (!rel)
    goto fail;

  file = grub_file_open (argv[0], GRUB_FILE_TYPE_PXECHAINLOADER);
  if (! file)
    goto fail;

  if (file->device->net && file->device->net->name)
    fname = file->device->net->name;
  else
    {
      fname = argv[0];
      if (fname[0] == '(')
	{
	  fname = grub_strchr (fname, ')');
	  if (fname)
	    fname++;
	  else
	    fname = argv[0];
	}
    }

  grub_memset (boot_file, 0, sizeof (boot_file));
  grub_strncpy (boot_file, fname, sizeof (boot_file));

  grub_memset (server_name, 0, sizeof (server_name));
  if (file->device->net && file->device->net->server)
    grub_strncpy (server_name, file->device->net->server, sizeof (server_name));

  edx = grub_get_root_biosnumber ();

  imagesize = grub_file_size (file);
  {
    grub_relocator_chunk_t ch;
    err = grub_relocator_alloc_chunk_addr (rel, &ch, 0x7c00, imagesize);
    if (err)
      goto fail;
    image = get_virtual_current_address (ch);
  }

  if (grub_file_read (file, image, imagesize) != (grub_ssize_t) imagesize)
    goto fail;
 
  grub_loader_set (grub_pxechain_boot, grub_pxechain_unload,
		   GRUB_LOADER_FLAG_NORETURN | GRUB_LOADER_FLAG_PXE_NOT_UNLOAD);
  return GRUB_ERR_NONE;

 fail:

  if (file)
    grub_file_close (file);

  grub_pxechain_unload ();

  return grub_errno;
}

static grub_command_t cmd;

GRUB_MOD_INIT(pxechainloader)
{
  cmd = grub_register_command ("pxechainloader", grub_cmd_pxechain,
			       0, N_("Load a PXE image."));
  my_mod = mod;
}

GRUB_MOD_FINI(pxechainloader)
{
  grub_unregister_command (cmd);
}
