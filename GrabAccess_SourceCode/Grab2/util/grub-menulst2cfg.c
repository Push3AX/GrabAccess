/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010  Free Software Foundation, Inc.
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

#include <grub/legacy_parse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <grub/util/misc.h>
#include <grub/misc.h>
#include <grub/i18n.h>

int
main (int argc, char **argv)
{
  FILE *in, *out;
  char *entryname = NULL;
  char *buf = NULL;
  size_t bufsize = 0;
  char *suffix = xstrdup ("");
  int suffixlen = 0;
  const char *out_fname = 0;

  grub_util_host_init (&argc, &argv);

  if (argc >= 2 && argv[1][0] == '-')
    {
      fprintf (stdout, _("Usage: %s [INFILE [OUTFILE]]\n"), argv[0]);
      return 0;
    }

  if (argc >= 2)
    {
      in = grub_util_fopen (argv[1], "r");
      if (!in)
	{
	  fprintf (stderr, _("cannot open `%s': %s"),
		   argv[1], strerror (errno));
	  return 1;
	}
    }
  else
    in = stdin;

  if (argc >= 3)
    {
      out = grub_util_fopen (argv[2], "w");
      if (!out)
	{					
	  if (in != stdin)
	    fclose (in);
	  fprintf (stderr, _("cannot open `%s': %s"),
		   argv[2], strerror (errno));
	  return 1;
	}
      out_fname = argv[2];
    }
  else
    out = stdout;

  while (1)
    {
      char *parsed;

      if (getline (&buf, &bufsize, in) < 0)
	break;

      {
	char *oldname = NULL;
	char *newsuffix;

	oldname = entryname;
	parsed = grub_legacy_parse (buf, &entryname, &newsuffix);
	if (newsuffix)
	  {
	    suffixlen += strlen (newsuffix);
	    suffix = xrealloc (suffix, suffixlen + 1);
	    strcat (suffix, newsuffix);
	  }
	if (oldname != entryname && oldname)
	  fprintf (out, "}\n\n");
	if (oldname != entryname)
	  {
	    char *escaped = grub_legacy_escape (entryname, strlen (entryname));
	    fprintf (out, "menuentry \'%s\' {\n", escaped);
	    free (escaped);
	    free (oldname);
	  }
      }

      if (parsed)
	fprintf (out, "%s%s", entryname ? "  " : "", parsed);
      free (parsed);
      parsed = NULL;
    }

  if (entryname)
    fprintf (out, "}\n\n");

  if (fwrite (suffix, 1, suffixlen, out) != suffixlen)
    {
      if (out_fname)
	grub_util_error ("cannot write to `%s': %s",
			 out_fname, strerror (errno));
      else
	grub_util_error ("cannot write to the stdout: %s", strerror (errno));
    }

  free (buf);
  free (suffix);
  free (entryname);

  if (in != stdin)
    fclose (in);
  if (out != stdout)
    fclose (out);

  return 0;
}
