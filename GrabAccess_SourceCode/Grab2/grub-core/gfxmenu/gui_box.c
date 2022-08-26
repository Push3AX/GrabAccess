/* gui_box.c - GUI container that stack components. */
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

struct component_node
{
  grub_gui_component_t component;
  struct component_node *next;
  struct component_node *prev;
};

typedef struct grub_gui_box *grub_gui_box_t;

typedef void (*layout_func_t) (grub_gui_box_t self, int modify_layout,
                               unsigned *minimal_width,
			       unsigned *minimal_height);

struct grub_gui_box
{
  struct grub_gui_container container;

  grub_gui_container_t parent;
  grub_video_rect_t bounds;
  char *id;

  /* Doubly linked list of components with dummy head & tail nodes.  */
  struct component_node chead;
  struct component_node ctail;

  /* The layout function: differs for vertical and horizontal boxes.  */
  layout_func_t layout_func;
};

static void
box_destroy (void *vself)
{
  grub_gui_box_t self = vself;
  struct component_node *cur;
  struct component_node *next;
  for (cur = self->chead.next; cur != &self->ctail; cur = next)
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
box_get_id (void *vself)
{
  grub_gui_box_t self = vself;
  return self->id;
}

static int
box_is_instance (void *vself __attribute__((unused)), const char *type)
{
  return (grub_strcmp (type, "component") == 0
          || grub_strcmp (type, "container") == 0);
}

static void
layout_horizontally (grub_gui_box_t self, int modify_layout,
                     unsigned *min_width, unsigned *min_height)
{
  /* Start at the left (chead) and set the x coordinates as we go right.  */
  /* All components have their width set to the box's width.  */

  struct component_node *cur;
  unsigned w = 0, mwfrac = 0, h = 0, x = 0;
  grub_fixed_signed_t wfrac = 0;
  int bogus_frac = 0;

  for (cur = self->chead.next; cur != &self->ctail; cur = cur->next)
    {
      grub_gui_component_t c = cur->component;
      unsigned mw = 0, mh = 0;

      if (c->ops->get_minimal_size)
	c->ops->get_minimal_size (c, &mw, &mh);

      if (c->h > (signed) h)
	h = c->h;
      if (mh > h)
	h = mh;
      wfrac += c->wfrac;
      w += c->w;
      if (mw - c->w > 0)
	mwfrac += mw - c->w;
    }
  if (wfrac > GRUB_FIXED_1 || (w > 0 && wfrac == GRUB_FIXED_1))
    bogus_frac = 1;

  if (min_width)
    {
      if (wfrac < GRUB_FIXED_1)
	*min_width = grub_fixed_sfs_divide (w, GRUB_FIXED_1 - wfrac);
      else
	*min_width = w;
      if (*min_width < w + mwfrac)
	*min_width = w + mwfrac;
    }
  if (min_height)
    *min_height = h;

  if (!modify_layout)
    return;

  for (cur = self->chead.next; cur != &self->ctail; cur = cur->next)
    {
      grub_video_rect_t r;
      grub_gui_component_t c = cur->component;
      unsigned mw = 0, mh = 0;

      r.x = x;
      r.y = 0;
      r.height = h;

      if (c->ops->get_minimal_size)
	c->ops->get_minimal_size (c, &mw, &mh);

      r.width = c->w;
      if (!bogus_frac)
	r.width += grub_fixed_sfs_multiply (self->bounds.width, c->wfrac);

      if (r.width < mw)
	r.width = mw;

      c->ops->set_bounds (c, &r);

      x += r.width;
    }
}

static void
layout_vertically (grub_gui_box_t self, int modify_layout,
                     unsigned *min_width, unsigned *min_height)
{
  /* Start at the top (chead) and set the y coordinates as we go rdown.  */
  /* All components have their height set to the box's height.  */

  struct component_node *cur;
  unsigned h = 0, mhfrac = 0, w = 0, y = 0;
  grub_fixed_signed_t hfrac = 0;
  int bogus_frac = 0;

  for (cur = self->chead.next; cur != &self->ctail; cur = cur->next)
    {
      grub_gui_component_t c = cur->component;
      unsigned mw = 0, mh = 0;

      if (c->ops->get_minimal_size)
	c->ops->get_minimal_size (c, &mw, &mh);

      if (c->w > (signed) w)
	w = c->w;
      if (mw > w)
	w = mw;
      hfrac += c->hfrac;
      h += c->h;
      if (mh - c->h > 0)
	mhfrac += mh - c->h;
    }
  if (hfrac > GRUB_FIXED_1 || (h > 0 && hfrac == GRUB_FIXED_1))
    bogus_frac = 1;

  if (min_height)
    {
      if (hfrac < GRUB_FIXED_1)
	*min_height = grub_fixed_sfs_divide (h, GRUB_FIXED_1 - hfrac);
      else
	*min_height = h;
      if (*min_height < h + mhfrac)
	*min_height = h + mhfrac;
    }
  if (min_width)
    *min_width = w;

  if (!modify_layout)
    return;

  for (cur = self->chead.next; cur != &self->ctail; cur = cur->next)
    {
      grub_video_rect_t r;
      grub_gui_component_t c = cur->component;
      unsigned mw = 0, mh = 0;

      r.x = 0;
      r.y = y;
      r.width = w;

      if (c->ops->get_minimal_size)
	c->ops->get_minimal_size (c, &mw, &mh);

      r.height = c->h;
      if (!bogus_frac)
	r.height += grub_fixed_sfs_multiply (self->bounds.height, c->hfrac);

      if (r.height < mh)
	r.height = mh;

      c->ops->set_bounds (c, &r);

      y += r.height;
    }
}

static void
box_paint (void *vself, const grub_video_rect_t *region)
{
  grub_gui_box_t self = vself;

  struct component_node *cur;
  grub_video_rect_t vpsave;

  grub_video_area_status_t box_area_status;
  grub_video_get_area_status (&box_area_status);

  grub_gui_set_viewport (&self->bounds, &vpsave);
  for (cur = self->chead.next; cur != &self->ctail; cur = cur->next)
    {
      grub_gui_component_t comp = cur->component;
      grub_video_rect_t r;
      comp->ops->get_bounds(comp, &r);

      if (!grub_video_have_common_points (region, &r))
        continue;

      /* Paint the child.  */
      if (box_area_status == GRUB_VIDEO_AREA_ENABLED
          && grub_video_bounds_inside_region (&r, region))
        grub_video_set_area_status (GRUB_VIDEO_AREA_DISABLED);
      comp->ops->paint (comp, region);
      if (box_area_status == GRUB_VIDEO_AREA_ENABLED)
        grub_video_set_area_status (GRUB_VIDEO_AREA_ENABLED);
    }
  grub_gui_restore_viewport (&vpsave);
}

static void
box_set_parent (void *vself, grub_gui_container_t parent)
{
  grub_gui_box_t self = vself;
  self->parent = parent;
}

static grub_gui_container_t
box_get_parent (void *vself)
{
  grub_gui_box_t self = vself;
  return self->parent;
}

static void
box_set_bounds (void *vself, const grub_video_rect_t *bounds)
{
  grub_gui_box_t self = vself;
  self->bounds = *bounds;
  self->layout_func (self, 1, 0, 0);   /* Relayout the children.  */
}

static void
box_get_bounds (void *vself, grub_video_rect_t *bounds)
{
  grub_gui_box_t self = vself;
  *bounds = self->bounds;
}

/* The box's preferred size is based on the preferred sizes
   of its children.  */
static void
box_get_minimal_size (void *vself, unsigned *width, unsigned *height)
{
  grub_gui_box_t self = vself;
  self->layout_func (self, 0, width, height);   /* Just calculate the size.  */
}

static grub_err_t
box_set_property (void *vself, const char *name, const char *value)
{
  grub_gui_box_t self = vself;
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
box_add (void *vself, grub_gui_component_t comp)
{
  grub_gui_box_t self = vself;
  struct component_node *node;
  node = grub_malloc (sizeof (*node));
  if (! node)
    return;   /* Note: probably should handle the error.  */
  node->component = comp;
  /* Insert the node before the tail.  */
  node->prev = self->ctail.prev;
  node->prev->next = node;
  node->next = &self->ctail;
  node->next->prev = node;

  comp->ops->set_parent (comp, (grub_gui_container_t) self);
  self->layout_func (self, 1, 0, 0);   /* Relayout the children.  */
}

static void
box_remove (void *vself, grub_gui_component_t comp)
{
  grub_gui_box_t self = vself;
  struct component_node *cur;
  for (cur = self->chead.next; cur != &self->ctail; cur = cur->next)
    {
      if (cur->component == comp)
        {
          /* Unlink 'cur' from the list.  */
          cur->prev->next = cur->next;
          cur->next->prev = cur->prev;
          /* Free the node's memory (but don't destroy the component).  */
          grub_free (cur);
          /* Must not loop again, since 'cur' would be dereferenced!  */
          return;
        }
    }
}

static void
box_iterate_children (void *vself,
                      grub_gui_component_callback cb, void *userdata)
{
  grub_gui_box_t self = vself;
  struct component_node *cur;
  for (cur = self->chead.next; cur != &self->ctail; cur = cur->next)
    cb (cur->component, userdata);
}

static struct grub_gui_component_ops box_comp_ops =
  {
    .destroy = box_destroy,
    .get_id = box_get_id,
    .is_instance = box_is_instance,
    .paint = box_paint,
    .set_parent = box_set_parent,
    .get_parent = box_get_parent,
    .set_bounds = box_set_bounds,
    .get_bounds = box_get_bounds,
    .get_minimal_size = box_get_minimal_size,
    .set_property = box_set_property
  };

static struct grub_gui_container_ops box_ops =
{
  .add = box_add,
  .remove = box_remove,
  .iterate_children = box_iterate_children
};

/* Box constructor.  Specify the appropriate layout function to create
   a horizontal or vertical stacking box.  */
static grub_gui_box_t
box_new (layout_func_t layout_func)
{
  grub_gui_box_t box;
  box = grub_zalloc (sizeof (*box));
  if (! box)
    return 0;
  box->container.ops = &box_ops;
  box->container.component.ops = &box_comp_ops;
  box->chead.next = &box->ctail;
  box->ctail.prev = &box->chead;
  box->layout_func = layout_func;
  return box;
}

/* Create a new container that stacks its child components horizontally,
   from left to right.  Each child get a width corresponding to its
   preferred width.  The height of each child is set the maximum of the
   preferred heights of all children.  */
grub_gui_container_t
grub_gui_hbox_new (void)
{
  return (grub_gui_container_t) box_new (layout_horizontally);
}

/* Create a new container that stacks its child components verticallyj,
   from top to bottom.  Each child get a height corresponding to its
   preferred height.  The width of each child is set the maximum of the
   preferred widths of all children.  */
grub_gui_container_t
grub_gui_vbox_new (void)
{
  return (grub_gui_container_t) box_new (layout_vertically);
}
