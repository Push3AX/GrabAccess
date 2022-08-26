/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013 Free Software Foundation, Inc.
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

#include <string.h>

#include <grub/emu/config.h>
#include <grub/util/misc.h>

void
grub_util_parse_config (FILE *f, struct grub_util_config *cfg, int simple)
{
  char *buffer = NULL;
  size_t sz = 0;
  while (getline (&buffer, &sz, f) >= 0)
    {
      const char *ptr;
      for (ptr = buffer; *ptr && grub_isspace (*ptr); ptr++);
      if (grub_strncmp (ptr, "GRUB_ENABLE_CRYPTODISK=",
			sizeof ("GRUB_ENABLE_CRYPTODISK=") - 1) == 0)
	{
	  ptr += sizeof ("GRUB_ENABLE_CRYPTODISK=") - 1;
	  if (*ptr == '"' || *ptr == '\'')
	    ptr++;
	  if (*ptr == 'y')
	    cfg->is_cryptodisk_enabled = 1;
	  continue;
	}
      if (grub_strncmp (ptr, "SUSE_BTRFS_SNAPSHOT_BOOTING=",
			sizeof ("SUSE_BTRFS_SNAPSHOT_BOOTING=") - 1) == 0)
	{
	  ptr += sizeof ("SUSE_BTRFS_SNAPSHOT_BOOTING=") - 1;
	  if (*ptr == '"' || *ptr == '\'')
	    ptr++;
	  if (grub_strncmp(ptr, "true", sizeof ("true") - 1) == 0)
	    cfg->is_suse_btrfs_snapshot_enabled = 1;
	  continue;
	}
      if (grub_strncmp (ptr, "GRUB_DISTRIBUTOR=",
			sizeof ("GRUB_DISTRIBUTOR=") - 1) == 0)
	{
	  char *optr;
	  enum { NONE, SNGLQUOT, DBLQUOT } state;

	  ptr += sizeof ("GRUB_DISTRIBUTOR=") - 1;

	  if (simple)
	    {
	      char *ptr2;
	      free (cfg->grub_distributor);
	      cfg->grub_distributor = xstrdup (ptr);
	      for (ptr2 = cfg->grub_distributor
		     + grub_strlen (cfg->grub_distributor) - 1;
		   ptr2 >= cfg->grub_distributor
		     && (*ptr2 == '\r' || *ptr2 == '\n'); ptr2--);
	      ptr2[1] = '\0';
	      continue;
	    }
	  free (cfg->grub_distributor);
	  cfg->grub_distributor = xmalloc (strlen (ptr) + 1);
	  optr = cfg->grub_distributor;
	  state = NONE;

	  for (; *ptr; ptr++)
	    switch (*ptr)
	      {
	      case '\\':
		if (state == SNGLQUOT)
		  {
		    *optr++ = *ptr;
		    continue;
		  }
		if (ptr[1])
		  {
		    *optr++ = ptr[1];
		    ptr++;
		    continue;
		  }
		ptr++;
		break;
	      case '"':
		if (state == NONE)
		  {
		    state = DBLQUOT;
		    continue;
		  }
		if (state == DBLQUOT)
		  {
		    state = NONE;
		    continue;
		  }
		*optr++ = *ptr;
		continue;
	      case '\'':
		if (state == SNGLQUOT)
		  {
		    state = NONE;
		    continue;
		  }
		if (state == NONE)
		  {
		    state = SNGLQUOT;
		    continue;
		  }
		*optr++ = *ptr;
		continue;
	      default:
		*optr++ = *ptr;
		continue;
	      }
	  *optr = '\0';
	}
    }
}

