/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2006,2007,2008,2009,2010  Free Software Foundation, Inc.
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

#define grub_video_render_target grub_video_fbrender_target

#include <grub/err.h>
#include <grub/types.h>
#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/video.h>
#include <grub/video_fb.h>
#include <grub/pci.h>
#include <grub/vga.h>

GRUB_MOD_LICENSE ("GPLv3+");

static struct
{
  struct grub_video_mode_info mode_info;

  grub_uint8_t *ptr;
  int mapped;
  grub_uint32_t base;
  grub_pci_device_t dev;
} framebuffer;

#define BOCHS_APERTURE_SIZE 0x800000
#define BOCHS_MAX_WIDTH 1600
#define BOCHS_MAX_HEIGHT 1200
#define BOCHS_WIDTH_ALIGN 8

enum
  {
    BOCHS_VBE_INDEX = 0x1ce,
    BOCHS_VBE_DATA = 0x1cf,
  };

enum
  {
    BOCHS_VBE_WIDTH = 1,
    BOCHS_VBE_HEIGHT = 2,
    BOCHS_VBE_BPP = 3,
    BOCHS_VBE_ENABLE = 4,
    BOCHS_VBE_Y_OFFSET = 9,
    BOCHS_VBE_MAX
  };

static void
vbe_write (grub_uint16_t val, grub_uint16_t addr)
{
  grub_outw (addr, BOCHS_VBE_INDEX);
  grub_outw (val, BOCHS_VBE_DATA);
}

static grub_uint16_t
vbe_read (grub_uint16_t addr)
{
  grub_outw (addr, BOCHS_VBE_INDEX);
  return grub_inw (BOCHS_VBE_DATA);
}

struct saved_state
{
  grub_uint8_t cr[256];
  grub_uint8_t gr[256];
  grub_uint8_t sr[256];
  grub_uint8_t r[256];
  grub_uint8_t g[256];
  grub_uint8_t b[256];
  grub_uint8_t vbe[BOCHS_VBE_MAX];
  int vbe_enable;
  /* We need to preserve VGA font and VGA text. */
  grub_uint8_t vram[32 * 4 * 256];
};

static struct saved_state initial_state;
static int state_saved = 0;

static void
save_state (struct saved_state *st)
{
  unsigned i;

  for (i = 0; i < ARRAY_SIZE (st->cr); i++)
    st->cr[i] = grub_vga_cr_read (i);
  for (i = 0; i < ARRAY_SIZE (st->gr); i++)
    st->gr[i] = grub_vga_gr_read (i);
  for (i = 0; i < ARRAY_SIZE (st->sr); i++)
    st->sr[i] = grub_vga_sr_read (i);

  for (i = 0; i < 256; i++)
    grub_vga_palette_read (i, st->r + i, st->g + i, st->b + i);

  st->vbe_enable = vbe_read (BOCHS_VBE_ENABLE) & 1;
  if (st->vbe_enable)
    for (i = 0; i < ARRAY_SIZE (st->vbe); i++)
      st->vbe[i] = vbe_read (i);

  grub_vga_sr_write (GRUB_VGA_SR_MEMORY_MODE_CHAIN4, GRUB_VGA_SR_MEMORY_MODE);
  grub_memcpy (st->vram, framebuffer.ptr, sizeof (st->vram));
  grub_vga_sr_write (st->sr[GRUB_VGA_SR_MEMORY_MODE], GRUB_VGA_SR_MEMORY_MODE);
}

static void
restore_state (struct saved_state *st)
{
  unsigned i;

  if (st->vbe_enable)
    for (i = 0; i < ARRAY_SIZE (st->vbe); i++)
      vbe_write (st->vbe[i], i);
  else
    vbe_write (0, BOCHS_VBE_ENABLE);

  grub_vga_cr_write (0, 0x11);
  for (i = 0; i < ARRAY_SIZE (st->cr); i++)
    grub_vga_cr_write (st->cr[i], i);
  for (i = 0; i < ARRAY_SIZE (st->sr); i++)
    grub_vga_sr_write (st->sr[i], i);
  for (i = 0; i < ARRAY_SIZE (st->gr); i++)
    grub_vga_gr_write (st->gr[i], i);

  for (i = 0; i < 256; i++)
    grub_vga_palette_write (i, st->r[i], st->g[i], st->b[i]);

  grub_vga_sr_write (GRUB_VGA_SR_MEMORY_MODE_CHAIN4, GRUB_VGA_SR_MEMORY_MODE);
  grub_memcpy (framebuffer.ptr, st->vram, sizeof (st->vram));
  grub_vga_sr_write (st->sr[GRUB_VGA_SR_MEMORY_MODE], GRUB_VGA_SR_MEMORY_MODE);
}

static grub_err_t
grub_video_bochs_video_init (void)
{
  /* Reset frame buffer.  */
  grub_memset (&framebuffer, 0, sizeof(framebuffer));

  return grub_video_fb_init ();
}

static grub_err_t
grub_video_bochs_video_fini (void)
{
  if (framebuffer.mapped)
    grub_pci_device_unmap_range (framebuffer.dev, framebuffer.ptr,
				 BOCHS_APERTURE_SIZE);

  if (state_saved)
    {
      restore_state (&initial_state);
      state_saved = 0;
    }

  return grub_video_fb_fini ();
}

static grub_err_t
doublebuf_pageflipping_set_page (int page)
{
  int start = framebuffer.mode_info.height * page;

  vbe_write (start, BOCHS_VBE_Y_OFFSET);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_video_bochs_set_palette (unsigned int start, unsigned int count,
			       struct grub_video_palette_data *palette_data)
{
  if (framebuffer.mode_info.mode_type == GRUB_VIDEO_MODE_TYPE_INDEX_COLOR)
    {
      unsigned i;
      if (start >= 0x100)
	return GRUB_ERR_NONE;
      if (start + count >= 0x100)
	count = 0x100 - start;

      for (i = 0; i < count; i++)
	grub_vga_palette_write (start + i, palette_data[i].r, palette_data[i].g,
				palette_data[i].b);
    }

  /* Then set color to emulated palette.  */
  return grub_video_fb_set_palette (start, count, palette_data);
}

/* Helper for grub_video_bochs_setup.  */
static int
find_card (grub_pci_device_t dev, grub_pci_id_t pciid, void *data)
{
  int *found = data;
  grub_pci_address_t addr;
  grub_uint32_t class;

  addr = grub_pci_make_address (dev, GRUB_PCI_REG_CLASS);
  class = grub_pci_read (addr);

  if (((class >> 16) & 0xffff) != 0x0300 || pciid != 0x11111234)
    return 0;

  addr = grub_pci_make_address (dev, GRUB_PCI_REG_ADDRESS_REG0);
  framebuffer.base = grub_pci_read (addr) & GRUB_PCI_ADDR_MEM_MASK;
  if (!framebuffer.base)
    return 0;
  *found = 1;
  framebuffer.dev = dev;

  /* Enable address spaces.  */
  addr = grub_pci_make_address (framebuffer.dev, GRUB_PCI_REG_COMMAND);
  grub_pci_write (addr, 0x7);

  return 1;
}

static grub_err_t
grub_video_bochs_setup (unsigned int width, unsigned int height,
			grub_video_mode_type_t mode_type,
			grub_video_mode_type_t mode_mask)
{
  int depth;
  grub_err_t err;
  int found = 0;
  int pitch, bytes_per_pixel;
  grub_size_t page_size;        /* The size of a page in bytes.  */

  /* Decode depth from mode_type.  If it is zero, then autodetect.  */
  depth = (mode_type & GRUB_VIDEO_MODE_TYPE_DEPTH_MASK)
          >> GRUB_VIDEO_MODE_TYPE_DEPTH_POS;

  if (width == 0 || height == 0)
    {
      width = 800;
      height = 600;
    }

  if (width > BOCHS_MAX_WIDTH)
    return grub_error (GRUB_ERR_IO, "width must be at most %d",
		       BOCHS_MAX_WIDTH);

  if (height > BOCHS_MAX_HEIGHT)
    return grub_error (GRUB_ERR_IO, "height must be at most %d",
		       BOCHS_MAX_HEIGHT);

  if (width & (BOCHS_WIDTH_ALIGN - 1))
    return grub_error (GRUB_ERR_IO, "width must be a multiple of %d",
		       BOCHS_WIDTH_ALIGN);

  if (depth == 0
      && !grub_video_check_mode_flag (mode_type, mode_mask,
				      GRUB_VIDEO_MODE_TYPE_INDEX_COLOR, 0))
    depth = 24;

  if (depth == 0)
    depth = 8;

  if (depth != 32 && depth != 24 && depth != 16 && depth != 15 && depth != 8
      && depth != 4)
    return grub_error (GRUB_ERR_IO, "only 32, 24, 16, 15 and 8-bpp are"
		       " supported by bochs video");

  if (depth == 4)
    return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET, "4-bpp isn't supported");

  bytes_per_pixel = (depth + 7) / 8;
  if (depth == 4)
    pitch = width / 2;
  else
    pitch = width * bytes_per_pixel;

  page_size = pitch * height;

  if (page_size > BOCHS_APERTURE_SIZE)
    return grub_error (GRUB_ERR_IO, "Not enough video memory for this mode");

  grub_pci_iterate (find_card, &found);
  if (!found)
    return grub_error (GRUB_ERR_IO, "Couldn't find graphics card");

  if (found && framebuffer.base == 0)
    {
      /* FIXME: change framebuffer base */
      return grub_error (GRUB_ERR_IO, "PCI BAR not set");
    }

  /* We can safely discard volatile attribute.  */
  framebuffer.ptr = (void *) grub_pci_device_map_range (framebuffer.dev,
							framebuffer.base,
							BOCHS_APERTURE_SIZE);
  framebuffer.mapped = 1;

  if (!state_saved)
    {
      save_state (&initial_state);
      state_saved = 1;
    }

  {
    vbe_write (0, BOCHS_VBE_ENABLE);

    vbe_write (width, BOCHS_VBE_WIDTH);
    vbe_write (height, BOCHS_VBE_HEIGHT);
    vbe_write (depth, BOCHS_VBE_BPP);

    vbe_write (1, BOCHS_VBE_ENABLE);
    doublebuf_pageflipping_set_page (0);
  }

  /* Fill mode info details.  */
  framebuffer.mode_info.width = width;
  framebuffer.mode_info.height = height;
  framebuffer.mode_info.mode_type = GRUB_VIDEO_MODE_TYPE_RGB;
  framebuffer.mode_info.bpp = depth;
  framebuffer.mode_info.bytes_per_pixel = bytes_per_pixel;
  framebuffer.mode_info.pitch = pitch;
  framebuffer.mode_info.number_of_colors = 256;
  framebuffer.mode_info.reserved_mask_size = 0;
  framebuffer.mode_info.reserved_field_pos = 0;

  switch (depth)
    {
    case 4:
    case 8:
      framebuffer.mode_info.mode_type = GRUB_VIDEO_MODE_TYPE_INDEX_COLOR;
      framebuffer.mode_info.number_of_colors = 16;
      break;
    case 16:
      framebuffer.mode_info.red_mask_size = 5;
      framebuffer.mode_info.red_field_pos = 11;
      framebuffer.mode_info.green_mask_size = 6;
      framebuffer.mode_info.green_field_pos = 5;
      framebuffer.mode_info.blue_mask_size = 5;
      framebuffer.mode_info.blue_field_pos = 0;
      break;

    case 15:
      framebuffer.mode_info.red_mask_size = 5;
      framebuffer.mode_info.red_field_pos = 10;
      framebuffer.mode_info.green_mask_size = 5;
      framebuffer.mode_info.green_field_pos = 5;
      framebuffer.mode_info.blue_mask_size = 5;
      framebuffer.mode_info.blue_field_pos = 0;
      break;

    case 32:
      framebuffer.mode_info.reserved_mask_size = 8;
      framebuffer.mode_info.reserved_field_pos = 24;
      /* Fallthrough.  */

    case 24:
      framebuffer.mode_info.red_mask_size = 8;
      framebuffer.mode_info.red_field_pos = 16;
      framebuffer.mode_info.green_mask_size = 8;
      framebuffer.mode_info.green_field_pos = 8;
      framebuffer.mode_info.blue_mask_size = 8;
      framebuffer.mode_info.blue_field_pos = 0;
      break;
    }

  framebuffer.mode_info.blit_format = grub_video_get_blit_format (&framebuffer.mode_info);

  if (BOCHS_APERTURE_SIZE >= 2 * page_size)
    err = grub_video_fb_setup (mode_type, mode_mask,
			       &framebuffer.mode_info,
			       framebuffer.ptr,
			       doublebuf_pageflipping_set_page,
			       framebuffer.ptr + page_size);
  else
    err = grub_video_fb_setup (mode_type, mode_mask,
			       &framebuffer.mode_info,
			       framebuffer.ptr, 0, 0);


  /* Copy default palette to initialize emulated palette.  */
  err = grub_video_bochs_set_palette (0, GRUB_VIDEO_FBSTD_NUMCOLORS,
				      grub_video_fbstd_colors);
  return err;
}

static struct grub_video_adapter grub_video_bochs_adapter =
  {
    .name = "Bochs PCI Video Driver",
    .id = GRUB_VIDEO_DRIVER_BOCHS,

    .prio = GRUB_VIDEO_ADAPTER_PRIO_NATIVE,

    .init = grub_video_bochs_video_init,
    .fini = grub_video_bochs_video_fini,
    .setup = grub_video_bochs_setup,
    .get_info = grub_video_fb_get_info,
    .get_info_and_fini = grub_video_fb_get_info_and_fini,
    .set_palette = grub_video_bochs_set_palette,
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
    .swap_buffers = grub_video_fb_swap_buffers,
    .create_render_target = grub_video_fb_create_render_target,
    .delete_render_target = grub_video_fb_delete_render_target,
    .set_active_render_target = grub_video_fb_set_active_render_target,
    .get_active_render_target = grub_video_fb_get_active_render_target,

    .next = 0
  };

GRUB_MOD_INIT(video_bochs)
{
  grub_video_register (&grub_video_bochs_adapter);
}

GRUB_MOD_FINI(video_bochs)
{
  grub_video_unregister (&grub_video_bochs_adapter);
}
