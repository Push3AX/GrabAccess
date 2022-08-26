/* gui_label.c - GUI component to display a line of text.  */
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
#include <grub/i18n.h>
#include <grub/color.h>
#include <grub/env.h>
#include <grub/command.h>
#include <grub/parser.h>

static const char *align_options[] =
{
  "left",
  "center",
  "right",
  0
};

static void
label_destroy (void *vself)
{
  grub_gui_label_t self = vself;
  grub_gfxmenu_timeout_unregister ((grub_gui_component_t) self);
  grub_free (self->text);
  grub_free (self->template);
  grub_free (self);
}

static const char *
label_get_id (void *vself)
{
  grub_gui_label_t self = vself;
  return self->id;
}

static int
label_is_instance (void *vself __attribute__((unused)), const char *type)
{
  return (grub_strcmp (type, "component") == 0
          || grub_strcmp (type, "label") == 0);
}

static void
label_paint (void *vself, const grub_video_rect_t *region)
{
  grub_gui_label_t self = vself;

  if (! self->visible)
    return;

  if (!grub_video_have_common_points (region, &self->bounds))
    return;

  /* Calculate the starting x coordinate.  */
  int left_x;
  if (self->align == align_left)
    left_x = 0;
  else if (self->align == align_center)
    left_x = (self->bounds.width
          - grub_font_get_string_width (self->font, self->text)) / 2;
  else if (self->align == align_right)
    left_x = (self->bounds.width
              - grub_font_get_string_width (self->font, self->text));
  else
    return;   /* Invalid alignment.  */

  if (left_x < 0 || left_x > (int) self->bounds.width)
    left_x = 0;

  grub_video_rect_t vpsave;
  grub_gui_set_viewport (&self->bounds, &vpsave);
  grub_font_draw_string (self->text,
                         self->font,
                         grub_video_map_rgba_color (self->color),
                         left_x,
                         grub_font_get_ascent (self->font));
  grub_gui_restore_viewport (&vpsave);
}

static void
label_set_parent (void *vself, grub_gui_container_t parent)
{
  grub_gui_label_t self = vself;
  self->parent = parent;
}

static grub_gui_container_t
label_get_parent (void *vself)
{
  grub_gui_label_t self = vself;
  return self->parent;
}

static void
label_set_bounds (void *vself, const grub_video_rect_t *bounds)
{
  grub_gui_label_t self = vself;
  self->bounds = *bounds;
}

static void
label_get_bounds (void *vself, grub_video_rect_t *bounds)
{
  grub_gui_label_t self = vself;
  *bounds = self->bounds;
}

static void
label_refresh_default (void *vself __attribute__ ((unused)),
                       grub_gfxmenu_view_t view __attribute__ ((unused)))
{
}

static void
label_refresh_help_message (void *vself, grub_gfxmenu_view_t view)
{
  grub_gui_label_t self = vself;
  grub_menu_entry_t e;
  e = grub_menu_get_entry (view->menu, view->selected);
  grub_free (self->text);
  if (e && e->help_message)
    self->text = grub_strdup (e->help_message);
  else
    self->text = grub_strdup ("");
}

static void
label_refresh_menu_title (void *vself, grub_gfxmenu_view_t view)
{
  grub_gui_label_t self = vself;
  grub_menu_entry_t e;
  e = grub_menu_get_entry (view->menu, view->selected);
  grub_free (self->text);
  if (e && e->title)
    self->text = grub_strdup (e->title);
  else
    self->text = grub_strdup ("");
}

static void
label_refresh_var (void *vself,
                   grub_gfxmenu_view_t view __attribute__ ((unused)))
{
  int n;
  char **args = NULL;
  grub_gui_label_t self = vself;
  grub_free (self->text);
  if (self->template && self->template[0])
  {
    if ((!grub_parser_split_cmdline (self->template, 0, 0, &n, &args)) && (n >= 0))
    {
      grub_command_t cmd;
      cmd = grub_command_find (args[0]);
      if (cmd)
        (cmd->func) (cmd, n-1, &args[1]);
      grub_free (args[0]);
      grub_free (args);
    }
  }
  if (self->env)
    self->text = grub_strdup (grub_env_get (self->env));
  else
    self->text = grub_strdup ("");
}

static void
label_get_minimal_size (void *vself, unsigned *width, unsigned *height)
{
  grub_gui_label_t self = vself;
  if (self->refresh_text == label_refresh_default)
    *width = grub_font_get_string_width (self->font, self->text);
  else
    *width = 65535;
  *height = (grub_font_get_ascent (self->font)
             + grub_font_get_descent (self->font));
}

#pragma GCC diagnostic ignored "-Wformat-nonliteral"

static void
label_set_state (void *vself, int visible, int start __attribute__ ((unused)),
         int current, int end __attribute__ ((unused)))
{
  grub_gui_label_t self = vself;
  self->value = -current;
  self->visible = visible;
  grub_free (self->text);
  self->text = grub_xasprintf (self->template ? : "%d", self->value);
}

static grub_err_t
label_set_property (void *vself, const char *name, const char *value)
{
  grub_gui_label_t self = vself;
  if (grub_strcmp (name, "text") == 0)
  {
    self->refresh_text = label_refresh_default;
    grub_free (self->text);
    grub_free (self->template);
    if (! value)
    {
      self->template = NULL;
      self->text = grub_strdup ("");
    }
    else
    {
      if (grub_strcmp (value, "@KEYMAP_LONG@") == 0)
        value = _("Press enter to boot the selected OS, "
           "`e' to edit the commands before booting "
           "or `c' for a command-line. ESC to return previous menu.");
      else if (grub_strcmp (value, "@KEYMAP_MIDDLE@") == 0)
        value = _("Press enter to boot the selected OS, "
           "`e' to edit the commands before booting "
           "or `c' for a command-line.");
      else if (grub_strcmp (value, "@KEYMAP_SHORT@") == 0)
        value = _("enter: boot, `e': options, `c': cmd-line");
      else if (grub_strcmp (value, "@KEYMAP_SCROLL_ENTRY@") == 0)
        value = _("ctrl+l: scroll entry left, ctrl+r: scroll entry right");
      else if (value[0] == '@' && value[1] == '@' && value[2] != '\0')
      {
        value = grub_env_get (&value[2]);
        if (!value)
          value = "";
      }
      /* FIXME: Add more templates here if needed.  */

      if (grub_printf_fmt_check(value, "%d") != GRUB_ERR_NONE)
        value = ""; /* Unsupported format. */

      self->template = grub_strdup (value);
      self->text = grub_xasprintf (value, self->value);
    }
  }
  else if (grub_strcmp (name, "translate") == 0)
  {
    self->refresh_text = label_refresh_default;
    grub_free (self->text);
    self->text = grub_strdup (value ? grub_gettext (value): "");
  }
  else if (grub_strcmp (name, "var") == 0)
  {
    self->refresh_text = label_refresh_var;
    grub_free (self->env);
    grub_free (self->text);
    self->text = grub_strdup ("");
    self->env = grub_strdup (value);
  }
  else if (grub_strcmp (name, "hook") == 0)
  {
    grub_free (self->template);
    self->template = grub_strdup (value ? value : "");
  }
  else if (grub_strcmp (name, "font") == 0)
  {
    self->font = grub_font_get (value);
  }
  else if (grub_strcmp (name, "color") == 0)
  {
    grub_video_parse_color (value, &self->color);
  }
  else if (grub_strcmp (name, "align") == 0)
  {
    int i;
    for (i = 0; align_options[i]; i++)
    {
      if (grub_strcmp (align_options[i], value) == 0)
      {
        self->align = i;   /* Set the alignment mode.  */
        break;
      }
    }
  }
  else if (grub_strcmp (name, "visible") == 0)
  {
    self->visible = grub_strcmp (value, "false") != 0;
  }
  else if (grub_strcmp (name, "id") == 0)
  {
    self->refresh_text = label_refresh_default;
    grub_gfxmenu_timeout_unregister ((grub_gui_component_t) self);
    grub_free (self->id);
    if (!value)
      self->id = 0;
    else if (grub_strcmp (value, GRUB_GFXMENU_TIMEOUT_COMPONENT_ID) == 0)
    {
      self->id = grub_strdup (value);
      grub_gfxmenu_timeout_register ((grub_gui_component_t) self, label_set_state);
    }
    else if (grub_strcmp (value, GRUB_GFXMENU_HELPMSG_COMPONENT_ID) == 0)
    {
      grub_free (self->text);
      self->text = grub_strdup ("");
      self->id = grub_strdup (value);
      self->refresh_text = label_refresh_help_message;
    }
    else if (grub_strcmp (value, GRUB_GFXMENU_TITLE_COMPONENT_ID) == 0)
    {
      grub_free (self->text);
      self->text = grub_strdup ("");
      self->id = grub_strdup (value);
      self->refresh_text = label_refresh_menu_title;
    }
  }
  return GRUB_ERR_NONE;
}

#pragma GCC diagnostic error "-Wformat-nonliteral"

static struct grub_gui_component_ops label_ops =
{
  .destroy = label_destroy,
  .get_id = label_get_id,
  .is_instance = label_is_instance,
  .paint = label_paint,
  .set_parent = label_set_parent,
  .get_parent = label_get_parent,
  .set_bounds = label_set_bounds,
  .get_bounds = label_get_bounds,
  .get_minimal_size = label_get_minimal_size,
  .set_property = label_set_property
};

grub_gui_component_t
grub_gui_label_new (void)
{
  grub_gui_label_t label;
  label = grub_zalloc (sizeof (*label));
  if (! label)
    return 0;
  label->comp.ops = &label_ops;
  label->visible = 1;
  label->text = grub_strdup ("");
  label->font = grub_font_get ("Unifont Regular 16");
  label->color.red = 0;
  label->color.green = 0;
  label->color.blue = 0;
  label->color.alpha = 255;
  label->align = align_left;
  label->refresh_text = label_refresh_default;
  return (grub_gui_component_t) label;
}
