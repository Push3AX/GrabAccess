/* gui_circular_process.c - GUI circular progress indicator component.  */
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

#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/gui.h>
#include <grub/font.h>
#include <grub/gui_string_util.h>
#include <grub/gfxmenu_view.h>
#include <grub/gfxwidgets.h>
#include <grub/trig.h>

struct grub_gui_circular_progress
{
  struct grub_gui_progress progress;

  grub_gui_container_t parent;
  grub_video_rect_t bounds;
  char *id;
  int visible;
  int start;
  int end;
  int value;
  unsigned num_ticks;
  int start_angle;
  int ticks_disappear;
  char *theme_dir;
  int need_to_load_pixmaps;
  char *center_file;
  char *tick_file;
  struct grub_video_bitmap *center_bitmap;
  struct grub_video_bitmap *tick_bitmap;
};

typedef struct grub_gui_circular_progress *circular_progress_t;

static void
circprog_destroy (void *vself)
{
  circular_progress_t self = vself;
  grub_gfxmenu_timeout_unregister ((grub_gui_component_t) self);
  grub_free (self);
}

static const char *
circprog_get_id (void *vself)
{
  circular_progress_t self = vself;
  return self->id;
}

static int
circprog_is_instance (void *vself __attribute__((unused)), const char *type)
{
  return grub_strcmp (type, "component") == 0;
}

static struct grub_video_bitmap *
load_bitmap (const char *dir, const char *file)
{
  struct grub_video_bitmap *bitmap;
  char *abspath;

  /* Check arguments.  */
  if (! dir || ! file)
    return 0;

  /* Resolve to an absolute path.  */
  abspath = grub_resolve_relative_path (dir, file);
  if (! abspath)
    return 0;

  /* Load the image.  */
  grub_errno = GRUB_ERR_NONE;
  grub_video_bitmap_load (&bitmap, abspath);
  grub_errno = GRUB_ERR_NONE;

  grub_free (abspath);
  return bitmap;
}

static int
check_pixmaps (circular_progress_t self)
{
  if (self->need_to_load_pixmaps)
    {
      if (self->center_bitmap)
        grub_video_bitmap_destroy (self->center_bitmap);
      self->center_bitmap = load_bitmap (self->theme_dir, self->center_file);
      self->tick_bitmap = load_bitmap (self->theme_dir, self->tick_file);
      self->need_to_load_pixmaps = 0;
    }

  return (self->center_bitmap != 0 && self->tick_bitmap != 0);
}

static void
circprog_paint (void *vself, const grub_video_rect_t *region)
{
  circular_progress_t self = vself;

  if (! self->visible)
    return;

  if (!grub_video_have_common_points (region, &self->bounds))
    return;

  if (! check_pixmaps (self))
    return;

  grub_video_rect_t vpsave;
  grub_gui_set_viewport (&self->bounds, &vpsave);

  int width = self->bounds.width;
  int height = self->bounds.height;
  int center_width = grub_video_bitmap_get_width (self->center_bitmap);
  int center_height = grub_video_bitmap_get_height (self->center_bitmap);
  int tick_width = grub_video_bitmap_get_width (self->tick_bitmap);
  int tick_height = grub_video_bitmap_get_height (self->tick_bitmap);
  grub_video_blit_bitmap (self->center_bitmap, GRUB_VIDEO_BLIT_BLEND,
                          (width - center_width) / 2,
                          (height - center_height) / 2, 0, 0,
                          center_width, center_height);

  if (self->num_ticks)
    {
      int radius = grub_min (height, width) / 2 - grub_max (tick_height, tick_width) / 2 - 1;
      unsigned nticks;
      unsigned tick_begin;
      unsigned tick_end;
      if (self->end <= self->start
	  || self->value <= self->start)
	nticks = 0;
      else
	nticks = ((unsigned) (self->num_ticks
			      * (self->value - self->start)))
	  / ((unsigned) (self->end - self->start));
      /* Do ticks appear or disappear as the value approached the end?  */
      if (self->ticks_disappear)
	{
	  tick_begin = nticks;
	  tick_end = self->num_ticks;
	}
      else
	{
	  tick_begin = 0;
	  tick_end = nticks;
	}

      unsigned i;
      for (i = tick_begin; i < tick_end; i++)
	{
	  int x;
	  int y;
	  int angle;

	  /* Calculate the location of the tick.  */
	  angle = self->start_angle
	    + i * GRUB_TRIG_ANGLE_MAX / self->num_ticks;
	  x = width / 2 + (grub_cos (angle) * radius / GRUB_TRIG_FRACTION_SCALE);
	  y = height / 2 + (grub_sin (angle) * radius / GRUB_TRIG_FRACTION_SCALE);

	  /* Adjust (x,y) so the tick is centered.  */
	  x -= tick_width / 2;
	  y -= tick_height / 2;

	  /* Draw the tick.  */
	  grub_video_blit_bitmap (self->tick_bitmap, GRUB_VIDEO_BLIT_BLEND,
				  x, y, 0, 0, tick_width, tick_height);
	}
    }
  grub_gui_restore_viewport (&vpsave);
}

static void
circprog_set_parent (void *vself, grub_gui_container_t parent)
{
  circular_progress_t self = vself;
  self->parent = parent;
}

static grub_gui_container_t
circprog_get_parent (void *vself)
{
  circular_progress_t self = vself;
  return self->parent;
}

static void
circprog_set_bounds (void *vself, const grub_video_rect_t *bounds)
{
  circular_progress_t self = vself;
  self->bounds = *bounds;
}

static void
circprog_get_bounds (void *vself, grub_video_rect_t *bounds)
{
  circular_progress_t self = vself;
  *bounds = self->bounds;
}

static void
circprog_set_state (void *vself, int visible, int start,
		    int current, int end)
{
  circular_progress_t self = vself;  
  self->visible = visible;
  self->start = start;
  self->value = current;
  self->end = end;
}

static int
parse_angle (const char *value)
{
  const char *ptr;
  int angle;

  angle = grub_strtol (value, &ptr, 10);
  if (grub_errno)
    return 0;
  while (grub_isspace (*ptr))
    ptr++;
  if (grub_strcmp (ptr, "deg") == 0
      /* Unicode symbol of degrees (a circle, U+b0). Put here in UTF-8 to
	 avoid potential problem with text file reesncoding  */
      || grub_strcmp (ptr, "\xc2\xb0") == 0)
    angle = grub_divide_round (angle * 64, 90);
  return angle;
}

static grub_err_t
circprog_set_property (void *vself, const char *name, const char *value)
{
  circular_progress_t self = vself;
  if (grub_strcmp (name, "num_ticks") == 0)
    {
      self->num_ticks = grub_strtoul (value, 0, 10);
    }
  else if (grub_strcmp (name, "start_angle") == 0)
    {
      self->start_angle = parse_angle (value);
    }
  else if (grub_strcmp (name, "ticks_disappear") == 0)
    {
      self->ticks_disappear = grub_strcmp (value, "false") != 0;
    }
  else if (grub_strcmp (name, "center_bitmap") == 0)
    {
      self->need_to_load_pixmaps = 1;
      grub_free (self->center_file);
      self->center_file = value ? grub_strdup (value) : 0;
    }
  else if (grub_strcmp (name, "tick_bitmap") == 0)
    {
      self->need_to_load_pixmaps = 1;
      grub_free (self->tick_file);
      self->tick_file = value ? grub_strdup (value) : 0;
    }
  else if (grub_strcmp (name, "theme_dir") == 0)
    {
      self->need_to_load_pixmaps = 1;
      grub_free (self->theme_dir);
      self->theme_dir = value ? grub_strdup (value) : 0;
    }
  else if (grub_strcmp (name, "id") == 0)
    {
      grub_gfxmenu_timeout_unregister ((grub_gui_component_t) self);
      grub_free (self->id);
      if (value)
        self->id = grub_strdup (value);
      else
        self->id = 0;
      if (self->id && grub_strcmp (self->id, GRUB_GFXMENU_TIMEOUT_COMPONENT_ID)
	  == 0)
	grub_gfxmenu_timeout_register ((grub_gui_component_t) self,
				       circprog_set_state);
    }
  return grub_errno;
}

static struct grub_gui_component_ops circprog_ops =
{
  .destroy = circprog_destroy,
  .get_id = circprog_get_id,
  .is_instance = circprog_is_instance,
  .paint = circprog_paint,
  .set_parent = circprog_set_parent,
  .get_parent = circprog_get_parent,
  .set_bounds = circprog_set_bounds,
  .get_bounds = circprog_get_bounds,
  .set_property = circprog_set_property
};

static struct grub_gui_progress_ops circprog_prog_ops =
  {
    .set_state = circprog_set_state
  };

grub_gui_component_t
grub_gui_circular_progress_new (void)
{
  circular_progress_t self;
  self = grub_zalloc (sizeof (*self));
  if (! self)
    return 0;
  self->progress.ops = &circprog_prog_ops;
  self->progress.component.ops = &circprog_ops;
  self->visible = 1;
  self->num_ticks = 64;
  self->start_angle = -64;

  return (grub_gui_component_t) self;
}
