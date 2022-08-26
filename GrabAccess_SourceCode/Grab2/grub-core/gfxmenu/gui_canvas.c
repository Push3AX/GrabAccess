/* gui_canvas.c - GUI container allowing manually placed components. */
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
#include <grub/gui_string_util.h>

/* TODO Add layering so that components can be properly overlaid. */

struct component_node
{
  grub_gui_component_t component;
  struct component_node *next;
};

struct grub_gui_canvas
{
  struct grub_gui_container container;

  grub_gui_container_t parent;
  grub_video_rect_t bounds;
  char *id;
  /* Component list (dummy head node).  */
  struct component_node components;
};

typedef struct grub_gui_canvas *grub_gui_canvas_t;

static void
canvas_destroy (void *vself)
{
  grub_gui_canvas_t self = vself;
  struct component_node *cur;
  struct component_node *next;
  for (cur = self->components.next; cur; cur = next)
    {
      /* Copy the 'next' pointer, since we need it for the next iteration,
         and we're going to free the memory it is stored in.  */
      next = cur->next;
      /* Destroy the child component.  */
      cur->component->ops->destroy (cur->component);
      /* Free the linked list node.  */
      grub_free (cur);
    }
  grub_free (self);
}

static const char *
canvas_get_id (void *vself)
{
  grub_gui_canvas_t self = vself;
  return self->id;
}

static int
canvas_is_instance (void *vself __attribute__((unused)), const char *type)
{
  return (grub_strcmp (type, "component") == 0
          || grub_strcmp (type, "container") == 0);
}

static void
canvas_paint (void *vself, const grub_video_rect_t *region)
{
  grub_gui_canvas_t self = vself;

  struct component_node *cur;
  grub_video_rect_t vpsave;

  grub_video_area_status_t canvas_area_status;
  grub_video_get_area_status (&canvas_area_status);

  grub_gui_set_viewport (&self->bounds, &vpsave);
  for (cur = self->components.next; cur; cur = cur->next)
    {
      grub_video_rect_t r;
      grub_gui_component_t comp;
      signed x, y, w, h;

      comp = cur->component;

      w = grub_fixed_sfs_multiply (self->bounds.width, comp->wfrac) + comp->w;
      h = grub_fixed_sfs_multiply (self->bounds.height, comp->hfrac) + comp->h;
      x = grub_fixed_sfs_multiply (self->bounds.width, comp->xfrac) + comp->x;
      y = grub_fixed_sfs_multiply (self->bounds.height, comp->yfrac) + comp->y;

      if (comp->ops->get_minimal_size)
	{
	  unsigned mw;
	  unsigned mh;
	  comp->ops->get_minimal_size (comp, &mw, &mh);
	  if (w < (signed) mw)
	    w = mw;
	  if (h < (signed) mh)
	    h = mh;
	}

      /* Sanity checks.  */
      if (w <= 0)
	w = 32;
      if (h <= 0)
	h = 32;

      if (x >= (signed) self->bounds.width)
	x = self->bounds.width - 32;
      if (y >= (signed) self->bounds.height)
	y = self->bounds.height - 32;

      if (x < 0)
	x = 0;
      if (y < 0)
	y = 0;

      if (x + w >= (signed) self->bounds.width)
	w = self->bounds.width - x;
      if (y + h >= (signed) self->bounds.height)
	h = self->bounds.height - y;

      r.x = x;
      r.y = y;
      r.width = w;
      r.height = h;
      comp->ops->set_bounds (comp, &r);

      if (!grub_video_have_common_points (region, &r))
        continue;

      /* Paint the child.  */
      if (canvas_area_status == GRUB_VIDEO_AREA_ENABLED
          && grub_video_bounds_inside_region (&r, region))
        grub_video_set_area_status (GRUB_VIDEO_AREA_DISABLED);
      comp->ops->paint (comp, region);
      if (canvas_area_status == GRUB_VIDEO_AREA_ENABLED)
        grub_video_set_area_status (GRUB_VIDEO_AREA_ENABLED);
    }
  grub_gui_restore_viewport (&vpsave);
}

static void
canvas_set_parent (void *vself, grub_gui_container_t parent)
{
  grub_gui_canvas_t self = vself;
  self->parent = parent;
}

static grub_gui_container_t
canvas_get_parent (void *vself)
{
  grub_gui_canvas_t self = vself;
  return self->parent;
}

static void
canvas_set_bounds (void *vself, const grub_video_rect_t *bounds)
{
  grub_gui_canvas_t self = vself;
  self->bounds = *bounds;
}

static void
canvas_get_bounds (void *vself, grub_video_rect_t *bounds)
{
  grub_gui_canvas_t self = vself;
  *bounds = self->bounds;
}

static grub_err_t
canvas_set_property (void *vself, const char *name, const char *value)
{
  grub_gui_canvas_t self = vself;
  if (grub_strcmp (name, "id") == 0)
    {
      grub_free (self->id);
      if (value)
        {
          self->id = grub_strdup (value);
          if (! self->id)
            return grub_errno;
        }
      else
        self->id = 0;
    }
  return grub_errno;
}

static void
canvas_add (void *vself, grub_gui_component_t comp)
{
  grub_gui_canvas_t self = vself;
  struct component_node *node;
  node = grub_malloc (sizeof (*node));
  if (! node)
    return;   /* Note: probably should handle the error.  */
  node->component = comp;
  node->next = self->components.next;
  self->components.next = node;
  comp->ops->set_parent (comp, (grub_gui_container_t) self);
}

static void
canvas_remove (void *vself, grub_gui_component_t comp)
{
  grub_gui_canvas_t self = vself;
  struct component_node *cur;
  struct component_node *prev;
  prev = &self->components;
  for (cur = self->components.next; cur; prev = cur, cur = cur->next)
    {
      if (cur->component == comp)
        {
          /* Unlink 'cur' from the list.  */
          prev->next = cur->next;
          /* Free the node's memory (but don't destroy the component).  */
          grub_free (cur);
          /* Must not loop again, since 'cur' would be dereferenced!  */
          return;
        }
    }
}

static void
canvas_iterate_children (void *vself,
                         grub_gui_component_callback cb, void *userdata)
{
  grub_gui_canvas_t self = vself;
  struct component_node *cur;
  for (cur = self->components.next; cur; cur = cur->next)
    cb (cur->component, userdata);
}

static struct grub_gui_component_ops canvas_comp_ops =
{
  .destroy = canvas_destroy,
  .get_id = canvas_get_id,
  .is_instance = canvas_is_instance,
  .paint = canvas_paint,
  .set_parent = canvas_set_parent,
  .get_parent = canvas_get_parent,
  .set_bounds = canvas_set_bounds,
  .get_bounds = canvas_get_bounds,
  .set_property = canvas_set_property
};

static struct grub_gui_container_ops canvas_ops =
{
  .add = canvas_add,
  .remove = canvas_remove,
  .iterate_children = canvas_iterate_children
};

grub_gui_container_t
grub_gui_canvas_new (void)
{
  grub_gui_canvas_t canvas;
  canvas = grub_zalloc (sizeof (*canvas));
  if (! canvas)
    return 0;
  canvas->container.ops = &canvas_ops;
  canvas->container.component.ops = &canvas_comp_ops;
  return (grub_gui_container_t) canvas;
}
