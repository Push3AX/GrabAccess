/* grub-probe.c - probe device information for a given path */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2006,2007,2008,2009,2010  Free Software Foundation, Inc.
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
#include <grub/emu/misc.h>
#include <grub/util/misc.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/file.h>
#include <grub/fs.h>
#include <grub/partition.h>
#include <grub/msdos_partition.h>
#include <grub/emu/hostdisk.h>
#include <grub/emu/getroot.h>
#include <grub/term.h>
#include <grub/env.h>
#include <grub/diskfilter.h>
#include <grub/i18n.h>
#include <grub/crypto.h>
#include <grub/cryptodisk.h>
#include <grub/hfsplus.h>

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

#define _GNU_SOURCE	1
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#include <argp.h>
#pragma GCC diagnostic error "-Wmissing-prototypes"
#pragma GCC diagnostic error "-Wmissing-declarations"

#include "progname.h"

static void
bless (const char *path, int x86)
{
  char *drive_name = NULL;
  char **devices;
  char *grub_path = NULL;
  char *filebuf_via_grub = NULL, *filebuf_via_sys = NULL;
  grub_device_t dev = NULL;
  grub_err_t err;
  struct stat st;

  grub_path = grub_canonicalize_file_name (path);

  if (stat (grub_path, &st) < 0)
    grub_util_error (N_("cannot stat `%s': %s"),
		     grub_path, strerror (errno));

  devices = grub_guess_root_devices (grub_path);

  if (! devices || !devices[0])
    grub_util_error (_("cannot find a device for %s (is /dev mounted?)"), path);

  drive_name = grub_util_get_grub_dev (devices[0]);
  if (! drive_name)
    grub_util_error (_("cannot find a GRUB drive for %s.  Check your device.map"),
		     devices[0]);

  grub_util_info ("opening %s", drive_name);
  dev = grub_device_open (drive_name);
  if (! dev)
    grub_util_error ("%s", grub_errmsg);

  err = grub_mac_bless_inode (dev, st.st_ino, S_ISDIR (st.st_mode), x86);
  if (err)
    grub_util_error ("%s", grub_errmsg);
  free (grub_path);
  free (filebuf_via_grub);
  free (filebuf_via_sys);
  free (drive_name);
  free (devices);
  grub_device_close (dev);
}

static struct argp_option options[] = {
  {"x86",  'x', 0, 0,
   N_("bless for x86-based macs"), 0},
  {"ppc",  'p', 0, 0,
   N_("bless for ppc-based macs"), 0},
  {"verbose",     'v', 0,      0, N_("print verbose messages."), 0},
  { 0, 0, 0, 0, 0, 0 }
};

struct arguments
{
  char *arg;
  int ppc;
};

static error_t
argp_parser (int key, char *arg, struct argp_state *state)
{
  /* Get the input argument from argp_parse, which we
     know is a pointer to our arguments structure. */
  struct arguments *arguments = state->input;

  switch (key)
    {
    case 'v':
      verbosity++;
      break;

    case 'x':
      arguments->ppc = 0;
      break;

    case 'p':
      arguments->ppc = 1;
      break;

    case ARGP_KEY_NO_ARGS:
      fprintf (stderr, "%s", _("No path or device is specified.\n"));
      argp_usage (state);
      break;

    case ARGP_KEY_ARG:
      if (arguments->arg)
	{
	  fprintf (stderr, _("Unknown extra argument `%s'."), arg);
	  fprintf (stderr, "\n");
	  return ARGP_ERR_UNKNOWN;
	}
      arguments->arg = xstrdup (arg);
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static struct argp argp = {
  options, argp_parser, N_("--ppc PATH|--x86 FILE"),
  N_("Mac-style bless on HFS or HFS+"),
  NULL, NULL, NULL
};

int
main (int argc, char *argv[])
{
  struct arguments arguments;

  grub_util_host_init (&argc, &argv);

  /* Check for options.  */
  memset (&arguments, 0, sizeof (struct arguments));
  if (argp_parse (&argp, argc, argv, 0, 0, &arguments) != 0)
    {
      fprintf (stderr, "%s", _("Error in parsing command line arguments\n"));
      exit(1);
    }

  if (verbosity > 1)
    grub_env_set ("debug", "all");

  /* Initialize the emulated biosdisk driver.  */
  grub_util_biosdisk_init (NULL);

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

  /* Do it.  */
  bless (arguments.arg, !arguments.ppc);

  /* Free resources.  */
  grub_gcry_fini_all ();
  grub_fini_all ();
  grub_util_biosdisk_fini ();

  return 0;
}
