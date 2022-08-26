
#define grub_video_render_target grub_video_fbrender_target

#include <grub/video.h>
#include <grub/video_fb.h>
#include <grub/mm.h>
#include <grub/misc.h>

static struct
{
  struct grub_video_mode_info mode_info;
  struct grub_video_render_target *render_target;
  grub_uint8_t *ptr;
} framebuffer;

void (*grub_video_capture_refresh_cb) (void);

static grub_err_t
grub_video_capture_swap_buffers (void)
{
  if (grub_video_capture_refresh_cb)
    grub_video_capture_refresh_cb ();
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_video_capture_set_active_render_target (struct grub_video_render_target *target)
{
  if (target == GRUB_VIDEO_RENDER_TARGET_DISPLAY)
    target = framebuffer.render_target;

  return grub_video_fb_set_active_render_target (target);
}

static grub_err_t
grub_video_capture_fini (void)
{
  return GRUB_ERR_NONE;
}

static struct grub_video_adapter grub_video_capture_adapter =
  {
    .name = "Render capture",

    .prio = 0,
    .id = GRUB_VIDEO_ADAPTER_CAPTURE,

    .fini = grub_video_capture_fini,
    .get_info = grub_video_fb_get_info,
    .get_info_and_fini = 0,
    .set_palette = grub_video_fb_set_palette,
    .get_palette = grub_video_fb_get_palette,
    .set_viewport = grub_video_fb_set_viewport,
    .get_viewport = grub_video_fb_get_viewport,
    .set_region = grub_video_fb_set_region,
    .get_region = grub_video_fb_get_region,
    .set_area_status = grub_video_fb_set_area_status,
    .get_area_status = grub_video_fb_get_area_status,
    .map_color = grub_video_fb_map_color,
    .map_rgb = grub_video_fb_map_rgb,
    .map_rgba = grub_video_fb_map_rgba,
    .unmap_color = grub_video_fb_unmap_color,
    .fill_rect = grub_video_fb_fill_rect,
    .blit_bitmap = grub_video_fb_blit_bitmap,
    .blit_render_target = grub_video_fb_blit_render_target,
    .scroll = grub_video_fb_scroll,
    .swap_buffers = grub_video_capture_swap_buffers,
    .create_render_target = grub_video_fb_create_render_target,
    .delete_render_target = grub_video_fb_delete_render_target,
    .set_active_render_target = grub_video_capture_set_active_render_target,
    .get_active_render_target = grub_video_fb_get_active_render_target,

    .next = 0
  };

static struct grub_video_adapter *saved;
static struct grub_video_mode_info saved_mode_info;

grub_err_t
grub_video_capture_start (const struct grub_video_mode_info *mode_info,
			  struct grub_video_palette_data *palette,
			  unsigned int palette_size)
{
  grub_err_t err;
  grub_memset (&framebuffer, 0, sizeof (framebuffer));

  grub_video_fb_init ();

  framebuffer.mode_info = *mode_info;
  framebuffer.mode_info.blit_format = grub_video_get_blit_format (&framebuffer.mode_info);

  framebuffer.ptr = grub_calloc (framebuffer.mode_info.height, framebuffer.mode_info.pitch);
  if (!framebuffer.ptr)
    return grub_errno;
  
  err = grub_video_fb_create_render_target_from_pointer (&framebuffer.render_target,
							 &framebuffer.mode_info,
							 framebuffer.ptr);
  if (err)
    return err;
  err = grub_video_fb_set_active_render_target (framebuffer.render_target);
  if (err)
    return err;
  err = grub_video_fb_set_palette (0, palette_size, palette);
  if (err)
    return err;

  saved = grub_video_adapter_active;
  if (saved)
    {
      grub_video_get_info (&saved_mode_info);
      if (saved->fini)
	saved->fini ();
    }
  grub_video_adapter_active = &grub_video_capture_adapter;

  return GRUB_ERR_NONE;
}

void *
grub_video_capture_get_framebuffer (void)
{
  return framebuffer.ptr;
}

void
grub_video_capture_end (void)
{
  grub_video_fb_delete_render_target (framebuffer.render_target);
  grub_free (framebuffer.ptr);
  grub_video_fb_fini ();
  grub_video_adapter_active = saved;
  if (saved)
    {
      if (saved->init)
	saved->init ();
      if (saved->setup)
	saved->setup (saved_mode_info.width, saved_mode_info.height, 0, 0);
    }
}
