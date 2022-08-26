/* macho2img.c - tool to convert Mach-O to raw imagw.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009 Free Software Foundation, Inc.
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
#include <grub/macho.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Please don't internationalise this file. It's pointless.  */

/* XXX: this file assumes particular Mach-O layout and does no checks. */
/* However as build system ensures correct usage of this tool this
   shouldn't be a problem. */

int
main (int argc, char **argv)
{
  FILE *in, *out;
  int do_bss = 0;
  char *buf;
  int bufsize;
  struct grub_macho_header32 *head;
  struct grub_macho_segment32 *curcmd;
  unsigned i;
  unsigned bssstart = 0;
  unsigned bssend = 0;

  if (argc && strcmp (argv[1], "--bss") == 0)
    do_bss = 1;
  if (argc < 2 + do_bss)
    {
      printf ("Usage: %s [--bss] filename.exec filename.img\n"
	      "Convert Mach-O into raw image\n", argv[0]);
      return 0;
    }
  in = fopen (argv[1 + do_bss], "rb");
  if (! in)
    {
      printf ("Couldn't open %s\n", argv[1 + do_bss]);
      return 1;
    }
  out = fopen (argv[2 + do_bss], "wb");
  if (! out)
    {
      fclose (in);
      printf ("Couldn't open %s\n", argv[2 + do_bss]);
      return 2;
    }
  fseek (in, 0, SEEK_END);
  bufsize = ftell (in);
  fseek (in, 0, SEEK_SET);
  buf = malloc (bufsize);
  if (! buf)
    {
      fclose (in);
      fclose (out);
      printf ("Couldn't allocate buffer\n");
      return 3;
    }
  fread (buf, 1, bufsize, in);
  head = (struct grub_macho_header32 *) buf;
  if (grub_le_to_cpu32 (head->magic) != GRUB_MACHO_MAGIC32)
    {
      fclose (in);
      fclose (out);
      free (buf);
      printf ("Invalid Mach-O file\n");
      return 4;
    }
  curcmd = (struct grub_macho_segment32 *) (buf + sizeof (*head));
  for (i = 0; i < grub_le_to_cpu32 (head->ncmds); i++,
	 curcmd = (struct grub_macho_segment32 *)
	 (((char *) curcmd) + curcmd->cmdsize))
    {
      if (curcmd->cmd != GRUB_MACHO_CMD_SEGMENT32)
	continue;
      fwrite (buf + grub_le_to_cpu32 (curcmd->fileoff), 1,
	      grub_le_to_cpu32 (curcmd->filesize), out);
      if (grub_le_to_cpu32 (curcmd->vmsize)
	  > grub_le_to_cpu32 (curcmd->filesize))
	{
	  bssstart = grub_le_to_cpu32 (curcmd->vmaddr)
	    + grub_le_to_cpu32 (curcmd->filesize) ;
	  bssend = grub_le_to_cpu32 (curcmd->vmaddr)
	    + grub_le_to_cpu32 (curcmd->vmsize) ;
	}
    }
  if (do_bss)
    {
      grub_uint32_t tmp;
      fseek (out, 0x5c, SEEK_SET);
      tmp = grub_cpu_to_le32 (bssstart);
      fwrite (&tmp, 4, 1, out);
      tmp = grub_cpu_to_le32 (bssend);
      fwrite (&tmp, 4, 1, out);
    }
  fclose (in);
  fclose (out);
  printf("macho2img complete\n");
  return 0;
}
