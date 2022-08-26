/* menu.c - General supporting functionality for menus.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2003,2004,2005,2006,2007,2008,2009,2010  Free Software Foundation, Inc.
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

#include <grub/normal.h>
#include <grub/misc.h>
#include <grub/loader.h>
#include <grub/mm.h>
#include <grub/time.h>
#include <grub/env.h>
#include <grub/menu_viewer.h>
#include <grub/command.h>
#include <grub/parser.h>
#include <grub/auth.h>
#include <grub/i18n.h>
#include <grub/term.h>
#include <grub/script_sh.h>
#include <grub/gfxterm.h>
#include <grub/dl.h>

#if defined (__i386__) || defined (__x86_64__)
#include <grub/i386/engine_sound.h>
#include <grub/speaker.h>
#endif

/* Time to delay after displaying an error message about a default/fallback
   entry failing to boot.  */
#define DEFAULT_ENTRY_ERROR_DELAY_MS  2500

grub_err_t (*grub_gfxmenu_try_hook) (int entry, grub_menu_t menu,
                     int nested) = NULL;

#define MENU_INCLUDE_HIDDEN 0x10000

enum timeout_style {
  TIMEOUT_STYLE_MENU,
  TIMEOUT_STYLE_COUNTDOWN,
  TIMEOUT_STYLE_HIDDEN
};

struct timeout_style_name {
  const char *name;
  enum timeout_style style;
} timeout_style_names[] = {
  {"menu", TIMEOUT_STYLE_MENU},
  {"countdown", TIMEOUT_STYLE_COUNTDOWN},
  {"hidden", TIMEOUT_STYLE_HIDDEN},
  {NULL, 0}
};

/* Wait until the user pushes any key so that the user
   can see what happened.  */
void
grub_wait_after_message (void)
{
  grub_uint64_t endtime;
  grub_xputs ("\n");
  grub_printf_ (N_("Press any key to continue..."));
  grub_refresh ();

  endtime = grub_get_time_ms () + 10000;

  while (grub_get_time_ms () < endtime
     && grub_getkey_noblock () == GRUB_TERM_NO_KEY);

  grub_xputs ("\n");
}

/* Get a menu entry by its index in the entry list.  */
grub_menu_entry_t
grub_menu_get_entry (grub_menu_t menu, int no)
{
  grub_menu_entry_t e = 0;

  if (no & 0x40000000)
  {
    no &= ~0x40000000;
    for (e = menu->entry_list; e && no > 0; e = e->next, no--)
      ;
  }
  else
  {
    for (e = menu->entry_list; e && no > 0; e = e->next, no--)
    {
       /* Skip hidden entries */
       while (e && (e->flag & GRUB_MENU_FLAG_HIDDEN))
         e = e->next;
    }
    while (e && (e->flag & GRUB_MENU_FLAG_HIDDEN))
      e = e->next;
  }
  return e;
}

/* Get the index of a menu entry associated with a given hotkey, or -1.  */
static int
get_entry_index_by_hotkey (grub_menu_t menu, int hotkey)
{
  grub_menu_entry_t entry;
  int i;

  for (i = 0, entry = menu->entry_list; entry;
       i++, entry = entry->next)
    if (entry->hotkey == hotkey)
      return i | 0x40000000;

  return -1;
}

static int
get_next_entry_by_first_char (grub_menu_t menu, int key, int cur)
{
  grub_menu_entry_t entry;
  int i;

  for (i = cur + 1, entry = grub_menu_get_entry (menu, cur + 1); entry;
       i++, entry = entry->next)
  {
    if (!(entry->flag & GRUB_MENU_FLAG_HIDDEN)
        && key == grub_tolower (entry->id[0]))
      return i;
  }
  for (i = 0, entry = menu->entry_list; (i < cur) && entry;
       i++, entry = entry->next)
  {
    if (!(entry->flag & GRUB_MENU_FLAG_HIDDEN)
        && key == grub_tolower (entry->id[0]))
      return i;
  }

  return -1;
}

/* Return the timeout style.  If the variable "timeout_style" is not set or
   invalid, default to TIMEOUT_STYLE_MENU.  */
static enum timeout_style
get_timeout_style (void)
{
  const char *val;
  struct timeout_style_name *style_name;

  val = grub_env_get ("timeout_style");
  if (!val)
    return TIMEOUT_STYLE_MENU;

  for (style_name = timeout_style_names; style_name->name; style_name++)
    if (grub_strcmp (style_name->name, val) == 0)
      return style_name->style;

  return TIMEOUT_STYLE_MENU;
}

/* Return the current timeout. If the variable "timeout" is not set or
   invalid, return -1.  */
int
grub_menu_get_timeout (void)
{
  const char *val;
  int timeout;

  val = grub_env_get ("timeout");
  if (! val)
    return -1;

  grub_error_push ();

  timeout = (int) grub_strtoul (val, 0, 0);

  /* If the value is invalid, unset the variable.  */
  if (grub_errno != GRUB_ERR_NONE)
    {
      grub_env_unset ("timeout");
      grub_errno = GRUB_ERR_NONE;
      timeout = -1;
    }

  grub_error_pop ();

  return timeout;
}

/* Set current timeout in the variable "timeout".  */
void
grub_menu_set_timeout (int timeout)
{
  /* Ignore TIMEOUT if it is zero, because it will be unset really soon.  */
  if (timeout > 0)
    {
      char buf[16];

      grub_snprintf (buf, sizeof (buf), "%d", timeout);
      grub_env_set ("timeout", buf);
    }
}

static int
menuentry_eq (const char *id, const char *spec)
{
  const char *ptr1, *ptr2;
  ptr1 = id;
  ptr2 = spec;
  while (1)
    {
      if (*ptr2 == '>' && ptr2[1] != '>' && *ptr1 == 0)
    return ptr2 - spec;
      if (*ptr2 == '>' && ptr2[1] != '>')
    return 0;
      if (*ptr2 == '>')
    ptr2++;
      if (*ptr1 != *ptr2)
    return 0;
      if (*ptr1 == 0)
    return ptr1 - id;
      ptr1++;
      ptr2++;
    }
  return 0;
}

/* Get the first entry number from the value of the environment variable NAME,
   which is a space-separated list of non-negative integers.  The entry number
   which is returned is stripped from the value of NAME.  If no entry number
   can be found, -1 is returned.  */
static int
get_and_remove_first_entry_number (grub_menu_t menu, const char *name)
{
  const char *val, *tail;
  int entry;
  int sz = 0;

  val = grub_env_get (name);
  if (! val)
    return -1;

  grub_error_push ();

  entry = (int) grub_strtoul (val, &tail, 0);

  if (grub_errno == GRUB_ERR_BAD_NUMBER)
    {
      /* See if the variable matches the title of a menu entry.  */
      grub_menu_entry_t e = menu->entry_list;
      int i;

      for (i = 0; e; i++)
    {
      sz = menuentry_eq (e->title, val);
      if (sz < 1)
        sz = menuentry_eq (e->id, val);

      if (sz >= 1)
        {
          entry = i;
          break;
        }
      e = e->next;
    }

      if (sz > 0)
    grub_errno = GRUB_ERR_NONE;

      if (! e)
    entry = -1;
    }

  if (grub_errno == GRUB_ERR_NONE)
    {
      if (sz > 0)
    tail += sz;

      /* Skip whitespace to find the next entry.  */
      while (*tail && grub_isspace (*tail))
    tail++;
      grub_env_set (name, tail);
    }
  else
    {
      grub_env_unset (name);
      grub_errno = GRUB_ERR_NONE;
      entry = -1;
    }

  grub_error_pop ();

  return entry;
}

/* Run a menu entry.  */
static grub_err_t
grub_menu_execute_entry(grub_menu_entry_t entry, int auto_boot)
{
  grub_err_t err = GRUB_ERR_NONE;
  int errs_before;
  grub_menu_t menu = NULL;
  char *optr, *buf, *oldchosen = NULL, *olddefault = NULL;
  const char *ptr, *chosen, *def;
  grub_size_t sz = 0;

  if (entry->restricted)
    err = grub_auth_check_authentication (entry->users);

  if (err)
    {
      grub_print_error ();
      grub_errno = GRUB_ERR_NONE;
      return err;
    }

  errs_before = grub_err_printed_errors;

  chosen = grub_env_get ("chosen");
  def = grub_env_get ("default");

  if (entry->flag & GRUB_MENU_FLAG_SUBMENU)
    {
      grub_env_context_open ();
      menu = grub_zalloc (sizeof (*menu));
      if (! menu)
    return err;
      grub_env_set_menu (menu);
      if (auto_boot)
    grub_env_set ("timeout", "0");
    }

  for (ptr = entry->id; *ptr; ptr++)
    sz += (*ptr == '>') ? 2 : 1;
  if (chosen)
    {
      oldchosen = grub_strdup (chosen);
      if (!oldchosen)
    grub_print_error ();
    }
  if (def)
    {
      olddefault = grub_strdup (def);
      if (!olddefault)
    grub_print_error ();
    }
  sz++;
  if (chosen)
    sz += grub_strlen (chosen);
  sz++;
  buf = grub_malloc (sz);
  if (!buf)
    grub_print_error ();
  else
    {
      optr = buf;
      if (chosen)
    {
      optr = grub_stpcpy (optr, chosen);
      *optr++ = '>';
    }
      for (ptr = entry->id; *ptr; ptr++)
    {
      if (*ptr == '>')
        *optr++ = '>';
      *optr++ = *ptr;
    }
      *optr = 0;
      grub_env_set ("chosen", buf);
      grub_env_export ("chosen");
      grub_free (buf);
    }

  for (ptr = def; ptr && *ptr; ptr++)
    {
      if (ptr[0] == '>' && ptr[1] == '>')
    {
      ptr++;
      continue;
    }
      if (ptr[0] == '>')
    break;
    }

  if (ptr && ptr[0] && ptr[1])
    grub_env_set ("default", ptr + 1);
  else
    grub_env_unset ("default");

  grub_script_execute_new_scope (entry->sourcecode, entry->argc, entry->args);

  //if (errs_before != grub_err_printed_errors)
    //grub_wait_after_message ();

  errs_before = grub_err_printed_errors;

  if (grub_errno == GRUB_ERR_NONE && grub_loader_is_loaded ())
    /* Implicit execution of boot, only if something is loaded.  */
    err = grub_command_execute ("boot", 0, 0);

  if (errs_before != grub_err_printed_errors)
    grub_wait_after_message ();

  if (entry->flag & GRUB_MENU_FLAG_SUBMENU)
    {
      if (menu && menu->size)
    {
      grub_show_menu (menu, 1, auto_boot);
      grub_normal_free_menu (menu);
    }
      grub_env_context_close ();
    }
  if (oldchosen)
    grub_env_set ("chosen", oldchosen);
  else
    grub_env_unset ("chosen");
  if (olddefault)
    grub_env_set ("default", olddefault);
  else
    grub_env_unset ("default");
  grub_env_unset ("timeout");

  return err;
}

/* Execute ENTRY from the menu MENU, falling back to entries specified
   in the environment variable "fallback" if it fails.  CALLBACK is a
   pointer to a struct of function pointers which are used to allow the
   caller provide feedback to the user.  */
static void
grub_menu_execute_with_fallback (grub_menu_t menu,
                 grub_menu_entry_t entry,
                 int autobooted,
                 grub_menu_execute_callback_t callback,
                 void *callback_data)
{
  int fallback_entry;
  grub_err_t err;

  callback->notify_booting (entry, callback_data);

  err = grub_menu_execute_entry (entry, 1);
  if (err == GRUB_ERR_NONE)
    return;

  /* Deal with fallback entries.  */
  while ((fallback_entry = get_and_remove_first_entry_number (menu, "fallback"))
     >= 0)
    {
      grub_print_error ();
      grub_errno = GRUB_ERR_NONE;

      entry = grub_menu_get_entry (menu, fallback_entry);
      callback->notify_fallback (entry, callback_data);
      err = grub_menu_execute_entry (entry, 1);
      if (err == GRUB_ERR_NONE)
        return;
    }

  if (!autobooted)
    callback->notify_failure (callback_data);
}

static struct grub_menu_viewer *viewers;

static void
menu_set_chosen_entry (int entry)
{
  struct grub_menu_viewer *cur;
  for (cur = viewers; cur; cur = cur->next)
    cur->set_chosen_entry (entry, cur->data);
}

static void
menu_scroll_chosen_entry (int diren)
{
  struct grub_menu_viewer *cur;
  for (cur = viewers; cur; cur = cur->next)
    if (cur->scroll_chosen_entry)
      cur->scroll_chosen_entry (cur->data, diren);
}

/* Speed of engine.  */
static grub_uint64_t
engine_get_speed (const char *incantation)
{
  const char *val;
  grub_uint64_t speed;

  val = grub_env_get (incantation);
  if (!val)
    {
      return 0;
    }

  grub_error_push ();

  speed = (grub_uint64_t) grub_strtoul (val, 0, 0);

  if (grub_errno != GRUB_ERR_NONE)
    {
      grub_env_unset (incantation);
      grub_errno = GRUB_ERR_NONE;
      speed = 0;
    }

  grub_error_pop ();

  return speed;
}

/* To refresh animation.  */
static void
menu_set_animation_state (int need_refresh)
{
  struct grub_menu_viewer *cur;

  for (cur = viewers; cur; cur = cur->next)
    {
      cur->set_animation_state (need_refresh, cur->data);
    }
}

#if defined (__i386__) || defined (__x86_64__)
/* Does the engine need sound?  */
grub_err_t (*engine_need_sound) (void) = NULL;
static struct engine_sound_player *players;

/* To refresh sound.  */
static void
menu_refresh_sound_player (int is_selected, int cur_sound)
{
  struct engine_sound_player *cur;
  for (cur = players; cur; cur = cur->next)
    {
      cur->refresh_player_state (is_selected, cur_sound, cur->data);
    }
}

/* Destroy the sound player.  */
static void
player_fini (void)
{
  /* Random operation or timeout, beep off.  */
  grub_speaker_beep_off ();
  struct engine_sound_player *cur, *next;
  for (cur = players; cur; cur = next)
    {
      next = cur->next;
      cur->fini (cur->data);
      grub_free (cur);
    }
  players = NULL;
}

void
engine_register_player (struct engine_sound_player *player)
{
  player->next = players;
  players = player;
}
#endif

static void
menu_print_timeout (int timeout)
{
  struct grub_menu_viewer *cur;
  for (cur = viewers; cur; cur = cur->next)
    cur->print_timeout (timeout, cur->data);
}

static void
menu_fini (void)
{
  struct grub_menu_viewer *cur, *next;

  for (cur = viewers; cur; cur = next)
    {
      next = cur->next;
      cur->fini (cur->data);
      grub_free (cur);
    }
  viewers = NULL;
}

static void
menu_init (int entry, grub_menu_t menu, int nested, grub_uint64_t *frame_speed, int *egn_refresh)
{
  struct grub_term_output *term;
  int gfxmenu = 0;

  FOR_ACTIVE_TERM_OUTPUTS(term)
    if (term->fullscreen)
      {
    if (grub_env_get ("theme"))
      {
        if (!grub_gfxmenu_try_hook)
          {
        grub_dl_load ("gfxmenu");
        grub_print_error ();
          }
        if (grub_gfxmenu_try_hook)
          {
        grub_err_t err;
        err = grub_gfxmenu_try_hook (entry, menu, nested);
        if(!err)
          {
            gfxmenu = 1;
            break;
          }
          }
        else
          grub_error (GRUB_ERR_BAD_MODULE,
              N_("module `%s' isn't loaded"),
              "gfxmenu");
        grub_print_error ();
        grub_wait_after_message ();
      }
    grub_errno = GRUB_ERR_NONE;
    term->fullscreen ();
    break;
      }

  FOR_ACTIVE_TERM_OUTPUTS(term)
  {
    grub_err_t err;

    if (grub_strcmp (term->name, "gfxterm") == 0 && gfxmenu)
      {
          if (*frame_speed)
            {
              *egn_refresh = 1;
            }
          continue;
        }

    err = grub_menu_try_text (term, entry, menu, nested);
    if(!err)
      continue;
    grub_print_error ();
    grub_errno = GRUB_ERR_NONE;
  }
}

static void
clear_timeout (void)
{
  struct grub_menu_viewer *cur;
  for (cur = viewers; cur; cur = cur->next)
    cur->clear_timeout (cur->data);
}

static void
update_screen (void)
{
  struct grub_menu_viewer *cur;

  for (cur = viewers; cur; cur = cur->next)
    cur->update_screen (cur->data);
}

void
grub_menu_register_viewer (struct grub_menu_viewer *viewer)
{
  viewer->next = viewers;
  viewers = viewer;
}

/* Get the entry number from the variable NAME.  */
static int
get_entry_number (grub_menu_t menu, const char *name)
{
  const char *val;
  int entry;

  val = grub_env_get (name);
  if (! val)
    return -1;

  grub_error_push ();

  entry = (int) grub_strtoul (val, 0, 0);

  if (grub_errno == GRUB_ERR_BAD_NUMBER)
    {
      /* See if the variable matches the title of a menu entry.  */
      grub_menu_entry_t e = menu->entry_list;
      int i;

      grub_errno = GRUB_ERR_NONE;

      /* Skip hidden entries */
      while (e && e->flag & GRUB_MENU_FLAG_HIDDEN)
        e = e->next;

      for (i = 0; e; i++)
    {
      if (menuentry_eq (e->title, val)
          || menuentry_eq (e->id, val))
        {
          entry = i;
          break;
        }
      e = e->next;
    }
      /* Skip hidden entries */
      while (e && e->flag & GRUB_MENU_FLAG_HIDDEN)
        e = e->next;

      if (! e)
    entry = -1;
    }

  if (grub_errno != GRUB_ERR_NONE)
    {
      grub_errno = GRUB_ERR_NONE;
      entry = -1;
    }

  grub_error_pop ();

  return entry;
}

/* Check whether a second has elapsed since the last tick.  If so, adjust
   the timer and return 1; otherwise, return 0.  */
static int
has_second_elapsed (grub_uint64_t *saved_time)
{
  grub_uint64_t current_time;

  current_time = grub_get_time_ms ();
  if (current_time - *saved_time >= 1000)
    {
      *saved_time = current_time;
      return 1;
    }
  else
    return 0;
}

static void
print_countdown (struct grub_term_coordinate *pos, int n)
{
  grub_term_restore_pos (pos);
  /* NOTE: Do not remove the trailing space characters.
     They are required to clear the line.  */
  grub_printf ("%d    ", n);
  grub_refresh ();
}

#define GRUB_MENU_PAGE_SIZE 10

/* Show the menu and handle menu entry selection.  Returns the menu entry
   index that should be executed or -1 if no entry should be executed (e.g.,
   Esc pressed to exit a sub-menu or switching menu viewers).
   If the return value is not -1, then *AUTO_BOOT is nonzero iff the menu
   entry to be executed is a result of an automatic default selection because
   of the timeout.  */
static int
run_menu (grub_menu_t menu, int nested, int *auto_boot)
{
  grub_uint64_t saved_time;
  int default_entry, current_entry;
  int timeout;
  enum timeout_style timeout_style;

  /* Mark the beginning of the engine.  */
  int animation_open = 0;
  int egn_refresh = 0;

#if defined (__i386__) || defined (__x86_64__)
  int sound_open = 0;
  int cur_sound = ENGINE_START_SOUND;
#endif

  /* Speed of engine.  */
  grub_uint64_t s1_time;
  grub_uint64_t frame_speed = engine_get_speed (ENGINE_FRAME_SPEED);
#if defined (__i386__) || defined (__x86_64__)
  grub_uint64_t s2_time;
  grub_uint64_t sound_speed = engine_get_speed (ENGINE_SOUND_SPEED);
#endif

  default_entry = get_entry_number (menu, "default");

  /* If DEFAULT_ENTRY is not within the menu entries, fall back to
     the first entry.  */
  if (default_entry < 0 || default_entry >= menu->size)
    default_entry = 0;

  timeout = grub_menu_get_timeout ();
  if (timeout < 0)
    /* If there is no timeout, the "countdown" and "hidden" styles result in
       the system doing nothing and providing no or very little indication
       why.  Technically this is what the user asked for, but it's not very
       useful and likely to be a source of confusion, so we disallow this.  */
    grub_env_unset ("timeout_style");

  timeout_style = get_timeout_style ();

  if (timeout_style == TIMEOUT_STYLE_COUNTDOWN
      || timeout_style == TIMEOUT_STYLE_HIDDEN)
  {
    static struct grub_term_coordinate *pos;
    int entry = -1;

    if (timeout_style == TIMEOUT_STYLE_COUNTDOWN && timeout)
    {
      pos = grub_term_save_pos ();
      print_countdown (pos, timeout);
    }

    /* Enter interruptible sleep until Escape or a menu hotkey is pressed,
        or the timeout expires.  */
    saved_time = grub_get_time_ms ();
    while (1)
    {
      int key;

      key = grub_getkey_noblock ();
      if (key != GRUB_TERM_NO_KEY)
      {
        entry = get_entry_index_by_hotkey (menu, key);
        if (entry >= 0)
          break;
      }
      if (grub_key_is_interrupt (key))
      {
        timeout = -1;
        break;
      }

      if (timeout > 0 && has_second_elapsed (&saved_time))
      {
        timeout--;
        if (timeout_style == TIMEOUT_STYLE_COUNTDOWN)
          print_countdown (pos, timeout);
      }

      if (timeout == 0)
        /* We will fall through to auto-booting the default entry.  */
        break;
    }

    grub_env_unset ("timeout");
    grub_env_unset ("timeout_style");
    if (entry >= 0)
    {
      *auto_boot = 0;
      return entry;
    }
  }

  /* If timeout is 0, drawing is pointless (and ugly).  */
  if (timeout == 0)
  {
    *auto_boot = 1;
    return default_entry;
  }

  current_entry = default_entry;
  menu_set_chosen_entry (current_entry);

refresh:
  menu_init (current_entry, menu, nested, &frame_speed, &egn_refresh);

  /* Initialize the time.  */
  saved_time = grub_get_time_ms ();

  timeout = grub_menu_get_timeout ();

  if (timeout > 0)
    menu_print_timeout (timeout);
  else
    clear_timeout ();

  /* Initialize the animation engine.  */
  s1_time = grub_get_time_ms ();

  if (!animation_open && egn_refresh)
  {
    menu_set_animation_state (egn_refresh);
    animation_open = 1;
  }

  /* Initialize the sound engine.  */
#if defined (__i386__) || defined (__x86_64__)
  s2_time = grub_get_time_ms ();

  if (!sound_open && sound_speed)
  {
    grub_err_t err;
    err = engine_need_sound ();
    if (err == GRUB_ERR_NONE)
    {
      menu_refresh_sound_player (current_entry, cur_sound);
      sound_open = 1;
    }
  }
#endif

  while (1)
  {
    int c;
    const char *disable_esc = NULL;
    const char *disable_console = NULL;
    const char *disable_edit = NULL;
    timeout = grub_menu_get_timeout ();

    if (grub_normal_exit_level)
      return -1;

    if (has_second_elapsed (&saved_time))
    {
      if (timeout > 0)
      {
        timeout--;
        grub_menu_set_timeout (timeout);
        menu_print_timeout (timeout);
      }
      update_screen ();
    }

    if (timeout == 0)
    {
      grub_env_unset ("timeout");
      *auto_boot = 1;
      menu_fini ();
#if defined (__i386__) || defined (__x86_64__)
      if (sound_open)
        player_fini ();
#endif
      return default_entry;
    }

    grub_uint64_t cur_time = grub_get_time_ms ();

    /* Refresh the animation.  */
    if (animation_open && (cur_time - s1_time >= frame_speed))
    {
      s1_time = cur_time;
      menu_set_animation_state (egn_refresh);
    }

#if defined (__i386__) || defined (__x86_64__)
    /* Refresh the sound.  */
    if (sound_open && (cur_time - s2_time >= sound_speed))
    {
      s2_time = cur_time;
      menu_refresh_sound_player (current_entry, cur_sound);
    }
#endif

    c = grub_getkey_noblock ();

    /* Negative values are returned on error. */
    if ((c != GRUB_TERM_NO_KEY) && (c > 0))
    {
      if (timeout >= 0)
      {
        grub_env_unset ("timeout");
        grub_env_unset ("fallback");
        clear_timeout ();
      }

#if defined (__i386__) || defined (__x86_64__)
      cur_sound = ENGINE_SELECT_SOUND;
#endif
      switch (c)
      {
        case GRUB_TERM_KEY_HOME:
        case GRUB_TERM_CTRL | 'a':
          current_entry = 0;
          menu_set_chosen_entry (current_entry);
#if defined (__i386__) || defined (__x86_64__)
          if (sound_open)
            menu_refresh_sound_player (current_entry, cur_sound);
#endif
          break;

        case GRUB_TERM_KEY_END:
        case GRUB_TERM_CTRL | 'e':
          current_entry = menu->size - 1;
          menu_set_chosen_entry (current_entry);
#if defined (__i386__) || defined (__x86_64__)
          if (sound_open)
            menu_refresh_sound_player (current_entry, cur_sound);
#endif
          break;

        case GRUB_TERM_KEY_UP:
        case GRUB_TERM_KEY_LEFT:
        case GRUB_TERM_CTRL | 'p':
        case '^':
          if (current_entry > 0)
            current_entry--;
          menu_set_chosen_entry (current_entry);
#if defined (__i386__) || defined (__x86_64__)
          if (sound_open)
            menu_refresh_sound_player (current_entry, cur_sound);
#endif
          break;

        case GRUB_TERM_CTRL | 'n':
        case GRUB_TERM_KEY_RIGHT:
        case GRUB_TERM_KEY_DOWN:
          if (current_entry < menu->size - 1)
            current_entry++;
          menu_set_chosen_entry (current_entry);
#if defined (__i386__) || defined (__x86_64__)
          if (sound_open)
            menu_refresh_sound_player (current_entry, cur_sound);
#endif
          break;

        case GRUB_TERM_CTRL | 'g':
        case GRUB_TERM_KEY_PPAGE:
          if (current_entry < GRUB_MENU_PAGE_SIZE)
            current_entry = 0;
          else
            current_entry -= GRUB_MENU_PAGE_SIZE;
          menu_set_chosen_entry (current_entry);
#if defined (__i386__) || defined (__x86_64__)
          if (sound_open)
            menu_refresh_sound_player (current_entry, cur_sound);
#endif
          break;

        case GRUB_TERM_CTRL | 'c':
        case GRUB_TERM_KEY_NPAGE:
          if (current_entry + GRUB_MENU_PAGE_SIZE < menu->size)
            current_entry += GRUB_MENU_PAGE_SIZE;
          else
            current_entry = menu->size - 1;
          menu_set_chosen_entry (current_entry);
#if defined (__i386__) || defined (__x86_64__)
          if (sound_open)
            menu_refresh_sound_player (current_entry, cur_sound);
#endif
          break;

        case GRUB_TERM_CTRL | 'l':
          menu_scroll_chosen_entry (1);
          break;
        case GRUB_TERM_CTRL | 'r':
          menu_scroll_chosen_entry (-1);
          break;
        case '\n':
        case '\r':
        //case GRUB_TERM_KEY_RIGHT:
        //case GRUB_TERM_CTRL | 'f':
          menu_fini ();
#if defined (__i386__) || defined (__x86_64__)
          if (sound_open)
            player_fini ();
#endif
          *auto_boot = 0;
          return current_entry;

        case GRUB_TERM_ESC:
          disable_esc = grub_env_get ("grub_disable_esc");
          if (! disable_esc || disable_esc[0] == '0')
          {
            if (nested)
            {
              menu_fini ();
#if defined (__i386__) || defined (__x86_64__)
              if (sound_open)
                player_fini ();
#endif
              return -1;
            }
            break;
          }
          goto hotkey;

        case 'c':
          disable_console = grub_env_get ("grub_disable_console");
          if (! disable_console || disable_console[0] == '0')
          {
            menu_fini ();
#if defined (__i386__) || defined (__x86_64__)
            if (sound_open)
              player_fini ();
#endif
            grub_cmdline_run (1, 0);
            goto refresh;
          }
          goto hotkey;

        case 'e':
          disable_edit = grub_env_get ("grub_disable_edit");
          if (! disable_edit || disable_edit[0] == '0')
          {
            menu_fini ();
#if defined (__i386__) || defined (__x86_64__)
            if (sound_open)
              player_fini ();
#endif
            {
              grub_menu_entry_t e = grub_menu_get_entry (menu, current_entry);
              if (e)
                grub_menu_entry_run (e);
            }
            goto refresh;
          }
          goto hotkey;

        default:
hotkey:
          {
            int entry;

            entry = get_entry_index_by_hotkey (menu, c);
            if (entry >= 0)
            {
              menu_fini ();
#if defined (__i386__) || defined (__x86_64__)
              if (sound_open)
                player_fini ();
#endif
              *auto_boot = 0;
              return entry;
            }
          }
          /* jump to first-character menu */
          {
            int entry;
            int key = grub_tolower (c);
            const char *enable_jump = NULL;
            enable_jump = grub_env_get ("grub_enable_menu_jump");
            if (enable_jump && enable_jump[0] == '1'
                && (key >= 'a' && key <= 'z'))
            {
              entry = get_next_entry_by_first_char (menu, key, current_entry);
              if (entry >= 0)
              {
                current_entry = entry;
                menu_set_chosen_entry (entry);
                break;
              }
            }
          }
          break;
      }
    }
  }

  /* Never reach here.  */
}

/* Callback invoked immediately before a menu entry is executed.  */
static void
notify_booting (grub_menu_entry_t entry,
        void *userdata __attribute__((unused)))
{
  grub_dprintf ("menu", "Booting %s\n", entry->title);
}

/* Callback invoked when a default menu entry executed because of a timeout
   has failed and an attempt will be made to execute the next fallback
   entry, ENTRY.  */
static void
notify_fallback (grub_menu_entry_t entry,
         void *userdata __attribute__((unused)))
{
  grub_printf ("\n   ");
  grub_printf_ (N_("Falling back to `%s'"), entry->title);
  grub_printf ("\n\n");
  grub_millisleep (DEFAULT_ENTRY_ERROR_DELAY_MS);
}

/* Callback invoked when a menu entry has failed and there is no remaining
   fallback entry to attempt.  */
static void
notify_execution_failure (void *userdata __attribute__((unused)))
{
  if (grub_errno != GRUB_ERR_NONE)
    {
      grub_print_error ();
      grub_errno = GRUB_ERR_NONE;
    }
  grub_printf ("\n  ");
  grub_printf_ (N_("Failed to boot both default and fallback entries.\n"));
  //grub_wait_after_message ();
}

/* Callbacks used by the text menu to provide user feedback when menu entries
   are executed.  */
static struct grub_menu_execute_callback execution_callback =
{
  .notify_booting = notify_booting,
  .notify_fallback = notify_fallback,
  .notify_failure = notify_execution_failure
};

static grub_err_t
show_menu (grub_menu_t menu, int nested, int autobooted)
{
  while (1)
    {
      int boot_entry;
      grub_menu_entry_t e;
      int auto_boot;

      boot_entry = run_menu (menu, nested, &auto_boot);
      if (boot_entry < 0)
    break;

      e = grub_menu_get_entry (menu, boot_entry);
      if (! e)
    continue; /* Menu is empty.  */

      grub_cls ();

      if (auto_boot)
    grub_menu_execute_with_fallback (menu, e, autobooted,
                     &execution_callback, 0);
      else
    grub_menu_execute_entry (e, 0);
      if (autobooted)
    break;
    }

  return GRUB_ERR_NONE;
}

grub_err_t
grub_show_menu (grub_menu_t menu, int nested, int autoboot)
{
  grub_err_t err1, err2;

  while (1)
    {
      err1 = show_menu (menu, nested, autoboot);
      autoboot = 0;
      grub_print_error ();

      if (grub_normal_exit_level)
    break;

      err2 = grub_auth_check_authentication (NULL);
      if (err2)
    {
      grub_print_error ();
      grub_errno = GRUB_ERR_NONE;
      continue;
    }

      break;
    }

  if (nested && grub_normal_exit_level == -1)
    {
      menu_fini();
      grub_normal_exit_level++;
    }

  return err1;
}
