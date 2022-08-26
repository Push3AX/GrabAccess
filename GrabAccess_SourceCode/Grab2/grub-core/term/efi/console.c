/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2008  Free Software Foundation, Inc.
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

#include <grub/term.h>
#include <grub/misc.h>
#include <grub/types.h>
#include <grub/err.h>
#include <grub/efi/efi.h>
#include <grub/efi/api.h>
#include <grub/efi/console.h>

typedef enum {
    GRUB_TEXT_MODE_UNDEFINED = -1,
    GRUB_TEXT_MODE_UNAVAILABLE = 0,
    GRUB_TEXT_MODE_AVAILABLE
}
grub_text_mode;

static grub_text_mode text_mode = GRUB_TEXT_MODE_UNDEFINED;
static grub_term_color_state text_colorstate = GRUB_TERM_COLOR_UNDEFINED;

static grub_uint32_t
map_char (grub_uint32_t c)
{
  /* Map some unicode characters to the EFI character.  */
  switch (c)
    {
    case GRUB_UNICODE_LEFTARROW:
      c = GRUB_UNICODE_BLACK_LEFT_TRIANGLE;
      break;
    case GRUB_UNICODE_UPARROW:
      c = GRUB_UNICODE_BLACK_UP_TRIANGLE;
      break;
    case GRUB_UNICODE_RIGHTARROW:
      c = GRUB_UNICODE_BLACK_RIGHT_TRIANGLE;
      break;
    case GRUB_UNICODE_DOWNARROW:
      c = GRUB_UNICODE_BLACK_DOWN_TRIANGLE;
      break;
    case GRUB_UNICODE_HLINE:
      c = GRUB_UNICODE_LIGHT_HLINE;
      break;
    case GRUB_UNICODE_VLINE:
      c = GRUB_UNICODE_LIGHT_VLINE;
      break;
    case GRUB_UNICODE_CORNER_UL:
      c = GRUB_UNICODE_LIGHT_CORNER_UL;
      break;
    case GRUB_UNICODE_CORNER_UR:
      c = GRUB_UNICODE_LIGHT_CORNER_UR;
      break;
    case GRUB_UNICODE_CORNER_LL:
      c = GRUB_UNICODE_LIGHT_CORNER_LL;
      break;
    case GRUB_UNICODE_CORNER_LR:
      c = GRUB_UNICODE_LIGHT_CORNER_LR;
      break;
    }

  return c;
}

static void
grub_console_setcolorstate (struct grub_term_output *term
			    __attribute__ ((unused)),
			    grub_term_color_state state)
{
  grub_efi_simple_text_output_interface_t *o;

  if (grub_efi_is_finished)
    return;

  o = grub_efi_system_table->con_out;

  switch (state) {
    case GRUB_TERM_COLOR_STANDARD:
      efi_call_2 (o->set_attributes, o, GRUB_TERM_DEFAULT_STANDARD_COLOR
		  & 0x7f);
      break;
    case GRUB_TERM_COLOR_NORMAL:
      efi_call_2 (o->set_attributes, o, grub_term_normal_color & 0x7f);
      break;
    case GRUB_TERM_COLOR_HIGHLIGHT:
      efi_call_2 (o->set_attributes, o, grub_term_highlight_color & 0x7f);
      break;
    default:
      break;
  }
}

static void
grub_console_setcursor (struct grub_term_output *term __attribute__ ((unused)),
			int on)
{
  grub_efi_simple_text_output_interface_t *o;

  if (grub_efi_is_finished)
    return;

  o = grub_efi_system_table->con_out;
  efi_call_2 (o->enable_cursor, o, on);
}

static grub_err_t
grub_prepare_for_text_output (struct grub_term_output *term)
{
  if (grub_efi_is_finished)
    return GRUB_ERR_BAD_DEVICE;

  if (text_mode != GRUB_TEXT_MODE_UNDEFINED)
    return text_mode ? GRUB_ERR_NONE : GRUB_ERR_BAD_DEVICE;

  if (! grub_efi_set_text_mode (1))
    {
      /* This really should never happen */
      grub_error (GRUB_ERR_BAD_DEVICE, "cannot set text mode");
      text_mode = GRUB_TEXT_MODE_UNAVAILABLE;
      return GRUB_ERR_BAD_DEVICE;
    }

  grub_console_setcursor (term, 1);
  if (text_colorstate != GRUB_TERM_COLOR_UNDEFINED)
    grub_console_setcolorstate (term, text_colorstate);
  text_mode = GRUB_TEXT_MODE_AVAILABLE;
  return GRUB_ERR_NONE;
}

static void
grub_console_putchar (struct grub_term_output *term,
		      const struct grub_unicode_glyph *c)
{
  grub_efi_char16_t str[2 + 30];
  grub_efi_simple_text_output_interface_t *o;
  unsigned i, j;

  if (grub_prepare_for_text_output (term) != GRUB_ERR_NONE)
    return;

  o = grub_efi_system_table->con_out;

  /* For now, do not try to use a surrogate pair.  */
  if (c->base > 0xffff)
    str[0] = '?';
  else
    str[0] = (grub_efi_char16_t)  map_char (c->base & 0xffff);
  j = 1;
  for (i = 0; i < c->ncomb && j + 1 < ARRAY_SIZE (str); i++)
    if (c->base < 0xffff)
      str[j++] = grub_unicode_get_comb (c)[i].code;
  str[j] = 0;

  /* Should this test be cached?  */
  if ((c->base > 0x7f || c->ncomb)
      && efi_call_2 (o->test_string, o, str) != GRUB_EFI_SUCCESS)
    return;

  efi_call_2 (o->output_string, o, str);
}

const unsigned efi_codes[] =
  {
    0, GRUB_TERM_KEY_UP, GRUB_TERM_KEY_DOWN, GRUB_TERM_KEY_RIGHT,
    GRUB_TERM_KEY_LEFT, GRUB_TERM_KEY_HOME, GRUB_TERM_KEY_END, GRUB_TERM_KEY_INSERT,
    GRUB_TERM_KEY_DC, GRUB_TERM_KEY_PPAGE, GRUB_TERM_KEY_NPAGE, GRUB_TERM_KEY_F1,
    GRUB_TERM_KEY_F2, GRUB_TERM_KEY_F3, GRUB_TERM_KEY_F4, GRUB_TERM_KEY_F5,
    GRUB_TERM_KEY_F6, GRUB_TERM_KEY_F7, GRUB_TERM_KEY_F8, GRUB_TERM_KEY_F9,
    GRUB_TERM_KEY_F10, GRUB_TERM_KEY_F11, GRUB_TERM_KEY_F12, GRUB_TERM_ESC
  };

static int
grub_efi_translate_key (grub_efi_input_key_t key)
{
  if (key.scan_code == 0)
    {
      /* Some firmware implementations use VT100-style codes against the spec.
	 This is especially likely if driven by serial.
       */
      if (key.unicode_char < 0x20 && key.unicode_char != 0
	  && key.unicode_char != '\t' && key.unicode_char != '\b'
	  && key.unicode_char != '\n' && key.unicode_char != '\r')
	return GRUB_TERM_CTRL | (key.unicode_char - 1 + 'a');
      else
	return key.unicode_char;
    }
  /* Some devices send enter with scan_code 0x0d (F3) and unicode_char 0x0d. */
  else if (key.scan_code == '\r' && key.unicode_char == '\r')
    return key.unicode_char;
  else if (key.scan_code < ARRAY_SIZE (efi_codes))
    return efi_codes[key.scan_code];

  if ((key.unicode_char >= 0x20 && key.unicode_char <= 0x7f)
      || key.unicode_char == '\t' || key.unicode_char == '\b'
      || key.unicode_char == '\n' || key.unicode_char == '\r')
    return key.unicode_char;

  return GRUB_TERM_NO_KEY;
}

static int
grub_console_getkey_con (struct grub_term_input *term __attribute__ ((unused)))
{
  grub_efi_simple_input_interface_t *i;
  grub_efi_input_key_t key;
  grub_efi_status_t status;

  i = grub_efi_system_table->con_in;
  status = efi_call_2 (i->read_key_stroke, i, &key);

  if (status != GRUB_EFI_SUCCESS)
    return GRUB_TERM_NO_KEY;

  return grub_efi_translate_key(key);
}

/*
 * When more then just modifiers are pressed, our getkeystatus() consumes a
 * press from the queue, this function buffers the press for the regular
 * getkey() so that it does not get lost.
 */
static grub_err_t
grub_console_read_key_stroke (
                   grub_efi_simple_text_input_ex_interface_t *text_input,
                   grub_efi_key_data_t *key_data_ret, int *key_ret,
                   int consume)
{
  static grub_efi_key_data_t key_data;
  grub_efi_status_t status;
  int key;

  if (!text_input)
    return GRUB_ERR_EOF;

  key = grub_efi_translate_key (key_data.key);
  if (key == GRUB_TERM_NO_KEY)
  {
    status = efi_call_2 (text_input->read_key_stroke, text_input, &key_data);
    if (status != GRUB_EFI_SUCCESS)
      return GRUB_ERR_EOF;

    key = grub_efi_translate_key (key_data.key);
  }

  *key_data_ret = key_data;
  *key_ret = key;

  if (consume)
  {
    key_data.key.scan_code = 0;
    key_data.key.unicode_char = 0;
  }

  return GRUB_ERR_NONE;
}

static int
grub_console_getkey_ex (struct grub_term_input *term)
{
  grub_efi_key_data_t key_data;
  grub_efi_uint32_t kss;
  grub_err_t err;
  int key = -1;

  err = grub_console_read_key_stroke (term->data, &key_data, &key, 1);
  if (err != GRUB_ERR_NONE || key == GRUB_TERM_NO_KEY)
    return GRUB_TERM_NO_KEY;

  kss = key_data.key_state.key_shift_state;
  if (kss & GRUB_EFI_SHIFT_STATE_VALID)
    {
      if ((kss & GRUB_EFI_LEFT_SHIFT_PRESSED
	   || kss & GRUB_EFI_RIGHT_SHIFT_PRESSED)
	  && (key & GRUB_TERM_EXTENDED))
	key |= GRUB_TERM_SHIFT;
      if (kss & GRUB_EFI_LEFT_ALT_PRESSED || kss & GRUB_EFI_RIGHT_ALT_PRESSED)
	key |= GRUB_TERM_ALT;
      if (kss & GRUB_EFI_LEFT_CONTROL_PRESSED
	  || kss & GRUB_EFI_RIGHT_CONTROL_PRESSED)
	key |= GRUB_TERM_CTRL;
    }

  return key;
}

static int
grub_console_getkeystatus (struct grub_term_input *term)
{
  grub_efi_key_data_t key_data;
  grub_efi_uint32_t kss;
  int key, mods = 0;

  if (grub_efi_is_finished)
    return 0;

  if (grub_console_read_key_stroke (term->data, &key_data, &key, 0))
    return 0;

  kss = key_data.key_state.key_shift_state;
  if (kss & GRUB_EFI_SHIFT_STATE_VALID)
    {
      if (kss & GRUB_EFI_LEFT_SHIFT_PRESSED)
        mods |= GRUB_TERM_STATUS_LSHIFT;
      if (kss & GRUB_EFI_RIGHT_SHIFT_PRESSED)
        mods |= GRUB_TERM_STATUS_RSHIFT;
      if (kss & GRUB_EFI_LEFT_ALT_PRESSED)
        mods |= GRUB_TERM_STATUS_LALT;
      if (kss & GRUB_EFI_RIGHT_ALT_PRESSED)
        mods |= GRUB_TERM_STATUS_RALT;
      if (kss & GRUB_EFI_LEFT_CONTROL_PRESSED)
        mods |= GRUB_TERM_STATUS_LCTRL;
      if (kss & GRUB_EFI_RIGHT_CONTROL_PRESSED)
        mods |= GRUB_TERM_STATUS_RCTRL;
    }

  return mods;
}

static grub_err_t
grub_efi_console_input_init (struct grub_term_input *term)
{
  grub_efi_guid_t text_input_ex_guid =
    GRUB_EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;

  if (grub_efi_is_finished)
    return 0;

  grub_efi_simple_text_input_ex_interface_t *text_input = term->data;
  if (text_input)
    return 0;

  text_input = grub_efi_open_protocol(grub_efi_system_table->console_in_handler,
				      &text_input_ex_guid,
				      GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
  term->data = (void *)text_input;

  return 0;
}

static int
grub_console_getkey (struct grub_term_input *term)
{
  if (grub_efi_is_finished)
    return 0;

  if (term->data)
    return grub_console_getkey_ex(term);
  else
    return grub_console_getkey_con(term);
}

static struct grub_term_coordinate
grub_console_getwh (struct grub_term_output *term)
{
  grub_efi_simple_text_output_interface_t *o;
  grub_efi_uintn_t columns, rows;

  o = grub_efi_system_table->con_out;
  if (grub_prepare_for_text_output (term) != GRUB_ERR_NONE ||
      efi_call_4 (o->query_mode, o, o->mode->mode,
		  &columns, &rows) != GRUB_EFI_SUCCESS)
    {
      /* Why does this fail?  */
      columns = 80;
      rows = 25;
    }

  return (struct grub_term_coordinate) { columns, rows };
}

static struct grub_term_coordinate
grub_console_getxy (struct grub_term_output *term __attribute__ ((unused)))
{
  grub_efi_simple_text_output_interface_t *o;

  if (grub_efi_is_finished || text_mode != GRUB_TEXT_MODE_AVAILABLE)
    return (struct grub_term_coordinate) { 0, 0 };

  o = grub_efi_system_table->con_out;
  return (struct grub_term_coordinate) { o->mode->cursor_column, o->mode->cursor_row };
}

static void
grub_console_gotoxy (struct grub_term_output *term,
		     struct grub_term_coordinate pos)
{
  grub_efi_simple_text_output_interface_t *o;

  if (grub_prepare_for_text_output (term) != GRUB_ERR_NONE)
    return;

  o = grub_efi_system_table->con_out;
  efi_call_3 (o->set_cursor_position, o, pos.x, pos.y);
}

static void
grub_console_cls (struct grub_term_output *term __attribute__ ((unused)))
{
  grub_efi_simple_text_output_interface_t *o;
  grub_efi_int32_t orig_attr;

  if (grub_efi_is_finished || text_mode != GRUB_TEXT_MODE_AVAILABLE)
    return;

  o = grub_efi_system_table->con_out;
  orig_attr = o->mode->attribute;
  efi_call_2 (o->set_attributes, o, GRUB_EFI_BACKGROUND_BLACK);
  efi_call_1 (o->clear_screen, o);
  efi_call_2 (o->set_attributes, o, orig_attr);
}

static grub_err_t
grub_efi_console_output_fini (struct grub_term_output *term)
{
  if (text_mode != GRUB_TEXT_MODE_AVAILABLE)
    return 0;

  grub_console_setcursor (term, 0);
  grub_efi_set_text_mode (0);
  text_mode = GRUB_TEXT_MODE_UNDEFINED;
  return 0;
}

static struct grub_term_input grub_console_term_input =
  {
    .name = "console",
    .getkey = grub_console_getkey,
    .getkeystatus = grub_console_getkeystatus,
    .init = grub_efi_console_input_init,
  };

static struct grub_term_output grub_console_term_output =
  {
    .name = "console",
    .fini = grub_efi_console_output_fini,
    .putchar = grub_console_putchar,
    .getwh = grub_console_getwh,
    .getxy = grub_console_getxy,
    .gotoxy = grub_console_gotoxy,
    .cls = grub_console_cls,
    .setcolorstate = grub_console_setcolorstate,
    .setcursor = grub_console_setcursor,
    .flags = GRUB_TERM_CODE_TYPE_VISUAL_GLYPHS,
    .progress_update_divisor = GRUB_PROGRESS_FAST
  };

void
grub_console_init (void)
{
  grub_term_register_output ("console", &grub_console_term_output);
  grub_term_register_input ("console", &grub_console_term_input);
}

void
grub_console_fini (void)
{
  grub_term_unregister_input (&grub_console_term_input);
  grub_term_unregister_output (&grub_console_term_output);
}
