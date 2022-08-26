/* xnu_uuid.c - transform 64-bit serial number
   to 128-bit uuid suitable for xnu. */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1995,1996,1998,1999,2001,2002,
 *                2003, 2009  Free Software Foundation, Inc.
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

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/fs.h>
#include <grub/file.h>
#include <grub/misc.h>
#include <grub/env.h>
#include <grub/command.h>
#include <grub/i18n.h>
#include <grub/crypto.h>

GRUB_MOD_LICENSE ("GPLv3+");

/* This prefix is used by xnu and boot-132 to hash
   together with volume serial. */
static grub_uint8_t hash_prefix[16]
  = {0xB3, 0xE2, 0x0F, 0x39, 0xF2, 0x92, 0x11, 0xD6,
     0x97, 0xA4, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC};

static grub_err_t
grub_cmd_xnu_uuid (grub_command_t cmd __attribute__ ((unused)),
		   int argc, char **args)
{
  grub_uint64_t serial;
  grub_uint8_t *xnu_uuid;
  char uuid_string[sizeof ("xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx")];
  char *ptr;
  void *ctx;
  int low = 0;

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "UUID required");

  if (argc > 1 && grub_strcmp (args[0], "-l") == 0)
    {
      low = 1;
      argc--;
      args++;
    }

  serial = grub_cpu_to_be64 (grub_strtoull (args[0], 0, 16));

  ctx = grub_zalloc (GRUB_MD_MD5->contextsize);
  if (!ctx)
    return grub_errno;
  GRUB_MD_MD5->init (ctx);
  GRUB_MD_MD5->write (ctx, hash_prefix, sizeof (hash_prefix));
  GRUB_MD_MD5->write (ctx, &serial, sizeof (serial));
  GRUB_MD_MD5->final (ctx);
  xnu_uuid = GRUB_MD_MD5->read (ctx);

  grub_snprintf (uuid_string, sizeof (uuid_string),
		"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		(unsigned int) xnu_uuid[0], (unsigned int) xnu_uuid[1],
		(unsigned int) xnu_uuid[2], (unsigned int) xnu_uuid[3],
		(unsigned int) xnu_uuid[4], (unsigned int) xnu_uuid[5],
		(unsigned int) ((xnu_uuid[6] & 0xf) | 0x30),
		(unsigned int) xnu_uuid[7],
		(unsigned int) ((xnu_uuid[8] & 0x3f) | 0x80),
		(unsigned int) xnu_uuid[9],
		(unsigned int) xnu_uuid[10], (unsigned int) xnu_uuid[11],
		(unsigned int) xnu_uuid[12], (unsigned int) xnu_uuid[13],
		(unsigned int) xnu_uuid[14], (unsigned int) xnu_uuid[15]);
  if (!low)
    for (ptr = uuid_string; *ptr; ptr++)
      *ptr = grub_toupper (*ptr);
  if (argc == 1)
    grub_printf ("%s\n", uuid_string);
  if (argc > 1)
    grub_env_set (args[1], uuid_string);

  grub_free (ctx);

  return GRUB_ERR_NONE;
}

static grub_command_t cmd;


GRUB_MOD_INIT (xnu_uuid)
{
  cmd = grub_register_command ("xnu_uuid", grub_cmd_xnu_uuid,
			       /* TRANSLATORS: GRUBUUID stands for "filesystem
				  UUID as used in GRUB".  */
			       N_("[-l] GRUBUUID [VARNAME]"),
			       N_("Transform 64-bit UUID to format "
				  "suitable for XNU. If -l is given keep "
				  "it lowercase as done by blkid."));
}

GRUB_MOD_FINI (xnu_uuid)
{
  grub_unregister_command (cmd);
}
