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
#include <grub/misc.h>
#include <grub/term.h>
#include <grub/keyboard_layouts.h>
#include <grub/ps2.h>

#define KEYBOARD_LED_SCROLL		(1 << 0)
#define KEYBOARD_LED_NUM		(1 << 1)
#define KEYBOARD_LED_CAPS		(1 << 2)

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
    /* 0x52 */ GRUB_KEYBOARD_KEY_NUM0,        GRUB_KEYBOARD_KEY_NUMDOT, 
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
    /* 0x68 */ GRUB_KEYBOARD_KEY_RIGHT,       0,
    /* 0x6a */ 0,                             0,
    /* 0x6c */ 0,                             0,
    /* 0x6e */ 0,                             0,
    /* 0x70 */ 0,                             0,
    /* 0x72 */ 0,                             GRUB_KEYBOARD_KEY_JP_RO,
    /* 0x74 */ 0,                             0,
    /* 0x76 */ 0,                             0,
    /* 0x78 */ 0,                             0,
    /* 0x7a */ 0,                             0,
    /* 0x7c */ 0,                             GRUB_KEYBOARD_KEY_JP_YEN,
    /* 0x7e */ GRUB_KEYBOARD_KEY_KPCOMMA
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
    /* 0x50 */ 0,                             GRUB_KEYBOARD_KEY_JP_RO,
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
    /* 0x6a */ GRUB_KEYBOARD_KEY_JP_YEN,      GRUB_KEYBOARD_KEY_NUM4,
    /* 0x6c */ GRUB_KEYBOARD_KEY_NUM7,        GRUB_KEYBOARD_KEY_KPCOMMA,
    /* 0x6e */ 0,                             0,
    /* 0x70 */ GRUB_KEYBOARD_KEY_NUM0,        GRUB_KEYBOARD_KEY_NUMDOT,
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

static int
fetch_key (struct grub_ps2_state *ps2_state, grub_uint8_t at_key, int *is_break)
{
  int was_ext = 0;
  int ret = 0;

  /* May happen if no keyboard is connected. Just ignore this.  */
  if (at_key == 0xff)
    return -1;
  if (at_key == 0xe0)
    {
      ps2_state->e0_received = 1;
      return -1;
    }

  if ((ps2_state->current_set == 2 || ps2_state->current_set == 3) && at_key == 0xf0)
    {
      ps2_state->f0_received = 1;
      return -1;
    }

  /* Setting LEDs may generate ACKs.  */
  if (at_key == GRUB_AT_ACK)
    return -1;

  was_ext = ps2_state->e0_received;
  ps2_state->e0_received = 0;

  switch (ps2_state->current_set)
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
      *is_break = ps2_state->f0_received;
      ps2_state->f0_received = 0;
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
		      at_key, ps2_state->current_set);
      else
	grub_dprintf ("atkeyb", "Unknown key 0x%02x from set %d\n",
		      at_key, ps2_state->current_set);
      return -1;
    }
  return ret;
}

/* FIXME: This should become an interrupt service routine.  For now
   it's just used to catch events from control keys.  */
static int
grub_keyboard_isr (struct grub_ps2_state *ps2_state,
		   grub_keyboard_key_t key, int is_break)
{
  if (!is_break)
    switch (key)
      {
      case GRUB_KEYBOARD_KEY_LEFT_SHIFT:
	ps2_state->at_keyboard_status |= GRUB_TERM_STATUS_LSHIFT;
	return 1;
      case GRUB_KEYBOARD_KEY_RIGHT_SHIFT:
	ps2_state->at_keyboard_status |= GRUB_TERM_STATUS_RSHIFT;
	return 1;
      case GRUB_KEYBOARD_KEY_LEFT_CTRL:
	ps2_state->at_keyboard_status |= GRUB_TERM_STATUS_LCTRL;
	return 1;
      case GRUB_KEYBOARD_KEY_RIGHT_CTRL:
	ps2_state->at_keyboard_status |= GRUB_TERM_STATUS_RCTRL;
	return 1;
      case GRUB_KEYBOARD_KEY_RIGHT_ALT:
	ps2_state->at_keyboard_status |= GRUB_TERM_STATUS_RALT;
	return 1;
      case GRUB_KEYBOARD_KEY_LEFT_ALT:
	ps2_state->at_keyboard_status |= GRUB_TERM_STATUS_LALT;
	return 1;
      default:
	return 0;
      }
  else
    switch (key)
      {
      case GRUB_KEYBOARD_KEY_LEFT_SHIFT:
	ps2_state->at_keyboard_status &= ~GRUB_TERM_STATUS_LSHIFT;
	return 1;
      case GRUB_KEYBOARD_KEY_RIGHT_SHIFT:
	ps2_state->at_keyboard_status &= ~GRUB_TERM_STATUS_RSHIFT;
	return 1;
      case GRUB_KEYBOARD_KEY_LEFT_CTRL:
	ps2_state->at_keyboard_status &= ~GRUB_TERM_STATUS_LCTRL;
	return 1;
      case GRUB_KEYBOARD_KEY_RIGHT_CTRL:
	ps2_state->at_keyboard_status &= ~GRUB_TERM_STATUS_RCTRL;
	return 1;
      case GRUB_KEYBOARD_KEY_RIGHT_ALT:
	ps2_state->at_keyboard_status &= ~GRUB_TERM_STATUS_RALT;
	return 1;
      case GRUB_KEYBOARD_KEY_LEFT_ALT:
	ps2_state->at_keyboard_status &= ~GRUB_TERM_STATUS_LALT;
	return 1;
      default:
	return 0;
      }
}

/* If there is a key pending, return it; otherwise return GRUB_TERM_NO_KEY.  */
int
grub_ps2_process_incoming_byte (struct grub_ps2_state *ps2_state,
				grub_uint8_t at_key)
{
  int code;
  int is_break = 0;

  code = fetch_key (ps2_state, at_key, &is_break);
  if (code == -1)
    return GRUB_TERM_NO_KEY;

  if (grub_keyboard_isr (ps2_state, code, is_break))
    return GRUB_TERM_NO_KEY;
  if (is_break)
    return GRUB_TERM_NO_KEY;
#ifdef DEBUG_AT_KEYBOARD
  grub_dprintf ("atkeyb", "Detected key 0x%x\n", code);
#endif
  switch (code)
    {
      case GRUB_KEYBOARD_KEY_CAPS_LOCK:
	ps2_state->at_keyboard_status ^= GRUB_TERM_STATUS_CAPS;
	ps2_state->led_status ^= KEYBOARD_LED_CAPS;

#ifdef DEBUG_AT_KEYBOARD
	grub_dprintf ("atkeyb", "caps_lock = %d\n", !!(ps2_state->at_keyboard_status & GRUB_KEYBOARD_STATUS_CAPS_LOCK));
#endif
	return GRUB_TERM_NO_KEY;
      case GRUB_KEYBOARD_KEY_NUM_LOCK:
	ps2_state->at_keyboard_status ^= GRUB_TERM_STATUS_NUM;
	ps2_state->led_status ^= KEYBOARD_LED_NUM;

#ifdef DEBUG_AT_KEYBOARD
	grub_dprintf ("atkeyb", "num_lock = %d\n", !!(ps2_state->at_keyboard_status & GRUB_KEYBOARD_STATUS_NUM_LOCK));
#endif
	return GRUB_TERM_NO_KEY;
      case GRUB_KEYBOARD_KEY_SCROLL_LOCK:
	ps2_state->at_keyboard_status ^= GRUB_TERM_STATUS_SCROLL;
	ps2_state->led_status ^= KEYBOARD_LED_SCROLL;
	return GRUB_TERM_NO_KEY;
      default:
	return grub_term_map_key (code, ps2_state->at_keyboard_status);
    }
}
