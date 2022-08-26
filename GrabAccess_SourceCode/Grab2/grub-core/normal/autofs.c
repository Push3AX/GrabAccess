/* autofs.c - support auto-loading from fs.lst */
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

#include <grub/mm.h>
#include <grub/dl.h>
#include <grub/env.h>
#include <grub/misc.h>
#include <grub/fs.h>
#include <grub/normal.h>

/* This is used to store the names of filesystem modules for auto-loading.  */
static grub_named_list_t fs_module_list;

/* The auto-loading hook for filesystems.  */
static int
autoload_fs_module (void)
{
  grub_named_list_t p;
  int ret = 0;

  while ((p = fs_module_list) != NULL)
    {
      if (! grub_dl_get (p->name) && grub_dl_load (p->name))
	{
	  ret = 1;
	  break;
	}

      if (grub_errno)
	grub_print_error ();

      fs_module_list = p->next;
      grub_free (p->name);
      grub_free (p);
    }

  return ret;
}

/* Read the file fs.lst for auto-loading.  */
void
read_fs_list (const char *prefix)
{
  if (prefix)
    {
      char *filename;

      filename = grub_xasprintf ("%s/" GRUB_TARGET_CPU "-" GRUB_PLATFORM
				 "/fs.lst", prefix);
      if (filename)
	{
	  grub_file_t file;
	  grub_fs_autoload_hook_t tmp_autoload_hook;

	  /* This rules out the possibility that read_fs_list() is invoked
	     recursively when we call grub_file_open() below.  */
	  tmp_autoload_hook = grub_fs_autoload_hook;
	  grub_fs_autoload_hook = NULL;

	  file = grub_file_open (filename, GRUB_FILE_TYPE_GRUB_MODULE_LIST);
	  if (file)
	    {
	      /* Override previous fs.lst.  */
	      while (fs_module_list)
		{
		  grub_named_list_t tmp;
		  tmp = fs_module_list->next;
		  grub_free (fs_module_list);
		  fs_module_list = tmp;
		}

	      while (1)
		{
		  char *buf;
		  char *p;
		  char *q;
		  grub_named_list_t fs_mod;

		  buf = grub_file_getline (file);
		  if (! buf)
		    break;

		  p = buf;
		  q = buf + grub_strlen (buf) - 1;

		  /* Ignore space.  */
		  while (grub_isspace (*p))
		    p++;

		  while (p < q && grub_isspace (*q))
		    *q-- = '\0';

		  /* If the line is empty, skip it.  */
		  if (p >= q)
		    {
		      grub_free (buf);
		      continue;
		    }

		  fs_mod = grub_malloc (sizeof (*fs_mod));
		  if (! fs_mod)
		    {
		      grub_free (buf);
		      continue;
		    }

		  fs_mod->name = grub_strdup (p);
		  grub_free (buf);
		  if (! fs_mod->name)
		    {
		      grub_free (fs_mod);
		      continue;
		    }

		  fs_mod->next = fs_module_list;
		  fs_module_list = fs_mod;
		}

	      grub_file_close (file);
	      grub_fs_autoload_hook = tmp_autoload_hook;
	    }

	  grub_free (filename);
	}
    }

  /* Ignore errors.  */
  grub_errno = GRUB_ERR_NONE;

  /* Set the hook.  */
  grub_fs_autoload_hook = autoload_fs_module;
}
