/* grub-mkimage.c - make a bootable image */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2003,2004,2005,2006,2007,2008,2009,2010  Free Software Foundation, Inc.
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
#include <grub/types.h>
#include <grub/elf.h>
#include <grub/aout.h>
#include <grub/i18n.h>
#include <grub/kernel.h>
#include <grub/disk.h>
#include <grub/emu/misc.h>
#include <grub/util/misc.h>
#include <grub/util/resolve.h>
#include <grub/misc.h>
#include <grub/offsets.h>
#include <grub/crypto.h>
#include <grub/dl.h>
#include <time.h>
#include <multiboot.h>

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <grub/efi/pe32.h>
#include <grub/uboot/image.h>
#include <grub/arm/reloc.h>
#include <grub/ia64/reloc.h>
#include <grub/osdep/hostfile.h>
#include <grub/util/install.h>
#include <grub/emu/config.h>

#define _GNU_SOURCE	1

#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#include <argp.h>
#pragma GCC diagnostic error "-Wmissing-prototypes"
#pragma GCC diagnostic error "-Wmissing-declarations"


#include "progname.h"



static struct argp_option options[] = {
  {"directory",  'd', N_("DIR"), 0,
   /* TRANSLATORS: platform here isn't identifier. It can be translated.  */
   N_("use images and modules under DIR [default=%s/<platform>]"), 0},
  {"prefix",  'p', N_("DIR"), 0, N_("set prefix directory"), 0},
  {"memdisk",  'm', N_("FILE"), 0,
   /* TRANSLATORS: "memdisk" here isn't an identifier, it can be translated.
    "embed" is a verb (command description).  "*/
   N_("embed FILE as a memdisk image\n"
      "Implies `-p (memdisk)/boot/grub' and overrides any prefix supplied previously,"
      " but the prefix itself can be overridden by later options"), 0},
  {"dtb",  'D', N_("FILE"), 0, N_("embed FILE as a device tree (DTB)\n"), 0},
   /* TRANSLATORS: "embed" is a verb (command description).  "*/
  {"config",   'c', N_("FILE"), 0, N_("embed FILE as an early config"), 0},
   /* TRANSLATORS: "embed" is a verb (command description).  "*/
  {"pubkey",   'k', N_("FILE"), 0, N_("embed FILE as public key for signature checking"), 0},
  /* TRANSLATORS: NOTE is a name of segment.  */
  {"note",   'n', 0, 0, N_("add NOTE segment for CHRP IEEE1275"), 0},
  {"output",  'o', N_("FILE"), 0, N_("output a generated image to FILE [default=stdout]"), 0},
  {"format",  'O', N_("FORMAT"), 0, 0, 0},
  {"compression",  'C', "(xz|none|auto)", 0, N_("choose the compression to use for core image"), 0},
  {"sbat", 's', N_("FILE"), 0, N_("SBAT metadata"), 0},
  {"verbose",     'v', 0,      0, N_("print verbose messages."), 0},
  { 0, 0, 0, 0, 0, 0 }
};

#pragma GCC diagnostic ignored "-Wformat-nonliteral"

static char *
help_filter (int key, const char *text, void *input __attribute__ ((unused)))
{
  switch (key)
    {
    case 'd':
      return xasprintf (text, grub_util_get_pkglibdir ());
    case 'O':
      {
	char *formats = grub_install_get_image_targets_string (), *ret;
	ret = xasprintf ("%s\n%s %s", _("generate an image in FORMAT"),
			 _("available formats:"), formats);
	free (formats);
	return ret;
      }
    default:
      return (char *) text;
    }
}

#pragma GCC diagnostic error "-Wformat-nonliteral"

struct arguments
{
  size_t nmodules;
  size_t modules_max;
  char **modules;
  char *output;
  char *dir;
  char *prefix;
  char *memdisk;
  char *dtb;
  char **pubkeys;
  size_t npubkeys;
  char *font;
  char *config;
  char *sbat;
  int note;
  const struct grub_install_image_target_desc *image_target;
  grub_compression_t comp;
};

static error_t
argp_parser (int key, char *arg, struct argp_state *state)
{
  /* Get the input argument from argp_parse, which we
     know is a pointer to our arguments structure. */
  struct arguments *arguments = state->input;

  switch (key)
    {
    case 'o':
      if (arguments->output)
	free (arguments->output);

      arguments->output = xstrdup (arg);
      break;

    case 'O':
      {
	arguments->image_target = grub_install_get_image_target (arg);
	if (!arguments->image_target)
	  {
	    printf (_("unknown target format %s\n"), arg);
	    argp_usage (state);
	    exit (1);
	  }
	break;
      }
    case 'd':
      if (arguments->dir)
	free (arguments->dir);

      arguments->dir = xstrdup (arg);
      break;

    case 'n':
      arguments->note = 1;
      break;

    case 'm':
      if (arguments->memdisk)
	free (arguments->memdisk);

      arguments->memdisk = xstrdup (arg);

      if (arguments->prefix)
	free (arguments->prefix);

      arguments->prefix = xstrdup ("(memdisk)/boot/grub");
      break;

    case 'D':
      if (arguments->dtb)
	free (arguments->dtb);

      arguments->dtb = xstrdup (arg);
      break;

    case 'k':
      arguments->pubkeys = xrealloc (arguments->pubkeys,
				     sizeof (arguments->pubkeys[0])
				     * (arguments->npubkeys + 1));
      arguments->pubkeys[arguments->npubkeys++] = xstrdup (arg);
      break;

    case 'c':
      if (arguments->config)
	free (arguments->config);

      arguments->config = xstrdup (arg);
      break;

    case 'C':
      if (grub_strcmp (arg, "xz") == 0)
	{
#ifdef HAVE_LIBLZMA
	  arguments->comp = GRUB_COMPRESSION_XZ;
#else
	  grub_util_error ("%s",
			   _("grub-mkimage is compiled without XZ support"));
#endif
	}
      else if (grub_strcmp (arg, "none") == 0)
	arguments->comp = GRUB_COMPRESSION_NONE;
      else if (grub_strcmp (arg, "auto") == 0)
	arguments->comp = GRUB_COMPRESSION_AUTO;
      else
	grub_util_error (_("Unknown compression format %s"), arg);
      break;

    case 'p':
      if (arguments->prefix)
	free (arguments->prefix);

      arguments->prefix = xstrdup (arg);
      break;

    case 's':
      if (arguments->sbat)
	free (arguments->sbat);

      arguments->sbat = xstrdup (arg);
      break;

    case 'v':
      verbosity++;
      break;
    case ARGP_KEY_ARG:
      assert (arguments->nmodules < arguments->modules_max);
      arguments->modules[arguments->nmodules++] = xstrdup(arg);
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static struct argp argp = {
  options, argp_parser, N_("[OPTION]... [MODULES]"),
  N_("Make a bootable image of GRUB."),
  NULL, help_filter, NULL
};

int
main (int argc, char *argv[])
{
  FILE *fp = stdout;
  struct arguments arguments;
  unsigned i;

  grub_util_host_init (&argc, &argv);

  memset (&arguments, 0, sizeof (struct arguments));
  arguments.comp = GRUB_COMPRESSION_AUTO;
  arguments.modules_max = argc + 1;
  arguments.modules = xmalloc ((arguments.modules_max + 1)
			     * sizeof (arguments.modules[0]));
  memset (arguments.modules, 0, (arguments.modules_max + 1)
	  * sizeof (arguments.modules[0]));

  if (argp_parse (&argp, argc, argv, 0, 0, &arguments) != 0)
    {
      fprintf (stderr, "%s", _("Error in parsing command line arguments\n"));
      exit(1);
    }

  if (!arguments.image_target)
    {
      char *program = xstrdup(program_name);
      printf ("%s\n", _("Target format not specified (use the -O option)."));
      argp_help (&argp, stderr, ARGP_HELP_STD_USAGE, program);
      free (program);
      exit(1);
    }

  if (!arguments.prefix)
    {
      char *program = xstrdup(program_name);
      printf ("%s\n", _("Prefix not specified (use the -p option)."));
      argp_help (&argp, stderr, ARGP_HELP_STD_USAGE, program);
      free (program);
      exit(1);
    }

  if (arguments.output)
    {
      fp = grub_util_fopen (arguments.output, "wb");
      if (! fp)
	grub_util_error (_("cannot open `%s': %s"), arguments.output,
			 strerror (errno));
    }

  if (!arguments.dir)
    {
      const char *dn = grub_util_get_target_dirname (arguments.image_target);
      const char *pkglibdir = grub_util_get_pkglibdir ();
      char *ptr;
      arguments.dir = xmalloc (grub_strlen (pkglibdir) + grub_strlen (dn) + 2);
      ptr = grub_stpcpy (arguments.dir, pkglibdir);
      *ptr++ = '/';
      strcpy (ptr, dn);
    }

  grub_install_generate_image (arguments.dir, arguments.prefix, fp,
			       arguments.output, arguments.modules,
			       arguments.memdisk, arguments.pubkeys,
			       arguments.npubkeys, arguments.config,
			       arguments.image_target, arguments.note,
			       arguments.comp, arguments.dtb,
			       arguments.sbat);

  if (grub_util_file_sync (fp) < 0)
    grub_util_error (_("cannot sync `%s': %s"), arguments.output ? : "stdout",
		     strerror (errno));
  if (fclose (fp) == EOF)
    grub_util_error (_("cannot close `%s': %s"), arguments.output ? : "stdout",
		     strerror (errno));

  for (i = 0; i < arguments.nmodules; i++)
    free (arguments.modules[i]);

  free (arguments.dir);
  free (arguments.prefix);
  free (arguments.modules);

  if (arguments.output)
    free (arguments.output);

  if (arguments.sbat)
    free (arguments.sbat);

  return 0;
}
