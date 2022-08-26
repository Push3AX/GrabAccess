/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2003,2005,2007,2008,2009  Free Software Foundation, Inc.
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
#include <grub/err.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/env.h>
#include <grub/time.h>
#include <grub/dl.h>
#include <grub/keyboard_layouts.h>
#include <grub/command.h>
#include <grub/i18n.h>
#include <grub/file.h>

GRUB_MOD_LICENSE ("GPLv3+");

static struct grub_keyboard_layout layout_us = {
  .keyboard_map = {
    /* Keyboard errors. Handled by driver.  */
    /* 0x00 */   0,   0,   0,   0,

    /* 0x04 */ 'a',  'b',  'c',  'd', 
    /* 0x08 */ 'e',  'f',  'g',  'h',  'i', 'j', 'k', 'l',
    /* 0x10 */ 'm',  'n',  'o',  'p',  'q', 'r', 's', 't',
    /* 0x18 */ 'u',  'v',  'w',  'x',  'y', 'z', '1', '2',
    /* 0x20 */ '3',  '4',  '5',  '6',  '7', '8', '9', '0',
    /* 0x28 */ '\n', GRUB_TERM_ESC, GRUB_TERM_BACKSPACE, GRUB_TERM_TAB, ' ', '-', '=', '[',
    /* According to usage table 0x31 should be mapped to '/'
       but testing with real keyboard shows that 0x32 is remapped to '/'.
       Map 0x31 to 0. 
    */
    /* 0x30 */ ']',   0,   '\\', ';', '\'', '`', ',', '.',
    /* 0x39 is CapsLock. Handled by driver.  */
    /* 0x38 */ '/',   0,   GRUB_TERM_KEY_F1, GRUB_TERM_KEY_F2, 
    /* 0x3c */ GRUB_TERM_KEY_F3,     GRUB_TERM_KEY_F4,
    /* 0x3e */ GRUB_TERM_KEY_F5,     GRUB_TERM_KEY_F6,
    /* 0x40 */ GRUB_TERM_KEY_F7,     GRUB_TERM_KEY_F8,
    /* 0x42 */ GRUB_TERM_KEY_F9,     GRUB_TERM_KEY_F10,
    /* 0x44 */ GRUB_TERM_KEY_F11,    GRUB_TERM_KEY_F12,
    /* PrtScr and ScrollLock. Not handled yet.  */
    /* 0x46 */ 0,                    0,
    /* 0x48 is Pause. Not handled yet.  */
    /* 0x48 */ 0,                    GRUB_TERM_KEY_INSERT, 
    /* 0x4a */ GRUB_TERM_KEY_HOME,   GRUB_TERM_KEY_PPAGE,
    /* 0x4c */ GRUB_TERM_KEY_DC,     GRUB_TERM_KEY_END,
    /* 0x4e */ GRUB_TERM_KEY_NPAGE,  GRUB_TERM_KEY_RIGHT,
    /* 0x50 */ GRUB_TERM_KEY_LEFT,   GRUB_TERM_KEY_DOWN,
    /* 0x53 is NumLock. Handled by driver.  */
    /* 0x52 */ GRUB_TERM_KEY_UP,     0,
    /* 0x54 */ '/',                  '*', 
    /* 0x56 */ '-',                  '+',
    /* 0x58 */ '\n',                 GRUB_TERM_KEY_END, 
    /* 0x5a */ GRUB_TERM_KEY_DOWN,   GRUB_TERM_KEY_NPAGE,
    /* 0x5c */ GRUB_TERM_KEY_LEFT,   GRUB_TERM_KEY_CENTER,
    /* 0x5e */ GRUB_TERM_KEY_RIGHT,  GRUB_TERM_KEY_HOME,
    /* 0x60 */ GRUB_TERM_KEY_UP,     GRUB_TERM_KEY_PPAGE,
    /* 0x62 */ GRUB_TERM_KEY_INSERT, GRUB_TERM_KEY_DC,
    /* 0x64 */ '\\'
  },
  .keyboard_map_shift = {
    /* Keyboard errors. Handled by driver.  */
    /* 0x00 */   0,   0,   0,   0,

    /* 0x04 */ 'A',  'B',  'C',  'D', 
    /* 0x08 */ 'E',  'F',  'G',  'H',  'I', 'J', 'K', 'L',
    /* 0x10 */ 'M',  'N',  'O',  'P',  'Q', 'R', 'S', 'T',
    /* 0x18 */ 'U',  'V',  'W',  'X',  'Y', 'Z', '!', '@',
    /* 0x20 */ '#',  '$',  '%',  '^',  '&', '*', '(', ')',
    /* 0x28 */ '\n' | GRUB_TERM_SHIFT, GRUB_TERM_ESC | GRUB_TERM_SHIFT,
    /* 0x2a */ GRUB_TERM_BACKSPACE | GRUB_TERM_SHIFT, GRUB_TERM_TAB | GRUB_TERM_SHIFT,
    /* 0x2c */ ' '  | GRUB_TERM_SHIFT,  '_', '+', '{',
    /* According to usage table 0x31 should be mapped to '/'
       but testing with real keyboard shows that 0x32 is remapped to '/'.
       Map 0x31 to 0. 
    */
    /* 0x30 */ '}',  0,    '|',  ':',  '"', '~', '<', '>',
    /* 0x39 is CapsLock. Handled by driver.  */
    /* 0x38 */ '?',  0,
    /* 0x3a */ GRUB_TERM_KEY_F1 | GRUB_TERM_SHIFT,
    /* 0x3b */ GRUB_TERM_KEY_F2 | GRUB_TERM_SHIFT, 
    /* 0x3c */ GRUB_TERM_KEY_F3 | GRUB_TERM_SHIFT, 
    /* 0x3d */ GRUB_TERM_KEY_F4 | GRUB_TERM_SHIFT, 
    /* 0x3e */ GRUB_TERM_KEY_F5 | GRUB_TERM_SHIFT, 
    /* 0x3f */ GRUB_TERM_KEY_F6 | GRUB_TERM_SHIFT, 
    /* 0x40 */ GRUB_TERM_KEY_F7 | GRUB_TERM_SHIFT, 
    /* 0x41 */ GRUB_TERM_KEY_F8 | GRUB_TERM_SHIFT, 
    /* 0x42 */ GRUB_TERM_KEY_F9 | GRUB_TERM_SHIFT, 
    /* 0x43 */ GRUB_TERM_KEY_F10 | GRUB_TERM_SHIFT, 
    /* 0x44 */ GRUB_TERM_KEY_F11 | GRUB_TERM_SHIFT, 
    /* 0x45 */ GRUB_TERM_KEY_F12 | GRUB_TERM_SHIFT, 
    /* PrtScr and ScrollLock. Not handled yet.  */
    /* 0x46 */ 0,                    0,
    /* 0x48 is Pause. Not handled yet.  */
    /* 0x48 */ 0,                    GRUB_TERM_KEY_INSERT | GRUB_TERM_SHIFT, 
    /* 0x4a */ GRUB_TERM_KEY_HOME | GRUB_TERM_SHIFT,
    /* 0x4b */ GRUB_TERM_KEY_PPAGE | GRUB_TERM_SHIFT,
    /* 0x4c */ GRUB_TERM_KEY_DC | GRUB_TERM_SHIFT,
    /* 0x4d */ GRUB_TERM_KEY_END | GRUB_TERM_SHIFT,
    /* 0x4e */ GRUB_TERM_KEY_NPAGE | GRUB_TERM_SHIFT,
    /* 0x4f */ GRUB_TERM_KEY_RIGHT | GRUB_TERM_SHIFT,
    /* 0x50 */ GRUB_TERM_KEY_LEFT | GRUB_TERM_SHIFT,
    /* 0x51 */ GRUB_TERM_KEY_DOWN | GRUB_TERM_SHIFT,
    /* 0x53 is NumLock. Handled by driver.  */
    /* 0x52 */ GRUB_TERM_KEY_UP | GRUB_TERM_SHIFT,     0,
    /* 0x54 */ '/',                    '*', 
    /* 0x56 */ '-',                    '+',
    /* 0x58 */ '\n' | GRUB_TERM_SHIFT, '1', '2', '3', '4', '5','6', '7',
    /* 0x60 */ '8', '9', '0', '.', '|'
  }
};

static struct grub_keyboard_layout *grub_current_layout = &layout_us;

static int
map_key_core (int code, int status, int *alt_gr_consumed)
{
  *alt_gr_consumed = 0;

  if (code >= GRUB_KEYBOARD_LAYOUTS_ARRAY_SIZE)
    return 0;

  if (status & GRUB_TERM_STATUS_RALT)
    {
      if (status & (GRUB_TERM_STATUS_LSHIFT | GRUB_TERM_STATUS_RSHIFT))
	{
	  if (grub_current_layout->keyboard_map_shift_l3[code])
	    {
	      *alt_gr_consumed = 1;
	      return grub_current_layout->keyboard_map_shift_l3[code];
	    }
	}
      else if (grub_current_layout->keyboard_map_l3[code])
	{
	  *alt_gr_consumed = 1;
	  return grub_current_layout->keyboard_map_l3[code];  
	}
    }
  if (status & (GRUB_TERM_STATUS_LSHIFT | GRUB_TERM_STATUS_RSHIFT))
    return grub_current_layout->keyboard_map_shift[code];
  else
    return grub_current_layout->keyboard_map[code];
}

unsigned
grub_term_map_key (grub_keyboard_key_t code, int status)
{
  int alt_gr_consumed = 0;
  int key;

  if (code >= 0x59 && code <= 0x63 && (status & GRUB_TERM_STATUS_NUM))
    {
      if (status & (GRUB_TERM_STATUS_RSHIFT | GRUB_TERM_STATUS_LSHIFT))
	status &= ~(GRUB_TERM_STATUS_RSHIFT | GRUB_TERM_STATUS_LSHIFT);
      else
	status |= GRUB_TERM_STATUS_RSHIFT;
    }

  key = map_key_core (code, status, &alt_gr_consumed);
  
  if (key == 0 || key == GRUB_TERM_SHIFT) {
    grub_printf ("Unknown key 0x%x detected\n", code);
    return GRUB_TERM_NO_KEY;
  }
  
  if (status & GRUB_TERM_STATUS_CAPS)
    {
      if ((key >= 'a') && (key <= 'z'))
	key += 'A' - 'a';
      else if ((key >= 'A') && (key <= 'Z'))
	key += 'a' - 'A';
    }
  
  if ((status & GRUB_TERM_STATUS_LALT) || 
      ((status & GRUB_TERM_STATUS_RALT) && !alt_gr_consumed))
    key |= GRUB_TERM_ALT;
  if (status & (GRUB_TERM_STATUS_LCTRL | GRUB_TERM_STATUS_RCTRL))
    key |= GRUB_TERM_CTRL;

  return key;
}

static grub_err_t
grub_cmd_keymap (struct grub_command *cmd __attribute__ ((unused)),
		 int argc, char *argv[])
{
  char *filename;
  grub_file_t file;
  grub_uint32_t version;
  grub_uint8_t magic[GRUB_KEYBOARD_LAYOUTS_FILEMAGIC_SIZE];
  struct grub_keyboard_layout *newmap = NULL;
  unsigned i;

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "file or layout name required");
  if (argv[0][0] != '(' && argv[0][0] != '/' && argv[0][0] != '+')
    {
      const char *prefix = grub_env_get ("prefix");
      if (!prefix)
	return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("variable `%s' isn't set"), "prefix");	
      filename = grub_xasprintf ("%s/layouts/%s.gkb", prefix, argv[0]);
      if (!filename)
	return grub_errno;
    }
  else
    filename = argv[0];

  file = grub_file_open (filename, GRUB_FILE_TYPE_KEYBOARD_LAYOUT);
  if (! file)
    goto fail;

  if (grub_file_read (file, magic, sizeof (magic)) != sizeof (magic))
    {
      if (!grub_errno)
	grub_error (GRUB_ERR_BAD_ARGUMENT, N_("premature end of file %s"),
		    filename);
      goto fail;
    }

  if (grub_memcmp (magic, GRUB_KEYBOARD_LAYOUTS_FILEMAGIC,
		   GRUB_KEYBOARD_LAYOUTS_FILEMAGIC_SIZE) != 0)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid magic");
      goto fail;
    }

  if (grub_file_read (file, &version, sizeof (version)) != sizeof (version))
    {
      if (!grub_errno)
	grub_error (GRUB_ERR_BAD_ARGUMENT, N_("premature end of file %s"),
		    filename);
      goto fail;
    }

  if (version != grub_cpu_to_le32_compile_time (GRUB_KEYBOARD_LAYOUTS_VERSION))
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid version");
      goto fail;
    }

  newmap = grub_malloc (sizeof (*newmap));
  if (!newmap)
    goto fail;

  if (grub_file_read (file, newmap, sizeof (*newmap)) != sizeof (*newmap))
    {
      if (!grub_errno)
	grub_error (GRUB_ERR_BAD_ARGUMENT, N_("premature end of file %s"),
		    filename);
      goto fail;
    }

  for (i = 0; i < ARRAY_SIZE (newmap->keyboard_map); i++)
    newmap->keyboard_map[i] = grub_le_to_cpu32(newmap->keyboard_map[i]);

  for (i = 0; i < ARRAY_SIZE (newmap->keyboard_map_shift); i++)
    newmap->keyboard_map_shift[i]
      = grub_le_to_cpu32(newmap->keyboard_map_shift[i]);

  for (i = 0; i < ARRAY_SIZE (newmap->keyboard_map_l3); i++)
    newmap->keyboard_map_l3[i]
      = grub_le_to_cpu32(newmap->keyboard_map_l3[i]);

  for (i = 0; i < ARRAY_SIZE (newmap->keyboard_map_shift_l3); i++)
    newmap->keyboard_map_shift_l3[i]
      = grub_le_to_cpu32(newmap->keyboard_map_shift_l3[i]);

  grub_current_layout = newmap;

  return GRUB_ERR_NONE;

 fail:
  if (filename != argv[0])
    grub_free (filename);
  grub_free (newmap);
  if (file)
    grub_file_close (file);
  return grub_errno;
}

static grub_command_t cmd;

GRUB_MOD_INIT(keylayouts)
{
  cmd = grub_register_command ("keymap", grub_cmd_keymap,
			       0, N_("Load a keyboard layout."));
}

GRUB_MOD_FINI(keylayouts)
{
  grub_unregister_command (cmd);
}
