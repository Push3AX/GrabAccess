/* engine_core.c - According to user settings, generate animation.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright 2015,2017 Ruyi Boy - All Rights Reserved
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
#include <grub/gui.h>
#include <grub/video.h>
#include <grub/bitmap.h>
#include <grub/bitmap_scale.h>
#include <grub/gfxmenu_view.h>
#include <grub/menu.h>

#define EXPLOSION_PROOF 35
#define PNG_EXTENSION ".png"
#define JPG_EXTENSION ".jpg"
#define JPEG_EXTENSION ".jpeg"
#define TGA_EXTENSION ".tga"
#define NOW_NORMAL_PLAY 0
#define NOW_PAUSE_PLAY 1
#define NOW_NOT_FOLLOW 0
#define NOW_FOLLOW_MENU 1
#define ATTACH_MENU_LEFT 0
#define ATTACH_MENU_RIGHT 1

typedef struct engine_picture_class *picture_class_t;
typedef struct engine_animation_class *animation_class_t;

enum play_mode
{
  PLAY_LOOP,
  PLAY_PAUSE,
  PLAY_DISAPPEAR
};

enum collision_detection
{
  HIT_COMEBACK,
  HIT_PAUSE,
  HIT_STOP,
  HIT_DISAPPEAR
};

enum move_to
{
  TO_RIGHT,
  TO_LEFT,
  TO_UP,
  TO_DOWN
};

enum attach_to_menu
{
  NOT_BIND,
  FIXED_POSITION,
  FOLLOW_SINGLE,
  FOLLOW_VARIETY,
  FULL_SCREEN_VARIETY
};

struct engine_picture_class
{
  int n_index;
  struct grub_video_bitmap *bitmap;
  struct engine_picture_class *next;
};

struct engine_animation_class
{
  struct grub_engine_animation animation;
  grub_gui_container_t parent;
  grub_video_rect_t bounds;
  char *id;
  char *dir_name;
  char *pic_ext;
  char *os_name;
  int ani_w;
  int ani_h;
  unsigned start_x;
  unsigned start_y;
  int pic_ratio;
  int pic_num;
  int cur_x;
  int cur_y;
  int cur_index;
  int move_speed;
  int is_selected;
  int play_mark;
  int follow_mark;
  int attach_mark;
  enum play_mode p_mode;
  enum collision_detection is_hit;
  enum move_to move_t;
  enum attach_to_menu bind_menu;
  struct engine_picture_class pic_cache;
  grub_gfxmenu_view_t view;
};

static grub_err_t
to_process_bitmap (struct grub_video_bitmap **prs,
		   struct grub_video_bitmap *raw, animation_class_t vself)
{
  grub_err_t err = verify_source_bitmap (raw);

  grub_uint8_t *rdata = raw->data;
  unsigned rw = raw->mode_info.width;
  unsigned rh = raw->mode_info.height;
  unsigned rstride = raw->mode_info.pitch;

  err = grub_video_bitmap_create (prs, vself->ani_w, vself->ani_h,
				  raw->mode_info.blit_format);
  if (err != GRUB_ERR_NONE)
    {
      return err;
    }

  struct grub_video_bitmap *psd = *prs;
  grub_uint8_t *pdata = psd->data;
  unsigned pw = psd->mode_info.width;
  unsigned ph = psd->mode_info.height;
  unsigned pstride = psd->mode_info.pitch;
  unsigned bpp = psd->mode_info.bytes_per_pixel;

  err = verify_bitmaps (psd, raw);
  if (err != GRUB_ERR_NONE)
    {
      return err;
    }

  unsigned py;
  for (py = 0; py < ph; py++)
    {
      unsigned px;
      for (px = 0; px < pw; px++)
	{
	  grub_uint8_t *pdt = 0;
	  grub_uint8_t *rdt = 0;
	  unsigned rx;
	  unsigned ry;
	  unsigned focus;

	  rx = rw * px / pw;
	  ry = rh * py / ph;

	  switch (vself->move_t)
	    {
	    case TO_RIGHT:
	      pdt = pdata + py * pstride + px * bpp;
	      break;

	    case TO_LEFT:
	      if (vself->pic_ratio == 1)
		{
		  pdt = pdata + py * pstride + (pw - px - 1) * bpp;
		}
	      else
		{
		  pdt = pdata + (ph - py - 1) * bpp + (pw - px - 1) * pstride;
		}
	      break;

	    case TO_UP:
	      if (vself->pic_ratio == 1)
		{
		  pdt = pdata + (ph - py - 1) * pstride + (pw - px - 1) * bpp;
		}
	      else
		{
		  pdt = pdata + py * bpp + (pw - px - 1) * pstride;
		}
	      break;

	    case TO_DOWN:
	      if (vself->pic_ratio == 1)
		{
		  pdt = pdata + py * pstride + px * bpp;
		}
	      else
		{
		  pdt = pdata + py * pstride + (pw - px - 1) * bpp;
		}
	      break;
	    }

	  rdt = rdata + ry * rstride + rx * bpp;

	  if (rx < rw - 1 && ry < rh - 1)
	    {
	      int h = (256 * rw * px / pw) - (rx * 256);
	      int v = (256 * rh * py / ph) - (ry * 256);

	      for (focus = 0; focus < bpp; focus++)
		{
		  int bp0 = rdt[focus];
		  int bp1 = rdt[focus + bpp];
		  int bp2 = rdt[focus + rstride];
		  int bp3 = rdt[focus + rstride + bpp];

		  int cr0 = (256 - h) * (256 - v);
		  int cr1 = h * (256 - v);
		  int cr2 = (256 - h) * v;
		  int cr3 = h * v;

		  int tot = cr0 * bp0 + cr2 * bp2 + cr1 * bp1 + cr3 * bp3;
		  tot = tot / (256 * 256);
		  pdt[focus] = tot;
		}
	    }
	  else
	    {
	      for (focus = 0; focus < bpp; focus++)
		{
		  pdt[focus] = rdt[focus];
		}
	    }
	}
    }

  return GRUB_ERR_NONE;
}

static struct grub_video_bitmap *
to_loading_picture (animation_class_t vself, const char *dir,
		    const char *file_name)
{
  char *path, *pstr;
  char *ext = vself->pic_ext;

  path = grub_malloc (
      grub_strlen (dir) + grub_strlen (file_name) + grub_strlen (ext) + 3);

  if (!path)
    {
      return 0;
    }

  pstr = grub_stpcpy (path, dir);
  if (path == pstr || pstr[-1] != '/')
    {
      *pstr++ = '/';
    }

  pstr = grub_stpcpy (pstr, file_name);
  pstr = grub_stpcpy (pstr, ext);
  *pstr = '\0';

  struct grub_video_bitmap *original_bitmap;
  grub_video_bitmap_load (&original_bitmap, path);
  grub_free (path);
  grub_errno = GRUB_ERR_NONE;

  if (!original_bitmap)
    {
      return 0;
    }

  struct grub_video_bitmap *processed_bitmap;

  to_process_bitmap (&processed_bitmap, original_bitmap, vself);

  grub_video_bitmap_destroy (original_bitmap);

  if (!processed_bitmap)
    {
      return 0;
    }

  return processed_bitmap;
}

static char *
to_convert_string (int src)
{
  char *tmp1 = grub_malloc (sizeof(char) * 10);
  if (!tmp1)
    {
      return 0;
    }

  char tmp2;
  int pt = 0;

  do
    {
      tmp1[pt] = src % 10 + '0';
      src /= 10;
      pt++;
    }
  while (src != 0);

  tmp1[pt] = '\0';
  int nt;

  for (nt = 0, pt--; nt <= pt / 2; nt++, pt--)
    {
      tmp2 = tmp1[nt];
      tmp1[nt] = tmp1[pt];
      tmp1[pt] = tmp2;
    }

  return tmp1;
}

static struct grub_video_bitmap *
get_picture_from_cache (animation_class_t vself)
{
  int pic_index = vself->cur_index;
  picture_class_t pct;

  for (pct = vself->pic_cache.next; pct; pct = pct->next)
    {
      if (pct->n_index == pic_index)
	{
	  return pct->bitmap;
	}
    }

  char *theme_dir = grub_get_dirname (vself->view->theme_path);
  char *tmp1_dir;
  char *tmp2_dir;
  char *os_name = vself->os_name;
  char *digital_name = to_convert_string (pic_index);
  if (!digital_name)
    {
      grub_free (theme_dir);
      return 0;
    }

  struct grub_video_bitmap *ani_bitmap = 0;

  tmp1_dir = grub_resolve_relative_path (theme_dir, vself->dir_name);
  grub_free (theme_dir);

  if (tmp1_dir)
    {
      if ((vself->bind_menu != FOLLOW_SINGLE) && os_name)
	{
	  tmp2_dir = grub_resolve_relative_path (tmp1_dir, os_name);
	  grub_free (tmp1_dir);

	  if (tmp2_dir)
	    {
	      ani_bitmap = to_loading_picture (vself, tmp2_dir, digital_name);
	      grub_free (tmp2_dir);
	    }
	}
      else
	{
	  ani_bitmap = to_loading_picture (vself, tmp1_dir, digital_name);
	  grub_free (tmp1_dir);
	}
    }

  grub_free (digital_name);

  if (!ani_bitmap)
    {
      return 0;
    }

  pct = grub_malloc (sizeof(*pct));
  if (!pct)
    {
      grub_video_bitmap_destroy (ani_bitmap);
      return 0;
    }

  pct->n_index = pic_index;
  pct->bitmap = ani_bitmap;
  pct->next = vself->pic_cache.next;
  vself->pic_cache.next = pct;
  return pct->bitmap;
}

static void
animation_clear_cache (animation_class_t vself)
{
  picture_class_t cur;
  picture_class_t next;

  for (cur = vself->pic_cache.next; cur; cur = next)
    {
      next = cur->next;
      grub_video_bitmap_destroy (cur->bitmap);
      grub_free (cur);
    }

  vself->pic_cache.next = 0;
}

static void
animation_destroy (void *vself)
{
  animation_class_t self = vself;

  grub_free (self->dir_name);
  grub_free (self->pic_ext);
  grub_free (self->os_name);
  animation_clear_cache (self);
  grub_free (self);
}

static const char *
animation_get_id (void *vself)
{
  animation_class_t self = vself;

  return self->id;
}

static int
animation_is_instance (void *vself __attribute__((unused)), const char *type)
{

  return (grub_strcmp (type, "component") == 0
      || grub_strcmp (type, "animation") == 0);
}

static void
collision_state_change (animation_class_t vself)
{
  animation_clear_cache (vself);

  switch (vself->is_hit)
    {
    case HIT_COMEBACK:
      vself->cur_index = 1;
      break;

    case HIT_PAUSE:
      vself->cur_index = vself->pic_num;
      if (vself->pic_ratio > 1)
	{
	  vself->move_t = TO_RIGHT;
	}
      vself->play_mark = NOW_PAUSE_PLAY;
      vself->move_speed = 0;
      break;

    case HIT_STOP:
      vself->cur_index = 1;
      if (vself->pic_ratio > 1)
	{
	  vself->move_t = TO_RIGHT;
	}
      vself->move_speed = 0;
      break;

    case HIT_DISAPPEAR:
      vself->cur_index = 0;
      vself->play_mark = NOW_PAUSE_PLAY;
      break;
    }
}

static void
two_way_collision (animation_class_t vself)
{
  int move_speed = vself->move_speed;
  int left_range = vself->bounds.x + move_speed;
  int right_range = vself->bounds.x + vself->bounds.width - vself->ani_w
      - move_speed;
  int up_range = vself->bounds.y + move_speed;
  int down_range = vself->bounds.y + vself->bounds.height - vself->ani_h
      - move_speed;

  switch (vself->move_t)
    {
    case TO_RIGHT:
      if (vself->cur_x < right_range)
	{
	  vself->cur_x += move_speed;
	}
      else if (vself->cur_x >= right_range)
	{
	  if (!vself->is_hit)
	    {
	      vself->move_t = TO_LEFT;
	    }
	  else
	    {
	      vself->move_t = TO_RIGHT;
	    }
	  vself->cur_x -= move_speed;
	  collision_state_change (vself);
	}
      break;

    case TO_LEFT:
      if (vself->cur_x > left_range)
	{
	  vself->cur_x -= move_speed;
	}
      else if (vself->cur_x <= left_range)
	{
	  vself->move_t = TO_RIGHT;
	  vself->cur_x += move_speed;
	  collision_state_change (vself);
	}
      break;

    case TO_UP:
      if (vself->cur_y > up_range)
	{
	  vself->cur_y -= move_speed;
	}
      else if (vself->cur_y <= up_range)
	{
	  vself->move_t = TO_DOWN;
	  vself->cur_y += move_speed;
	  collision_state_change (vself);
	}
      break;

    case TO_DOWN:
      if (vself->cur_y < down_range)
	{
	  vself->cur_y += move_speed;
	}
      else if (vself->cur_y >= down_range)
	{
	  if (!vself->is_hit)
	    {
	      vself->move_t = TO_UP;
	    }
	  else
	    {
	      vself->move_t = TO_DOWN;
	    }
	  vself->cur_y -= move_speed;
	  collision_state_change (vself);
	}
      break;
    }
}

static void
move_around_collision (animation_class_t vself)
{
  int move_speed = vself->move_speed;
  int left_range = vself->bounds.x + move_speed;
  int right_range = vself->bounds.x + vself->bounds.width - vself->ani_w
      - move_speed;
  int up_range = vself->bounds.y + move_speed;
  int down_range = vself->bounds.y + vself->bounds.height - vself->ani_h
      - move_speed;

  switch (vself->move_t)
    {
    case TO_RIGHT:
      if (vself->cur_x < right_range && vself->cur_y < down_range)
	{
	  vself->cur_x += move_speed;
	  vself->cur_y += move_speed;
	}
      else if (vself->cur_x >= right_range && vself->cur_y < down_range)
	{
	  vself->move_t = TO_DOWN;
	  vself->cur_x -= move_speed;
	  vself->cur_y += move_speed;
	  collision_state_change (vself);
	}
      else if (vself->cur_x >= right_range && vself->cur_y >= down_range)
	{
	  vself->move_t = TO_LEFT;
	  vself->cur_x -= move_speed;
	  vself->cur_y -= move_speed;
	  collision_state_change (vself);
	}
      else if (vself->cur_x < right_range && vself->cur_y >= down_range)
	{
	  vself->move_t = TO_UP;
	  vself->cur_x += move_speed;
	  vself->cur_y -= move_speed;
	  collision_state_change (vself);
	}
      break;

    case TO_LEFT:
      if (vself->cur_x > left_range && vself->cur_y > up_range)
	{
	  vself->cur_x -= move_speed;
	  vself->cur_y -= move_speed;
	}
      else if (vself->cur_x <= left_range && vself->cur_y > up_range)
	{
	  vself->move_t = TO_UP;
	  vself->cur_x += move_speed;
	  vself->cur_y -= move_speed;
	  collision_state_change (vself);
	}
      else if (vself->cur_x <= left_range && vself->cur_y <= up_range)
	{
	  vself->move_t = TO_RIGHT;
	  vself->cur_x += move_speed;
	  vself->cur_y += move_speed;
	  collision_state_change (vself);
	}
      else if (vself->cur_x > left_range && vself->cur_y <= up_range)
	{
	  vself->move_t = TO_DOWN;
	  vself->cur_x -= move_speed;
	  vself->cur_y += move_speed;
	  collision_state_change (vself);
	}
      break;

    case TO_UP:
      if (vself->cur_x < right_range && vself->cur_y > up_range)
	{
	  vself->cur_x += move_speed;
	  vself->cur_y -= move_speed;
	}
      else if (vself->cur_x >= right_range && vself->cur_y > up_range)
	{
	  vself->move_t = TO_LEFT;
	  vself->cur_x -= move_speed;
	  vself->cur_y -= move_speed;
	  collision_state_change (vself);
	}
      else if (vself->cur_x >= right_range && vself->cur_y <= up_range)
	{
	  vself->move_t = TO_DOWN;
	  vself->cur_x -= move_speed;
	  vself->cur_y += move_speed;
	  collision_state_change (vself);
	}
      else if (vself->cur_x < right_range && vself->cur_y <= up_range)
	{
	  vself->move_t = TO_RIGHT;
	  vself->cur_x += move_speed;
	  vself->cur_y += move_speed;
	  collision_state_change (vself);
	}
      break;

    case TO_DOWN:
      if (vself->cur_x > left_range && vself->cur_y < down_range)
	{
	  vself->cur_x -= move_speed;
	  vself->cur_y += move_speed;
	}
      else if (vself->cur_x <= left_range && vself->cur_y < down_range)
	{
	  vself->move_t = TO_RIGHT;
	  vself->cur_x += move_speed;
	  vself->cur_y += move_speed;
	  collision_state_change (vself);
	}
      else if (vself->cur_x <= left_range && vself->cur_y >= down_range)
	{
	  vself->move_t = TO_UP;
	  vself->cur_x += move_speed;
	  vself->cur_y -= move_speed;
	  collision_state_change (vself);
	}
      else if (vself->cur_x > left_range && vself->cur_y >= down_range)
	{
	  vself->move_t = TO_LEFT;
	  vself->cur_x -= move_speed;
	  vself->cur_y -= move_speed;
	  collision_state_change (vself);
	}
      break;
    }
}

static void
animation_check_collision (animation_class_t vself)
{
  if (vself->move_speed && !vself->play_mark)
    {
      if (vself->pic_ratio == 1)
	{
	  two_way_collision (vself);
	}
      else
	{
	  move_around_collision (vself);
	}
    }
}

static void
two_way_initial (animation_class_t vself, unsigned size)
{
  vself->start_x = 0;
  vself->start_y = 0;

  if (size == vself->bounds.width)
    {
      vself->move_t = TO_DOWN;
    }
  else if (size == vself->bounds.height)
    {
      vself->move_t = TO_RIGHT;
    }

}

static void
move_around_initial (animation_class_t vself, unsigned size)
{
  if (vself->start_x > vself->bounds.width - size)
    {
      vself->start_x = vself->bounds.width - size;
    }

  if (vself->start_y > vself->bounds.height - size)
    {
      vself->start_y = vself->bounds.height - size;
    }
}

static void
move_initial_parameter (animation_class_t vself)
{
  unsigned size = grub_min(vself->bounds.width, vself->bounds.height)
      / vself->pic_ratio;

  vself->ani_w = size;
  vself->ani_h = size;

  if (vself->pic_ratio == 1)
    {
      two_way_initial (vself, size);
    }
  else
    {
      move_around_initial (vself, size);
    }

  vself->cur_x = vself->bounds.x + vself->start_x;
  vself->cur_y = vself->bounds.y + vself->start_y;
}

static void
as_logo_function (animation_class_t vself)
{
  grub_menu_t menu = vself->view->menu;
  grub_menu_entry_t et;
  int pn = vself->is_selected;

  for (et = menu->entry_list; et && pn > 0; et = et->next, pn--)
    ;

  grub_free (vself->os_name);
  vself->os_name = grub_strdup (et->classes->name);
}

static void
set_logo_position (animation_class_t vself)
{
  switch (vself->attach_mark)
    {
    case ATTACH_MENU_LEFT:
      vself->cur_x = vself->view->point_x - vself->ani_w - vself->start_x;
      break;

    case ATTACH_MENU_RIGHT:
      vself->cur_x = vself->view->point_x + vself->start_x;
      break;
    }

  vself->cur_y = vself->view->point_y;

  if (vself->cur_x < 0)
    {
      vself->cur_x = 0;
    }

  if (vself->cur_y < 0)
    {
      vself->cur_y = 0;
    }
}

static void
stay_initial_parameter (animation_class_t vself)
{
  int cur_w = vself->bounds.width / vself->pic_ratio;
  int cur_h = vself->bounds.height / vself->pic_ratio;

  switch (vself->bind_menu)
    {
    case NOT_BIND:
      vself->ani_w = cur_w;
      vself->ani_h = cur_h;
      vself->cur_x = vself->bounds.x;
      vself->cur_y = vself->bounds.y;
      break;

    case FIXED_POSITION:
      vself->ani_w = cur_w;
      vself->ani_h = cur_h;
      vself->cur_x = vself->bounds.x;
      vself->cur_y = vself->bounds.y;
      vself->cur_index = 1;
      as_logo_function (vself);
      break;

    case FOLLOW_SINGLE:
      vself->ani_w = cur_w;
      vself->ani_h = cur_h;
      vself->follow_mark = NOW_FOLLOW_MENU;
      set_logo_position (vself);
      break;

    case FOLLOW_VARIETY:
      vself->ani_w = cur_w;
      vself->ani_h = cur_h;
      vself->cur_index = 1;
      vself->follow_mark = NOW_FOLLOW_MENU;
      set_logo_position (vself);
      as_logo_function (vself);
      break;

    case FULL_SCREEN_VARIETY:
      vself->ani_w = vself->bounds.width;
      vself->ani_h = vself->bounds.height;
      vself->cur_x = vself->bounds.x;
      vself->cur_y = vself->bounds.y;
      vself->cur_index = 1;
      as_logo_function (vself);
      break;
    }
}

static grub_video_rect_t
generate_new_bounds (animation_class_t vself)
{
  grub_video_rect_t cur_bounds;

  cur_bounds.x = vself->cur_x;
  cur_bounds.y = vself->cur_y;
  cur_bounds.width = vself->ani_w;
  cur_bounds.height = vself->ani_h;

  return cur_bounds;
}

static void
animation_paint (void *vself, const grub_video_rect_t *region)
{
  animation_class_t self = vself;
  enum attach_to_menu atm = self->bind_menu;
  grub_video_rect_t old_save;
  grub_video_rect_t new_bounds;

  if (!self->dir_name || !self->cur_index || !self->view->is_animation)
    {
      return;
    }

  if (!self->ani_w || !self->ani_h)
    {
      if (self->pic_ratio <= 0 || self->move_speed < 0 || !self->bounds.width
	  || !self->bounds.height)
	{
	  return;
	}

      if (atm || self->p_mode)
	{
	  self->move_speed = 0;
	  self->move_t = TO_RIGHT;
	}

      if (!self->move_speed)
	{
	  stay_initial_parameter (self);
	}
      else
	{
	  move_initial_parameter (self);
	}
    }

  if (atm && self->follow_mark)
    {
      set_logo_position (self);
    }
  else
    {
      animation_check_collision (self);
    }

  if (!grub_video_have_common_points (region, &self->bounds))
    {
      return;
    }

  new_bounds = generate_new_bounds (self);

  grub_gui_set_viewport (&new_bounds, &old_save);

  struct grub_video_bitmap *picture;
  picture = get_picture_from_cache (self);

  if (picture)
    {
      grub_video_blit_bitmap (picture, GRUB_VIDEO_BLIT_BLEND, 0, 0, 0, 0,
			      self->ani_w, self->ani_h);
    }
  else
    {
      self->cur_index = 0;
      self->play_mark = NOW_PAUSE_PLAY;
    }

  grub_gui_restore_viewport (&old_save);
}

static void
animation_set_parent (void *vself, grub_gui_container_t parent)
{
  animation_class_t self = vself;

  self->parent = parent;
}

static grub_gui_container_t
animation_get_parent (void *vself)
{
  animation_class_t self = vself;

  return self->parent;
}

static void
animation_set_bounds (void *vself, const grub_video_rect_t *bounds)
{
  animation_class_t self = vself;

  self->bounds = *bounds;

  if (self->bind_menu == FULL_SCREEN_VARIETY)
    {
      self->bounds = self->view->screen;
    }
}

static void
animation_get_bounds (void *vself, grub_video_rect_t *bounds)
{
  animation_class_t self = vself;

  *bounds = self->bounds;
}

static void
animation_get_minimal_size (void *vself, unsigned *width, unsigned *height)
{
  animation_class_t self = vself;

  *width = self->ani_w;
  *height = self->ani_h;
}

static grub_err_t
animation_set_property (void *vself, const char *name, const char *value)
{
  animation_class_t self = vself;

  if (grub_strcmp (name, "dir_name") == 0)
    {
      grub_free (self->dir_name);
      self->dir_name = value ? grub_strdup (value) : 0;
    }
  else if (grub_strcmp (name, "image_format") == 0)
    {
      grub_free (self->pic_ext);

      if (grub_strcmp (value, "png") == 0)
	{
	  self->pic_ext = grub_strdup (PNG_EXTENSION);
	}
      else if (grub_strcmp (value, "jpg") == 0)
	{
	  self->pic_ext = grub_strdup (JPG_EXTENSION);
	}
      else if (grub_strcmp (value, "jpeg") == 0)
	{
	  self->pic_ext = grub_strdup (JPEG_EXTENSION);
	}
      else if (grub_strcmp (value, "tga") == 0)
	{
	  self->pic_ext = grub_strdup (TGA_EXTENSION);
	}
    }
  else if (grub_strcmp (name, "start_x") == 0)
    {
      self->start_x = grub_strtol (value, 0, 10);
    }
  else if (grub_strcmp (name, "start_y") == 0)
    {
      self->start_y = grub_strtol (value, 0, 10);
    }
  else if (grub_strcmp (name, "size_ratio") == 0)
    {
      self->pic_ratio = grub_strtol (value, 0, 10);
    }
  else if (grub_strcmp (name, "frame_number") == 0)
    {
      self->pic_num = grub_strtol (value, 0, 10);
    }
  else if (grub_strcmp (name, "move_speed") == 0)
    {
      self->move_speed = grub_strtol (value, 0, 10);
    }
  else if (grub_strcmp (name, "move_direction") == 0)
    {
      if (grub_strcmp (value, "right") == 0)
	{
	  self->move_t = TO_RIGHT;
	}
      else if (grub_strcmp (value, "left") == 0)
	{
	  self->move_t = TO_LEFT;
	}
      else if (grub_strcmp (value, "up") == 0)
	{
	  self->move_t = TO_UP;
	}
      else if (grub_strcmp (value, "down") == 0)
	{
	  self->move_t = TO_DOWN;
	}
    }
  else if (grub_strcmp (name, "play_once") == 0)
    {
      if (grub_strcmp (value, "pause") == 0)
	{
	  self->p_mode = PLAY_PAUSE;
	}
      else if (grub_strcmp (value, "disappear") == 0)
	{
	  self->p_mode = PLAY_DISAPPEAR;
	}
    }
  else if (grub_strcmp (name, "hit_wall") == 0)
    {
      if (grub_strcmp (value, "pause") == 0)
	{
	  self->is_hit = HIT_PAUSE;
	}
      else if (grub_strcmp (value, "stop") == 0)
	{
	  self->is_hit = HIT_STOP;
	}
      else if (grub_strcmp (value, "disappear") == 0)
	{
	  self->is_hit = HIT_DISAPPEAR;
	}
    }
  else if (grub_strcmp (name, "bind_menu") == 0)
    {
      if (grub_strcmp (value, "fixed_position") == 0)
	{
	  self->bind_menu = FIXED_POSITION;
	}
      else if (grub_strcmp (value, "follow_single") == 0)
	{
	  self->bind_menu = FOLLOW_SINGLE;
	}
      else if (grub_strcmp (value, "follow_variety") == 0)
	{
	  self->bind_menu = FOLLOW_VARIETY;
	}
      else if (grub_strcmp (value, "full_screen") == 0)
	{
	  self->bind_menu = FULL_SCREEN_VARIETY;
	}
    }
  else if (grub_strcmp (name, "bind_direction") == 0)
    {
      if (grub_strcmp (value, "left") == 0)
	{
	  self->attach_mark = ATTACH_MENU_LEFT;
	}
      else if (grub_strcmp (value, "right") == 0)
	{
	  self->attach_mark = ATTACH_MENU_RIGHT;
	}
    }
  else if (grub_strcmp (name, "id") == 0)
    {
      grub_free (self->id);

      if (value)
	{
	  self->id = grub_strdup (value);
	}
      else
	{
	  self->id = 0;
	}
    }

  return grub_errno;
}

static struct grub_gui_component_ops animation_comp_ops =
  {
      .destroy = animation_destroy,
      .get_id = animation_get_id,
      .is_instance = animation_is_instance,
      .paint = animation_paint,
      .set_parent = animation_set_parent,
      .get_parent = animation_get_parent,
      .set_bounds = animation_set_bounds,
      .get_bounds = animation_get_bounds,
      .get_minimal_size = animation_get_minimal_size,
      .set_property = animation_set_property
  };

static void
get_playback_state (animation_class_t vself)
{
  switch (vself->p_mode)
    {
    case PLAY_LOOP:
      vself->cur_index = 1;
      break;

    case PLAY_PAUSE:
      vself->cur_index = vself->pic_num;
      vself->play_mark = NOW_PAUSE_PLAY;
      break;

    case PLAY_DISAPPEAR:
      vself->cur_index = 0;
      vself->play_mark = NOW_PAUSE_PLAY;
      break;
    }
}

static void
animation_refresh_info (void *vself, grub_gfxmenu_view_t view)
{
  animation_class_t self = vself;
  self->view = view;
  int cur_selected = view->selected;

  if (self->bind_menu && (self->is_selected != cur_selected))
    {
      if (self->bind_menu != FOLLOW_SINGLE)
	{
	  animation_clear_cache (self);
	}

      self->is_selected = cur_selected;
      self->play_mark = NOW_NORMAL_PLAY;
      self->cur_index = 0;
      as_logo_function (self);
    }

  if (self->view->need_refresh && !self->play_mark && self->pic_num > 0)
    {
      self->cur_index++;

      if (self->cur_index % EXPLOSION_PROOF == 0)
	{
	  animation_clear_cache (self);
	}

      if (self->cur_index > self->pic_num)
	{
	  get_playback_state (self);

	  if (self->pic_num > EXPLOSION_PROOF)
	    {
	      animation_clear_cache (self);
	    }
	}
    }
}

grub_gui_component_t
grub_engine_animation_new (void)
{
  animation_class_t self;

  self = grub_zalloc (sizeof(*self));
  if (!self)
    {
      return 0;
    }

  self->dir_name = 0;
  self->pic_ext = 0;
  self->os_name = 0;
  self->ani_w = 0;
  self->ani_h = 0;
  self->start_x = 0;
  self->start_y = 0;
  self->pic_ratio = 0;
  self->pic_num = 0;
  self->cur_x = 0;
  self->cur_y = 0;
  self->cur_index = 0;
  self->move_speed = 0;
  self->is_selected = 0;
  self->play_mark = NOW_NORMAL_PLAY;
  self->follow_mark = NOW_NOT_FOLLOW;
  self->attach_mark = ATTACH_MENU_LEFT;
  self->p_mode = PLAY_LOOP;
  self->is_hit = HIT_COMEBACK;
  self->move_t = TO_RIGHT;
  self->bind_menu = NOT_BIND;
  self->animation.component.ops = &animation_comp_ops;
  self->animation.refresh_animation = animation_refresh_info;
  self->pic_cache.n_index = 0;
  self->pic_cache.bitmap = 0;
  self->pic_cache.next = 0;

  return (grub_gui_component_t) self;
}
