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
  grub_size_t page_size;        /* The size of a page in bytes.  */

  grub_uint8_t *ptr;
  int mapped;
  grub_uint32_t base;
  grub_pci_device_t dev;
} framebuffer;

#define CIRRUS_APERTURE_SIZE 0x1000000

#define CIRRUS_MAX_WIDTH 0x800
#define CIRRUS_MAX_HEIGHT 0x800
#define CIRRUS_MAX_PITCH (0x1ff * GRUB_VGA_CR_PITCH_DIVISOR)

enum
  {
    CIRRUS_CR_EXTENDED_DISPLAY = 0x1b,
    CIRRUS_CR_EXTENDED_OVERLAY = 0x1d,
    CIRRUS_CR_MAX
  };

#define CIRRUS_CR_EXTENDED_DISPLAY_PITCH_MASK 0x10
#define CIRRUS_CR_EXTENDED_DISPLAY_PITCH_SHIFT 4
#define CIRRUS_CR_EXTENDED_DISPLAY_START_MASK1 0x1
#define CIRRUS_CR_EXTENDED_DISPLAY_START_SHIFT1 16
#define CIRRUS_CR_EXTENDED_DISPLAY_START_MASK2 0xc
#define CIRRUS_CR_EXTENDED_DISPLAY_START_SHIFT2 15

#define CIRRUS_CR_EXTENDED_OVERLAY_DISPLAY_START_MASK 0x80
#define CIRRUS_CR_EXTENDED_OVERLAY_DISPLAY_START_SHIFT 12

enum
  {
    CIRRUS_SR_EXTENDED_MODE = 7,
    CIRRUS_SR_MAX
  };

#define CIRRUS_SR_EXTENDED_MODE_LFB_ENABLE 0xf0
#define CIRRUS_SR_EXTENDED_MODE_ENABLE_EXT 0x01
#define CIRRUS_SR_EXTENDED_MODE_8BPP       0x00
#define CIRRUS_SR_EXTENDED_MODE_16BPP      0x06
#define CIRRUS_SR_EXTENDED_MODE_24BPP      0x04
#define CIRRUS_SR_EXTENDED_MODE_32BPP      0x08

#define CIRRUS_HIDDEN_DAC_ENABLE_EXT 0x80
#define CIRRUS_HIDDEN_DAC_ENABLE_ALL 0x40
#define CIRRUS_HIDDEN_DAC_8BPP 0
#define CIRRUS_HIDDEN_DAC_15BPP (CIRRUS_HIDDEN_DAC_ENABLE_EXT \
				 | CIRRUS_HIDDEN_DAC_ENABLE_ALL | 0)
#define CIRRUS_HIDDEN_DAC_16BPP (CIRRUS_HIDDEN_DAC_ENABLE_EXT \
				 | CIRRUS_HIDDEN_DAC_ENABLE_ALL | 1)
#define CIRRUS_HIDDEN_DAC_888COLOR (CIRRUS_HIDDEN_DAC_ENABLE_EXT \
				    | CIRRUS_HIDDEN_DAC_ENABLE_ALL | 5)

static void
write_hidden_dac (grub_uint8_t data)
{
  grub_inb (GRUB_VGA_IO_PALLETTE_WRITE_INDEX);
  grub_inb (GRUB_VGA_IO_PIXEL_MASK);
  grub_inb (GRUB_VGA_IO_PIXEL_MASK);
  grub_inb (GRUB_VGA_IO_PIXEL_MASK);
  grub_inb (GRUB_VGA_IO_PIXEL_MASK);
  grub_outb (data, GRUB_VGA_IO_PIXEL_MASK);
}

static grub_uint8_t
read_hidden_dac (void)
{
  grub_inb (GRUB_VGA_IO_PALLETTE_WRITE_INDEX);
  grub_inb (GRUB_VGA_IO_PIXEL_MASK);
  grub_inb (GRUB_VGA_IO_PIXEL_MASK);
  grub_inb (GRUB_VGA_IO_PIXEL_MASK);
  grub_inb (GRUB_VGA_IO_PIXEL_MASK);
  return grub_inb (GRUB_VGA_IO_PIXEL_MASK);
}

struct saved_state
{
  grub_uint8_t cr[CIRRUS_CR_MAX];
  grub_uint8_t gr[GRUB_VGA_GR_MAX];
  grub_uint8_t sr[CIRRUS_SR_MAX];
  grub_uint8_t hidden_dac;
  /* We need to preserve VGA font and VGA text. */
  grub_uint8_t vram[32 * 4 * 256];
  grub_uint8_t r[256];
  grub_uint8_t g[256];
  grub_uint8_t b[256];
};

static struct saved_state initial_state;
static int state_saved = 0;

static void
save_state (struct saved_state *st)
{
  unsigned i;
  for (i = 0; i < ARRAY_SIZE (st->cr); i++)
    st->cr[i] = grub_vga_cr_read (i);
  for (i = 0; i < ARRAY_SIZE (st->sr); i++)
    st->sr[i] = grub_vga_sr_read (i);
  for (i = 0; i < ARRAY_SIZE (st->gr); i++)
    st->gr[i] = grub_vga_gr_read (i);
  for (i = 0; i < 256; i++)
    grub_vga_palette_read (i, st->r + i, st->g + i, st->b + i);

  st->hidden_dac = read_hidden_dac ();
  grub_vga_sr_write (GRUB_VGA_SR_MEMORY_MODE_CHAIN4, GRUB_VGA_SR_MEMORY_MODE);
  grub_memcpy (st->vram, framebuffer.ptr, sizeof (st->vram));
}

static void
restore_state (struct saved_state *st)
{
  unsigned i;
  grub_vga_sr_write (GRUB_VGA_SR_MEMORY_MODE_CHAIN4, GRUB_VGA_SR_MEMORY_MODE);
  grub_memcpy (framebuffer.ptr, st->vram, sizeof (st->vram));
  for (i = 0; i < ARRAY_SIZE (st->cr); i++)
    grub_vga_cr_write (st->cr[i], i);
  for (i = 0; i < ARRAY_SIZE (st->sr); i++)
    grub_vga_sr_write (st->sr[i], i);
  for (i = 0; i < ARRAY_SIZE (st->gr); i++)
    grub_vga_gr_write (st->gr[i], i);
  for (i = 0; i < 256; i++)
    grub_vga_palette_write (i, st->r[i], st->g[i], st->b[i]);

  write_hidden_dac (st->hidden_dac);
}

static grub_err_t
grub_video_cirrus_video_init (void)
{
  /* Reset frame buffer.  */
  grub_memset (&framebuffer, 0, sizeof(framebuffer));

  return grub_video_fb_init ();
}

static grub_err_t
grub_video_cirrus_video_fini (void)
{
  if (framebuffer.mapped)
    grub_pci_device_unmap_range (framebuffer.dev, framebuffer.ptr,
				 CIRRUS_APERTURE_SIZE);

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
  int start = framebuffer.page_size * page / 4;
  grub_uint8_t cr_ext, cr_overlay;

  grub_vga_cr_write (start & 0xff, GRUB_VGA_CR_START_ADDR_LOW_REGISTER);
  grub_vga_cr_write ((start & 0xff00) >> 8,
		     GRUB_VGA_CR_START_ADDR_HIGH_REGISTER);

  cr_ext = grub_vga_cr_read (CIRRUS_CR_EXTENDED_DISPLAY);
  cr_ext &= ~(CIRRUS_CR_EXTENDED_DISPLAY_START_MASK1
	      | CIRRUS_CR_EXTENDED_DISPLAY_START_MASK2);
  cr_ext |= ((start >> CIRRUS_CR_EXTENDED_DISPLAY_START_SHIFT1)
	     & CIRRUS_CR_EXTENDED_DISPLAY_START_MASK1);
  cr_ext |= ((start >> CIRRUS_CR_EXTENDED_DISPLAY_START_SHIFT2)
	     & CIRRUS_CR_EXTENDED_DISPLAY_START_MASK2);
  grub_vga_cr_write (cr_ext, CIRRUS_CR_EXTENDED_DISPLAY);

  cr_overlay = grub_vga_cr_read (CIRRUS_CR_EXTENDED_OVERLAY);
  cr_overlay &= ~(CIRRUS_CR_EXTENDED_OVERLAY_DISPLAY_START_MASK);
  cr_overlay |= ((start >> CIRRUS_CR_EXTENDED_OVERLAY_DISPLAY_START_SHIFT)
		 & CIRRUS_CR_EXTENDED_OVERLAY_DISPLAY_START_MASK);
  grub_vga_cr_write (cr_overlay, CIRRUS_CR_EXTENDED_OVERLAY);

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_video_cirrus_set_palette (unsigned int start, unsigned int count,
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

/* Helper for grub_video_cirrus_setup.  */
static int
find_card (grub_pci_device_t dev, grub_pci_id_t pciid, void *data)
{
  int *found = data;
  grub_pci_address_t addr;
  grub_uint32_t class;

  addr = grub_pci_make_address (dev, GRUB_PCI_REG_CLASS);
  class = grub_pci_read (addr);

  if (((class >> 16) & 0xffff) != 0x0300 || pciid != 0x00b81013)
    return 0;

  addr = grub_pci_make_address (dev, GRUB_PCI_REG_ADDRESS_REG0);
  framebuffer.base = grub_pci_read (addr) & GRUB_PCI_ADDR_MEM_MASK;
  if (!framebuffer.base)
    return 0;

  *found = 1;

  /* Enable address spaces.  */
  addr = grub_pci_make_address (framebuffer.dev, GRUB_PCI_REG_COMMAND);
  grub_pci_write (addr, 0x7);

  framebuffer.dev = dev;

  return 1;
}

static grub_err_t
grub_video_cirrus_setup (unsigned int width, unsigned int height,
			 grub_video_mode_type_t mode_type,
			 grub_video_mode_type_t mode_mask)
{
  int depth;
  grub_err_t err;
  int found = 0;
  int pitch, bytes_per_pixel;

  /* Decode depth from mode_type.  If it is zero, then autodetect.  */
  depth = (mode_type & GRUB_VIDEO_MODE_TYPE_DEPTH_MASK)
          >> GRUB_VIDEO_MODE_TYPE_DEPTH_POS;

  if (width == 0 || height == 0)
    {
      width = 800;
      height = 600;
    }

  if (width & (GRUB_VGA_CR_WIDTH_DIVISOR - 1))
    return grub_error (GRUB_ERR_IO,
		       "screen width must be a multiple of %d",
		       GRUB_VGA_CR_WIDTH_DIVISOR);

  if (width > CIRRUS_MAX_WIDTH)
    return grub_error (GRUB_ERR_IO,
		       "screen width must be at most %d", CIRRUS_MAX_WIDTH);

  if (height > CIRRUS_MAX_HEIGHT)
    return grub_error (GRUB_ERR_IO,
		       "screen height must be at most %d", CIRRUS_MAX_HEIGHT);

  if (depth == 0
      && !grub_video_check_mode_flag (mode_type, mode_mask,
				      GRUB_VIDEO_MODE_TYPE_INDEX_COLOR, 0))
    depth = 24;
  else if (depth == 0)
    depth = 8;

  if (depth != 32 && depth != 24 && depth != 16 && depth != 15 && depth != 8)
    return grub_error (GRUB_ERR_IO, "only 32, 24, 16, 15 and 8-bit bpp are"
		       " supported by cirrus video");

  bytes_per_pixel = (depth + GRUB_CHAR_BIT - 1) / GRUB_CHAR_BIT;
  pitch = width * bytes_per_pixel;

  if (pitch > CIRRUS_MAX_PITCH)
    return grub_error (GRUB_ERR_IO,
		       "screen width must be at most %d at bitdepth %d",
		       CIRRUS_MAX_PITCH / bytes_per_pixel, depth);

  framebuffer.page_size = pitch * height;

  if (framebuffer.page_size > CIRRUS_APERTURE_SIZE)
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
							CIRRUS_APERTURE_SIZE);
  framebuffer.mapped = 1;

  if (!state_saved)
    {
      save_state (&initial_state);
      state_saved = 1;
    }

  {
    struct grub_video_hw_config config = {
      .pitch = pitch / GRUB_VGA_CR_PITCH_DIVISOR,
      .line_compare = 0x3ff,
      .vdisplay_end = height - 1,
      .horizontal_end = width / GRUB_VGA_CR_WIDTH_DIVISOR
    };
    grub_uint8_t sr_ext = 0, hidden_dac = 0;

    grub_vga_set_geometry (&config, grub_vga_cr_write);
    
    grub_vga_gr_write (GRUB_VGA_GR_MODE_256_COLOR | GRUB_VGA_GR_MODE_READ_MODE1,
		       GRUB_VGA_GR_MODE);
    grub_vga_gr_write (GRUB_VGA_GR_GR6_GRAPHICS_MODE, GRUB_VGA_GR_GR6);
    
    grub_vga_sr_write (GRUB_VGA_SR_MEMORY_MODE_NORMAL, GRUB_VGA_SR_MEMORY_MODE);

    grub_vga_cr_write ((config.pitch >> CIRRUS_CR_EXTENDED_DISPLAY_PITCH_SHIFT)
	      & CIRRUS_CR_EXTENDED_DISPLAY_PITCH_MASK,
	      CIRRUS_CR_EXTENDED_DISPLAY);

    grub_vga_cr_write (GRUB_VGA_CR_MODE_TIMING_ENABLE
		       | GRUB_VGA_CR_MODE_BYTE_MODE
		       | GRUB_VGA_CR_MODE_NO_HERCULES | GRUB_VGA_CR_MODE_NO_CGA,
		       GRUB_VGA_CR_MODE);

    doublebuf_pageflipping_set_page (0);

    sr_ext = CIRRUS_SR_EXTENDED_MODE_LFB_ENABLE
      | CIRRUS_SR_EXTENDED_MODE_ENABLE_EXT;
    switch (depth)
      {
	/* FIXME: support 8-bit grayscale and 8-bit RGB.  */
      case 32:
	hidden_dac = CIRRUS_HIDDEN_DAC_888COLOR;
	sr_ext |= CIRRUS_SR_EXTENDED_MODE_32BPP;
	break;
      case 24:
	hidden_dac = CIRRUS_HIDDEN_DAC_888COLOR;
	sr_ext |= CIRRUS_SR_EXTENDED_MODE_24BPP;
	break;
      case 16:
	hidden_dac = CIRRUS_HIDDEN_DAC_16BPP;
	sr_ext |= CIRRUS_SR_EXTENDED_MODE_16BPP;
	break;
      case 15:
	hidden_dac = CIRRUS_HIDDEN_DAC_15BPP;
	sr_ext |= CIRRUS_SR_EXTENDED_MODE_16BPP;
	break;
      case 8:
	hidden_dac = CIRRUS_HIDDEN_DAC_8BPP;
	sr_ext |= CIRRUS_SR_EXTENDED_MODE_8BPP;
	break;
      }
    grub_vga_sr_write (sr_ext, CIRRUS_SR_EXTENDED_MODE);
    write_hidden_dac (hidden_dac);
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

  if (CIRRUS_APERTURE_SIZE >= 2 * framebuffer.page_size)
    err = grub_video_fb_setup (mode_type, mode_mask,
			       &framebuffer.mode_info,
			       framebuffer.ptr,
			       doublebuf_pageflipping_set_page,
			       framebuffer.ptr + framebuffer.page_size);
  else
    err = grub_video_fb_setup (mode_type, mode_mask,
			       &framebuffer.mode_info,
			       framebuffer.ptr, 0, 0);


  /* Copy default palette to initialize emulated palette.  */
  err = grub_video_cirrus_set_palette (0, GRUB_VIDEO_FBSTD_NUMCOLORS,
				       grub_video_fbstd_colors);
  return err;
}

static struct grub_video_adapter grub_video_cirrus_adapter =
  {
    .name = "Cirrus CLGD 5446 PCI Video Driver",
    .id = GRUB_VIDEO_DRIVER_CIRRUS,

    .prio = GRUB_VIDEO_ADAPTER_PRIO_NATIVE,

    .init = grub_video_cirrus_video_init,
    .fini = grub_video_cirrus_video_fini,
    .setup = grub_video_cirrus_setup,
    .get_info = grub_video_fb_get_info,
    .get_info_and_fini = grub_video_fb_get_info_and_fini,
    .set_palette = grub_video_cirrus_set_palette,
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

GRUB_MOD_INIT(video_cirrus)
{
  grub_video_register (&grub_video_cirrus_adapter);
}

GRUB_MOD_FINI(video_cirrus)
{
  grub_video_unregister (&grub_video_cirrus_adapter);
}
