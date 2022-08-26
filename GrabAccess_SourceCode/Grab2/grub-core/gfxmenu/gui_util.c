/* gui_util.c - GUI utility functions.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008  Free Software Foundation, Inc.
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


struct find_by_id_state
{
  const char *match_id;
  grub_gui_component_callback match_callback;
  void *match_userdata;
};

static void
find_by_id_recursively (grub_gui_component_t component, void *userdata)
{
  struct find_by_id_state *state;
  const char *id;

  state = (struct find_by_id_state *) userdata;
  id = component->ops->get_id (component);
  if (id && grub_strcmp (id, state->match_id) == 0)
    state->match_callback (component, state->match_userdata);

  if (component->ops->is_instance (component, "container"))
    {
      grub_gui_container_t container;
      container = (grub_gui_container_t) component;
      container->ops->iterate_children (container,
                                        find_by_id_recursively,
                                        state);
    }
}

void
grub_gui_find_by_id (grub_gui_component_t root,
                     const char *id,
                     grub_gui_component_callback cb,
                     void *userdata)
{
  struct find_by_id_state state;
  state.match_id = id;
  state.match_callback = cb;
  state.match_userdata = userdata;
  find_by_id_recursively (root, &state);
}


struct iterate_recursively_state
{
  grub_gui_component_callback callback;
  void *userdata;
};

static
void iterate_recursively_cb (grub_gui_component_t component, void *userdata)
{
  struct iterate_recursively_state *state;

  state = (struct iterate_recursively_state *) userdata;
  state->callback (component, state->userdata);

  if (component->ops->is_instance (component, "container"))
    {
      grub_gui_container_t container;
      container = (grub_gui_container_t) component;
      container->ops->iterate_children (container,
                                        iterate_recursively_cb,
                                        state);
    }
}

void
grub_gui_iterate_recursively (grub_gui_component_t root,
                              grub_gui_component_callback cb,
                              void *userdata)
{
  struct iterate_recursively_state state;
  state.callback = cb;
  state.userdata = userdata;
  iterate_recursively_cb (root, &state);
}
