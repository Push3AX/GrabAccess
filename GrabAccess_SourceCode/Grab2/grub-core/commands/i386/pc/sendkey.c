/* sendkey.c - fake keystroke. */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009  Free Software Foundation, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/extcmd.h>
#include <grub/cpu/io.h>
#include <grub/loader.h>
#include <grub/i18n.h>
#include <grub/machine/kernel.h>

GRUB_MOD_LICENSE ("GPLv2+");

static char sendkey[0x20];
/* Length of sendkey.  */
static int keylen = 0;
static int noled = 0;
static const struct grub_arg_option options[] =
  {
    {"num", 'n', 0, N_("set numlock mode"), "[on|off]", ARG_TYPE_STRING},
    {"caps", 'c', 0, N_("set capslock mode"), "[on|off]", ARG_TYPE_STRING},
    {"scroll", 's', 0, N_("set scrolllock mode"), "[on|off]", ARG_TYPE_STRING},
    {"insert", 0, 0, N_("set insert mode"), "[on|off]", ARG_TYPE_STRING},
    {"pause", 0, 0, N_("set pause mode"), "[on|off]", ARG_TYPE_STRING},
    {"left-shift", 0, 0, N_("press left shift"), "[on|off]", ARG_TYPE_STRING},
    {"right-shift", 0, 0, N_("press right shift"), "[on|off]", ARG_TYPE_STRING},
    {"sysrq", 0, 0, N_("press SysRq"), "[on|off]", ARG_TYPE_STRING},
    {"numkey", 0, 0, N_("press NumLock key"), "[on|off]", ARG_TYPE_STRING},
    {"capskey", 0, 0, N_("press CapsLock key"), "[on|off]", ARG_TYPE_STRING},
    {"scrollkey", 0, 0, N_("press ScrollLock key"), "[on|off]", ARG_TYPE_STRING},
    {"insertkey", 0, 0, N_("press Insert key"), "[on|off]", ARG_TYPE_STRING},
    {"left-alt", 0, 0, N_("press left alt"), "[on|off]", ARG_TYPE_STRING},
    {"right-alt", 0, 0, N_("press right alt"), "[on|off]", ARG_TYPE_STRING},
    {"left-ctrl", 0, 0, N_("press left ctrl"), "[on|off]", ARG_TYPE_STRING},
    {"right-ctrl", 0, 0, N_("press right ctrl"), "[on|off]", ARG_TYPE_STRING},
    {"no-led", 0, 0, N_("don't update LED state"), 0, 0},
    {0, 0, 0, 0, 0, 0}
  };
static int simple_flag_offsets[] 
= {5, 6, 4, 7, 11, 1, 0, 10, 13, 14, 12, 15, 9, 3, 8, 2};

static grub_uint32_t andmask = 0xffffffff, ormask = 0;

struct 
keysym
{
  const char *unshifted_name;		/* the name in unshifted state */
  const char *shifted_name;		/* the name in shifted state */
  unsigned char unshifted_ascii;	/* the ascii code in unshifted state */
  unsigned char shifted_ascii;		/* the ascii code in shifted state */
  unsigned char keycode;		/* keyboard scancode */
};

/* The table for key symbols. If the "shifted" member of an entry is
   NULL, the entry does not have shifted state. Copied from GRUB Legacy setkey fuction  */
static struct keysym keysym_table[] =
{
  {"escape",		0,		0x1b,	0,	0x01},
  {"1",			"exclam",	'1',	'!',	0x02},
  {"2",			"at",		'2',	'@',	0x03},
  {"3",			"numbersign",	'3',	'#',	0x04},
  {"4",			"dollar",	'4',	'$',	0x05},
  {"5",			"percent",	'5',	'%',	0x06},
  {"6",			"caret",	'6',	'^',	0x07},
  {"7",			"ampersand",	'7',	'&',	0x08},
  {"8",			"asterisk",	'8',	'*',	0x09},
  {"9",			"parenleft",	'9',	'(',	0x0a},
  {"0",			"parenright",	'0',	')',	0x0b},
  {"minus",		"underscore",	'-',	'_',	0x0c},
  {"equal",		"plus",		'=',	'+',	0x0d},
  {"backspace",		0,		'\b',	0,	0x0e},
  {"tab",		0,		'\t',	0,	0x0f},
  {"q",			"Q",		'q',	'Q',	0x10},
  {"w",			"W",		'w',	'W',	0x11},
  {"e",			"E",		'e',	'E',	0x12},
  {"r",			"R",		'r',	'R',	0x13},
  {"t",			"T",		't',	'T',	0x14},
  {"y",			"Y",		'y',	'Y',	0x15},
  {"u",			"U",		'u',	'U',	0x16},
  {"i",			"I",		'i',	'I',	0x17},
  {"o",			"O",		'o',	'O',	0x18},
  {"p",			"P",		'p',	'P',	0x19},
  {"bracketleft",	"braceleft",	'[',	'{',	0x1a},
  {"bracketright",	"braceright",	']',	'}',	0x1b},
  {"enter",		0,		'\r',	0,	0x1c},
  {"control",		0,		0,	0,	0x1d},
  {"a",			"A",		'a',	'A',	0x1e},
  {"s",			"S",		's',	'S',	0x1f},
  {"d",			"D",		'd',	'D',	0x20},
  {"f",			"F",		'f',	'F',	0x21},
  {"g",			"G",		'g',	'G',	0x22},
  {"h",			"H",		'h',	'H',	0x23},
  {"j",			"J",		'j',	'J',	0x24},
  {"k",			"K",		'k',	'K',	0x25},
  {"l",			"L",		'l',	'L',	0x26},
  {"semicolon",		"colon",	';',	':',	0x27},
  {"quote",		"doublequote",	'\'',	'"',	0x28},
  {"backquote",		"tilde",	'`',	'~',	0x29},
  {"shift",		0,		0,	0,	0x2a},
  {"backslash",		"bar",		'\\',	'|',	0x2b},
  {"z",			"Z",		'z',	'Z',	0x2c},
  {"x",			"X",		'x',	'X',	0x2d},
  {"c",			"C",		'c',	'C',	0x2e},
  {"v",			"V",		'v',	'V',	0x2f},
  {"b",			"B",		'b',	'B',	0x30},
  {"n",			"N",		'n',	'N',	0x31},
  {"m",			"M",		'm',	'M',	0x32},
  {"comma",		"less",		',',	'<',	0x33},
  {"period",		"greater",	'.',	'>',	0x34},
  {"slash",		"question",	'/',	'?',	0x35},
  {"rshift",		0,		0,	0,	0x36},
  {"numasterisk",		0,		'*',	0,	0x37},
  {"alt",		0,		0,	0,	0x38},
  {"space",		0,		' ',	0,	0x39},
  {"capslock",		0,		0,	0,	0x3a},
  {"F1",		0,		0,	0,	0x3b},
  {"F2",		0,		0,	0,	0x3c},
  {"F3",		0,		0,	0,	0x3d},
  {"F4",		0,		0,	0,	0x3e},
  {"F5",		0,		0,	0,	0x3f},
  {"F6",	 	0,		0,	0,	0x40},
  {"F7",		0,		0,	0,	0x41},
  {"F8",		0,		0,	0,	0x42},
  {"F9",		0,		0,	0,	0x43},
  {"F10",		0,		0,	0,	0x44},
  {"num7",		"numhome",		'7',	0,	0x47},
  {"num8",		"numup",		'8',	0,	0x48},
  {"num9",		"numpgup",		'9',	0,	0x49},
  {"numminus",		0,		'-',	0,	0x4a},
  {"num4",		"numleft",		'4',	0,	0x4b},
  {"num5",		"numcenter",		'5',	0,	0x4c},
  {"num6",		"numright",		'6',	0,	0x4d},
  {"numplus",		0,		'-',	0,	0x4e},
  {"num1",		"numend",		'1',	0,	0x4f},
  {"num2",		"numdown",		'2',	0,	0x50},
  {"num3",		"numpgdown",		'3',	0,	0x51},
  {"num0",		"numinsert",		'0',	0,	0x52},
  {"numperiod",	"numdelete", 0,	0x7f,		0x53},
  {"F11",		0,		0,	0,	0x57},
  {"F12",		0,		0,	0,	0x58},
  {"numenter",		0,		'\r',	0,	0xe0},
  {"numslash",		0,		'/',	0,	0xe0},
  {"delete",		0,		0x7f,	0,	0xe0},
  {"insert",		0,		0xe0,	0,	0x52},
  {"home",		0,		0xe0,	0,	0x47},
  {"end",		0,		0xe0,	0,	0x4f},
  {"pgdown",		0,		0xe0,	0,	0x51},
  {"pgup",		0,		0xe0,	0,	0x49},
  {"down",		0,		0xe0,	0,	0x50},
  {"up",		0,		0xe0,	0,	0x48},
  {"left",		0,		0xe0,	0,	0x4b},
  {"right",		0,		0xe0,	0,	0x4d}
};

/* Set a simple flag in flags variable  
   OUTOFFSET - offset of flag in FLAGS,
   OP - action id
*/
static void
grub_sendkey_set_simple_flag (int outoffset, int op)
{      
  if (op == 2)
    {
      andmask |= (1 << outoffset);
      ormask &= ~(1 << outoffset);
    }
  else
    {
      andmask &= (~(1 << outoffset));
      if (op == 1)
	ormask |= (1 << outoffset);
      else
	ormask &= ~(1 << outoffset);
    }
}

static int
grub_sendkey_parse_op (struct grub_arg_list state)
{
  if (! state.set)
    return 2;

  if (grub_strcmp (state.arg, "off") == 0 || grub_strcmp (state.arg, "0") == 0 
      || grub_strcmp (state.arg, "unpress") == 0)
    return 0;

  if (grub_strcmp (state.arg, "on")  == 0 || grub_strcmp (state.arg, "1")  == 0
      || grub_strcmp (state.arg, "press") == 0)
    return 1;

  return 2;
}

static grub_uint32_t oldflags;

static grub_err_t
grub_sendkey_postboot (void)
{
  /* For convention: pointer to flags.  */
  grub_uint32_t *flags = (grub_uint32_t *) 0x417;

  *flags = oldflags;

  *((char *) 0x41a) = 0x1e;
  *((char *) 0x41c) = 0x1e;

  return GRUB_ERR_NONE;
}

/* Set keyboard buffer to our sendkey  */
static grub_err_t
grub_sendkey_preboot (int noret __attribute__ ((unused)))
{
  /* For convention: pointer to flags.  */
  grub_uint32_t *flags = (grub_uint32_t *) 0x417;

  oldflags = *flags;
  
  /* Set the sendkey.  */
  *((char *) 0x41a) = 0x1e;
  *((char *) 0x41c) = keylen + 0x1e;
  grub_memcpy ((char *) 0x41e, sendkey, 0x20);

  /* Transform "any ctrl" to "right ctrl" flag.  */
  if (*flags & (1 << 8))
    *flags &= ~(1 << 2);

  /* Transform "any alt" to "right alt" flag.  */
  if (*flags & (1 << 9))
    *flags &= ~(1 << 3);
  
  *flags = (*flags & andmask) | ormask;

  /* Transform "right ctrl" to "any ctrl" flag.  */
  if (*flags & (1 << 8))
    *flags |= (1 << 2);

  /* Transform "right alt" to "any alt" flag.  */
  if (*flags & (1 << 9))
    *flags |= (1 << 3);

  /* Write new LED state  */
  if (!noled)
    {
      int value = 0;
      int failed;
      /* Try 5 times  */
      for (failed = 0; failed < 5; failed++)
	{
	  value = 0;
	  /* Send command change LEDs  */
	  grub_outb (0xed, 0x60);

	  /* Wait */
	  do
	    value = grub_inb (0x60);
	  while ((value != 0xfa) && (value != 0xfe));

	  if (value == 0xfa)
	    {
	      /* Set new LEDs*/
	      grub_outb ((*flags >> 4) & 7, 0x60);
	      break;
	    }
	}
    }
  return GRUB_ERR_NONE;
}

/* Helper for grub_cmd_sendkey.  */
static int
find_key_code (char *key)
{
  unsigned i;

  for (i = 0; i < ARRAY_SIZE(keysym_table); i++)
    {
      if (keysym_table[i].unshifted_name 
	  && grub_strcmp (key, keysym_table[i].unshifted_name) == 0)
	return keysym_table[i].keycode;
      else if (keysym_table[i].shifted_name 
	       && grub_strcmp (key, keysym_table[i].shifted_name) == 0)
	return keysym_table[i].keycode;
    }

  return 0;
}

/* Helper for grub_cmd_sendkey.  */
static int
find_ascii_code (char *key)
{
  unsigned i;

  for (i = 0; i < ARRAY_SIZE(keysym_table); i++)
    {
      if (keysym_table[i].unshifted_name 
	  && grub_strcmp (key, keysym_table[i].unshifted_name) == 0)
	return keysym_table[i].unshifted_ascii;
      else if (keysym_table[i].shifted_name 
	       && grub_strcmp (key, keysym_table[i].shifted_name) == 0)
	return keysym_table[i].shifted_ascii;
    }

  return 0;
}

static grub_err_t
grub_cmd_sendkey (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;

  andmask = 0xffffffff;
  ormask = 0;

  {
    int i;

    keylen = 0;

    for (i = 0; i < argc && keylen < 0x20; i++)
      {
	int key_code;
	
	key_code = find_key_code (args[i]);
	if (key_code)
	  {
	    sendkey[keylen++] = find_ascii_code (args[i]);
	    sendkey[keylen++] = key_code;
	  }
      }
  }

  {
    unsigned i;
    for (i = 0; i < ARRAY_SIZE(simple_flag_offsets); i++)
      grub_sendkey_set_simple_flag (simple_flag_offsets[i], 
				    grub_sendkey_parse_op(state[i]));
  }

  /* Set noled. */
  noled = (state[ARRAY_SIZE(simple_flag_offsets)].set);

  return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd;
static struct grub_preboot *preboot_hook;

GRUB_MOD_INIT (sendkey)
{
#ifdef GRUB_MACHINE_MULTIBOOT
  if (!grub_mb_check_bios_int (0x16))
    return;
#endif
  cmd = grub_register_extcmd ("sendkey", grub_cmd_sendkey, 0,
			      N_("[KEYSTROKE1] [KEYSTROKE2] ..."),
			      /* TRANSLATORS: It can emulate multiple
				 keypresses.  */
			      N_("Emulate a keystroke sequence"), options);

  preboot_hook 
    = grub_loader_register_preboot_hook (grub_sendkey_preboot, 
					 grub_sendkey_postboot,
					 GRUB_LOADER_PREBOOT_HOOK_PRIO_CONSOLE);
}

GRUB_MOD_FINI (sendkey)
{
#ifdef GRUB_MACHINE_MULTIBOOT
  if (!grub_mb_check_bios_int (0x16))
    return;
#endif
  grub_unregister_extcmd (cmd);
  grub_loader_unregister_preboot_hook (preboot_hook);
}
