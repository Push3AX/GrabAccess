/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010,2012,2013 Free Software Foundation, Inc.
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
#include <grub/font.h>
#include <grub/gfxmenu_view.h>
#include <grub/color.h>
#include <grub/util/install.h>
#include <grub/emu/hostdisk.h>

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
  char *text;
  char *output;
  char *font;
  char *fgcolor;
  char *bgcolor;
  int verbosity;
};

static struct argp_option options[] = {
  {"input",  'i', N_("FILE"), 0,
   N_("read text from FILE."), 0},
  {"color",  'c', N_("COLOR"), 0,
   N_("use COLOR for text"), 0},
  {"bgcolor",  'b', N_("COLOR"), 0,
   N_("use COLOR for background"), 0},
  {"text",  't', N_("STRING"), 0,
  /* TRANSLATORS: The result is always stored to file and
     never shown directly, so don't use "show" as synonym for render. Use "create" if
     "render" doesn't translate directly.  */
   N_("set the label to render"), 0},
  {"output",  'o', N_("FILE"), 0,
   N_("set output filename. Default is STDOUT"), 0},
  {"font",  'f', N_("FILE"), 0,
   N_("use FILE as font (PF2)."), 0},
  {"verbose",     'v', 0,      0, N_("print verbose messages."), 0},
  { 0, 0, 0, 0, 0, 0 }
};

#include <grub/err.h>
#include <grub/types.h>
#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/video.h>
#include <grub/video_fb.h>

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

    case 'b':
      arguments->bgcolor = xstrdup (arg);
      break;

    case 'c':
      arguments->fgcolor = xstrdup (arg);
      break;

    case 'f':
      arguments->font = xstrdup (arg);
      break;

    case 't':
      arguments->text = xstrdup (arg);
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
  /* TRANSLATORS: This file takes a text and creates a graphical representation of it,
     putting the result into .disk_label file. The result is always stored to file and
     never shown directly, so don't use "show" as synonym for render. Use "create" if
     "render" doesn't translate directly.  */
  N_("Render Apple .disk_label."),
  NULL, NULL, NULL
};

int
main (int argc, char *argv[])
{
  char *text;
  struct arguments arguments;

  grub_util_host_init (&argc, &argv);

  /* Check for options.  */
  memset (&arguments, 0, sizeof (struct arguments));
  if (argp_parse (&argp, argc, argv, 0, 0, &arguments) != 0)
    {
      fprintf (stderr, "%s", _("Error in parsing command line arguments\n"));
      exit(1);
    }

  if ((!arguments.input && !arguments.text) || !arguments.font)
    {
      fprintf (stderr, "%s", _("Missing arguments\n"));
      exit(1);
    }

  if (arguments.text)
    text = arguments.text;
  else
    {
      FILE *in = grub_util_fopen (arguments.input, "r");
      size_t s;
      if (!in)
	grub_util_error (_("cannot open `%s': %s"), arguments.input,
			 strerror (errno));
      fseek (in, 0, SEEK_END);
      s = ftell (in);
      fseek (in, 0, SEEK_SET);
      text = xmalloc (s + 1);
      if (fread (text, 1, s, in) != s)
	grub_util_error (_("cannot read `%s': %s"), arguments.input,
			 strerror (errno));
      text[s] = 0;
      fclose (in);
    }

  grub_init_all ();
  grub_hostfs_init ();
  grub_host_init ();

  grub_util_render_label (arguments.font,
			  arguments.bgcolor,
			  arguments.fgcolor,
			  text,
			  arguments.output);

  return 0;
}
