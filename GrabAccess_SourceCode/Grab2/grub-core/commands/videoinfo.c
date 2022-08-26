/* videoinfo.c - command to list video modes.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2007,2008,2009,2010  Free Software Foundation, Inc.
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

#include <grub/video.h>
#include <grub/dl.h>
#include <grub/env.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/command.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/lua.h>
#include <grub/bitmap.h>
#include <grub/gfxmenu_view.h>

GRUB_MOD_LICENSE ("GPLv3+");

struct hook_ctx
{
  unsigned height, width, depth;
  struct grub_video_mode_info *current_mode;
};

static int
hook (const struct grub_video_mode_info *info, void *hook_arg)
{
  struct hook_ctx *ctx = hook_arg;

  if (ctx->height && ctx->width &&
      (info->width != ctx->width || info->height != ctx->height))
    return 0;

  if (ctx->depth && info->bpp != ctx->depth)
    return 0;

  if (info->mode_number == GRUB_VIDEO_MODE_NUMBER_INVALID)
    grub_printf ("        ");
  else
  {
    if (ctx->current_mode && info->mode_number == ctx->current_mode->mode_number)
      grub_printf ("*");
    else
      grub_printf (" ");
    grub_printf (" 0x%03x ", info->mode_number);
  }
  grub_printf ("%4d x %4d x %2d (%4d)  ", info->width, info->height, info->bpp,
               info->pitch);

  if (info->mode_type & GRUB_VIDEO_MODE_TYPE_PURE_TEXT)
    grub_xputs (_("Text-only "));
  /* Show mask and position details for direct color modes.  */
  if (info->mode_type & GRUB_VIDEO_MODE_TYPE_RGB)
    /* TRANSLATORS: "Direct color" is a mode when the color components
       are written dirrectly into memory.  */
    grub_printf_ (N_("Direct color, mask: %d/%d/%d/%d  pos: %d/%d/%d/%d"),
                  info->red_mask_size,
                  info->green_mask_size,
                  info->blue_mask_size,
                  info->reserved_mask_size,
                  info->red_field_pos,
                  info->green_field_pos,
                  info->blue_field_pos,
                  info->reserved_field_pos);
  if (info->mode_type & GRUB_VIDEO_MODE_TYPE_INDEX_COLOR)
    /* TRANSLATORS: In "paletted color" mode you write the index of the color
       in the palette. Synonyms include "packed pixel".  */
    grub_xputs (_("Paletted "));
  if (info->mode_type & GRUB_VIDEO_MODE_TYPE_YUV)
    grub_xputs (_("YUV "));
  if (info->mode_type & GRUB_VIDEO_MODE_TYPE_PLANAR)
    /* TRANSLATORS: "Planar" is the video memory where you have to write
       in several different banks "plans" to control the different color
       components of the same pixel.  */
    grub_xputs (_("Planar "));
  if (info->mode_type & GRUB_VIDEO_MODE_TYPE_HERCULES)
    grub_xputs (_("Hercules "));
  if (info->mode_type & GRUB_VIDEO_MODE_TYPE_CGA)
    grub_xputs (_("CGA "));
  if (info->mode_type & GRUB_VIDEO_MODE_TYPE_NONCHAIN4)
    /* TRANSLATORS: Non-chain 4 is a 256-color planar
       (unchained) video memory mode.  */
    grub_xputs (_("Non-chain 4 "));
  if (info->mode_type & GRUB_VIDEO_MODE_TYPE_1BIT_BITMAP)
    grub_xputs (_("Monochrome "));
  if (info->mode_type & GRUB_VIDEO_MODE_TYPE_UNKNOWN)
    grub_xputs (_("Unknown video mode "));

  grub_xputs ("\n");

  return 0;
}

static void
print_edid (struct grub_video_edid_info *edid_info)
{
  unsigned int edid_width, edid_height;

  if (grub_video_edid_checksum (edid_info))
  {
    grub_puts_ (N_("  EDID checksum invalid"));
    grub_errno = GRUB_ERR_NONE;
    return;
  }

  grub_printf_ (N_("  EDID version: %u.%u\n"),
                edid_info->version, edid_info->revision);
  if (grub_video_edid_preferred_mode (edid_info, &edid_width, &edid_height)
        == GRUB_ERR_NONE)
    grub_printf_ (N_("    Preferred mode: %ux%u\n"), edid_width, edid_height);
  else
  {
    grub_printf_ (N_("    No preferred mode available\n"));
    grub_errno = GRUB_ERR_NONE;
  }
}

struct s_hook_ctx
{
  unsigned int len;
  char* data;
};

static int
s_hook (const struct grub_video_mode_info *info, void *hook_arg)
{
  unsigned int len;
  struct s_hook_ctx *ctx = hook_arg;
  char buf[24];
  if (info->mode_type & GRUB_VIDEO_MODE_TYPE_PURE_TEXT)
    return 0;
  grub_snprintf (buf, sizeof(buf), "%dx%dx%d ",
                 info->width, info->height, info->bpp);
  len = grub_strlen(buf);
  if (ctx->data)
  {
    grub_strcpy(ctx->data + ctx->len, buf);
  }
  ctx->len += len;
  return 0;
}

static grub_err_t
grub_cmd_videoinfo (grub_command_t cmd __attribute__ ((unused)),
                    int argc, char **args)
{
  grub_video_adapter_t adapter;
  grub_video_driver_id_t id;
  struct hook_ctx ctx;

  ctx.height = ctx.width = ctx.depth = 0;
  if (argc)
  {
    const char *ptr;
    ptr = args[0];
    ctx.width = grub_strtoul (ptr, &ptr, 0);
    if (grub_errno)
      return grub_errno;
    if (*ptr != 'x')
      return grub_error (GRUB_ERR_BAD_ARGUMENT,
                         N_("invalid video mode specification `%s'"), args[0]);
    ptr++;
    ctx.height = grub_strtoul (ptr, &ptr, 0);
    if (grub_errno)
      return grub_errno;
    if (*ptr == 'x')
    {
      ptr++;
      ctx.depth = grub_strtoul (ptr, &ptr, 0);
      if (grub_errno)
        return grub_errno;
    }
  }

#ifdef GRUB_MACHINE_PCBIOS
  if (grub_strcmp (cmd->name, "vbeinfo") == 0)
    grub_dl_load ("vbe");
#endif

  id = grub_video_get_driver_id ();

  grub_puts_ (N_("List of supported video modes:"));
  grub_puts_ (N_("Legend: mask/position=red/green/blue/reserved"));

  FOR_VIDEO_ADAPTERS (adapter)
  {
    struct grub_video_mode_info info;
    struct grub_video_edid_info edid_info;

    grub_printf_ (N_("Adapter `%s':\n"), adapter->name);

    if (!adapter->iterate)
    {
      grub_puts_ (N_("  No info available"));
      continue;
    }

    ctx.current_mode = NULL;

    if (adapter->id == id)
    {
      if (grub_video_get_info (&info) == GRUB_ERR_NONE)
        ctx.current_mode = &info;
      else
        /* Don't worry about errors.  */
        grub_errno = GRUB_ERR_NONE;
    }
    else
    {
      if (adapter->init ())
      {
        grub_puts_ (N_("  Failed to initialize video adapter"));
        grub_errno = GRUB_ERR_NONE;
        continue;
      }
    }

    if (adapter->print_adapter_specific_info)
      adapter->print_adapter_specific_info ();

    adapter->iterate (hook, &ctx);

    if (adapter->get_edid && adapter->get_edid (&edid_info) == GRUB_ERR_NONE)
      print_edid (&edid_info);
    else
      grub_errno = GRUB_ERR_NONE;

    ctx.current_mode = NULL;

    if (adapter->id != id)
    {
      if (adapter->fini ())
      {
        grub_errno = GRUB_ERR_NONE;
        continue;
      }
    }
  }
  return GRUB_ERR_NONE;
}

static const struct grub_arg_option options[] =
{
  {"list", 'l', 0, N_("List video modes."), 0, 0},
  {"current", 'c', 0, N_("Get current video mode."), 0, 0},
  {0, 0, 0, 0, 0, 0}
};

enum options
{
  GFXMODE_LIST,
  GFXMODE_CUR,
};

/* usb-modboot */
/* https://github.com/schierlm/usb-modboot/blob/master/grub.patch */

static grub_err_t
grub_cmd_videomode (grub_extcmd_context_t ctxt,
                    int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  grub_video_adapter_t adapter;
  grub_video_driver_id_t id;
  struct s_hook_ctx ctx;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("variable name expected"));

  if (state[GFXMODE_CUR].set)
  {
    char screen_wxh[20];
    struct grub_video_mode_info info;
    unsigned int width = 0, height = 0;
    if (grub_video_get_info (&info) == GRUB_ERR_NONE)
    {
      width = info.width;
      height = info.height;
    }
    grub_snprintf (screen_wxh, 20, "%ux%u", width, height);
    grub_env_set (args[0], screen_wxh);
    return 0;
  }

#ifdef GRUB_MACHINE_PCBIOS
  grub_dl_load ("vbe");
#endif

  id = grub_video_get_driver_id ();

  FOR_VIDEO_ADAPTERS (adapter)
  {
    if (! adapter->iterate ||
        (adapter->id != id && (id != GRUB_VIDEO_DRIVER_NONE ||
         adapter->init() != GRUB_ERR_NONE)))
    {
      continue;
    }

    ctx.data = NULL;
    ctx.len = 0;
    adapter->iterate (s_hook, &ctx);
    ctx.data = grub_malloc(ctx.len+1);
    ctx.data[0] = '\0'; ctx.len = 0;
    adapter->iterate (s_hook, &ctx);

    if (adapter->id != id)
    {
      adapter->fini();
    }

    if (id != GRUB_VIDEO_DRIVER_NONE || ctx.len)
    {
      grub_env_set (args[0], ctx.data);
      grub_free(ctx.data);
      break;
    }
    else
    {
      grub_free(ctx.data);
    }
  }

  return 0;
}

static grub_command_t cmd;
#ifdef GRUB_MACHINE_PCBIOS
static grub_command_t cmd_vbe;
#endif
static grub_extcmd_t cmd_gfx;

/* Lua function: video.swap_buffers().  */
static int
lua_video_swap_buffers (lua_State *state)
{
  if (grub_video_swap_buffers () != GRUB_ERR_NONE)
    return luaL_error (state, "Error swapping video buffers: %s", grub_errmsg);
  return 0;
}

static grub_video_color_t
check_grub_color (lua_State *state, int narg)
{
  /* Get the color components.  */
  luaL_argcheck (state, lua_istable (state, narg), narg, "should be a color");
  lua_getfield (state, narg, "r");
  lua_getfield (state, narg, "g");
  lua_getfield (state, narg, "b");
  lua_getfield (state, narg, "a");
  grub_video_color_t color;
  color = grub_video_map_rgba (luaL_checkint (state, -4),
                               luaL_checkint (state, -3),
                               luaL_checkint (state, -2),
                               luaL_optint (state, -1, 255));
  lua_pop (state, 4);
  return color;
}

/* Lua function: video.fill_rect(color, x, y, width, height). */
static int
lua_video_fill_rect (lua_State *state)
{
  grub_video_color_t color = check_grub_color (state, 1);
  int x = luaL_checkint (state, 2);
  int y = luaL_checkint (state, 3);
  int w = luaL_checkint (state, 4);
  int h = luaL_checkint (state, 5);
  if (grub_video_fill_rect (color, x, y, w, h) != GRUB_ERR_NONE)
    return luaL_error (state, "Error filling rectangle: %s", grub_errmsg);
  return 0;
}

static int
lua_video_draw_pixel (lua_State *state)
{
  grub_video_color_t color = check_grub_color (state, 1);
  int x = luaL_checkint (state, 2);
  int y = luaL_checkint (state, 3);
  if (grub_video_fill_rect (color, x, y, 1, 1) != GRUB_ERR_NONE)
    return luaL_error (state, "Error filling rectangle: %s", grub_errmsg);
  return 0;
}

static int
lua_video_get_info (lua_State *state)
{
  struct grub_video_mode_info info;
  unsigned int width = 0, height = 0;
  if (grub_video_get_info (&info) == GRUB_ERR_NONE)
  {
    width = info.width;
    height = info.height;
  }
  lua_pushinteger (state, width);
  lua_pushinteger (state, height);
  return 2;
}

/* Lua function: video.draw_string(text, font, color, x, y). */
static int
lua_video_draw_string (lua_State *state)
{
  grub_font_draw_string (luaL_checkstring (state, 1),
                          grub_font_get (luaL_checkstring (state, 2)),
                          check_grub_color (state, 3),
                          luaL_checkint (state, 4),
                          luaL_checkint (state, 5));
  return 0;
}

static int
l_hook (const struct grub_video_mode_info *info, void *hook_arg)
{
  unsigned int len;
  struct s_hook_ctx *ctx = hook_arg;
  char buf[24];
  if (info->mode_type & GRUB_VIDEO_MODE_TYPE_PURE_TEXT)
    return 0;
  grub_snprintf (buf, sizeof(buf), "%dx%dx%d ", info->width, info->height, info->bpp);
  len = grub_strlen(buf);
  if (ctx->data)
    {
      grub_strcpy(ctx->data + ctx->len, buf);
    }
  ctx->len += len;
  return 0;
}

/* Lua function: video.info(). */
static int
lua_video_info (lua_State *state __attribute__ ((unused)))
{
  grub_video_adapter_t adapter;
  grub_video_driver_id_t id;
  struct s_hook_ctx ctx;
#ifdef GRUB_MACHINE_PCBIOS
  grub_dl_load ("vbe");
#endif
  id = grub_video_get_driver_id ();
  FOR_VIDEO_ADAPTERS (adapter)
  {
    if (! adapter->iterate || (adapter->id != id && (id != GRUB_VIDEO_DRIVER_NONE || adapter->init() != GRUB_ERR_NONE)))
      {
        continue;
      }
    ctx.data = NULL;
    ctx.len = 0;
    adapter->iterate (l_hook, &ctx);
    ctx.data = grub_malloc(ctx.len+1);
    ctx.data[0] = '\0'; ctx.len = 0;
    adapter->iterate (l_hook, &ctx);
    if (adapter->id != id)
      adapter->fini();
    if (id != GRUB_VIDEO_DRIVER_NONE || ctx.len)
      {
        lua_pushstring (state, ctx.data);
        grub_free(ctx.data);
	break;
      }
    else
      grub_free(ctx.data);
  }
  return 1;
}

static int
lua_video_bitmap_load (lua_State *state)
{
  struct grub_video_bitmap *bitmap = NULL;
  const char *filename;

  filename = luaL_checkstring (state, 1);
  grub_video_bitmap_load (&bitmap, filename);
  save_errno (state);

  if (! bitmap)
    return 0;

  lua_pushlightuserdata (state, bitmap);
  return 1;
}

static int
lua_video_bitmap_close (lua_State *state)
{
  struct grub_video_bitmap *bitmap;

  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  bitmap = lua_touserdata (state, 1);
  grub_video_bitmap_destroy (bitmap);

  return 0;
}

static int
lua_video_bitmap_get_info (lua_State *state)
{
  unsigned int width, height;
  struct grub_video_bitmap *bitmap;

  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  bitmap = lua_touserdata (state, 1);
  width = grub_video_bitmap_get_width (bitmap);
  height = grub_video_bitmap_get_height (bitmap);
  lua_pushinteger (state, width);
  lua_pushinteger (state, height);
  return 2;
}

static int
lua_video_bitmap_blit (lua_State *state)
{
  struct grub_video_bitmap *bitmap;
  int x, y, offset_x, offset_y, width, height;

  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  bitmap = lua_touserdata (state, 1);
  x = luaL_checkint (state, 2);
  y = luaL_checkint (state, 3);
  offset_x = luaL_checkint (state, 4);
  offset_y = luaL_checkint (state, 5);
  width = luaL_checkint (state, 6);
  height = luaL_checkint (state, 7);
  grub_video_blit_bitmap (bitmap, GRUB_VIDEO_BLIT_BLEND,
                          x, y, offset_x, offset_y,
                          width, height);
  return 0;
}

/* src, w, h */
static int
lua_video_bitmap_rescale (lua_State *state)
{
  int w, h;
  struct grub_video_bitmap *bitmap;
  struct grub_video_bitmap *new_bitmap = NULL;

  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  bitmap = lua_touserdata (state, 1);
  w = luaL_checkint (state, 2);
  h = luaL_checkint (state, 3);
  grub_video_bitmap_create_scaled (&new_bitmap, w, h, bitmap,
                                   GRUB_VIDEO_BITMAP_SCALE_METHOD_BEST);
  if (! new_bitmap)
    return 0;
  lua_pushlightuserdata (state, new_bitmap);
  return 1;
}

luaL_Reg videolib[] =
{
  {"swap_buffers", lua_video_swap_buffers},
  {"fill_rect", lua_video_fill_rect},
  {"draw_pixel", lua_video_draw_pixel},
  {"get_info", lua_video_get_info},
  {"draw_string", lua_video_draw_string},
  {"info", lua_video_info},
  {"bitmap_load", lua_video_bitmap_load},
  {"bitmap_close", lua_video_bitmap_close},
  {"bitmap_info", lua_video_bitmap_get_info},
  {"bitmap_blit", lua_video_bitmap_blit},
  {"bitmap_rescale", lua_video_bitmap_rescale},
  {0, 0}
};

GRUB_MOD_INIT(videoinfo)
{
  cmd = grub_register_command ("videoinfo", grub_cmd_videoinfo,
                /* TRANSLATORS: "x" has to be entered in,
                   like an identifier, so please don't
                   use better Unicode codepoints.  */
                  N_("[WxH[xD]]"),
                  N_("List available video modes. If "
                     "resolution is given show only modes"
                     " matching it."));
#ifdef GRUB_MACHINE_PCBIOS
  cmd_vbe = grub_register_command ("vbeinfo", grub_cmd_videoinfo,
                /* TRANSLATORS: "x" has to be entered in,
                   like an identifier, so please don't
                   use better Unicode codepoints.  */
                  N_("[WxH[xD]]"),
                  N_("List available video modes. If "
                     "resolution is given show only modes"
                     " matching it."));
#endif
  cmd_gfx = grub_register_extcmd ("videomode", grub_cmd_videomode, 0,
                  N_("VARIABLE"),
                  N_("Store available video modes in a variable."),
                  options);
  if (grub_lua_global_state)
  {
    lua_gc (grub_lua_global_state, LUA_GCSTOP, 0);
    luaL_register (grub_lua_global_state, "video", videolib);
    lua_gc (grub_lua_global_state, LUA_GCRESTART, 0);
  }
}

GRUB_MOD_FINI(videoinfo)
{
  grub_unregister_command (cmd);
#ifdef GRUB_MACHINE_PCBIOS
  grub_unregister_command (cmd_vbe);
#endif
  grub_unregister_extcmd (cmd_gfx);
}

