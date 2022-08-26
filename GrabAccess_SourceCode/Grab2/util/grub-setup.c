/* grub-setup.c - make GRUB usable */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2004,2005,2006,2007,2008,2009,2010,2011  Free Software Foundation, Inc.
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

#define _GNU_SOURCE	1

#include <string.h>

#include <grub/types.h>
#include <grub/emu/misc.h>
#include <grub/util/misc.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/file.h>
#include <grub/fs.h>
#include <grub/partition.h>
#include <grub/env.h>
#include <grub/emu/hostdisk.h>
#include <grub/term.h>
#include <grub/i18n.h>
#include <grub/crypto.h>
#include <grub/emu/getroot.h>
#include <grub/util/install.h>

#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#include <argp.h>
#pragma GCC diagnostic error "-Wmissing-prototypes"
#pragma GCC diagnostic error "-Wmissing-declarations"

/* On SPARC this program fills in various fields inside of the 'boot' and 'core'
 * image files.
 *
 * The 'boot' image needs to know the OBP path name of the root
 * device.  It also needs to know the initial block number of
 * 'core' (which is 'diskboot' concatenated with 'kernel' and
 * all the modules, this is created by grub-mkimage).  This resulting
 * 'boot' image is 512 bytes in size and is placed in the second block
 * of a partition.
 *
 * The initial 'diskboot' block acts as a loader for the actual GRUB
 * kernel.  It contains the loading code and then a block list.
 *
 * The block list of 'core' starts at the end of the 'diskboot' image
 * and works it's way backwards towards the end of the code of 'diskboot'.
 *
 * We patch up the images with the necessary values and write out the
 * result.
 */

#define DEFAULT_BOOT_FILE	"boot.img"
#define DEFAULT_CORE_FILE	"core.img"

/* Non-printable "keys" for arguments with no short form.
 * See grub-core/lib/gnulib/argp.h for details. */
enum {
  NO_RS_CODES_KEY = 0x100,
};

static struct argp_option options[] = {
  {"boot-image",  'b', N_("FILE"), 0,
   N_("use FILE as the boot image [default=%s]"), 0},
  {"core-image",  'c', N_("FILE"), 0,
   N_("use FILE as the core image [default=%s]"), 0},
  {"directory",   'd', N_("DIR"),  0,
   N_("use GRUB files in the directory DIR [default=%s]"), 0},
  {"device-map",  'm', N_("FILE"), 0,
   N_("use FILE as the device map [default=%s]"), 0},
  {"force",       'f', 0,      0,
   N_("install even if problems are detected"), 0},
  {"skip-fs-probe",'s',0,      0,
   N_("do not probe for filesystems in DEVICE"), 0},
  {"verbose",     'v', 0,      0, N_("print verbose messages."), 0},
  {"allow-floppy", 'a', 0,      0,
   /* TRANSLATORS: The potential breakage isn't limited to floppies but it's
      likely to make the install unbootable from HDD.  */
   N_("make the drive also bootable as floppy (default for fdX devices). May break on some BIOSes."), 0},
  {"no-rs-codes", NO_RS_CODES_KEY, 0,      0,
   N_("Do not apply any reed-solomon codes when embedding core.img. "
      "This option is only available on x86 BIOS targets."), 0},
  { 0, 0, 0, 0, 0, 0 }
};

#pragma GCC diagnostic ignored "-Wformat-nonliteral"

static char *
help_filter (int key, const char *text, void *input __attribute__ ((unused)))
{
  switch (key)
    {
      case 'b':
        return xasprintf (text, DEFAULT_BOOT_FILE);

      case 'c':
        return xasprintf (text, DEFAULT_CORE_FILE);

      case 'd':
        return xasprintf (text, DEFAULT_DIRECTORY);

      case 'm':
        return xasprintf (text, DEFAULT_DEVICE_MAP);

      default:
        return (char *) text;
    }
}

#pragma GCC diagnostic error "-Wformat-nonliteral"

struct arguments
{
  char *boot_file;
  char *core_file;
  char *dir;
  char *dev_map;
  int  force;
  int  fs_probe;
  int allow_floppy;
  char *device;
  int add_rs_codes;
};

static error_t
argp_parser (int key, char *arg, struct argp_state *state)
{
  /* Get the input argument from argp_parse, which we
     know is a pointer to our arguments structure. */
  struct arguments *arguments = state->input;

  switch (key)
    {
      case 'a':
        arguments->allow_floppy = 1;
        break;

      case 'b':
        if (arguments->boot_file)
          free (arguments->boot_file);

        arguments->boot_file = xstrdup (arg);
        break;

      case 'c':
        if (arguments->core_file)
          free (arguments->core_file);

        arguments->core_file = xstrdup (arg);
        break;

      case 'd':
        if (arguments->dir)
          free (arguments->dir);

        arguments->dir = xstrdup (arg);
        break;

      case 'm':
        if (arguments->dev_map)
          free (arguments->dev_map);

        arguments->dev_map = xstrdup (arg);
        break;

      case 'f':
        arguments->force = 1;
        break;

      case 's':
        arguments->fs_probe = 0;
        break;

      case 'v':
        verbosity++;
        break;

      case NO_RS_CODES_KEY:
        arguments->add_rs_codes = 0;
        break;

      case ARGP_KEY_ARG:
        if (state->arg_num == 0)
          arguments->device = xstrdup(arg);
        else
          {
            /* Too many arguments. */
	    fprintf (stderr, _("Unknown extra argument `%s'."), arg);
	    fprintf (stderr, "\n");
            argp_usage (state);
          }
        break;

      case ARGP_KEY_NO_ARGS:
          fprintf (stderr, "%s", _("No device is specified.\n"));
          argp_usage (state);
	  exit (1);
          break;

      default:
        return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

static struct argp argp = {
  options, argp_parser, N_("DEVICE"),
  "\n"N_("\
Set up images to boot from DEVICE.\n\
\n\
You should not normally run this program directly.  Use grub-install instead.")
"\v"N_("\
DEVICE must be an OS device (e.g. /dev/sda)."),
  NULL, help_filter, NULL
};

static char *
get_device_name (char *dev)
{
  size_t len = strlen (dev);

  if (dev[0] != '(' || dev[len - 1] != ')')
    return 0;

  dev[len - 1] = '\0';
  return dev + 1;
}

int
main (int argc, char *argv[])
{
  char *root_dev = NULL;
  char *dest_dev = NULL;
  struct arguments arguments;

  grub_util_host_init (&argc, &argv);

  /* Default option values. */
  memset (&arguments, 0, sizeof (struct arguments));
  arguments.fs_probe  = 1;
  arguments.add_rs_codes = 1;

  /* Parse our arguments */
  if (argp_parse (&argp, argc, argv, 0, 0, &arguments) != 0)
    {
      fprintf (stderr, "%s", _("Error in parsing command line arguments\n"));
      exit(1);
    }

#ifdef GRUB_SETUP_SPARC64
  arguments.force = 1;
#endif

  if (verbosity > 1)
    grub_env_set ("debug", "all");

  /* Initialize the emulated biosdisk driver.  */
  grub_util_biosdisk_init (arguments.dev_map ? : DEFAULT_DEVICE_MAP);

  /* Initialize all modules. */
  grub_init_all ();
  grub_gcry_init_all ();

  grub_lvm_fini ();
  grub_mdraid09_fini ();
  grub_mdraid1x_fini ();
  grub_diskfilter_fini ();
  grub_diskfilter_init ();
  grub_mdraid09_init ();
  grub_mdraid1x_init ();
  grub_lvm_init ();

  dest_dev = get_device_name (arguments.device);
  if (! dest_dev)
    {
      /* Possibly, the user specified an OS device file.  */
      dest_dev = grub_util_get_grub_dev (arguments.device);
      if (! dest_dev)
        {
          char *program = xstrdup(program_name);
          fprintf (stderr, _("Invalid device `%s'.\n"), arguments.device);
          argp_help (&argp, stderr, ARGP_HELP_STD_USAGE, program);
          free(program);
          exit(1);
        }
      grub_util_info ("transformed OS device `%s' into GRUB device `%s'",
                      arguments.device, dest_dev);
    }
  else
    {
      /* For simplicity.  */
      dest_dev = xstrdup (dest_dev);
      grub_util_info ("Using `%s' as GRUB device", dest_dev);
    }

  /* Do the real work.  */
  GRUB_SETUP_FUNC (arguments.dir ? : DEFAULT_DIRECTORY,
		   arguments.boot_file ? : DEFAULT_BOOT_FILE,
		   arguments.core_file ? : DEFAULT_CORE_FILE,
		   dest_dev, arguments.force,
		   arguments.fs_probe, arguments.allow_floppy,
		   arguments.add_rs_codes, 0);

  /* Free resources.  */
  grub_fini_all ();
  grub_util_biosdisk_fini ();

  free (arguments.boot_file);
  free (arguments.core_file);
  free (arguments.dir);
  free (arguments.dev_map);
  free (arguments.device);
  free (root_dev);
  free (dest_dev);

  return 0;
}
