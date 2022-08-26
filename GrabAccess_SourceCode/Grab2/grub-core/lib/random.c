/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2016 Free Software Foundation, Inc.
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

#include <grub/random.h>
#include <grub/dl.h>
#include <grub/lib/hexdump.h>
#include <grub/command.h>
#include <grub/mm.h>

GRUB_MOD_LICENSE ("GPLv3+");

grub_err_t
grub_crypto_get_random (void *buffer, grub_size_t sz)
{
  /* This is an arbitrer between different methods.
     TODO: Add more methods in the future.  */
  /* TODO: Add some PRNG smartness to reduce damage from bad entropy. */
  if (grub_crypto_arch_get_random (buffer, sz))
    return GRUB_ERR_NONE;
  return grub_error (GRUB_ERR_IO, "no random sources found");
}

static int
get_num_digits (int val)
{
  int ret = 0;
  while (val != 0)
    {
      ret++;
      val /= 10;
    }
  if (ret == 0)
    return 1;
  return ret;
}

#pragma GCC diagnostic ignored "-Wformat-nonliteral"

static grub_err_t
grub_cmd_hexdump_random (grub_command_t cmd __attribute__ ((unused)), int argc, char **args)
{
  grub_size_t length = 64;
  grub_err_t err;
  void *buffer;
  grub_uint8_t *ptr;
  int stats[256];
  int i, digits = 2;
  char template[10];

  if (argc >= 1)
    length = grub_strtoull (args[0], 0, 0);

  if (length == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "length pust be positive");

  buffer = grub_malloc (length);
  if (!buffer)
    return grub_errno;

  err = grub_crypto_get_random (buffer, length);
  if (err)
    {
      grub_free (buffer);
      return err;
    }

  hexdump (0, buffer, length);
  grub_memset(stats, 0, sizeof(stats));
  for (ptr = buffer; ptr < (grub_uint8_t *) buffer + length; ptr++)
    stats[*ptr]++;
  grub_printf ("Statistics:\n");
  for (i = 0; i < 256; i++)
    {
      int z = get_num_digits (stats[i]);
      if (z > digits)
	digits = z;
    }

  grub_snprintf (template, sizeof (template), "%%0%dd ", digits);

  for (i = 0; i < 256; i++)
    {
      grub_printf ("%s", template);//, stats[i]);
      if ((i & 0xf) == 0xf)
	grub_printf ("\n");
    }

  grub_free (buffer);

  return 0;
}

static grub_command_t cmd;

GRUB_MOD_INIT (random)
{
  cmd = grub_register_command ("hexdump_random", grub_cmd_hexdump_random,
			      N_("[LENGTH]"),
			      N_("Hexdump random data."));
}

GRUB_MOD_FINI (random)
{
  grub_unregister_command (cmd);
}
