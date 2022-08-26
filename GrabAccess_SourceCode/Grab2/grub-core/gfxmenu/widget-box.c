/* widget_box.c - Pixmap-stylized box widget. */
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
#include <grub/video.h>
#include <grub/bitmap.h>
#include <grub/bitmap_scale.h>
#include <grub/gfxwidgets.h>

enum box_pixmaps
{
  BOX_PIXMAP_NW, BOX_PIXMAP_NE, BOX_PIXMAP_SE, BOX_PIXMAP_SW,
  BOX_PIXMAP_N, BOX_PIXMAP_E, BOX_PIXMAP_S, BOX_PIXMAP_W,
  BOX_PIXMAP_CENTER
};

static const char *box_pixmap_names[] = {
  /* Corners: */
  "nw", "ne", "se", "sw",
  /* Sides: */
  "n", "e", "s", "w",
  /* Center: */
  "c"
};

#define BOX_NUM_PIXMAPS (sizeof(box_pixmap_names)/sizeof(*box_pixmap_names))

static int
get_height (struct grub_video_bitmap *bitmap)
{
  if (bitmap)
    return grub_video_bitmap_get_height (bitmap);
  else
    return 0;
}

static int
get_width (struct grub_video_bitmap *bitmap)
{
  if (bitmap)
    return grub_video_bitmap_get_width (bitmap);
  else
    return 0;
}

static void
blit (grub_gfxmenu_box_t self, int pixmap_index, int x, int y)
{
  struct grub_video_bitmap *bitmap;
  bitmap = self->scaled_pixmaps[pixmap_index];
  if (! bitmap)
    return;
  grub_video_blit_bitmap (bitmap, GRUB_VIDEO_BLIT_BLEND,
                          x, y, 0, 0,
                          grub_video_bitmap_get_width (bitmap),
                          grub_video_bitmap_get_height (bitmap));
}

static void
draw (grub_gfxmenu_box_t self, int x, int y)
{
  int height_n;
  int width_w;
  int tmp;

  /* Count maximum height of NW, N, NE.  */
  height_n = get_height (self->scaled_pixmaps[BOX_PIXMAP_NW]);
  tmp = get_height (self->scaled_pixmaps[BOX_PIXMAP_N]);
  if (tmp > height_n)
    height_n = tmp;
  tmp = get_height (self->scaled_pixmaps[BOX_PIXMAP_NE]);
  if (tmp > height_n)
    height_n = tmp;

  /* Count maximum width of NW, W, SW.  */
  width_w = get_width (self->scaled_pixmaps[BOX_PIXMAP_NW]);
  tmp = get_width (self->scaled_pixmaps[BOX_PIXMAP_W]);
  if (tmp > width_w)
    width_w = tmp;
  tmp = get_width (self->scaled_pixmaps[BOX_PIXMAP_SW]);
  if (tmp > width_w)
    width_w = tmp;

  /* Draw sides.  */
  blit (self, BOX_PIXMAP_N, x + width_w, y);
  blit (self, BOX_PIXMAP_S, x + width_w, y + height_n + self->content_height);
  blit (self, BOX_PIXMAP_E, x + width_w + self->content_width, y + height_n);
  blit (self, BOX_PIXMAP_W, x, y + height_n);

  /* Draw corners.  */
  blit (self, BOX_PIXMAP_NW, x, y);
  blit (self, BOX_PIXMAP_NE, x + width_w + self->content_width, y);
  blit (self, BOX_PIXMAP_SE,
        x + width_w + self->content_width,
        y + height_n + self->content_height);
  blit (self, BOX_PIXMAP_SW, x, y + height_n + self->content_height);

  /* Draw center.  */
  blit (self, BOX_PIXMAP_CENTER, x + width_w, y + height_n);
}

static grub_err_t
scale_pixmap (grub_gfxmenu_box_t self, int i, int w, int h)
{
  struct grub_video_bitmap **scaled = &self->scaled_pixmaps[i];
  struct grub_video_bitmap *raw = self->raw_pixmaps[i];

  if (raw == 0)
    return grub_errno;

  if (w == -1)
    w = grub_video_bitmap_get_width (raw);
  if (h == -1)
    h = grub_video_bitmap_get_height (raw);

  if (*scaled == 0
      || ((int) grub_video_bitmap_get_width (*scaled) != w)
      || ((int) grub_video_bitmap_get_height (*scaled) != h))
    {
      if (*scaled)
        {
          grub_video_bitmap_destroy (*scaled);
          *scaled = 0;
        }

      /* Don't try to create a bitmap with a zero dimension.  */
      if (w != 0 && h != 0)
        grub_video_bitmap_create_scaled (scaled, w, h, raw,
                                         GRUB_VIDEO_BITMAP_SCALE_METHOD_BEST);
    }

  return grub_errno;
}

static void
set_content_size (grub_gfxmenu_box_t self,
                  int width, int height)
{
  self->content_width = width;
  self->content_height = height;

  /* Resize sides to match the width and height.  */
  /* It is assumed that the corners width/height match the adjacent sides.  */

  /* Resize N and S sides to match width.  */
  if (scale_pixmap(self, BOX_PIXMAP_N, width, -1) != GRUB_ERR_NONE)
    return;
  if (scale_pixmap(self, BOX_PIXMAP_S, width, -1) != GRUB_ERR_NONE)
    return;

  /* Resize E and W sides to match height.  */
  if (scale_pixmap(self, BOX_PIXMAP_E, -1, height) != GRUB_ERR_NONE)
      return;
  if (scale_pixmap(self, BOX_PIXMAP_W, -1, height) != GRUB_ERR_NONE)
    return;

  /* Don't scale the corners--they are assumed to match the sides. */
  if (scale_pixmap(self, BOX_PIXMAP_NW, -1, -1) != GRUB_ERR_NONE)
    return;
  if (scale_pixmap(self, BOX_PIXMAP_SW, -1, -1) != GRUB_ERR_NONE)
    return;
  if (scale_pixmap(self, BOX_PIXMAP_NE, -1, -1) != GRUB_ERR_NONE)
    return;
  if (scale_pixmap(self, BOX_PIXMAP_SE, -1, -1) != GRUB_ERR_NONE)
    return;

  /* Scale the center area. */
  if (scale_pixmap(self, BOX_PIXMAP_CENTER, width, height) != GRUB_ERR_NONE)
    return;
}

static int
get_border_width (grub_gfxmenu_box_t self)
{
  return (get_width (self->raw_pixmaps[BOX_PIXMAP_E])
	  + get_width (self->raw_pixmaps[BOX_PIXMAP_W]));
}

static int
get_left_pad (grub_gfxmenu_box_t self)
{
  int v, c;

  v = get_width (self->raw_pixmaps[BOX_PIXMAP_W]);
  c = get_width (self->raw_pixmaps[BOX_PIXMAP_NW]);
  if (c > v)
    v = c;
  c = get_width (self->raw_pixmaps[BOX_PIXMAP_SW]);
  if (c > v)
    v = c;

  return v;
}

static int
get_top_pad (grub_gfxmenu_box_t self)
{
  int v, c;

  v = get_height (self->raw_pixmaps[BOX_PIXMAP_N]);
  c = get_height (self->raw_pixmaps[BOX_PIXMAP_NW]);
  if (c > v)
    v = c;
  c = get_height (self->raw_pixmaps[BOX_PIXMAP_NE]);
  if (c > v)
    v = c;

  return v;
}

static int
get_right_pad (grub_gfxmenu_box_t self)
{
  int v, c;

  v = get_width (self->raw_pixmaps[BOX_PIXMAP_E]);
  c = get_width (self->raw_pixmaps[BOX_PIXMAP_NE]);
  if (c > v)
    v = c;
  c = get_width (self->raw_pixmaps[BOX_PIXMAP_SE]);
  if (c > v)
    v = c;

  return v;
}

static int
get_bottom_pad (grub_gfxmenu_box_t self)
{
  int v, c;

  v = get_height (self->raw_pixmaps[BOX_PIXMAP_S]);
  c = get_height (self->raw_pixmaps[BOX_PIXMAP_SW]);
  if (c > v)
    v = c;
  c = get_height (self->raw_pixmaps[BOX_PIXMAP_SE]);
  if (c > v)
    v = c;

  return v;
}

static void
destroy (grub_gfxmenu_box_t self)
{
  unsigned i;
  for (i = 0; i < BOX_NUM_PIXMAPS; i++)
    {
      if (self->raw_pixmaps[i])
        grub_video_bitmap_destroy(self->raw_pixmaps[i]);
      self->raw_pixmaps[i] = 0;

      if (self->scaled_pixmaps[i])
        grub_video_bitmap_destroy(self->scaled_pixmaps[i]);
      self->scaled_pixmaps[i] = 0;
    }
  grub_free (self->raw_pixmaps);
  self->raw_pixmaps = 0;
  grub_free (self->scaled_pixmaps);
  self->scaled_pixmaps = 0;

  /* Free self:  must be the last step!  */
  grub_free (self);
}


/* Create a new box.  If PIXMAPS_PREFIX and PIXMAPS_SUFFIX are both non-null,
   then an attempt is made to load the north, south, east, west, northwest,
   northeast, southeast, southwest, and center pixmaps.
   If either PIXMAPS_PREFIX or PIXMAPS_SUFFIX is 0, then no pixmaps are
   loaded, and the box has zero-width borders and is drawn transparent.  */
grub_gfxmenu_box_t
grub_gfxmenu_create_box (const char *pixmaps_prefix,
                         const char *pixmaps_suffix)
{
  unsigned i;
  grub_gfxmenu_box_t box;

  box = (grub_gfxmenu_box_t) grub_malloc (sizeof (*box));
  if (! box)
    return 0;

  box->content_width = 0;
  box->content_height = 0;
  box->raw_pixmaps =
    (struct grub_video_bitmap **)
    grub_calloc (BOX_NUM_PIXMAPS, sizeof (struct grub_video_bitmap *));
  box->scaled_pixmaps =
    (struct grub_video_bitmap **)
    grub_calloc (BOX_NUM_PIXMAPS, sizeof (struct grub_video_bitmap *));

  /* Initialize all pixmap pointers to NULL so that proper destruction can
     be performed if an error is encountered partway through construction.  */
  for (i = 0; i < BOX_NUM_PIXMAPS; i++)
      box->raw_pixmaps[i] = 0;
  for (i = 0; i < BOX_NUM_PIXMAPS; i++)
      box->scaled_pixmaps[i] = 0;

  /* Load the pixmaps.  */
  for (i = 0; i < BOX_NUM_PIXMAPS; i++)
    {
      if (pixmaps_prefix && pixmaps_suffix)
        {
          char *path;
          char *path_end;

          path = grub_malloc (grub_strlen (pixmaps_prefix)
                              + grub_strlen (box_pixmap_names[i])
                              + grub_strlen (pixmaps_suffix)
                              + 1);
          if (! path)
            goto fail_and_destroy;

          /* Construct the specific path for this pixmap.  */
          path_end = grub_stpcpy (path, pixmaps_prefix);
          path_end = grub_stpcpy (path_end, box_pixmap_names[i]);
          path_end = grub_stpcpy (path_end, pixmaps_suffix);

          grub_video_bitmap_load (&box->raw_pixmaps[i], path);
          grub_free (path);

          /* Ignore missing pixmaps.  */
          grub_errno = GRUB_ERR_NONE;
        }
    }

  box->draw = draw;
  box->set_content_size = set_content_size;
  box->get_border_width = get_border_width;

  box->get_left_pad = get_left_pad;
  box->get_top_pad = get_top_pad;
  box->get_right_pad = get_right_pad;
  box->get_bottom_pad = get_bottom_pad;
  box->destroy = destroy;
  return box;

fail_and_destroy:
  destroy (box);
  return 0;
}
