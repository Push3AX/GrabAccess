/*
 *  GRUB  --  GRand Unified Bootloader
 *
 *  Copyright (C) 2012  Google Inc.
 *  Copyright (C) 2016  Free Software Foundation, Inc.
 *
 *  This is based on depthcharge code.
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

#include <grub/ps2.h>
#include <grub/fdtbus.h>
#include <grub/err.h>
#include <grub/machine/kernel.h>
#include <grub/misc.h>
#include <grub/term.h>
#include <grub/time.h>
#include <grub/fdtbus.h>
#include <grub/arm/cros_ec.h>

static struct grub_ps2_state ps2_state;

struct grub_cros_ec_keyscan old_scan;

static const struct grub_fdtbus_dev *cros_ec;

static grub_uint8_t map_code[GRUB_CROS_EC_KEYSCAN_COLS][GRUB_CROS_EC_KEYSCAN_ROWS];

static grub_uint8_t e0_translate[16] =
  {
    0x1c, 0x1d, 0x35, 0x00,
    0x38, 0x00, 0x47, 0x48,
    0x49, 0x4b, 0x4d, 0x4f,
    0x50, 0x51, 0x52, 0x53,
  };

/* If there is a character pending, return it;
   otherwise return GRUB_TERM_NO_KEY.  */
static int
grub_cros_keyboard_getkey (struct grub_term_input *term __attribute__ ((unused)))
{
  struct grub_cros_ec_keyscan scan;
  int i, j;
  if (grub_cros_ec_scan_keyboard (cros_ec, &scan) < 0)
    return GRUB_TERM_NO_KEY;
  for (i = 0; i < GRUB_CROS_EC_KEYSCAN_COLS; i++)
    if (scan.data[i] ^ old_scan.data[i])
      for (j = 0; j < GRUB_CROS_EC_KEYSCAN_ROWS; j++)
	if ((scan.data[i] ^ old_scan.data[i]) & (1 << j))
	  {
	    grub_uint8_t code = map_code[i][j];
	    int ret;
	    grub_uint8_t brk = 0;
	    if (!(scan.data[i] & (1 << j)))
	      brk = 0x80;
	    grub_dprintf ("cros_keyboard", "key <%d, %d> code %x\n", i, j, code);
	    if (code < 0x60)
	      ret = grub_ps2_process_incoming_byte (&ps2_state, code | brk);
	    else if (code >= 0x60 && code < 0x70 && e0_translate[code - 0x60])
	      {
		grub_ps2_process_incoming_byte (&ps2_state, 0xe0);
		ret = grub_ps2_process_incoming_byte (&ps2_state, e0_translate[code - 0x60] | brk);
	      }
	    else
	      ret = GRUB_TERM_NO_KEY;
	    old_scan.data[i] ^= (1 << j);
	    if (ret != GRUB_TERM_NO_KEY)
	      return ret;
	  }
  return GRUB_TERM_NO_KEY;
}

static struct grub_term_input grub_cros_keyboard_term =
  {
    .name = "cros_keyboard",
    .getkey = grub_cros_keyboard_getkey
  };

static grub_err_t
cros_attach (const struct grub_fdtbus_dev *dev)
{
  grub_size_t keymap_size, i;
  const grub_uint8_t *keymap = grub_fdtbus_get_prop (dev, "linux,keymap", &keymap_size);

  if (!dev->parent || !grub_cros_ec_validate (dev->parent))
    return GRUB_ERR_IO;

  if (keymap)
    {
      for (i = 0; i + 3 < keymap_size; i += 4)
	if (keymap[i+1] < GRUB_CROS_EC_KEYSCAN_COLS && keymap[i] < GRUB_CROS_EC_KEYSCAN_ROWS
	    && keymap[i+2] == 0 && keymap[i+3] < 0x80)
	  map_code[keymap[i+1]][keymap[i]] = keymap[i+3];
    }

  cros_ec = dev->parent;
  ps2_state.current_set = 1;
  ps2_state.at_keyboard_status = 0;
  grub_term_register_input ("cros_keyboard", &grub_cros_keyboard_term);
  return GRUB_ERR_NONE;
}

static struct grub_fdtbus_driver cros =
{
  .compatible = "google,cros-ec-keyb",
  .attach = cros_attach
};

void
grub_cros_init (void)
{
  grub_fdtbus_register (&cros);
}
