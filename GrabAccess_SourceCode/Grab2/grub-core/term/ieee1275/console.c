/*  console.c -- Open Firmware console for GRUB.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2003,2004,2005,2007,2008,2009  Free Software Foundation, Inc.
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
#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/time.h>
#include <grub/terminfo.h>
#include <grub/ieee1275/console.h>
#include <grub/ieee1275/ieee1275.h>

static grub_ieee1275_ihandle_t stdout_ihandle;
static grub_ieee1275_ihandle_t stdin_ihandle;

extern struct grub_terminfo_output_state grub_console_terminfo_output;

struct color
{
  int red;
  int green;
  int blue;
};

/* Use serial colors as they are default on most firmwares and some firmwares
   ignore set-color!. Additionally output may be redirected to serial.  */
static struct color colors[] =
  {
    // {R, G, B}
    {0x00, 0x00, 0x00}, // 0 = black
    {0xA8, 0x00, 0x00}, // 1 = red
    {0x00, 0xA8, 0x00}, // 2 = green
    {0xFE, 0xFE, 0x54}, // 3 = yellow
    {0x00, 0x00, 0xA8}, // 4 = blue
    {0xA8, 0x00, 0xA8}, // 5 = magenta
    {0x00, 0xA8, 0xA8}, // 6 = cyan
    {0xFE, 0xFE, 0xFE}  // 7 = white
  };

static void
put (struct grub_term_output *term __attribute__ ((unused)), const int c)
{
  char chr = c;

  grub_ieee1275_write (stdout_ihandle, &chr, 1, 0);
}

static int
readkey (struct grub_term_input *term __attribute__ ((unused)))
{
  grub_uint8_t c;
  grub_ssize_t actual = 0;

  grub_ieee1275_read (stdin_ihandle, &c, 1, &actual);
  if (actual > 0)
    return c;
  return -1;
}

static void
grub_console_dimensions (void)
{
  grub_ieee1275_ihandle_t options;
  grub_ieee1275_phandle_t stdout_phandle;
  char val[1024];

  /* Always assume 80x24 on serial since screen-#rows/screen-#columns is often
     garbage for such devices.  */
  if (! grub_ieee1275_instance_to_package (stdout_ihandle,
					   &stdout_phandle)
      && ! grub_ieee1275_package_to_path (stdout_phandle,
					  val, sizeof (val) - 1, 0))
    {
      grub_ieee1275_ihandle_t stdout_options;
      val[sizeof (val) - 1] = 0;      

      if (! grub_ieee1275_finddevice (val, &stdout_options)
	  && ! grub_ieee1275_get_property (stdout_options, "device_type",
					   val, sizeof (val) - 1, 0))
	{
	  val[sizeof (val) - 1] = 0;
	  if (grub_strcmp (val, "serial") == 0)
	    {
	      grub_console_terminfo_output.size.x = 80;
	      grub_console_terminfo_output.size.y = 24;
	      return;
	    }
	}
    }

  if (! grub_ieee1275_finddevice ("/options", &options)
      && options != (grub_ieee1275_ihandle_t) -1)
    {
      if (! grub_ieee1275_get_property (options, "screen-#columns",
					val, sizeof (val) - 1, 0))
	{
	  val[sizeof (val) - 1] = 0;
	  grub_console_terminfo_output.size.x
	    = (grub_uint8_t) grub_strtoul (val, 0, 10);
	}
      if (! grub_ieee1275_get_property (options, "screen-#rows",
					val, sizeof (val) - 1, 0))
	{
	  val[sizeof (val) - 1] = 0;
	  grub_console_terminfo_output.size.y
	    = (grub_uint8_t) grub_strtoul (val, 0, 10);
	}
    }

  /* Bogus default value on SLOF in QEMU.  */
  if (grub_console_terminfo_output.size.x == 200
      && grub_console_terminfo_output.size.y == 200)
    {
      grub_console_terminfo_output.size.x = 80;
      grub_console_terminfo_output.size.y = 24;
    }

  /* Use a small console by default.  */
  if (! grub_console_terminfo_output.size.x)
    grub_console_terminfo_output.size.x = 80;
  if (! grub_console_terminfo_output.size.y)
    grub_console_terminfo_output.size.y = 24;
}

static void
grub_console_setcursor (struct grub_term_output *term,
			  int on)
{
  grub_terminfo_setcursor (term, on);

  if (!grub_ieee1275_test_flag (GRUB_IEEE1275_FLAG_HAS_CURSORONOFF))
    return;

  /* Understood by the Open Firmware flavour in OLPC.  */
  if (on)
    grub_ieee1275_interpret ("cursor-on", 0);
  else
    grub_ieee1275_interpret ("cursor-off", 0);
}

static grub_err_t
grub_console_init_input (struct grub_term_input *term)
{
  grub_ssize_t actual;

  if (grub_ieee1275_get_integer_property (grub_ieee1275_chosen, "stdin", &stdin_ihandle,
					  sizeof stdin_ihandle, &actual)
      || actual != sizeof stdin_ihandle)
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "cannot find stdin");

  return grub_terminfo_input_init (term);
}

static grub_err_t
grub_console_init_output (struct grub_term_output *term)
{
  grub_ssize_t actual;
  unsigned int col;

  /* The latest PowerMacs don't actually initialize the screen for us, so we
   * use this trick to re-open the output device (but we avoid doing this on
   * platforms where it's known to be broken). */
  if (! grub_ieee1275_test_flag (GRUB_IEEE1275_FLAG_BROKEN_OUTPUT))
    grub_ieee1275_interpret ("output-device output", 0);

  if (grub_ieee1275_get_integer_property (grub_ieee1275_chosen, "stdout", &stdout_ihandle,
					  sizeof stdout_ihandle, &actual)
      || actual != sizeof stdout_ihandle)
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "cannot find stdout");

  /* Initialize colors.  */
  for (col = 0; col < ARRAY_SIZE (colors); col++)
    grub_ieee1275_set_color (stdout_ihandle, col, colors[col].red,
			     colors[col].green, colors[col].blue);

  /* Set the right fg and bg colors. */
  grub_terminfo_setcolorstate (term, GRUB_TERM_COLOR_NORMAL);

  grub_console_dimensions ();

  grub_terminfo_output_init (term);

  return 0;
}



struct grub_terminfo_input_state grub_console_terminfo_input =
  {
    .readkey = readkey
  };

struct grub_terminfo_output_state grub_console_terminfo_output =
  {
    .put = put,
    .size = { 80, 24 }
  };

static struct grub_term_input grub_console_term_input =
  {
    .name = "console",
    .init = grub_console_init_input,
    .getkey = grub_terminfo_getkey,
    .data = &grub_console_terminfo_input
  };

static struct grub_term_output grub_console_term_output =
  {
    .name = "console",
    .init = grub_console_init_output,
    .putchar = grub_terminfo_putchar,
    .getxy = grub_terminfo_getxy,
    .getwh = grub_terminfo_getwh,
    .gotoxy = grub_terminfo_gotoxy,
    .cls = grub_terminfo_cls,
    .setcolorstate = grub_terminfo_setcolorstate,
    .setcursor = grub_console_setcursor,
    .flags = GRUB_TERM_CODE_TYPE_ASCII,
    .data = &grub_console_terminfo_output,
    .progress_update_divisor = GRUB_PROGRESS_FAST
  };

void
grub_console_init_early (void)
{
  grub_term_register_input ("console", &grub_console_term_input);
  grub_term_register_output ("console", &grub_console_term_output);
}

void
grub_console_init_lately (void)
{
  const char *type;

  if (grub_ieee1275_test_flag (GRUB_IEEE1275_FLAG_CURSORONOFF_ANSI_BROKEN))
    type = "ieee1275-nocursor";
  else
    type = "ieee1275";
  grub_terminfo_init ();
  grub_terminfo_output_register (&grub_console_term_output, type);
}

void
grub_console_fini (void)
{
}
