/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2006,2007,2008,2009,2010,2011,2012,2013  Free Software Foundation, Inc.
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

#include <config-util.h>
#include <config.h>

#include <grub/emu/getroot.h>
#include <grub/mm.h>

#ifdef HAVE_DEVICE_MAPPER

#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#include <grub/osdep/major.h>
#include <libdevmapper.h>

#include <grub/types.h>
#include <grub/util/misc.h>

#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/emu/misc.h>
#include <grub/emu/hostdisk.h>

static int
grub_util_open_dm (const char *os_dev, struct dm_tree **tree,
		   struct dm_tree_node **node)
{
  uint32_t maj, min;
  struct stat st;

  *node = NULL;
  *tree = NULL;

  if (stat (os_dev, &st) < 0)
    return 0;

  maj = major (st.st_rdev);
  min = minor (st.st_rdev);

  if (!dm_is_dm_major (maj))
    return 0;

  *tree = dm_tree_create ();
  if (! *tree)
    {
      grub_puts_ (N_("Failed to create `device-mapper' tree"));
      grub_dprintf ("hostdisk", "dm_tree_create failed\n");
      return 0;
    }

  if (! dm_tree_add_dev (*tree, maj, min))
    {
      grub_dprintf ("hostdisk", "dm_tree_add_dev failed\n");
      dm_tree_free (*tree);
      *tree = NULL;
      return 0;
    }

  *node = dm_tree_find_node (*tree, maj, min);
  if (! *node)
    {
      grub_dprintf ("hostdisk", "dm_tree_find_node failed\n");
      dm_tree_free (*tree);
      *tree = NULL;
      return 0;
    }
  return 1;
}

static char *
get_dm_uuid (const char *os_dev)
{
  struct dm_tree *tree;
  struct dm_tree_node *node;
  const char *node_uuid;
  char *ret;

  if (!grub_util_open_dm (os_dev, &tree, &node))
    return NULL;

  node_uuid = dm_tree_node_get_uuid (node);
  if (! node_uuid)
    {
      grub_dprintf ("hostdisk", "%s has no DM uuid\n", os_dev);
      dm_tree_free (tree);
      return NULL;
    }

  ret = grub_strdup (node_uuid);

  dm_tree_free (tree);

  return ret;
}

enum grub_dev_abstraction_types
grub_util_get_dm_abstraction (const char *os_dev)
{
  char *uuid;

  uuid = get_dm_uuid (os_dev);

  if (uuid == NULL)
    return GRUB_DEV_ABSTRACTION_NONE;

  if (strncmp (uuid, "LVM-", 4) == 0)
    {
      grub_free (uuid);
      return GRUB_DEV_ABSTRACTION_LVM;
    }
  if (strncmp (uuid, "CRYPT-LUKS1-", 12) == 0)
    {
      grub_free (uuid);
      return GRUB_DEV_ABSTRACTION_LUKS;
    }

  grub_free (uuid);
  return GRUB_DEV_ABSTRACTION_NONE;
}

void
grub_util_pull_devmapper (const char *os_dev)
{
  struct dm_tree *tree;
  struct dm_tree_node *node;
  struct dm_tree_node *child;
  void *handle = NULL;
  char *lastsubdev = NULL;
  char *uuid;

  uuid = get_dm_uuid (os_dev);

  if (!grub_util_open_dm (os_dev, &tree, &node))
    {
      grub_free (uuid);
      return;
    }

  while ((child = dm_tree_next_child (&handle, node, 0)))
    {
      const struct dm_info *dm = dm_tree_node_get_info (child);
      char *subdev;
      if (!dm)
	continue;
      subdev = grub_find_device ("/dev", makedev (dm->major, dm->minor));
      if (subdev)
	{
	  lastsubdev = subdev;
	  grub_util_pull_device (subdev);
	}
    }
  if (uuid && strncmp (uuid, "CRYPT-LUKS1-", sizeof ("CRYPT-LUKS1-") - 1) == 0
      && lastsubdev)
    {
      char *grdev = grub_util_get_grub_dev (lastsubdev);
      dm_tree_free (tree);
      if (grdev)
	{
	  grub_err_t err;
	  err = grub_cryptodisk_cheat_mount (grdev, os_dev);
	  if (err)
	    grub_util_error (_("can't mount encrypted volume `%s': %s"),
			     lastsubdev, grub_errmsg);
	}
      grub_free (grdev);
    }
  else
    dm_tree_free (tree);
  grub_free (uuid);
}

char *
grub_util_devmapper_part_to_disk (struct stat *st,
				  int *is_part, const char *path)
{
  int major, minor;

  if (grub_util_get_dm_node_linear_info (st->st_rdev,
					 &major, &minor, 0))
    {
      *is_part = 1;
      return grub_find_device ("/dev", makedev (major, minor));
    }
  *is_part = 0;
  return xstrdup (path);
}

char *
grub_util_get_devmapper_grub_dev (const char *os_dev)
{
  char *uuid, *optr;
  char *grub_dev;

  uuid = get_dm_uuid (os_dev);
  if (!uuid)
    return NULL;

  switch (grub_util_get_dev_abstraction (os_dev))
    {
    case GRUB_DEV_ABSTRACTION_LVM:
      {
	unsigned i;
	int dashes[] = { 0, 6, 10, 14, 18, 22, 26, 32, 38, 42, 46, 50, 54, 58};

	grub_dev = xmalloc (grub_strlen (uuid) + 40);
	optr = grub_stpcpy (grub_dev, "lvmid/");
	for (i = 0; i < ARRAY_SIZE (dashes) - 1; i++)
	  {
	    memcpy (optr, uuid + sizeof ("LVM-") - 1 + dashes[i],
		    dashes[i+1] - dashes[i]);
	    optr += dashes[i+1] - dashes[i];
	    *optr++ = '-';
	  }
	optr = stpcpy (optr, uuid + sizeof ("LVM-") - 1 + dashes[i]);
	*optr = '\0';
	grub_dev[sizeof("lvmid/xxxxxx-xxxx-xxxx-xxxx-xxxx-xxxx-xxxxxx") - 1]
	  = '/';
	free (uuid);
	return grub_dev;
      }

    case GRUB_DEV_ABSTRACTION_LUKS:
      {
	char *dash;

	dash = grub_strchr (uuid + sizeof ("CRYPT-LUKS1-") - 1, '-');
	if (dash)
	  *dash = 0;
	grub_dev = grub_xasprintf ("cryptouuid/%s",
				   uuid + sizeof ("CRYPT-LUKS1-") - 1);
	grub_free (uuid);
	return grub_dev;
      }

    default:
      grub_free (uuid);
      return NULL;
    }
}

char *
grub_util_get_vg_uuid (const char *os_dev)
{
  char *uuid, *vgid;
  int dashes[] = { 0, 6, 10, 14, 18, 22, 26, 32};
  unsigned i;
  char *optr;

  uuid = get_dm_uuid (os_dev);
  if (!uuid)
    return NULL;

  vgid = xmalloc (grub_strlen (uuid));
  optr = vgid;
  for (i = 0; i < ARRAY_SIZE (dashes) - 1; i++)
    {
      memcpy (optr, uuid + sizeof ("LVM-") - 1 + dashes[i],
	      dashes[i+1] - dashes[i]);
      optr += dashes[i+1] - dashes[i];
      *optr++ = '-';
    }
  optr--;
  *optr = '\0';
  grub_free (uuid);
  return vgid;
}

void
grub_util_devmapper_cleanup (void)
{
  dm_lib_release ();
}

#else
void
grub_util_pull_devmapper (const char *os_dev __attribute__ ((unused)))
{
  return;
}

void
grub_util_devmapper_cleanup (void)
{
}

enum grub_dev_abstraction_types
grub_util_get_dm_abstraction (const char *os_dev __attribute__ ((unused)))
{
  return GRUB_DEV_ABSTRACTION_NONE;
}

char *
grub_util_get_vg_uuid (const char *os_dev __attribute__ ((unused)))
{
  return NULL;
}

char *
grub_util_devmapper_part_to_disk (struct stat *st __attribute__ ((unused)),
				  int *is_part __attribute__ ((unused)),
				  const char *os_dev __attribute__ ((unused)))
{
  return NULL;
}

char *
grub_util_get_devmapper_grub_dev (const char *os_dev __attribute__ ((unused)))
{
  return NULL;
}

#endif
