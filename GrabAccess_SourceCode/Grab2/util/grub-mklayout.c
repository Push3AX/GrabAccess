/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010 Free Software Foundation, Inc.
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

#include <config.h>

#include <grub/util/misc.h>
#include <grub/i18n.h>
#include <grub/term.h>
#include <grub/keyboard_layouts.h>

#define _GNU_SOURCE	1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <errno.h>

#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wmissing-declarations"

#include <argp.h>
#pragma GCC diagnostic error "-Wmissing-prototypes"
#pragma GCC diagnostic error "-Wmissing-declarations"

#include "progname.h"

struct arguments
{
  char *input;
  char *output;
  int verbosity;
};

static struct argp_option options[] = {
  {"input",  'i', N_("FILE"), 0,
   N_("set input filename. Default is STDIN"), 0},
  {"output",  'o', N_("FILE"), 0,
   N_("set output filename. Default is STDOUT"), 0},
  {"verbose",     'v', 0,      0, N_("print verbose messages."), 0},
  { 0, 0, 0, 0, 0, 0 }
};

struct console_grub_equivalence
{
  const char *layout;
  grub_uint32_t grub;
};

static struct console_grub_equivalence console_grub_equivalences_shift[] = {
  {"KP_0", '0'},
  {"KP_1", '1'},
  {"KP_2", '2'},
  {"KP_3", '3'},
  {"KP_4", '4'},
  {"KP_5", '5'},
  {"KP_6", '6'},
  {"KP_7", '7'},
  {"KP_8", '8'},
  {"KP_9", '9'},
  {"KP_Period", '.'},

  {NULL, '\0'}
};

static struct console_grub_equivalence console_grub_equivalences_unshift[] = {
  {"KP_0", GRUB_TERM_KEY_INSERT},
  {"KP_1", GRUB_TERM_KEY_END},
  {"KP_2", GRUB_TERM_KEY_DOWN},
  {"KP_3", GRUB_TERM_KEY_NPAGE},
  {"KP_4", GRUB_TERM_KEY_LEFT},
  {"KP_5", GRUB_TERM_KEY_CENTER},
  {"KP_6", GRUB_TERM_KEY_RIGHT},
  {"KP_7", GRUB_TERM_KEY_HOME},
  {"KP_8", GRUB_TERM_KEY_UP},
  {"KP_9", GRUB_TERM_KEY_PPAGE},
  {"KP_Period", GRUB_TERM_KEY_DC},

  {NULL, '\0'}
};

static struct console_grub_equivalence console_grub_equivalences_common[] = {
  {"Escape", GRUB_TERM_ESC},
  {"Tab", GRUB_TERM_TAB},
  {"Delete", GRUB_TERM_BACKSPACE},

  {"KP_Enter", '\n'},
  {"Return", '\n'},

  {"KP_Multiply", '*'},
  {"KP_Subtract", '-'},
  {"KP_Add", '+'},
  {"KP_Divide", '/'},

  {"F1", GRUB_TERM_KEY_F1},
  {"F2", GRUB_TERM_KEY_F2},
  {"F3", GRUB_TERM_KEY_F3},
  {"F4", GRUB_TERM_KEY_F4},
  {"F5", GRUB_TERM_KEY_F5},
  {"F6", GRUB_TERM_KEY_F6},
  {"F7", GRUB_TERM_KEY_F7},
  {"F8", GRUB_TERM_KEY_F8},
  {"F9", GRUB_TERM_KEY_F9},
  {"F10", GRUB_TERM_KEY_F10},
  {"F11", GRUB_TERM_KEY_F11},
  {"F12", GRUB_TERM_KEY_F12},
  {"F13", GRUB_TERM_KEY_F1 | GRUB_TERM_SHIFT},
  {"F14", GRUB_TERM_KEY_F2 | GRUB_TERM_SHIFT},
  {"F15", GRUB_TERM_KEY_F3 | GRUB_TERM_SHIFT},
  {"F16", GRUB_TERM_KEY_F4 | GRUB_TERM_SHIFT},
  {"F17", GRUB_TERM_KEY_F5 | GRUB_TERM_SHIFT},
  {"F18", GRUB_TERM_KEY_F6 | GRUB_TERM_SHIFT},
  {"F19", GRUB_TERM_KEY_F7 | GRUB_TERM_SHIFT},
  {"F20", GRUB_TERM_KEY_F8 | GRUB_TERM_SHIFT},
  {"F21", GRUB_TERM_KEY_F9 | GRUB_TERM_SHIFT},
  {"F22", GRUB_TERM_KEY_F10 | GRUB_TERM_SHIFT},
  {"F23", GRUB_TERM_KEY_F11 | GRUB_TERM_SHIFT},
  {"F24", GRUB_TERM_KEY_F12 | GRUB_TERM_SHIFT},
  {"Console_13", GRUB_TERM_KEY_F1 | GRUB_TERM_ALT},
  {"Console_14", GRUB_TERM_KEY_F2 | GRUB_TERM_ALT},
  {"Console_15", GRUB_TERM_KEY_F3 | GRUB_TERM_ALT},
  {"Console_16", GRUB_TERM_KEY_F4 | GRUB_TERM_ALT},
  {"Console_17", GRUB_TERM_KEY_F5 | GRUB_TERM_ALT},
  {"Console_18", GRUB_TERM_KEY_F6 | GRUB_TERM_ALT},
  {"Console_19", GRUB_TERM_KEY_F7 | GRUB_TERM_ALT},
  {"Console_20", GRUB_TERM_KEY_F8 | GRUB_TERM_ALT},
  {"Console_21", GRUB_TERM_KEY_F9 | GRUB_TERM_ALT},
  {"Console_22", GRUB_TERM_KEY_F10 | GRUB_TERM_ALT},
  {"Console_23", GRUB_TERM_KEY_F11 | GRUB_TERM_ALT},
  {"Console_24", GRUB_TERM_KEY_F12 | GRUB_TERM_ALT},
  {"Console_25", GRUB_TERM_KEY_F1 | GRUB_TERM_SHIFT | GRUB_TERM_ALT},
  {"Console_26", GRUB_TERM_KEY_F2 | GRUB_TERM_SHIFT | GRUB_TERM_ALT},
  {"Console_27", GRUB_TERM_KEY_F3 | GRUB_TERM_SHIFT | GRUB_TERM_ALT},
  {"Console_28", GRUB_TERM_KEY_F4 | GRUB_TERM_SHIFT | GRUB_TERM_ALT},
  {"Console_29", GRUB_TERM_KEY_F5 | GRUB_TERM_SHIFT | GRUB_TERM_ALT},
  {"Console_30", GRUB_TERM_KEY_F6 | GRUB_TERM_SHIFT | GRUB_TERM_ALT},
  {"Console_31", GRUB_TERM_KEY_F7 | GRUB_TERM_SHIFT | GRUB_TERM_ALT},
  {"Console_32", GRUB_TERM_KEY_F8 | GRUB_TERM_SHIFT | GRUB_TERM_ALT},
  {"Console_33", GRUB_TERM_KEY_F9 | GRUB_TERM_SHIFT | GRUB_TERM_ALT},
  {"Console_34", GRUB_TERM_KEY_F10 | GRUB_TERM_SHIFT | GRUB_TERM_ALT},
  {"Console_35", GRUB_TERM_KEY_F11 | GRUB_TERM_SHIFT | GRUB_TERM_ALT},
  {"Console_36", GRUB_TERM_KEY_F12 | GRUB_TERM_SHIFT | GRUB_TERM_ALT},

  {"Insert", GRUB_TERM_KEY_INSERT},
  {"Down", GRUB_TERM_KEY_DOWN},
  {"Up", GRUB_TERM_KEY_UP},
  {"Home", GRUB_TERM_KEY_HOME},
  {"End", GRUB_TERM_KEY_END},
  {"Right", GRUB_TERM_KEY_RIGHT},
  {"Left", GRUB_TERM_KEY_LEFT},
  {"Next", GRUB_TERM_KEY_NPAGE},
  {"Prior", GRUB_TERM_KEY_PPAGE},
  {"Remove", GRUB_TERM_KEY_DC},
  {"VoidSymbol", 0},

  /* "Undead" keys since no dead key support in GRUB.  */
  {"dead_acute", '\''},
  {"dead_circumflex", '^'},
  {"dead_grave", '`'},
  {"dead_tilde", '~'},
  {"dead_diaeresis", '"'},
  
  /* Following ones don't provide any useful symbols for shell.  */
  {"dead_cedilla", 0},
  {"dead_ogonek", 0},
  {"dead_caron", 0},
  {"dead_breve", 0},
  {"dead_doubleacute", 0},

  /* Unused in GRUB.  */
  {"Pause", 0},
  {"Scroll_Forward", 0},
  {"Scroll_Backward", 0},
  {"Hex_0", 0},
  {"Hex_1", 0},
  {"Hex_2", 0},
  {"Hex_3", 0},
  {"Hex_4", 0},
  {"Hex_5", 0},
  {"Hex_6", 0},
  {"Hex_7", 0},
  {"Hex_8", 0},
  {"Hex_9", 0},
  {"Hex_A", 0},
  {"Hex_B", 0},
  {"Hex_C", 0},
  {"Hex_D", 0},
  {"Hex_E", 0},
  {"Hex_F", 0},
  {"Scroll_Lock", 0},
  {"Show_Memory", 0},
  {"Show_Registers", 0},
  {"Control_backslash", 0},
  {"Compose", 0},

  {NULL, '\0'}
};

static grub_uint8_t linux_to_usb_map[128] = {
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
  /* 0x58 */ GRUB_KEYBOARD_KEY_F12,         GRUB_KEYBOARD_KEY_JP_RO,
  /* 0x5a */ 0,                             0,
  /* 0x5c */ 0,                             0,
  /* 0x5e */ 0,                             0,
  /* 0x60 */ GRUB_KEYBOARD_KEY_NUMENTER,    GRUB_KEYBOARD_KEY_RIGHT_CTRL,
  /* 0x62 */ GRUB_KEYBOARD_KEY_NUMSLASH,    0,
  /* 0x64 */ GRUB_KEYBOARD_KEY_RIGHT_ALT,   0,
  /* 0x66 */ GRUB_KEYBOARD_KEY_HOME,        GRUB_KEYBOARD_KEY_UP,
  /* 0x68 */ GRUB_KEYBOARD_KEY_PPAGE,       GRUB_KEYBOARD_KEY_LEFT,
  /* 0x6a */ GRUB_KEYBOARD_KEY_RIGHT,       GRUB_KEYBOARD_KEY_END,
  /* 0x6c */ GRUB_KEYBOARD_KEY_DOWN,        GRUB_KEYBOARD_KEY_NPAGE, 
  /* 0x6e */ GRUB_KEYBOARD_KEY_INSERT,      GRUB_KEYBOARD_KEY_DELETE,
  /* 0x70 */ 0,                             0,
  /* 0x72 */ 0,                             GRUB_KEYBOARD_KEY_JP_RO,
  /* 0x74 */ 0,                             0,
  /* 0x76 */ 0,                             0,
  /* 0x78 */ 0,                             GRUB_KEYBOARD_KEY_KPCOMMA,
  /* 0x7a */ 0,                             0,
  /* 0x7c */ GRUB_KEYBOARD_KEY_JP_YEN,
}; 

static void
add_special_keys (struct grub_keyboard_layout *layout)
{
  (void) layout;
}

static unsigned
lookup (char *code, int shift)
{
  int i;
  struct console_grub_equivalence *pr;

  if (shift)
    pr = console_grub_equivalences_shift;
  else
    pr =  console_grub_equivalences_unshift;

  for (i = 0; pr[i].layout != NULL; i++)
    if (strcmp (code, pr[i].layout) == 0)
      return pr[i].grub;

  for (i = 0; console_grub_equivalences_common[i].layout != NULL; i++)
    if (strcmp (code, console_grub_equivalences_common[i].layout) == 0)
      return console_grub_equivalences_common[i].grub;

  /* TRANSLATORS: scan identifier is keyboard key symbolic name.  */
  fprintf (stderr, _("Unknown keyboard scan identifier %s\n"), code);

  return '\0';
}

static unsigned int
get_grub_code (char *layout_code, int shift)
{
  unsigned int code;

  if (strncmp (layout_code, "U+", sizeof ("U+") - 1) == 0)
    sscanf (layout_code, "U+%x", &code);
  else if (strncmp (layout_code, "+U+", sizeof ("+U+") - 1) == 0)
    sscanf (layout_code, "+U+%x", &code);
  else
    code = lookup (layout_code, shift);
  return code;
}

static void
write_file (FILE *out, const char *fname, struct grub_keyboard_layout *layout)
{
  grub_uint32_t version;
  unsigned i;

  version = grub_cpu_to_le32_compile_time (GRUB_KEYBOARD_LAYOUTS_VERSION);
  
  for (i = 0; i < ARRAY_SIZE (layout->keyboard_map); i++)
    layout->keyboard_map[i] = grub_cpu_to_le32(layout->keyboard_map[i]);

  for (i = 0; i < ARRAY_SIZE (layout->keyboard_map_shift); i++)
    layout->keyboard_map_shift[i]
      = grub_cpu_to_le32(layout->keyboard_map_shift[i]);

  for (i = 0; i < ARRAY_SIZE (layout->keyboard_map_l3); i++)
    layout->keyboard_map_l3[i]
      = grub_cpu_to_le32(layout->keyboard_map_l3[i]);

  for (i = 0; i < ARRAY_SIZE (layout->keyboard_map_shift_l3); i++)
    layout->keyboard_map_shift_l3[i]
      = grub_cpu_to_le32(layout->keyboard_map_shift_l3[i]);

  if (fwrite (GRUB_KEYBOARD_LAYOUTS_FILEMAGIC, 1,
	      GRUB_KEYBOARD_LAYOUTS_FILEMAGIC_SIZE, out)
      != GRUB_KEYBOARD_LAYOUTS_FILEMAGIC_SIZE
      || fwrite (&version, sizeof (version), 1, out) != 1
      || fwrite (layout, 1, sizeof (*layout), out) != sizeof (*layout))
    {
      if (fname)
	grub_util_error ("cannot write to `%s': %s", fname, strerror (errno));
      else
	grub_util_error ("cannot write to the stdout: %s", strerror (errno));
    }
}

static void
write_keymaps (FILE *in, FILE *out, const char *out_filename)
{
  struct grub_keyboard_layout layout;
  char line[2048];
  int ok;

  memset (&layout, 0, sizeof (layout));

  /* Process the ckbcomp output and prepare the layouts.  */
  ok = 0;
  while (fgets (line, sizeof (line), in))
    {
      if (strncmp (line, "keycode", sizeof ("keycode") - 1) == 0)
	{
	  unsigned keycode_linux;
	  unsigned keycode_usb;
	  char normal[64];
	  char shift[64];
	  char normalalt[64];
	  char shiftalt[64];

	  sscanf (line, "keycode %u = %60s %60s %60s %60s", &keycode_linux,
		  normal, shift, normalalt, shiftalt);

	  if (keycode_linux >= ARRAY_SIZE (linux_to_usb_map))
	    {
	      /* TRANSLATORS: scan code is keyboard key numeric identifier.  */
	      fprintf (stderr, _("Unknown keyboard scan code 0x%02x\n"), keycode_linux);
	      continue;
	    }

	  /* Not used.  */
	  if (keycode_linux == 0x77 /* Pause */
	      /* Some obscure keys */
	      || keycode_linux == 0x63 || keycode_linux == 0x7d
	      || keycode_linux == 0x7e)
	    continue;

	  /* Not remappable.  */
	  if (keycode_linux == 0x1d /* Left CTRL */
	      || keycode_linux == 0x61 /* Right CTRL */
	      || keycode_linux == 0x2a /* Left Shift. */
	      || keycode_linux == 0x36 /* Right Shift. */
	      || keycode_linux == 0x38 /* Left ALT. */
	      || keycode_linux == 0x64 /* Right ALT. */
	      || keycode_linux == 0x3a /* CapsLock. */
	      || keycode_linux == 0x45 /* NumLock. */
	      || keycode_linux == 0x46 /* ScrollLock. */)
	    continue;

	  keycode_usb = linux_to_usb_map[keycode_linux];
	  if (keycode_usb == 0
	      || keycode_usb >= GRUB_KEYBOARD_LAYOUTS_ARRAY_SIZE)
	    {
	      /* TRANSLATORS: scan code is keyboard key numeric identifier.  */
	      fprintf (stderr, _("Unknown keyboard scan code 0x%02x\n"), keycode_linux);
	      continue;
	    }
	  if (keycode_usb < GRUB_KEYBOARD_LAYOUTS_ARRAY_SIZE)
	    {
	      layout.keyboard_map[keycode_usb] = get_grub_code (normal, 0);
	      layout.keyboard_map_shift[keycode_usb] = get_grub_code (shift, 1);
	      layout.keyboard_map_l3[keycode_usb]
		= get_grub_code (normalalt, 0);
	      layout.keyboard_map_shift_l3[keycode_usb]
		= get_grub_code (shiftalt, 1);
	      ok = 1;
	    }
	}
    }

  if (ok == 0)
    {
      /* TRANSLATORS: this error is triggered when input doesn't contain any
	 key descriptions.  */
      fprintf (stderr, "%s", _("ERROR: no valid keyboard layout found. Check the input.\n"));
      exit (1);
    }

  add_special_keys (&layout);

  write_file (out, out_filename, &layout);
}

static error_t
argp_parser (int key, char *arg, struct argp_state *state)
{
  /* Get the input argument from argp_parse, which we
     know is a pointer to our arguments structure. */
  struct arguments *arguments = state->input;

  switch (key)
    {
    case 'i':
      arguments->input = xstrdup (arg);
      break;

    case 'o':
      arguments->output = xstrdup (arg);
      break;

    case 'v':
      arguments->verbosity++;
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

static struct argp argp = {
  options, argp_parser, N_("[OPTIONS]"),
  /* TRANSLATORS: "one" is a shortcut for "keyboard layout".  */
  N_("Generate GRUB keyboard layout from Linux console one."),
  NULL, NULL, NULL
};

int
main (int argc, char *argv[])
{
  FILE *in, *out;
  struct arguments arguments;

  grub_util_host_init (&argc, &argv);

  /* Check for options.  */
  memset (&arguments, 0, sizeof (struct arguments));
  if (argp_parse (&argp, argc, argv, 0, 0, &arguments) != 0)
    {
      fprintf (stderr, "%s", _("Error in parsing command line arguments\n"));
      exit(1);
    }

  if (arguments.input)
    in = grub_util_fopen (arguments.input, "r");
  else
    in = stdin;

  if (!in)
    grub_util_error (_("cannot open `%s': %s"), arguments.input ? : "stdin",
		     strerror (errno));

  if (arguments.output)
    out = grub_util_fopen (arguments.output, "wb");
  else
    out = stdout;

  if (!out)
    {
      if (in != stdin)
	fclose (in);
      grub_util_error (_("cannot open `%s': %s"), arguments.output ? : "stdout",
		       strerror (errno));
    }

  write_keymaps (in, out, arguments.output);

  if (in != stdin)
    fclose (in);

  if (out != stdout)
    fclose (out);

  return 0;
}
