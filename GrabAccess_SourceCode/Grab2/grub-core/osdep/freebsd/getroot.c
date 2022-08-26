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

#include <grub/types.h>
# include <sys/param.h>
# include <sys/mount.h>
# include <sys/disk.h> /* DIOCGMEDIASIZE */
# include <sys/param.h>
# include <sys/sysctl.h>
#include <libgeom.h>

#include <grub/util/misc.h>

#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/emu/misc.h>
#include <grub/emu/hostdisk.h>
#include <grub/emu/getroot.h>
#include <grub/cryptodisk.h>

#include <sys/wait.h>

#include <libgeom.h>

#define LVM_DEV_MAPPER_STRING "/dev/linux_lvm/"

static const char *
grub_util_get_geom_abstraction (const char *dev)
{
  char *whole;
  struct gmesh mesh;
  struct gclass *class;
  const char *name;
  int err;

  if (strncmp (dev, "/dev/", sizeof ("/dev/") - 1) != 0)
    return 0;
  name = dev + sizeof ("/dev/") - 1;
  grub_util_follow_gpart_up (name, NULL, &whole);

  grub_util_info ("following geom '%s'", name);

  err = geom_gettree (&mesh);
  if (err != 0)
    /* TRANSLATORS: geom is the name of (k)FreeBSD device framework.
       Usually left untranslated.
     */
    grub_util_error ("%s", _("couldn't open geom"));

  LIST_FOREACH (class, &mesh.lg_class, lg_class)
    {
      struct ggeom *geom;
      LIST_FOREACH (geom, &class->lg_geom, lg_geom)
	{ 
	  struct gprovider *provider;
	  LIST_FOREACH (provider, &geom->lg_provider, lg_provider)
	    if (strcmp (provider->lg_name, name) == 0)
	      return class->lg_name;
	}
    }
  return NULL;
}

enum grub_dev_abstraction_types
grub_util_get_dev_abstraction_os (const char *os_dev)
{
  const char *abstrac;
  abstrac = grub_util_get_geom_abstraction (os_dev);
  grub_util_info ("abstraction of %s is %s", os_dev, abstrac);
  if (abstrac && grub_strcasecmp (abstrac, "eli") == 0)
    return GRUB_DEV_ABSTRACTION_GELI;

  /* Check for LVM.  */
  if (!strncmp (os_dev, LVM_DEV_MAPPER_STRING, sizeof(LVM_DEV_MAPPER_STRING)-1))
    return GRUB_DEV_ABSTRACTION_LVM;
  return GRUB_DEV_ABSTRACTION_NONE;
}

char *
grub_util_part_to_disk (const char *os_dev, struct stat *st,
			int *is_part)
{
  char *out, *out2;

  if (! S_ISCHR (st->st_mode))
    {
      *is_part = 0;
      return xstrdup (os_dev);
    }

  if (strncmp (os_dev, "/dev/", sizeof ("/dev/") - 1) != 0)
    return xstrdup (os_dev);
  grub_util_follow_gpart_up (os_dev + sizeof ("/dev/") - 1, NULL, &out);

  if (grub_strcmp (os_dev + sizeof ("/dev/") - 1, out) != 0)
    *is_part = 1;
  out2 = xasprintf ("/dev/%s", out);
  free (out);

  return out2;
}

int
grub_util_pull_device_os (const char *os_dev,
			  enum grub_dev_abstraction_types ab)
{
  switch (ab)
    {
    case GRUB_DEV_ABSTRACTION_GELI:
      {
	char *whole;
	struct gmesh mesh;
	struct gclass *class;
	const char *name;
	int err;
	char *lastsubdev = NULL;

	if (strncmp (os_dev, "/dev/", sizeof ("/dev/") - 1) != 0)
	  return 1;
	name = os_dev + sizeof ("/dev/") - 1;
	grub_util_follow_gpart_up (name, NULL, &whole);

	grub_util_info ("following geom '%s'", name);

	err = geom_gettree (&mesh);
	if (err != 0)
	  /* TRANSLATORS: geom is the name of (k)FreeBSD device framework.
	     Usually left untranslated.
	  */
	  grub_util_error ("%s", _("couldn't open geom"));

	LIST_FOREACH (class, &mesh.lg_class, lg_class)
	  {
	    struct ggeom *geom;
	    LIST_FOREACH (geom, &class->lg_geom, lg_geom)
	      { 
		struct gprovider *provider;
		LIST_FOREACH (provider, &geom->lg_provider, lg_provider)
		  if (strcmp (provider->lg_name, name) == 0)
		    {
		      struct gconsumer *consumer;
		      char *fname;

		      LIST_FOREACH (consumer, &geom->lg_consumer, lg_consumer)
			break;
		      if (!consumer)
			grub_util_error ("%s",
					 _("couldn't find geli consumer"));
		      fname = xasprintf ("/dev/%s", consumer->lg_provider->lg_name);
		      grub_util_info ("consumer %s", consumer->lg_provider->lg_name);
		      lastsubdev = consumer->lg_provider->lg_name;
		      grub_util_pull_device (fname);
		      free (fname);
		    }
	      }
	  }
	if (ab == GRUB_DEV_ABSTRACTION_GELI && lastsubdev)
	  {
	    char *fname = xasprintf ("/dev/%s", lastsubdev);
	    char *grdev = grub_util_get_grub_dev (fname);
	    free (fname);

	    if (grdev)
	      {
		grub_err_t gr_err;
		gr_err = grub_cryptodisk_cheat_mount (grdev, os_dev);
		if (gr_err)
		  grub_util_error (_("can't mount encrypted volume `%s': %s"),
				   lastsubdev, grub_errmsg);
	      }

	    grub_free (grdev);
	  }
      }
      return 1;
    default:
      return 0;
    }
}

char *
grub_util_get_grub_dev_os (const char *os_dev)
{
  char *grub_dev = NULL;

  switch (grub_util_get_dev_abstraction (os_dev))
    {
      /* Fallback for non-devmapper build. In devmapper-builds LVM is handled
	 in rub_util_get_devmapper_grub_dev and this point isn't reached.
       */
    case GRUB_DEV_ABSTRACTION_LVM:
      {
	unsigned short len;
	grub_size_t offset = sizeof (LVM_DEV_MAPPER_STRING) - 1;

	len = strlen (os_dev) - offset + 1;
	grub_dev = xmalloc (len + sizeof ("lvm/"));

	grub_memcpy (grub_dev, "lvm/", sizeof ("lvm/") - 1);
	grub_memcpy (grub_dev + sizeof ("lvm/") - 1, os_dev + offset, len);
      }
      break;

    case GRUB_DEV_ABSTRACTION_GELI:
      {
	char *whole;
	struct gmesh mesh;
	struct gclass *class;
	const char *name;
	int err;

	if (strncmp (os_dev, "/dev/", sizeof ("/dev/") - 1) != 0)
	  return 0;
	name = os_dev + sizeof ("/dev/") - 1;
	grub_util_follow_gpart_up (name, NULL, &whole);

	grub_util_info ("following geom '%s'", name);

	err = geom_gettree (&mesh);
	if (err != 0)
	  /* TRANSLATORS: geom is the name of (k)FreeBSD device framework.
	     Usually left untranslated.
	  */
	  grub_util_error ("%s", _("couldn't open geom"));

	LIST_FOREACH (class, &mesh.lg_class, lg_class)
	  {
	    struct ggeom *geom;
	    LIST_FOREACH (geom, &class->lg_geom, lg_geom)
	      { 
		struct gprovider *provider;
		LIST_FOREACH (provider, &geom->lg_provider, lg_provider)
		  if (strcmp (provider->lg_name, name) == 0)
		    {
		      struct gconsumer *consumer;
		      char *fname;
		      char *uuid;

		      LIST_FOREACH (consumer, &geom->lg_consumer, lg_consumer)
			break;
		      if (!consumer)
			grub_util_error ("%s",
					 _("couldn't find geli consumer"));
		      fname = xasprintf ("/dev/%s", consumer->lg_provider->lg_name);
		      uuid = grub_util_get_geli_uuid (fname);
		      if (!uuid)
			grub_util_error ("%s",
					 _("couldn't retrieve geli UUID"));
		      grub_dev = xasprintf ("cryptouuid/%s", uuid);
		      free (fname);
		      free (uuid);
		    }
	      }
	  }
      }
      break;

    default:  
      break;
    }

  return grub_dev;
}

/* FIXME: geom actually gives us the whole container hierarchy.
   It can be used more efficiently than this.  */
void
grub_util_follow_gpart_up (const char *name, grub_disk_addr_t *off_out, char **name_out)
{
  struct gmesh mesh;
  struct gclass *class;
  int err;
  struct ggeom *geom;

  grub_util_info ("following geom '%s'", name);

  err = geom_gettree (&mesh);
  if (err != 0)
    /* TRANSLATORS: geom is the name of (k)FreeBSD device framework.
       Usually left untranslated.
     */
    grub_util_error ("%s", _("couldn't open geom"));

  LIST_FOREACH (class, &mesh.lg_class, lg_class)
    if (strcasecmp (class->lg_name, "part") == 0)
      break;
  if (!class)
    /* TRANSLATORS: geom is the name of (k)FreeBSD device framework.
       Usually left untranslated. "part" is the identifier of one of its
       classes.  */
    grub_util_error ("%s", _("couldn't find geom `part' class"));

  LIST_FOREACH (geom, &class->lg_geom, lg_geom)
    { 
      struct gprovider *provider;
      LIST_FOREACH (provider, &geom->lg_provider, lg_provider)
	if (strcmp (provider->lg_name, name) == 0)
	  {
	    char *name_tmp = xstrdup (geom->lg_name);
	    grub_disk_addr_t off = 0;
	    struct gconfig *config;
	    grub_util_info ("geom '%s' has parent '%s'", name, geom->lg_name);

	    grub_util_follow_gpart_up (name_tmp, &off, name_out);
	    free (name_tmp);
	    LIST_FOREACH (config, &provider->lg_config, lg_config)
	      if (strcasecmp (config->lg_name, "offset") == 0)
		off += strtoull (config->lg_val, 0, 10) / provider->lg_sectorsize;
	    if (off_out)
	      *off_out = off;
	    return;
	  }
    }
  grub_util_info ("geom '%s' has no parent", name);
  if (name_out)
    *name_out = xstrdup (name);
  if (off_out)
    *off_out = 0;
}

grub_disk_addr_t
grub_util_find_partition_start_os (const char *dev)
{
  grub_disk_addr_t out;
  if (strncmp (dev, "/dev/", sizeof ("/dev/") - 1) != 0)
    return 0;
  grub_util_follow_gpart_up (dev + sizeof ("/dev/") - 1, &out, NULL);

  return out;
}
