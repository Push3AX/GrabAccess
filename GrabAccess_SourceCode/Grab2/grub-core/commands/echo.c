/* echo.c - Command to display a line of text  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2010,2020  Free Software Foundation, Inc.
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
#include <grub/env.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/term.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options[] =
{
  {0, 'n', 0, N_("Do not output the trailing newline."), 0, 0},
  {0, 'e', 0, N_("Enable interpretation of backslash escapes, e.g. \\t=tab,\\n=new line,\\e0x1f=white text on blue background."), 0, 0},
  {0, 0, 0, 0, 0, 0}
};

static int newline;

static const char *color_list[16] =
{
  "black",
  "blue",
  "green",
  "cyan",
  "red",
  "magenta",
  "brown",
  "light-gray",
  "dark-gray",
  "light-blue",
  "light-green",
  "light-cyan",
  "light-red",
  "light-magenta",
  "yellow",
  "white"
};

static grub_uint8_t read_xdigit (char num)
{
  if (num >= '0' && num <= '9')
    return num - '0';
  if (num >= 'a' && num <= 'z')
    return num - 'a' + 0x0a;
  if (num >= 'A' && num <= 'Z')
    return num - 'A' + 0x0a;
  return 0;
}

static int
parse_bash_color (const char *text)
{
  char str[5];
  char src[64];
  grub_uint8_t fg, bg;
  if (!text || grub_strlen(text) < 4)
    return 0;
  grub_memcpy (str, text, 4);
  str[4] = '\0';
  if (str[0] != '0' || (str[1] != 'x' && str[1] != 'X') ||
      !grub_isxdigit (str[2]) || !grub_isxdigit (str[3]))
    return 0;
  bg = read_xdigit (str[2]);
  fg = read_xdigit (str[3]);
  if (fg > 0x0f || bg > 0x0f)
    return 0;
  grub_snprintf (src, 64, "set color_normal=%s/%s",
                 color_list[fg], color_list[bg]);
  grub_script_execute_sourcecode (src);
  grub_refresh ();
  return 4;
}

static void
parse_print (const char *text)
{
  char *start, *end;
  char *str = NULL;
  if (!text)
    return;
  str = grub_strdup (text);
  if (!str)
    return;
  start = str, end = str;
  while (*end)
  {
    if (*end == '\\')
    {
      *end = '\0';
      grub_xputs (start);
      end++;
      if (*end == '\0')
        break;
      switch (*end)
      {
        case '\\':
          grub_printf ("\\");
          break;
        case 'a':
          grub_printf ("\a");
          break;
        case 'c':
          newline = 0;
          break;
        case 'f':
          grub_printf ("\f");
          break;
        case 'n':
          grub_printf ("\n");
          break;
        case 'r':
          grub_printf ("\r");
          break;
        case 't':
          grub_printf ("\t");
          break;
        case 'v':
          grub_printf ("\v");
          break;
        case 'e':
          end += parse_bash_color (end + 1);
          break;
      }
      end++;
      start = end;
      continue;
    }
    end++;
  }
  if (*start != '\0')
    grub_xputs (start);
  grub_free (str);
}

static grub_err_t
grub_cmd_echo (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  int i;
  newline = 1;

  /* Check if `-n' was used.  */
  if (state[0].set)
    newline = 0;

  for (i = 0; i < argc; i++)
  {

    if (state[1].set)
      parse_print (args[i]);
    else
      grub_xputs (args[i]);

    /* If another argument follows, insert a space.  */
    if (i != argc - 1)
      grub_printf (" ");
  }

  if (newline)
    grub_printf ("\n");

  grub_refresh ();

  return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(echo)
{
  cmd = grub_register_extcmd ("echo", grub_cmd_echo,
                  GRUB_COMMAND_ACCEPT_DASH
                  | GRUB_COMMAND_OPTIONS_AT_START,
                  N_("[OPTIONS] STRING"), N_("Display a line of text."),
                  options);
}

GRUB_MOD_FINI(echo)
{
  grub_unregister_extcmd (cmd);
}
