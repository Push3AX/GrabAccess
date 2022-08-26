/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2007  Free Software Foundation, Inc.
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include <grub/emu/misc.h>
#include <grub/misc.h>
#include <grub/util/misc.h>
#include <grub/util/resolve.h>
#include <grub/i18n.h>

/* Module.  */
struct mod_list
{
  const char *name;
  struct mod_list *next;
};

/* Dependency.  */
struct dep_list
{
  const char *name;
  struct mod_list *list;
  struct dep_list *next;
};

static char buf[1024];

static void
free_mod_list (struct mod_list *head)
{
  while (head)
    {
      struct mod_list *next;

      next = head->next;
      free ((void *) head->name);
      free (head);
      head = next;
    }
}

static void
free_dep_list (struct dep_list *head)
{
  while (head)
    {
      struct dep_list *next;

      next = head->next;
      free ((void *) head->name);
      free_mod_list (head->list);
      free (head);
      head = next;
    }
}

/* Read the list of dependencies.  */
static struct dep_list *
read_dep_list (FILE *fp)
{
  struct dep_list *dep_list = 0;

  while (fgets (buf, sizeof (buf), fp))
    {
      char *p;
      struct dep_list *dep;

      /* Get the target name.  */
      p = strchr (buf, ':');
      if (! p)
	grub_util_error (_("invalid line format: %s"), buf);

      *p++ = '\0';

      dep = xmalloc (sizeof (*dep));
      dep->name = xstrdup (buf);
      dep->list = 0;

      dep->next = dep_list;
      dep_list = dep;

      /* Add dependencies.  */
      while (p < (buf + sizeof (buf)) && *p)
	{
	  struct mod_list *mod;
	  char *name;

	  /* Skip whitespace.  */
	  while (*p && grub_isspace (*p))
	    p++;

	  if (! *p)
	    break;

	  name = p;

	  /* Skip non-whitespace.  */
	  while (*p && ! grub_isspace (*p))
	    p++;

	  *p++ = '\0';

	  mod = (struct mod_list *) xmalloc (sizeof (*mod));
	  mod->name = xstrdup (name);
	  mod->next = dep->list;
	  dep->list = mod;
	}

	if ((p - buf) == sizeof (buf))
	  grub_util_error (_("line too long, length greater than %zu: module %s"), sizeof (buf), dep->name);
    }

  return dep_list;
}

static char *
get_module_name (const char *str)
{
  char *base;
  char *ext;

  base = strrchr (str, '/');
  if (! base)
    base = (char *) str;
  else
    base++;

  ext = strrchr (base, '.');
  if (ext && strcmp (ext, ".mod") == 0)
    {
      char *name;

      name = xmalloc (ext - base + 1);
      memcpy (name, base, ext - base);
      name[ext - base] = '\0';
      return name;
    }

  return xstrdup (base);
}

static char *
get_module_path (const char *prefix, const char *str)
{
  char *dir;
  char *base;
  char *ext;
  char *ret;

  ext = strrchr (str, '.');
  if (ext && strcmp (ext, ".mod") == 0)
    base = xstrdup (str);
  else
    {
      base = xmalloc (strlen (str) + 4 + 1);
      sprintf (base, "%s.mod", str);
    }

  dir = strchr (str, '/');
  if (dir)
    return base;

  ret = grub_util_get_path (prefix, base);
  free (base);
  return ret;
}

static void
add_module (const char *dir,
	    struct dep_list *dep_list,
	    struct mod_list **mod_head,
	    struct grub_util_path_list **path_head,
	    const char *name)
{
  char *mod_name;
  struct grub_util_path_list *path;
  struct mod_list *mod;
  struct dep_list *dep;

  mod_name = get_module_name (name);

  /* Check if the module has already been added.  */
  for (mod = *mod_head; mod; mod = mod->next)
    if (strcmp (mod->name, mod_name) == 0)
      {
	free (mod_name);
	return;
      }

  /* Resolve dependencies.  */
  for (dep = dep_list; dep; dep = dep->next)
    if (strcmp (dep->name, mod_name) == 0)
      {
	for (mod = dep->list; mod; mod = mod->next)
	  add_module (dir, dep_list, mod_head, path_head, mod->name);

	break;
      }

  /* Add this module.  */
  mod = (struct mod_list *) xmalloc (sizeof (*mod));
  mod->name = mod_name;
  mod->next = *mod_head;
  *mod_head = mod;

  /* Add this path.  */
  path = (struct grub_util_path_list *) xmalloc (sizeof (*path));
  path->name = get_module_path (dir, name);
  path->next = *path_head;
  *path_head = path;
}

struct grub_util_path_list *
grub_util_resolve_dependencies (const char *prefix,
				const char *dep_list_file,
				char *modules[])
{
  char *path;
  FILE *fp;
  struct dep_list *dep_list;
  struct mod_list *mod_list = 0;
  struct grub_util_path_list *path_list = 0;

  path = grub_util_get_path (prefix, dep_list_file);
  fp = grub_util_fopen (path, "r");
  if (! fp)
    grub_util_error (_("cannot open `%s': %s"), path, strerror (errno));

  free (path);
  dep_list = read_dep_list (fp);
  fclose (fp);

  while (*modules)
    {
      add_module (prefix, dep_list, &mod_list, &path_list, *modules);
      modules++;
    }

  free_dep_list (dep_list);
  free_mod_list (mod_list);

  { /* Reverse the path_list */
    struct grub_util_path_list *p, *prev, *next;

    for (p = path_list, prev = NULL; p; p = next)
      {
	next = p->next;
	p->next = prev;
	prev = p;
      }

    return prev;
  }
}

void
grub_util_free_path_list (struct grub_util_path_list *path_list)
{
  struct grub_util_path_list *next;

  while (path_list)
    {
      next = path_list->next;
      free ((void *) path_list->name);
      free (path_list);
      path_list = next;
    }
}
