/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2007,2008,2009  Free Software Foundation, Inc.
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
#include <grub/at_keyboard.h>
#include <grub/misc.h>
#include <grub/term.h>
#include <grub/time.h>
#include <grub/ps2.h>
#include <grub/fdtbus.h>

static volatile grub_uint32_t *pl050_regs;

static struct grub_ps2_state ps2_state;

static void
keyboard_controller_wait_until_ready (void)
{
  while (! (pl050_regs[1] & 0x40));
}

static grub_uint8_t
wait_ack (void)
{
  grub_uint64_t endtime;
  grub_uint8_t ack;

  endtime = grub_get_time_ms () + 20;
  do
    ack = pl050_regs[2];
  while (ack != GRUB_AT_ACK && ack != GRUB_AT_NACK
	 && grub_get_time_ms () < endtime);
  return ack;
}


static int
write_mode (int mode)
{
  unsigned i;
  for (i = 0; i < GRUB_AT_TRIES; i++)
    {
      grub_uint8_t ack;
      keyboard_controller_wait_until_ready ();
      pl050_regs[2] = 0xf0;
      keyboard_controller_wait_until_ready ();
      pl050_regs[2] = mode;
      keyboard_controller_wait_until_ready ();
      ack = wait_ack ();
      if (ack == GRUB_AT_NACK)
	continue;
      if (ack == GRUB_AT_ACK)
	break;
      return 0;
    }

  return (i != GRUB_AT_TRIES);
}

static int
query_mode (void)
{
  grub_uint8_t ret;
  int e;

  e = write_mode (0);
  if (!e)
    return 0;

  keyboard_controller_wait_until_ready ();

  do
    ret = pl050_regs[2];
  while (ret == GRUB_AT_ACK);

  /* QEMU translates the set even in no-translate mode.  */
  if (ret == 0x43 || ret == 1)
    return 1;
  if (ret == 0x41 || ret == 2)
    return 2;
  if (ret == 0x3f || ret == 3)
    return 3;
  return 0;
}

static void
set_scancodes (void)
{
  write_mode (2);
  ps2_state.current_set = query_mode ();
  grub_dprintf ("atkeyb", "returned set %d\n", ps2_state.current_set);
  if (ps2_state.current_set == 2)
    return;

  write_mode (1);
  ps2_state.current_set = query_mode ();
  grub_dprintf ("atkeyb", "returned set %d\n", ps2_state.current_set);
  if (ps2_state.current_set == 1)
    return;
  grub_dprintf ("atkeyb", "no supported scancode set found\n");
}

static void
keyboard_controller_led (grub_uint8_t leds)
{
  keyboard_controller_wait_until_ready ();
  pl050_regs[2] = 0xed;
  keyboard_controller_wait_until_ready ();
  pl050_regs[2] = leds & 0x7;
}

/* If there is a character pending, return it;
   otherwise return GRUB_TERM_NO_KEY.  */
static int
grub_pl050_keyboard_getkey (struct grub_term_input *term __attribute__ ((unused)))
{
  grub_uint8_t at_key;
  int ret;
  grub_uint8_t old_led;

  if (!(pl050_regs[1] & 0x10))
    return -1;
  at_key = pl050_regs[2];
  old_led = ps2_state.led_status;

  ret = grub_ps2_process_incoming_byte (&ps2_state, at_key);
  if (old_led != ps2_state.led_status)
    keyboard_controller_led (ps2_state.led_status);
  return ret;
}

static struct grub_term_input grub_pl050_keyboard_term =
  {
    .name = "pl050_keyboard",
    .getkey = grub_pl050_keyboard_getkey
  };

static grub_err_t
pl050_attach(const struct grub_fdtbus_dev *dev)
{
  const grub_uint32_t *reg;
  reg = grub_fdtbus_get_prop (dev, "reg", 0);

  /* Mouse.  Nothing to do.  */
  if (grub_be_to_cpu32 (*reg) == 0x7000)
    return 0;

  pl050_regs = grub_fdtbus_map_reg (dev, 0, 0);

  if (!grub_fdtbus_is_mapping_valid (pl050_regs))
    return grub_error (GRUB_ERR_IO, "could not map pl050");

  ps2_state.at_keyboard_status = 0;
  set_scancodes ();
  keyboard_controller_led (ps2_state.led_status);

  grub_term_register_input ("pl050_keyboard", &grub_pl050_keyboard_term);
  return GRUB_ERR_NONE;
}

struct grub_fdtbus_driver pl050 =
{
  .compatible = "arm,pl050",
  .attach = pl050_attach
};

void
grub_pl050_init (void)
{
  grub_fdtbus_register (&pl050);
}
