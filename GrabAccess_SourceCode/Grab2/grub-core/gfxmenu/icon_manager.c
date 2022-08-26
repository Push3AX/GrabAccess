/* icon_manager.c - gfxmenu icon manager.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008,2009  Free Software Foundation, Inc.
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

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/gui_string_util.h>
#include <grub/bitmap.h>
#include <grub/bitmap_scale.h>
#include <grub/menu.h>
#include <grub/icon_manager.h>
#include <grub/env.h>

/* Currently hard coded to '.png' extension.  */
static const char icon_extension[] = ".png";

typedef struct icon_entry
{
  char *class_name;
  struct grub_video_bitmap *bitmap;
  struct icon_entry *next;
} *icon_entry_t;

struct grub_gfxmenu_icon_manager
{
  char *theme_path;
  int icon_width;
  int icon_height;

  /* Icon cache: linked list w/ dummy head node.  */
  struct icon_entry cache;
};


/* Create a new icon manager and return a point to it.  */
grub_gfxmenu_icon_manager_t
grub_gfxmenu_icon_manager_new (void)
{
  grub_gfxmenu_icon_manager_t mgr;
  mgr = grub_malloc (sizeof (*mgr));
  if (! mgr)
    return 0;

  mgr->theme_path = 0;
  mgr->icon_width = 0;
  mgr->icon_height = 0;

  /* Initialize the dummy head node.  */
  mgr->cache.class_name = 0;
  mgr->cache.bitmap = 0;
  mgr->cache.next = 0;

  return mgr;
}

/* Destroy the icon manager MGR, freeing all resources used by it.

Note: Any bitmaps returned by grub_gfxmenu_icon_manager_get_icon()
are destroyed and must not be used by the caller after this function
is called.  */
void
grub_gfxmenu_icon_manager_destroy (grub_gfxmenu_icon_manager_t mgr)
{
  grub_gfxmenu_icon_manager_clear_cache (mgr);
  grub_free (mgr->theme_path);
  grub_free (mgr);
}

/* Clear the icon cache.  */
void
grub_gfxmenu_icon_manager_clear_cache (grub_gfxmenu_icon_manager_t mgr)
{
  icon_entry_t cur;
  icon_entry_t next;
  for (cur = mgr->cache.next; cur; cur = next)
    {
      next = cur->next;
      grub_free (cur->class_name);
      grub_video_bitmap_destroy (cur->bitmap);
      grub_free (cur);
    }
  mgr->cache.next = 0;
}

/* Set the theme path.  If the theme path is changed, the icon cache
   is cleared.  */
void
grub_gfxmenu_icon_manager_set_theme_path (grub_gfxmenu_icon_manager_t mgr,
                                          const char *path)
{
  /* Clear the cache if the theme path has changed.  */
  if (mgr->theme_path == 0 && path == 0)
    return;
  if (mgr->theme_path == 0 || path == 0
      || grub_strcmp (mgr->theme_path, path) != 0)
    grub_gfxmenu_icon_manager_clear_cache (mgr);

  grub_free (mgr->theme_path);
  mgr->theme_path = path ? grub_strdup (path) : 0;
}

/* Set the icon size.  When icons are requested from the icon manager,
   they are scaled to this size before being returned.  If the size is
   changed, the icon cache is cleared.  */
void
grub_gfxmenu_icon_manager_set_icon_size (grub_gfxmenu_icon_manager_t mgr,
                                         int width, int height)
{
  /* If the width or height is changed, we must clear the cache, since the
     scaled bitmaps are stored in the cache.  */
  if (width != mgr->icon_width || height != mgr->icon_height)
    grub_gfxmenu_icon_manager_clear_cache (mgr);

  mgr->icon_width = width;
  mgr->icon_height = height;
}

/* Try to load an icon for the specified CLASS_NAME in the directory DIR.
   Returns 0 if the icon could not be loaded, or returns a pointer to a new
   bitmap if it was successful.  */
static struct grub_video_bitmap *
try_loading_icon (grub_gfxmenu_icon_manager_t mgr,
                  const char *dir, const char *class_name)
{
  char *path, *ptr;

  path = grub_malloc (grub_strlen (dir) + grub_strlen (class_name)
		      + grub_strlen (icon_extension) + 3);
  if (! path)
    return 0;

  ptr = grub_stpcpy (path, dir);
  if (path == ptr || ptr[-1] != '/')
    *ptr++ = '/';
  ptr = grub_stpcpy (ptr, class_name);
  ptr = grub_stpcpy (ptr, icon_extension);
  *ptr = '\0';

  struct grub_video_bitmap *raw_bitmap;
  grub_video_bitmap_load (&raw_bitmap, path);
  grub_free (path);
  grub_errno = GRUB_ERR_NONE;  /* Critical to clear the error!!  */
  if (! raw_bitmap)
    return 0;

  struct grub_video_bitmap *scaled_bitmap;
  grub_video_bitmap_create_scaled (&scaled_bitmap,
                                   mgr->icon_width, mgr->icon_height,
                                   raw_bitmap,
                                   GRUB_VIDEO_BITMAP_SCALE_METHOD_BEST);
  grub_video_bitmap_destroy (raw_bitmap);
  if (! scaled_bitmap)
    return 0;

  return scaled_bitmap;
}

/* Get the icon for the specified class CLASS_NAME.  If an icon for
   CLASS_NAME already exists in the cache, then a reference to the cached
   bitmap is returned.  If it is not cached, then it is loaded and cached.
   If no icon could be could for CLASS_NAME, then 0 is returned.  */
static struct grub_video_bitmap *
get_icon_by_class (grub_gfxmenu_icon_manager_t mgr, const char *class_name)
{
  /* First check the icon cache.  */
  icon_entry_t entry;
  for (entry = mgr->cache.next; entry; entry = entry->next)
    {
      if (grub_strcmp (entry->class_name, class_name) == 0)
        return entry->bitmap;
    }

  if (! mgr->theme_path)
    return 0;

  /* Otherwise, we search for an icon to load.  */
  char *theme_dir = grub_get_dirname (mgr->theme_path);
  char *icons_dir;
  struct grub_video_bitmap *icon;
  icon = 0;
  /* First try the theme's own icons, from "grub/themes/NAME/icons/"  */
  icons_dir = grub_resolve_relative_path (theme_dir, "icons/");
  if (icons_dir)
    {
      icon = try_loading_icon (mgr, icons_dir, class_name);
      grub_free (icons_dir);
    }

  grub_free (theme_dir);
  if (! icon)
    {
      const char *icondir;

      icondir = grub_env_get ("icondir");
      if (icondir)
	icon = try_loading_icon (mgr, icondir, class_name);
    }

  /* No icon was found.  */
  /* This should probably be noted in the cache, so that a search is not
     performed each time an icon for CLASS_NAME is requested.  */
  if (! icon)
    return 0;

  /* Insert a new cache entry for this icon.  */
  entry = grub_malloc (sizeof (*entry));
  if (! entry)
    {
      grub_video_bitmap_destroy (icon);
      return 0;
    }
  entry->class_name = grub_strdup (class_name);
  entry->bitmap = icon;
  entry->next = mgr->cache.next;
  mgr->cache.next = entry;   /* Link it into the cache.  */
  return entry->bitmap;
}

/* Get the best available icon for ENTRY.  Beginning with the first class
   listed in the menu entry and proceeding forward, an icon for each class
   is searched for.  The first icon found is returned.  The returned icon
   is scaled to the size specified by
   grub_gfxmenu_icon_manager_set_icon_size().

     Note:  Bitmaps returned by this function are destroyed when the
            icon manager is destroyed.
 */
struct grub_video_bitmap *
grub_gfxmenu_icon_manager_get_icon (grub_gfxmenu_icon_manager_t mgr,
                                    grub_menu_entry_t entry)
{
  struct grub_menu_entry_class *c;
  struct grub_video_bitmap *icon;

  /* Try each class in succession.  */
  icon = 0;
  for (c = entry->classes; c && ! icon; c = c->next)
    icon = get_icon_by_class (mgr, c->name);
  return icon;
}
