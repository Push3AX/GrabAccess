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

#include <grub/dl.h>
#include <grub/at_keyboard.h>
#include <grub/cpu/at_keyboard.h>
#include <grub/cpu/io.h>
#include <grub/misc.h>
#include <grub/term.h>
#include <grub/time.h>
#include <grub/loader.h>
#include <grub/ps2.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_uint8_t grub_keyboard_controller_orig;
static grub_uint8_t grub_keyboard_orig_set;
struct grub_ps2_state ps2_state;

static int ping_sent;

static void
grub_keyboard_controller_init (void);

static void
keyboard_controller_wait_until_ready (void)
{
  /* 50 us would be enough but our current time resolution is 1ms.  */
  grub_millisleep (1);
  while (! KEYBOARD_COMMAND_ISREADY (grub_inb (KEYBOARD_REG_STATUS)));
}

static grub_uint8_t
wait_ack (void)
{
  grub_uint64_t endtime;
  grub_uint8_t ack;

  endtime = grub_get_time_ms () + 20;
  do {
    keyboard_controller_wait_until_ready ();
    ack = grub_inb (KEYBOARD_REG_DATA);
  } while (ack != GRUB_AT_ACK && ack != GRUB_AT_NACK
	   && grub_get_time_ms () < endtime);
  return ack;
}

static int
at_command (grub_uint8_t data)
{
  unsigned i;
  for (i = 0; i < GRUB_AT_TRIES; i++)
    {
      grub_uint8_t ack;
      keyboard_controller_wait_until_ready ();
      grub_outb (data, KEYBOARD_REG_STATUS);
      ack = wait_ack ();
      if (ack == GRUB_AT_NACK)
	continue;
      if (ack == GRUB_AT_ACK)
	break;
      return 0;
    }
  return (i != GRUB_AT_TRIES);
}

static void
grub_keyboard_controller_write (grub_uint8_t c)
{
  at_command (KEYBOARD_COMMAND_WRITE);
  keyboard_controller_wait_until_ready ();
  grub_outb (c, KEYBOARD_REG_DATA);
}

#if defined (GRUB_MACHINE_MIPS_LOONGSON) || defined (GRUB_MACHINE_QEMU) || defined (GRUB_MACHINE_COREBOOT) || defined (GRUB_MACHINE_MIPS_QEMU_MIPS)
#define USE_SCANCODE_SET 1
#else
#define USE_SCANCODE_SET 0
#endif

#if !USE_SCANCODE_SET

static grub_uint8_t
grub_keyboard_controller_read (void)
{
  at_command (KEYBOARD_COMMAND_READ);
  keyboard_controller_wait_until_ready ();
  return grub_inb (KEYBOARD_REG_DATA);
}

#endif

static int
write_mode (int mode)
{
  unsigned i;
  for (i = 0; i < GRUB_AT_TRIES; i++)
    {
      grub_uint8_t ack;
      keyboard_controller_wait_until_ready ();
      grub_outb (0xf0, KEYBOARD_REG_DATA);
      keyboard_controller_wait_until_ready ();
      grub_outb (mode, KEYBOARD_REG_DATA);
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

  do {
    keyboard_controller_wait_until_ready ();
    ret = grub_inb (KEYBOARD_REG_DATA);
  } while (ret == GRUB_AT_ACK);
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
  /* You must have visited computer museum. Keyboard without scancode set
     knowledge. Assume XT. */
  if (!grub_keyboard_orig_set)
    {
      grub_dprintf ("atkeyb", "No sets support assumed\n");
      ps2_state.current_set = 1;
      return;
    }

#if !USE_SCANCODE_SET
  ps2_state.current_set = 1;
  return;
#else

  grub_keyboard_controller_write (grub_keyboard_controller_orig
				  & ~KEYBOARD_AT_TRANSLATE
				  & ~KEYBOARD_AT_DISABLE);

  keyboard_controller_wait_until_ready ();
  grub_outb (KEYBOARD_COMMAND_ENABLE, KEYBOARD_REG_DATA);

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
#endif
}

static void
keyboard_controller_led (grub_uint8_t leds)
{
  keyboard_controller_wait_until_ready ();
  grub_outb (0xed, KEYBOARD_REG_DATA);
  keyboard_controller_wait_until_ready ();
  grub_outb (leds & 0x7, KEYBOARD_REG_DATA);
}

int
grub_at_keyboard_is_alive (void)
{
  if (ps2_state.current_set != 0)
    return 1;
  if (ping_sent
      && KEYBOARD_COMMAND_ISREADY (grub_inb (KEYBOARD_REG_STATUS))
      && grub_inb (KEYBOARD_REG_DATA) == 0x55)
    {
      grub_keyboard_controller_init ();
      return 1;
    }

  if (KEYBOARD_COMMAND_ISREADY (grub_inb (KEYBOARD_REG_STATUS)))
    {
      grub_outb (0xaa, KEYBOARD_REG_STATUS);
      ping_sent = 1;
    }
  return 0;
}

/* If there is a character pending, return it;
   otherwise return GRUB_TERM_NO_KEY.  */
static int
grub_at_keyboard_getkey (struct grub_term_input *term __attribute__ ((unused)))
{
  grub_uint8_t at_key;
  int ret;
  grub_uint8_t old_led;

  if (!grub_at_keyboard_is_alive ())
    return GRUB_TERM_NO_KEY;

  if (! KEYBOARD_ISREADY (grub_inb (KEYBOARD_REG_STATUS)))
    return GRUB_TERM_NO_KEY;
  at_key = grub_inb (KEYBOARD_REG_DATA);
  old_led = ps2_state.led_status;

  ret = grub_ps2_process_incoming_byte (&ps2_state, at_key);
  if (old_led != ps2_state.led_status)
    keyboard_controller_led (ps2_state.led_status);
  return ret;
}

static void
grub_keyboard_controller_init (void)
{
  ps2_state.at_keyboard_status = 0;
  /* Drain input buffer. */
  while (1)
    {
      keyboard_controller_wait_until_ready ();
      if (! KEYBOARD_ISREADY (grub_inb (KEYBOARD_REG_STATUS)))
	break;
      keyboard_controller_wait_until_ready ();
      grub_inb (KEYBOARD_REG_DATA);
    }
#if defined (GRUB_MACHINE_MIPS_LOONGSON) || defined (GRUB_MACHINE_MIPS_QEMU_MIPS)
  grub_keyboard_controller_orig = 0;
  grub_keyboard_orig_set = 2;
#elif defined (GRUB_MACHINE_QEMU) || defined (GRUB_MACHINE_COREBOOT)
  /* *BSD relies on those settings.  */
  grub_keyboard_controller_orig = KEYBOARD_AT_TRANSLATE;
  grub_keyboard_orig_set = 2;
#else
  grub_keyboard_controller_orig = grub_keyboard_controller_read ();
  grub_keyboard_orig_set = query_mode ();
#endif
  set_scancodes ();
  keyboard_controller_led (ps2_state.led_status);
}

static grub_err_t
grub_keyboard_controller_fini (struct grub_term_input *term __attribute__ ((unused)))
{
  if (ps2_state.current_set == 0)
    return GRUB_ERR_NONE;
  if (grub_keyboard_orig_set)
    write_mode (grub_keyboard_orig_set);
  grub_keyboard_controller_write (grub_keyboard_controller_orig);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_at_fini_hw (int noreturn __attribute__ ((unused)))
{
  return grub_keyboard_controller_fini (NULL);
}

static grub_err_t
grub_at_restore_hw (void)
{
  if (ps2_state.current_set == 0)
    return GRUB_ERR_NONE;

  /* Drain input buffer. */
  while (1)
    {
      keyboard_controller_wait_until_ready ();
      if (! KEYBOARD_ISREADY (grub_inb (KEYBOARD_REG_STATUS)))
	break;
      keyboard_controller_wait_until_ready ();
      grub_inb (KEYBOARD_REG_DATA);
    }
  set_scancodes ();
  keyboard_controller_led (ps2_state.led_status);

  return GRUB_ERR_NONE;
}


static struct grub_term_input grub_at_keyboard_term =
  {
    .name = "at_keyboard",
    .fini = grub_keyboard_controller_fini,
    .getkey = grub_at_keyboard_getkey
  };

GRUB_MOD_INIT(at_keyboard)
{
  grub_term_register_input ("at_keyboard", &grub_at_keyboard_term);
  grub_loader_register_preboot_hook (grub_at_fini_hw, grub_at_restore_hw,
				     GRUB_LOADER_PREBOOT_HOOK_PRIO_CONSOLE);
}

GRUB_MOD_FINI(at_keyboard)
{
  grub_keyboard_controller_fini (NULL);
  grub_term_unregister_input (&grub_at_keyboard_term);
}
