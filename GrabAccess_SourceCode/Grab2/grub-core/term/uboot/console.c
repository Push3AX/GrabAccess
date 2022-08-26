/* console.c - console interface layer for U-Boot platforms */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013  Free Software Foundation, Inc.
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
#include <grub/terminfo.h>
#include <grub/uboot/uboot.h>
#include <grub/uboot/console.h>

static void
put (struct grub_term_output *term __attribute__ ((unused)), const int c)
{
  grub_uboot_putc (c);
}

static int
readkey (struct grub_term_input *term __attribute__ ((unused)))
{
  if (grub_uboot_tstc () > 0)
    return grub_uboot_getc ();

  return -1;
}

static void
uboot_console_setcursor (struct grub_term_output *term
			 __attribute__ ((unused)), int on
			 __attribute__ ((unused)))
{
  grub_terminfo_setcursor (term, on);
}

static grub_err_t
uboot_console_init_input (struct grub_term_input *term)
{
  return grub_terminfo_input_init (term);
}

extern struct grub_terminfo_output_state uboot_console_terminfo_output;


static grub_err_t
uboot_console_init_output (struct grub_term_output *term)
{
  grub_terminfo_output_init (term);

  return 0;
}

struct grub_terminfo_input_state uboot_console_terminfo_input = {
  .readkey = readkey
};

struct grub_terminfo_output_state uboot_console_terminfo_output = {
  .put = put,
  /* FIXME: In rare cases when console isn't serial,
     determine real width.  */
  .size = { 80, 24 }
};

static struct grub_term_input uboot_console_term_input = {
  .name = "console",
  .init = uboot_console_init_input,
  .getkey = grub_terminfo_getkey,
  .data = &uboot_console_terminfo_input
};

static struct grub_term_output uboot_console_term_output = {
  .name = "console",
  .init = uboot_console_init_output,
  .putchar = grub_terminfo_putchar,
  .getwh = grub_terminfo_getwh,
  .getxy = grub_terminfo_getxy,
  .gotoxy = grub_terminfo_gotoxy,
  .cls = grub_terminfo_cls,
  .setcolorstate = grub_terminfo_setcolorstate,
  .setcursor = uboot_console_setcursor,
  .flags = GRUB_TERM_CODE_TYPE_ASCII,
  .data = &uboot_console_terminfo_output,
  .progress_update_divisor = GRUB_PROGRESS_FAST
};

void
grub_console_init_early (void)
{
  grub_term_register_input ("console", &uboot_console_term_input);
  grub_term_register_output ("console", &uboot_console_term_output);
}


/*
 * grub_console_init_lately():
 *   Initializes terminfo formatting by registering terminal type.
 *   Called after heap has been configured.
 *   
 */
void
grub_console_init_lately (void)
{
  const char *type;

  /* See if explicitly set by U-Boot environment */
  type = grub_uboot_env_get ("grub_term");
  if (!type)
    type = "vt100";

  grub_terminfo_init ();
  grub_terminfo_output_register (&uboot_console_term_output, type);
}

void
grub_console_fini (void)
{
}
