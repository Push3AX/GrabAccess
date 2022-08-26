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
#include <grub/ps2mouse.h>
#include <grub/cpu/at_keyboard.h>
#include <grub/cpu/io.h>
#include <grub/misc.h>
#include <grub/term.h>
#include <grub/keyboard_layouts.h>
#include <grub/time.h>
#include <grub/loader.h>
#include <grub/command.h>
#include <grub/env.h>

GRUB_MOD_LICENSE ("GPLv3+");

struct MouseState
{
	grub_uint8_t left : 1, middle : 1, right : 1, locked : 1, x_enabled : 1, y_enabled : 1;
};

static grub_int8_t mouse_byte[3];    //signed char
static grub_int32_t mouse_y = 0;
static grub_int32_t mouse_x = 0;
static struct MouseState mouse_state;
static grub_uint8_t mouse_mode = MMODE_DEFAULT;
static grub_int32_t mouse_movehunky = 20;
static grub_int32_t mouse_movehunkx = 12;

#define MOUSE_ISBUTTON_DOWN(btn) ((mouse_byte[0] & (btn)) != 0)
#define MOUSE_GET_X() (mouse_byte[1])
#define MOUSE_GET_Y() (mouse_byte[2])

////////^ MOUSE ^/////v Keyboard v///////////////


static short at_keyboard_status = 0;
static int e0_received = 0;
static int f0_received = 0;

static grub_uint8_t led_status;

#define KEYBOARD_LED_SCROLL		(1 << 0)
#define KEYBOARD_LED_NUM		(1 << 1)
#define KEYBOARD_LED_CAPS		(1 << 2)

static grub_uint8_t grub_keyboard_controller_orig;
static grub_uint8_t grub_keyboard_orig_set;
static grub_uint8_t current_set; 

static const grub_uint8_t set1_mapping[128] =
  {
    /* 0x00 */ 0 /* Unused  */,               GRUB_KEYBOARD_KEY_ESCAPE, 
    /* 0x02 */ GRUB_KEYBOARD_KEY_1,           GRUB_KEYBOARD_KEY_2, 
    /* 0x04 */ GRUB_KEYBOARD_KEY_3,           GRUB_KEYBOARD_KEY_4, 
    /* 0x06 */ GRUB_KEYBOARD_KEY_5,           GRUB_KEYBOARD_KEY_6, 
    /* 0x08 */ GRUB_KEYBOARD_KEY_7,           GRUB_KEYBOARD_KEY_8, 
    /* 0x0a */ GRUB_KEYBOARD_KEY_9,           GRUB_KEYBOARD_KEY_0, 
    /* 0x0c */ GRUB_KEYBOARD_KEY_DASH,        GRUB_KEYBOARD_KEY_EQUAL, 
    /* 0x0e */ GRUB_KEYBOARD_KEY_BACKSPACE,   GRUB_KEYBOARD_KEY_TAB, 
    /* 0x10 */ GRUB_KEYBOARD_KEY_Q,           GRUB_KEYBOARD_KEY_W, 
    /* 0x12 */ GRUB_KEYBOARD_KEY_E,           GRUB_KEYBOARD_KEY_R, 
    /* 0x14 */ GRUB_KEYBOARD_KEY_T,           GRUB_KEYBOARD_KEY_Y, 
    /* 0x16 */ GRUB_KEYBOARD_KEY_U,           GRUB_KEYBOARD_KEY_I, 
    /* 0x18 */ GRUB_KEYBOARD_KEY_O,           GRUB_KEYBOARD_KEY_P, 
    /* 0x1a */ GRUB_KEYBOARD_KEY_LBRACKET,    GRUB_KEYBOARD_KEY_RBRACKET, 
    /* 0x1c */ GRUB_KEYBOARD_KEY_ENTER,       GRUB_KEYBOARD_KEY_LEFT_CTRL, 
    /* 0x1e */ GRUB_KEYBOARD_KEY_A,           GRUB_KEYBOARD_KEY_S, 
    /* 0x20 */ GRUB_KEYBOARD_KEY_D,           GRUB_KEYBOARD_KEY_F, 
    /* 0x22 */ GRUB_KEYBOARD_KEY_G,           GRUB_KEYBOARD_KEY_H, 
    /* 0x24 */ GRUB_KEYBOARD_KEY_J,           GRUB_KEYBOARD_KEY_K, 
    /* 0x26 */ GRUB_KEYBOARD_KEY_L,           GRUB_KEYBOARD_KEY_SEMICOLON, 
    /* 0x28 */ GRUB_KEYBOARD_KEY_DQUOTE,      GRUB_KEYBOARD_KEY_RQUOTE, 
    /* 0x2a */ GRUB_KEYBOARD_KEY_LEFT_SHIFT,  GRUB_KEYBOARD_KEY_BACKSLASH, 
    /* 0x2c */ GRUB_KEYBOARD_KEY_Z,           GRUB_KEYBOARD_KEY_X, 
    /* 0x2e */ GRUB_KEYBOARD_KEY_C,           GRUB_KEYBOARD_KEY_V, 
    /* 0x30 */ GRUB_KEYBOARD_KEY_B,           GRUB_KEYBOARD_KEY_N, 
    /* 0x32 */ GRUB_KEYBOARD_KEY_M,           GRUB_KEYBOARD_KEY_COMMA, 
    /* 0x34 */ GRUB_KEYBOARD_KEY_DOT,         GRUB_KEYBOARD_KEY_SLASH, 
    /* 0x36 */ GRUB_KEYBOARD_KEY_RIGHT_SHIFT, GRUB_KEYBOARD_KEY_NUMMUL, 
    /* 0x38 */ GRUB_KEYBOARD_KEY_LEFT_ALT,    GRUB_KEYBOARD_KEY_SPACE, 
    /* 0x3a */ GRUB_KEYBOARD_KEY_CAPS_LOCK,   GRUB_KEYBOARD_KEY_F1, 
    /* 0x3c */ GRUB_KEYBOARD_KEY_F2,          GRUB_KEYBOARD_KEY_F3, 
    /* 0x3e */ GRUB_KEYBOARD_KEY_F4,          GRUB_KEYBOARD_KEY_F5, 
    /* 0x40 */ GRUB_KEYBOARD_KEY_F6,          GRUB_KEYBOARD_KEY_F7, 
    /* 0x42 */ GRUB_KEYBOARD_KEY_F8,          GRUB_KEYBOARD_KEY_F9, 
    /* 0x44 */ GRUB_KEYBOARD_KEY_F10,         GRUB_KEYBOARD_KEY_NUM_LOCK, 
    /* 0x46 */ GRUB_KEYBOARD_KEY_SCROLL_LOCK, GRUB_KEYBOARD_KEY_NUM7, 
    /* 0x48 */ GRUB_KEYBOARD_KEY_NUM8,        GRUB_KEYBOARD_KEY_NUM9, 
    /* 0x4a */ GRUB_KEYBOARD_KEY_NUMMINUS,    GRUB_KEYBOARD_KEY_NUM4, 
    /* 0x4c */ GRUB_KEYBOARD_KEY_NUM5,        GRUB_KEYBOARD_KEY_NUM6, 
    /* 0x4e */ GRUB_KEYBOARD_KEY_NUMPLUS,     GRUB_KEYBOARD_KEY_NUM1, 
    /* 0x50 */ GRUB_KEYBOARD_KEY_NUM2,        GRUB_KEYBOARD_KEY_NUM3, 
    /* 0x52 */ GRUB_KEYBOARD_KEY_NUMDOT,      GRUB_KEYBOARD_KEY_NUMDOT, 
    /* 0x54 */ 0,                             0, 
    /* 0x56 */ GRUB_KEYBOARD_KEY_102ND,       GRUB_KEYBOARD_KEY_F11, 
    /* 0x58 */ GRUB_KEYBOARD_KEY_F12,         0,
    /* 0x5a */ 0,                             0,
    /* 0x5c */ 0,                             0,
    /* 0x5e */ 0,                             0,
    /* 0x60 */ 0,                             0,
    /* 0x62 */ 0,                             0,
    /* OLPC keys. Just mapped to normal keys.  */
    /* 0x64 */ 0,                             GRUB_KEYBOARD_KEY_UP,
    /* 0x66 */ GRUB_KEYBOARD_KEY_DOWN,        GRUB_KEYBOARD_KEY_LEFT,
    /* 0x68 */ GRUB_KEYBOARD_KEY_RIGHT
  };

static const struct
{
  grub_uint8_t from, to;
} set1_e0_mapping[] = 
  {
    {0x1c, GRUB_KEYBOARD_KEY_NUMENTER},
    {0x1d, GRUB_KEYBOARD_KEY_RIGHT_CTRL},
    {0x35, GRUB_KEYBOARD_KEY_NUMSLASH }, 
    {0x38, GRUB_KEYBOARD_KEY_RIGHT_ALT},
    {0x47, GRUB_KEYBOARD_KEY_HOME}, 
    {0x48, GRUB_KEYBOARD_KEY_UP},
    {0x49, GRUB_KEYBOARD_KEY_PPAGE}, 
    {0x4b, GRUB_KEYBOARD_KEY_LEFT},
    {0x4d, GRUB_KEYBOARD_KEY_RIGHT},
    {0x4f, GRUB_KEYBOARD_KEY_END}, 
    {0x50, GRUB_KEYBOARD_KEY_DOWN},
    {0x51, GRUB_KEYBOARD_KEY_NPAGE},
    {0x52, GRUB_KEYBOARD_KEY_INSERT},
    {0x53, GRUB_KEYBOARD_KEY_DELETE}, 
  };

static const grub_uint8_t set2_mapping[256] =
  {
    /* 0x00 */ 0,                             GRUB_KEYBOARD_KEY_F9,
    /* 0x02 */ 0,                             GRUB_KEYBOARD_KEY_F5,
    /* 0x04 */ GRUB_KEYBOARD_KEY_F3,          GRUB_KEYBOARD_KEY_F1,
    /* 0x06 */ GRUB_KEYBOARD_KEY_F2,          GRUB_KEYBOARD_KEY_F12,
    /* 0x08 */ 0,                             GRUB_KEYBOARD_KEY_F10,
    /* 0x0a */ GRUB_KEYBOARD_KEY_F8,          GRUB_KEYBOARD_KEY_F6,
    /* 0x0c */ GRUB_KEYBOARD_KEY_F4,          GRUB_KEYBOARD_KEY_TAB,
    /* 0x0e */ GRUB_KEYBOARD_KEY_RQUOTE,      0,
    /* 0x10 */ 0,                             GRUB_KEYBOARD_KEY_LEFT_ALT,
    /* 0x12 */ GRUB_KEYBOARD_KEY_LEFT_SHIFT,  0,
    /* 0x14 */ GRUB_KEYBOARD_KEY_LEFT_CTRL,   GRUB_KEYBOARD_KEY_Q,
    /* 0x16 */ GRUB_KEYBOARD_KEY_1,           0,
    /* 0x18 */ 0,                             0,
    /* 0x1a */ GRUB_KEYBOARD_KEY_Z,           GRUB_KEYBOARD_KEY_S,
    /* 0x1c */ GRUB_KEYBOARD_KEY_A,           GRUB_KEYBOARD_KEY_W,
    /* 0x1e */ GRUB_KEYBOARD_KEY_2,           0,
    /* 0x20 */ 0,                             GRUB_KEYBOARD_KEY_C,
    /* 0x22 */ GRUB_KEYBOARD_KEY_X,           GRUB_KEYBOARD_KEY_D,
    /* 0x24 */ GRUB_KEYBOARD_KEY_E,           GRUB_KEYBOARD_KEY_4,
    /* 0x26 */ GRUB_KEYBOARD_KEY_3,           0,
    /* 0x28 */ 0,                             GRUB_KEYBOARD_KEY_SPACE,
    /* 0x2a */ GRUB_KEYBOARD_KEY_V,           GRUB_KEYBOARD_KEY_F,
    /* 0x2c */ GRUB_KEYBOARD_KEY_T,           GRUB_KEYBOARD_KEY_R,
    /* 0x2e */ GRUB_KEYBOARD_KEY_5,           0,
    /* 0x30 */ 0,                             GRUB_KEYBOARD_KEY_N,
    /* 0x32 */ GRUB_KEYBOARD_KEY_B,           GRUB_KEYBOARD_KEY_H,
    /* 0x34 */ GRUB_KEYBOARD_KEY_G,           GRUB_KEYBOARD_KEY_Y,
    /* 0x36 */ GRUB_KEYBOARD_KEY_6,           0,
    /* 0x38 */ 0,                             0,
    /* 0x3a */ GRUB_KEYBOARD_KEY_M,           GRUB_KEYBOARD_KEY_J,
    /* 0x3c */ GRUB_KEYBOARD_KEY_U,           GRUB_KEYBOARD_KEY_7,
    /* 0x3e */ GRUB_KEYBOARD_KEY_8,           0,
    /* 0x40 */ 0,                             GRUB_KEYBOARD_KEY_COMMA,
    /* 0x42 */ GRUB_KEYBOARD_KEY_K,           GRUB_KEYBOARD_KEY_I,
    /* 0x44 */ GRUB_KEYBOARD_KEY_O,           GRUB_KEYBOARD_KEY_0,
    /* 0x46 */ GRUB_KEYBOARD_KEY_9,           0,
    /* 0x48 */ 0,                             GRUB_KEYBOARD_KEY_DOT,
    /* 0x4a */ GRUB_KEYBOARD_KEY_SLASH,       GRUB_KEYBOARD_KEY_L,
    /* 0x4c */ GRUB_KEYBOARD_KEY_SEMICOLON,   GRUB_KEYBOARD_KEY_P,
    /* 0x4e */ GRUB_KEYBOARD_KEY_DASH,        0,
    /* 0x50 */ 0,                             0,
    /* 0x52 */ GRUB_KEYBOARD_KEY_DQUOTE,      0,
    /* 0x54 */ GRUB_KEYBOARD_KEY_LBRACKET,    GRUB_KEYBOARD_KEY_EQUAL,
    /* 0x56 */ 0,                             0,
    /* 0x58 */ GRUB_KEYBOARD_KEY_CAPS_LOCK,   GRUB_KEYBOARD_KEY_RIGHT_SHIFT,
    /* 0x5a */ GRUB_KEYBOARD_KEY_ENTER,       GRUB_KEYBOARD_KEY_RBRACKET,
    /* 0x5c */ 0,                             GRUB_KEYBOARD_KEY_BACKSLASH,
    /* 0x5e */ 0,                             0,
    /* 0x60 */ 0,                             GRUB_KEYBOARD_KEY_102ND,
    /* 0x62 */ 0,                             0,
    /* 0x64 */ 0,                             0,
    /* 0x66 */ GRUB_KEYBOARD_KEY_BACKSPACE,   0,
    /* 0x68 */ 0,                             GRUB_KEYBOARD_KEY_NUM1,
    /* 0x6a */ 0,                             GRUB_KEYBOARD_KEY_NUM4,
    /* 0x6c */ GRUB_KEYBOARD_KEY_NUM7,        0,
    /* 0x6e */ 0,                             0,
    /* 0x70 */ GRUB_KEYBOARD_KEY_NUMDOT,      GRUB_KEYBOARD_KEY_NUM0,
    /* 0x72 */ GRUB_KEYBOARD_KEY_NUM2,        GRUB_KEYBOARD_KEY_NUM5,
    /* 0x74 */ GRUB_KEYBOARD_KEY_NUM6,        GRUB_KEYBOARD_KEY_NUM8,
    /* 0x76 */ GRUB_KEYBOARD_KEY_ESCAPE,      GRUB_KEYBOARD_KEY_NUM_LOCK,
    /* 0x78 */ GRUB_KEYBOARD_KEY_F11,         GRUB_KEYBOARD_KEY_NUMPLUS,
    /* 0x7a */ GRUB_KEYBOARD_KEY_NUM3,        GRUB_KEYBOARD_KEY_NUMMINUS,
    /* 0x7c */ GRUB_KEYBOARD_KEY_NUMMUL,      GRUB_KEYBOARD_KEY_NUM9,
    /* 0x7e */ GRUB_KEYBOARD_KEY_SCROLL_LOCK, 0,
    /* 0x80 */ 0,                             0, 
    /* 0x82 */ 0,                             GRUB_KEYBOARD_KEY_F7,
  };

static const struct
{
  grub_uint8_t from, to;
} set2_e0_mapping[] = 
  {
    {0x11, GRUB_KEYBOARD_KEY_RIGHT_ALT},
    {0x14, GRUB_KEYBOARD_KEY_RIGHT_CTRL},
    {0x4a, GRUB_KEYBOARD_KEY_NUMSLASH},
    {0x5a, GRUB_KEYBOARD_KEY_NUMENTER},
    {0x69, GRUB_KEYBOARD_KEY_END},
    {0x6b, GRUB_KEYBOARD_KEY_LEFT},
    {0x6c, GRUB_KEYBOARD_KEY_HOME},
    {0x70, GRUB_KEYBOARD_KEY_INSERT},
    {0x71, GRUB_KEYBOARD_KEY_DELETE},
    {0x72, GRUB_KEYBOARD_KEY_DOWN},
    {0x74, GRUB_KEYBOARD_KEY_RIGHT},
    {0x75, GRUB_KEYBOARD_KEY_UP},
    {0x7a, GRUB_KEYBOARD_KEY_NPAGE},
    {0x7d, GRUB_KEYBOARD_KEY_PPAGE},
  };


static inline void 
ps2_data_wait(void)
{
	grub_uint32_t time_out = 100000;
	while(time_out--)
	{
		//check if the data flag is present
		if(PS2_HAS_DATA(grub_inb(0x64)))
		{
			return;
		}
	}
}

static inline void 
ps2_command_wait(void)
{
	grub_uint32_t time_out = 100000;
	while(time_out--) 
	{
		//check if the buffer full flag is not present
		if(PS2_COMMAND_ISREADY(grub_inb(0x64)))
		{
			return;
		}
	}
}

inline static void 
mouse_write(grub_uint8_t a_write) //unsigned char
{
  //Wait to be able to send a command
  ps2_command_wait();
  //Tell the mouse we are sending a command
  grub_outb(0xD4, 0x64);
  //Wait for the final part
  ps2_command_wait();
  //Finally write
  grub_outb(a_write, 0x60);
}

inline static grub_uint8_t
ps2_read(void)
{
  //Get's response from mouse
  ps2_data_wait();
  return grub_inb(0x60);
}

static grub_uint8_t
wait_ack (void)
{
  grub_uint64_t endtime;
  grub_uint8_t ack;

  endtime = grub_get_time_ms () + 20;
  do
    ack = grub_inb (KEYBOARD_REG_DATA);
  while (ack != GRUB_AT_ACK && ack != GRUB_AT_NACK
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
      ps2_command_wait();
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
  ps2_command_wait();
  grub_outb (c, KEYBOARD_REG_DATA);
}

#if !defined (GRUB_MACHINE_MIPS_LOONGSON) && !defined (GRUB_MACHINE_QEMU) && !defined (GRUB_MACHINE_MIPS_QEMU_MIPS)

static grub_uint8_t
grub_keyboard_controller_read (void)
{
  at_command (KEYBOARD_COMMAND_READ);
  ps2_command_wait();
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
      ps2_command_wait();
      grub_outb (0xf0, KEYBOARD_REG_DATA);
      ps2_command_wait();
      grub_outb (mode, KEYBOARD_REG_DATA);
      ps2_command_wait();
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

  ps2_command_wait();

  do
    ret = grub_inb (KEYBOARD_REG_DATA);
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
  /* You must have visited computer museum. Keyboard without scancode set
     knowledge. Assume XT. */
  if (!grub_keyboard_orig_set)
    {
      grub_dprintf ("atkeyb", "No sets support assumed\n");
      current_set = 1;
      return;
    }

#if !(defined (GRUB_MACHINE_MIPS_LOONGSON) || defined (GRUB_MACHINE_QEMU) || defined (GRUB_MACHINE_MIPS_QEMU_MIPS))
  current_set = 1;
  return;
#else

  grub_keyboard_controller_write (grub_keyboard_controller_orig
				  & ~KEYBOARD_AT_TRANSLATE);

  write_mode (2);
  current_set = query_mode ();
  grub_dprintf ("atkeyb", "returned set %d\n", current_set);
  if (current_set == 2)
    return;

  write_mode (1);
  current_set = query_mode ();
  grub_dprintf ("atkeyb", "returned set %d\n", current_set);
  if (current_set == 1)
    return;
  grub_printf ("No supported scancode set found\n");
#endif
}


static void
keyboard_controller_led (grub_uint8_t leds)
{
	ps2_command_wait();
	grub_outb (0xed, KEYBOARD_REG_DATA);
	ps2_command_wait();
	grub_outb (leds & 0x7, KEYBOARD_REG_DATA);
}


static int
fetch_key (int *is_break)
{
  int was_ext = 0;
  grub_uint8_t at_key;
  int ret = 0;

  at_key = grub_inb (KEYBOARD_REG_DATA);
  if (at_key == 0xe0)
    {
      e0_received = 1;
      return -1;
    }

  if ((current_set == 2 || current_set == 3) && at_key == 0xf0)
    {
      f0_received = 1;
      return -1;
    }

  /* Setting LEDs may generate ACKs.  */
  if (at_key == GRUB_AT_ACK)
    return -1;

  was_ext = e0_received;
  e0_received = 0;

  switch (current_set)
    {
    case 1:
      *is_break = !!(at_key & 0x80);
      if (!was_ext)
	ret = set1_mapping[at_key & 0x7f];
      else
	{
	  unsigned i;
	  for (i = 0; i < ARRAY_SIZE (set1_e0_mapping); i++)
	    if (set1_e0_mapping[i].from == (at_key & 0x7f))
	      {
		ret = set1_e0_mapping[i].to;
		break;
	      }
	}
      break;
    case 2:
      *is_break = f0_received;
      f0_received = 0;
      if (!was_ext)
	ret = set2_mapping[at_key];
      else
	{
	  unsigned i;
	  for (i = 0; i < ARRAY_SIZE (set2_e0_mapping); i++)
	    if (set2_e0_mapping[i].from == at_key)
	      {
		ret = set2_e0_mapping[i].to;
		break;
	      }
	}	
      break;
    default:
      return -1;
    }
  if (!ret)
    {
      if (was_ext)
	grub_dprintf ("atkeyb", "Unknown key 0xe0+0x%02x from set %d\n",
		      at_key, current_set);
      else
	grub_dprintf ("atkeyb", "Unknown key 0x%02x from set %d\n",
		      at_key, current_set);
      return -1;
    }
  return ret;
}


/* FIXME: This should become an interrupt service routine.  For now
   it's just used to catch events from control keys.  */
static int
grub_keyboard_isr (grub_keyboard_key_t key, int is_break)
{
  if (!is_break)
    switch (key)
      {
      case GRUB_KEYBOARD_KEY_LEFT_SHIFT:
	at_keyboard_status |= GRUB_TERM_STATUS_LSHIFT;
	return 1;
      case GRUB_KEYBOARD_KEY_RIGHT_SHIFT:
	at_keyboard_status |= GRUB_TERM_STATUS_RSHIFT;
	return 1;
      case GRUB_KEYBOARD_KEY_LEFT_CTRL:
	at_keyboard_status |= GRUB_TERM_STATUS_LCTRL;
	return 1;
      case GRUB_KEYBOARD_KEY_RIGHT_CTRL:
	at_keyboard_status |= GRUB_TERM_STATUS_RCTRL;
	return 1;
      case GRUB_KEYBOARD_KEY_RIGHT_ALT:
	at_keyboard_status |= GRUB_TERM_STATUS_RALT;
	return 1;
      case GRUB_KEYBOARD_KEY_LEFT_ALT:
	at_keyboard_status |= GRUB_TERM_STATUS_LALT;
	return 1;
      default:
	return 0;
      }
  else
    switch (key)
      {
      case GRUB_KEYBOARD_KEY_LEFT_SHIFT:
	at_keyboard_status &= ~GRUB_TERM_STATUS_LSHIFT;
	return 1;
      case GRUB_KEYBOARD_KEY_RIGHT_SHIFT:
	at_keyboard_status &= ~GRUB_TERM_STATUS_RSHIFT;
	return 1;
      case GRUB_KEYBOARD_KEY_LEFT_CTRL:
	at_keyboard_status &= ~GRUB_TERM_STATUS_LCTRL;
	return 1;
      case GRUB_KEYBOARD_KEY_RIGHT_CTRL:
	at_keyboard_status &= ~GRUB_TERM_STATUS_RCTRL;
	return 1;
      case GRUB_KEYBOARD_KEY_RIGHT_ALT:
	at_keyboard_status &= ~GRUB_TERM_STATUS_RALT;
	return 1;
      case GRUB_KEYBOARD_KEY_LEFT_ALT:
	at_keyboard_status &= ~GRUB_TERM_STATUS_LALT;
	return 1;
      default:
	return 0;
      }
}


/* If there is a raw key pending, return it; otherwise return -1.  */
static int
grub_keyboard_getkey (void)
{
	int key;
	int is_break = 0;

	key = fetch_key (&is_break);
	if (key == -1)
		return -1;

	if (grub_keyboard_isr (key, is_break))
		return -1;
	if (is_break)
		return -1;
	return key;
}

static int
keyboard_decode(void)
{
	int code = grub_keyboard_getkey ();
	if (code == -1)
		return GRUB_TERM_NO_KEY;
#ifdef DEBUG_AT_KEYBOARD
	grub_dprintf ("atkeyb", "Detected key 0x%x\n", key);
#endif
	switch (code)
	{
		case GRUB_KEYBOARD_KEY_CAPS_LOCK:
			at_keyboard_status ^= GRUB_TERM_STATUS_CAPS;
			led_status ^= KEYBOARD_LED_CAPS;
			keyboard_controller_led (led_status);

#ifdef DEBUG_AT_KEYBOARD
			grub_dprintf ("atkeyb", "caps_lock = %d\n", !!(at_keyboard_status & KEYBOARD_STATUS_CAPS_LOCK));
#endif
			return GRUB_TERM_NO_KEY;
			
		case GRUB_KEYBOARD_KEY_NUM_LOCK:
			at_keyboard_status ^= GRUB_TERM_STATUS_NUM;
			led_status ^= KEYBOARD_LED_NUM;
			keyboard_controller_led (led_status);

#ifdef DEBUG_AT_KEYBOARD
			grub_dprintf ("atkeyb", "num_lock = %d\n", !!(at_keyboard_status & KEYBOARD_STATUS_NUM_LOCK));
#endif
			return GRUB_TERM_NO_KEY;
			
		case GRUB_KEYBOARD_KEY_SCROLL_LOCK:
			at_keyboard_status ^= GRUB_TERM_STATUS_SCROLL;
			led_status ^= KEYBOARD_LED_SCROLL;
			keyboard_controller_led (led_status);
			return GRUB_TERM_NO_KEY;
			
		default:
			return grub_term_map_key(code, at_keyboard_status);
	}
}


/////// v MOUSE v ///////// ^ Keyboard ^ /////////

//Mouse functions
static void 
mouse_handler(void)
{
	//Read the 3 bytes from the mouse
	mouse_byte[0] = ps2_read();
	ps2_data_wait();
	mouse_byte[1] = ps2_read();
	ps2_data_wait();
	mouse_byte[2] = ps2_read();
	
	//update change
	mouse_y += MOUSE_GET_Y();
	mouse_x += MOUSE_GET_X();
  	
	grub_dprintf("psmous", "mouseCycle\n(0) = %d(%d%d%d%d %d%d%d%d)\n(1) = %d(%d%d%d%d %d%d%d%d)\n(2) = %d(%d%d%d%d %d%d%d%d)\n", mouse_byte[0], BSPLIT(mouse_byte[0]), mouse_byte[1], BSPLIT(mouse_byte[1]), mouse_byte[2], BSPLIT(mouse_byte[2]));
}

static int
mouse_finish_scrolling(void)
{
	if (mouse_state.y_enabled)
	{
		if (mouse_y > mouse_movehunky)
		{
			grub_dprintf("psmous", "Sending Up using %d\n",  mouse_y);
			mouse_y -= mouse_movehunky;
			return GRUB_KEYBOARD_KEY_UP;
		}
		else if (mouse_y < -mouse_movehunky)
		{
			grub_dprintf("psmous", "Sending Down using %d\n",  mouse_y);
			mouse_y += mouse_movehunky;
			return GRUB_KEYBOARD_KEY_DOWN;
		}
	}
	if (mouse_state.x_enabled)
	{
		if (mouse_x > mouse_movehunkx)
		{
			grub_dprintf("psmous", "Sending Right using %d\n",  mouse_x);
			mouse_x -= mouse_movehunkx;
			return GRUB_KEYBOARD_KEY_RIGHT;
		}
		else if (mouse_x < -mouse_movehunkx)
		{
			grub_dprintf("psmous", "Sending Left using %d\n",  mouse_x);
			mouse_x += mouse_movehunkx;
			return GRUB_KEYBOARD_KEY_LEFT;
		}
	}
	return GRUB_TERM_NO_KEY;
}

static int
mouse_left_click(void)
{
	grub_dprintf("psmous", "Left click => ENTER using %d, locked: %d\n",  mouse_byte[0], mouse_state.locked);
	if (!mouse_state.locked)
	{
		mouse_y = 0;
		return GRUB_KEYBOARD_KEY_ENTER;
	}
	return GRUB_TERM_NO_KEY;
}
static int
mouse_right_click(void)
{
	grub_dprintf("psmous", "Right click => E using %d, locked: %d\n",  mouse_byte[0], mouse_state.locked);
	if (!mouse_state.locked)
	{
		mouse_y = 0;
		return GRUB_KEYBOARD_KEY_E;
	}
	return GRUB_TERM_NO_KEY;
}
static int
mouse_middle_click(void)
{
	grub_dprintf("psmous", "Middle click => C using %d, locked: %d\n",  mouse_byte[0], mouse_state.locked);
	if (!mouse_state.locked)
	{
		mouse_y = 0;
		return GRUB_KEYBOARD_KEY_C;
	}
	return GRUB_TERM_NO_KEY;
}

inline static int
ps2_keymap(int grubcode)
{
	if (grubcode == GRUB_TERM_NO_KEY)
		return grubcode;
	return grub_term_map_key (grubcode, 0);
}

/* If there is a character pending, return it;
   otherwise return GRUB_TERM_NO_KEY.  */
static int
grub_ps2_getkey (struct grub_term_input *term __attribute__ ((unused)))
{
	grub_uint8_t code = grub_inb(0x64);
	
	//check if the queue is empty
	if (!PS2_HAS_DATA(code))
	{
		//no keystrokes/movements. Continue any ongoing scrolling
		return ps2_keymap (mouse_finish_scrolling());
	}
	else if (PS2_ISKEYBOARD_EVENT(code)) //check if this is a non-mouse event
	{
		grub_dprintf("psmous", "GetKey(NON-Mouse) = %d(%d%d%d%d %d%d%d%d)\n", code, BSPLIT(code));
		//must be keyboard event; call keyboard handler
		return keyboard_decode();
	}
	else //its a mouse!
	{
		grub_dprintf("psmous", "GetKey(Mouse) = %d(%d%d%d%d %d%d%d%d)\n", code, BSPLIT(code));
rapidfire:
		//Read mouse data
		mouse_handler();
		
		//check for any clicks, when saved state and current state differ
		if (MOUSE_ISBUTTON_DOWN(MOUSE_BUTTON_LEFT) != mouse_state.left)
		{
			mouse_state.left = MOUSE_ISBUTTON_DOWN(MOUSE_BUTTON_LEFT);
			
			//If button not pressed anymore, fire click event
			if (!mouse_state.left)
			{
				return ps2_keymap (mouse_left_click());
			}
		}
		if (MOUSE_ISBUTTON_DOWN(MOUSE_BUTTON_RIGHT) != mouse_state.right)
		{
			mouse_state.right = MOUSE_ISBUTTON_DOWN(MOUSE_BUTTON_RIGHT);
			
			//If button not pressed anymore, fire click event
			if (!mouse_state.right)
			{
				return ps2_keymap (mouse_right_click());
			}
		}
		if (MOUSE_ISBUTTON_DOWN(MOUSE_BUTTON_MIDDLE) != mouse_state.middle)
		{
			mouse_state.middle = MOUSE_ISBUTTON_DOWN(MOUSE_BUTTON_MIDDLE);
			
			//If button not pressed anymore, fire click event
			if (!mouse_state.middle)
			{
				return ps2_keymap (mouse_middle_click());
			}
		}
		
		//are we in rapid-fire mode?
		code = grub_inb(0x64);
		if(PS2_HAS_DATA(code) && mouse_mode == MMODE_TOUCH && PS2_ISMOUSE_EVENT(code))
			goto rapidfire;
		
		//if not, just scroll
		return ps2_keymap (mouse_finish_scrolling());
	}
}

static grub_err_t
grub_ps2_controller_init (struct grub_term_input *term __attribute__ ((unused)))
{
  at_keyboard_status = 0;
  /* Drain input buffer. */
  while (1)
    {
       ps2_command_wait();
      if (!PS2_HAS_DATA(grub_inb (KEYBOARD_REG_STATUS)))
	break;
       ps2_command_wait();
      grub_inb (KEYBOARD_REG_DATA);
    }
#if defined (GRUB_MACHINE_MIPS_LOONGSON) || defined (GRUB_MACHINE_QEMU) || defined (GRUB_MACHINE_MIPS_QEMU_MIPS)
  grub_keyboard_controller_orig = 0;
  grub_keyboard_orig_set = 2;
#else
  grub_keyboard_controller_orig = grub_keyboard_controller_read ();
  grub_keyboard_orig_set = query_mode ();
#endif
  set_scancodes ();
  keyboard_controller_led (led_status);
  
  //Enable the auxiliary mouse device
  ps2_command_wait();
  grub_outb(0xA8, 0x64);
 
    //Tell the mouse to use default settings
  mouse_write(0xF6);
  ps2_read();  //Acknowledge
 
  //Enable the mouse
  mouse_write(0xF4);
  ps2_read();  //Acknowledge

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_mousectl (grub_command_t cmd __attribute__ ((unused)),
			 int argc, char **args)
{
	// Parse args; none means show info
	if (argc == 0)
	{
		grub_printf(N_("Current mode: "));
		switch (mouse_mode)
		{
			case MMODE_MOUSE:
				grub_printf(N_("mouse"));
				break;
			case MMODE_TOUCH:
				grub_printf(N_("touch"));
				break;
			default:
				grub_printf(N_("default (mixed)"));
				break;
		}
		grub_printf(N_("\nSensitivity: %d (x) %d (y)\n"), mouse_movehunkx, mouse_movehunky);
		grub_printf(N_("Enabled: %d (x) %d (y)\n"), mouse_state.x_enabled, mouse_state.y_enabled);
	}
	else // More than one arg is dependent on the first
	{
		// ops is a flag to later functions to check for specific later args
		int ops = 0;
		
		//check if changing the mode, and if so, change it accordingly
		if (grub_strcmp(args[0], "mousemode") == 0)
		{
			ops = 1;
			mouse_mode = MMODE_MOUSE;
			mouse_state.locked = 0;
		}
		else if (grub_strcmp(args[0], "touchmode") == 0)
		{
			ops = 1;
			mouse_mode = MMODE_TOUCH;
			mouse_state.locked = 1;
		}
		else if (grub_strcmp(args[0], "sensitivity") == 0)
		{
			//for sensitivity, only mark flag
			ops = 1;
		}
		else if (grub_strcmp(args[0], "enable") == 0) //enable x reporting
		{
			//for enable/disable, only mark flag until we get the axis
			ops = 3;
		}
		else if (grub_strcmp(args[0], "disable") == 0) //disable x reporting
		{
			//for enable/disable, only mark flag until we get the axis
			ops = 2;
		}
		
		if (ops == 2 || ops == 3)
		{
			if (argc >= 2) //enable _axis_
			{
				if (grub_strcmp(args[1], "x") == 0)
				{
					mouse_state.x_enabled = (ops & 1); //enabled is 3, disabled is 2. tricky
					mouse_x = 0;
					if (argc >= 3) //  check for sensitivity
					{
						mouse_movehunkx = grub_strtoul (args[2], 0, 10);
					}
				}
				else if (grub_strcmp(args[1], "y") == 0)
				{
					mouse_state.y_enabled = (ops & 1); //enabled is 3, disabled is 2. tricky
					mouse_y = 0;
					if (argc >= 3) //  check for sensitivity
					{
						mouse_movehunky = grub_strtoul (args[2], 0, 10);
					}
				}
				else if (grub_strcmp(args[1], "swap") == 0)
				{
					mouse_state.y_enabled = !mouse_state.y_enabled;
					mouse_state.x_enabled = !mouse_state.x_enabled;
					mouse_y = 0;
					mouse_x = 0;
					if (argc >= 3) //  check for sensitivity
					{
						mouse_movehunky = mouse_movehunkx = grub_strtoul (args[2], 0, 10);
					}
				}
				else //enable all!
				{
					mouse_state.y_enabled = (ops & 1); //enabled is 3, disabled is 2. tricky
					mouse_state.x_enabled = (ops & 1);
					mouse_y = 0;
					mouse_x = 0;
					if (argc >= 3) //  check for sensitivity
					{
						mouse_movehunky = mouse_movehunkx = grub_strtoul (args[2], 0, 10);
					}
				}
			}
		}
		
		if (ops == 1 && argc >= 2) //  check for sensitivity
		{
			if (grub_strcmp(args[1], "y") == 0)
				mouse_movehunky = grub_strtoul (args[2], 0, 10);
			else if (grub_strcmp(args[1], "x") == 0)
				mouse_movehunkx = grub_strtoul (args[2], 0, 10);
			else
				mouse_movehunky = mouse_movehunkx = grub_strtoul (args[1], 0, 10);
		}
		
		if (mouse_movehunky == 0)
			mouse_movehunky = 1;
		if (mouse_movehunkx == 0)
			mouse_movehunkx = 1;
	}
	return GRUB_ERR_NONE;
}

static grub_err_t
grub_ps2_controller_fini (struct grub_term_input *term __attribute__ ((unused)))
{
	grub_dprintf("psmous", "controller fini %d\n", 1);

	//Disable the mouse
	mouse_write(0xF5);
	ps2_read();  //Acknowledge
	
	
	if (grub_keyboard_orig_set)
		write_mode (grub_keyboard_orig_set);
	grub_keyboard_controller_write (grub_keyboard_controller_orig);
	
	return GRUB_ERR_NONE;
}

static grub_err_t
grub_ps2_fini_hw (int noreturn __attribute__ ((unused)))
{
	grub_dprintf("psmous", "controller fini hw %d\n", 2);
	return grub_ps2_controller_fini (NULL);
}

static grub_err_t
grub_ps2_restore_hw (void)
{
	grub_dprintf("psmous", "restore hw %d\n", 1);

	//Disable the mouse
	mouse_write(0xF5);
	ps2_read();  //Acknowledge
	
	/* Drain input buffer. */
	while (1)
	{
		ps2_command_wait();
		if (!PS2_HAS_DATA(grub_inb (KEYBOARD_REG_STATUS)))
			break;
		ps2_command_wait();
		grub_inb (KEYBOARD_REG_DATA);
	}
	set_scancodes ();
	keyboard_controller_led (led_status);
	
	return GRUB_ERR_NONE;
}


static struct grub_term_input grub_ps2mouse_term =
  {
    .name = "ps2mouse",
    .init = grub_ps2_controller_init,
    .fini = grub_ps2_controller_fini,
    .getkey = grub_ps2_getkey
  };
  
static grub_command_t cmd_mousectl;

GRUB_MOD_INIT(ps2mouse)
{
	// Initialize mouse_state to defaults
	mouse_state.locked = 1;
	mouse_state.left = 0;
	mouse_state.right = 0;
	mouse_state.middle = 0;
	mouse_state.x_enabled = 0;
	mouse_state.y_enabled = 1;
	
	// Register module and command with GRUB
	grub_term_register_input ("ps2mouse", &grub_ps2mouse_term);
	grub_loader_register_preboot_hook (grub_ps2_fini_hw, grub_ps2_restore_hw,
				     GRUB_LOADER_PREBOOT_HOOK_PRIO_CONSOLE);
	cmd_mousectl =
    grub_register_command ("mousectl", grub_cmd_mousectl,
			   N_("[mousemode|touchmode|sensitivity [x|y] [sensitivity]]|[enable|disable [x|y|swap [sensitivity]]]"),
			   N_("Edit behaviour of mouse."));
}

GRUB_MOD_FINI(ps2mouse)
{
	grub_dprintf("psmous", "module fini %d\n", 1);
	grub_ps2_controller_fini (NULL);
	grub_unregister_command (cmd_mousectl);
	grub_term_unregister_input (&grub_ps2mouse_term);
}
