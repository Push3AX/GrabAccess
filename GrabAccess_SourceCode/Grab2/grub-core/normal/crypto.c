/* crypto.c - support crypto autoload */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009  Free Software Foundation, Inc.
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

#include <grub/dl.h>
#include <grub/mm.h>
#include <grub/env.h>
#include <grub/misc.h>
#include <grub/crypto.h>
#include <grub/normal.h>

struct load_spec
{
  struct load_spec *next;
  char *name;
  char *modname;
};

static struct load_spec *crypto_specs = NULL;

static void 
grub_crypto_autoload (const char *name)
{
  struct load_spec *cur;
  grub_dl_t mod;
  static int depth = 0;

  /* Some bufio of filesystems may want some crypto modules.
     It may result in infinite recursion. Hence this check.  */
  if (depth)
    return;
  depth++;

  for (cur = crypto_specs; cur; cur = cur->next)
    if (grub_strcasecmp (name, cur->name) == 0)
      {
	mod = grub_dl_load (cur->modname);
	if (mod)
	  grub_dl_ref (mod);
	grub_errno = GRUB_ERR_NONE;
      }
  depth--;
}

static void 
grub_crypto_spec_free (void)
{
  struct load_spec *cur, *next;
  for (cur = crypto_specs; cur; cur = next)
    {
      next = cur->next;
      grub_free (cur->name);
      grub_free (cur->modname);
      grub_free (cur);
    }
  crypto_specs = NULL;
}


/* Read the file crypto.lst for auto-loading.  */
void
read_crypto_list (const char *prefix)
{
  char *filename;
  grub_file_t file;
  char *buf = NULL;

  if (!prefix)
    {
      grub_errno = GRUB_ERR_NONE;
      return;
    }
  
  filename = grub_xasprintf ("%s/" GRUB_TARGET_CPU "-" GRUB_PLATFORM
			     "/crypto.lst", prefix);
  if (!filename)
    {
      grub_errno = GRUB_ERR_NONE;
      return;
    }

  file = grub_file_open (filename, GRUB_FILE_TYPE_GRUB_MODULE_LIST);
  grub_free (filename);
  if (!file)
    {
      grub_errno = GRUB_ERR_NONE;
      return;
    }

  /* Override previous crypto.lst.  */
  grub_crypto_spec_free ();

  for (;; grub_free (buf))
    {
      char *p, *name;
      struct load_spec *cur;
      
      buf = grub_file_getline (file);
	
      if (! buf)
	break;
      
      name = buf;
      while (grub_isspace (name[0]))
	name++;

      p = grub_strchr (name, ':');
      if (! p)
	continue;
      
      *p = '\0';
      p++;
      while (*p == ' ' || *p == '\t')
	p++;

      cur = grub_malloc (sizeof (*cur));
      if (!cur)
	{
	  grub_errno = GRUB_ERR_NONE;
	  continue;
	}
      
      cur->name = grub_strdup (name);
      if (! cur->name)
	{
	  grub_errno = GRUB_ERR_NONE;
	  grub_free (cur);
	  continue;
	}
	
      cur->modname = grub_strdup (p);
      if (! cur->modname)
	{
	  grub_errno = GRUB_ERR_NONE;
	  grub_free (cur->name);
	  grub_free (cur);
	  continue;
	}
      cur->next = crypto_specs;
      crypto_specs = cur;
    }
  
  grub_file_close (file);

  grub_errno = GRUB_ERR_NONE;

  grub_crypto_autoload_hook = grub_crypto_autoload;
}
