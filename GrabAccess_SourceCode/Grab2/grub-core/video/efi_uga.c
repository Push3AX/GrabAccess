/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2006,2007,2008,2009  Free Software Foundation, Inc.
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
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/uga_draw.h>
#include <grub/pci.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_efi_guid_t uga_draw_guid = GRUB_EFI_UGA_DRAW_GUID;
static struct grub_efi_uga_draw_protocol *uga;
static grub_uint64_t uga_fb;
static grub_uint32_t uga_pitch;

static struct
{
  struct grub_video_mode_info mode_info;
  struct grub_video_render_target *render_target;
  grub_uint8_t *ptr;
} framebuffer;

#define RGB_MASK	0xffffff
#define RGB_MAGIC	0x121314
#define LINE_MIN	800
#define LINE_MAX	4096
#define FBTEST_STEP	(0x10000 >> 2)
#define FBTEST_COUNT	8

static int
find_line_len (grub_uint64_t *fb_base, grub_uint32_t *line_len)
{
  grub_uint32_t *base = (grub_uint32_t *) (grub_addr_t) *fb_base;
  int i;

  for (i = 0; i < FBTEST_COUNT; i++, base += FBTEST_STEP)
    {
      if ((*base & RGB_MASK) == RGB_MAGIC)
	{
	  int j;

	  for (j = LINE_MIN; j <= LINE_MAX; j++)
	    {
	      if ((base[j] & RGB_MASK) == RGB_MAGIC)
		{
		  *fb_base = (grub_uint64_t) (grub_addr_t) base;
		  *line_len = j << 2;

		  return 1;
		}
	    }

	  break;
	}
    }

  return 0;
}

/* Context for find_framebuf.  */
struct find_framebuf_ctx
{
  grub_uint64_t *fb_base;
  grub_uint32_t *line_len;
  int found;
};

/* Helper for find_framebuf.  */
static int
find_card (grub_pci_device_t dev, grub_pci_id_t pciid, void *data)
{
  struct find_framebuf_ctx *ctx = data;
  grub_pci_address_t addr, rcaddr;
  grub_uint32_t subclass;

  addr = grub_pci_make_address (dev, GRUB_PCI_REG_CLASS);
  subclass = (grub_pci_read (addr) >> 16) & 0xffff;

  if (subclass != GRUB_PCI_CLASS_SUBCLASS_VGA)
    return 0;

  /* Enable MEM address spaces */
  rcaddr = grub_pci_make_address (dev, GRUB_PCI_REG_COMMAND);
  grub_pci_write_word (rcaddr, grub_pci_read_word (rcaddr) | GRUB_PCI_COMMAND_MEM_ENABLED);

    {
      int i;

      grub_dprintf ("video", "Display controller: %d:%d.%d\nDevice id: %x\n",
		    grub_pci_get_bus (dev), grub_pci_get_device (dev),
		    grub_pci_get_function (dev), pciid);
      addr += 8;
      for (i = 0; i < 6; i++, addr += 4)
	{
	  grub_uint32_t old_bar1, old_bar2, type;
	  grub_uint64_t base64;

	  old_bar1 = grub_pci_read (addr);
	  if ((! old_bar1) || (old_bar1 & GRUB_PCI_ADDR_SPACE_IO))
	    continue;

	  type = old_bar1 & GRUB_PCI_ADDR_MEM_TYPE_MASK;
	  if (type == GRUB_PCI_ADDR_MEM_TYPE_64)
	    {
	      if (i == 5)
		break;

	      i++;
	      addr += 4;
	      old_bar2 = grub_pci_read (addr);
	    }
	  else
	    old_bar2 = 0;

	  base64 = old_bar2;
	  base64 <<= 32;
	  base64 |= (old_bar1 & GRUB_PCI_ADDR_MEM_MASK);

	  grub_dprintf ("video", "%s(%d): 0x%" PRIxGRUB_UINT64_T "\n",
			((old_bar1 & GRUB_PCI_ADDR_MEM_PREFETCH) ?
			"VMEM" : "MMIO"), type == GRUB_PCI_ADDR_MEM_TYPE_64 ? i - 1 : i,
			base64);

#if GRUB_CPU_SIZEOF_VOID_P == 4
	  if (old_bar2)
	    continue;
#endif

	  if ((old_bar1 & GRUB_PCI_ADDR_MEM_PREFETCH) && (! ctx->found))
	    {
	      *ctx->fb_base = base64;
	      if (find_line_len (ctx->fb_base, ctx->line_len))
		ctx->found++;
	    }
	}
    }

  return ctx->found;
}

static int
find_framebuf (grub_uint64_t *fb_base, grub_uint32_t *line_len)
{
  struct find_framebuf_ctx ctx = {
    .fb_base = fb_base,
    .line_len = line_len,
    .found = 0
  };

  grub_pci_iterate (find_card, &ctx);
  return ctx.found;
}

static int
check_protocol (void)
{
  grub_efi_uga_draw_protocol_t *c;

  c = grub_efi_locate_protocol (&uga_draw_guid, 0);
  if (c)
    {
      grub_uint32_t width, height, depth, rate, pixel;
      int ret;

      if (efi_call_5 (c->get_mode, c, &width, &height, &depth, &rate))
	return 0;

      grub_efi_set_text_mode (0);
      pixel = RGB_MAGIC;
      efi_call_10 (c->blt, c, (struct grub_efi_uga_pixel *) &pixel,
		   GRUB_EFI_UGA_VIDEO_FILL, 0, 0, 0, 0, 1, height, 0);
      ret = find_framebuf (&uga_fb, &uga_pitch);
      grub_efi_set_text_mode (1);

      if (ret)
	{
	  uga = c;
	  return 1;
	}
    }

  return 0;
}

static grub_err_t
grub_video_uga_init (void)
{
  grub_memset (&framebuffer, 0, sizeof(framebuffer));
  return grub_video_fb_init ();
}

static grub_err_t
grub_video_uga_fini (void)
{
  return grub_video_fb_fini ();
}

static grub_err_t
grub_video_uga_setup (unsigned int width, unsigned int height,
		      unsigned int mode_type,
		      unsigned int mode_mask __attribute__ ((unused)))
{
  unsigned int depth;
  int found = 0;

  depth = (mode_type & GRUB_VIDEO_MODE_TYPE_DEPTH_MASK)
    >> GRUB_VIDEO_MODE_TYPE_DEPTH_POS;

  {
    grub_uint32_t w;
    grub_uint32_t h;
    grub_uint32_t d;
    grub_uint32_t r;

    if ((! efi_call_5 (uga->get_mode, uga, &w, &h, &d, &r)) &&
	((! width) || (width == w)) &&
	((! height) || (height == h)) &&
	((! depth) || (depth == d)))
      {
	framebuffer.mode_info.width = w;
	framebuffer.mode_info.height = h;
	framebuffer.mode_info.pitch = uga_pitch;
	framebuffer.ptr = (grub_uint8_t *) (grub_addr_t) uga_fb;

	found = 1;
      }
  }

  if (found)
    {
      grub_err_t err;

      framebuffer.mode_info.mode_type = GRUB_VIDEO_MODE_TYPE_RGB;
      framebuffer.mode_info.bpp = 32;
      framebuffer.mode_info.bytes_per_pixel = 4;
      framebuffer.mode_info.number_of_colors = 256;
      framebuffer.mode_info.red_mask_size = 8;
      framebuffer.mode_info.red_field_pos = 16;
      framebuffer.mode_info.green_mask_size = 8;
      framebuffer.mode_info.green_field_pos = 8;
      framebuffer.mode_info.blue_mask_size = 8;
      framebuffer.mode_info.blue_field_pos = 0;
      framebuffer.mode_info.reserved_mask_size = 8;
      framebuffer.mode_info.reserved_field_pos = 24;

      framebuffer.mode_info.blit_format =
	grub_video_get_blit_format (&framebuffer.mode_info);

      err = grub_video_fb_create_render_target_from_pointer
	(&framebuffer.render_target,
	 &framebuffer.mode_info,
	 framebuffer.ptr);

      if (err)
	return err;

      err = grub_video_fb_set_active_render_target
	(framebuffer.render_target);

      if (err)
	return err;

      err = grub_video_fb_set_palette (0, GRUB_VIDEO_FBSTD_NUMCOLORS,
				       grub_video_fbstd_colors);

      return err;
    }

  return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "no matching mode found");
}

static grub_err_t
grub_video_uga_swap_buffers (void)
{
  /* TODO: Implement buffer swapping.  */
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_video_uga_set_active_render_target (struct grub_video_render_target *target)
{
  if (target == GRUB_VIDEO_RENDER_TARGET_DISPLAY)
    target = framebuffer.render_target;

  return grub_video_fb_set_active_render_target (target);
}

static grub_err_t
grub_video_uga_get_info_and_fini (struct grub_video_mode_info *mode_info,
				  void **framebuf)
{
  grub_memcpy (mode_info, &(framebuffer.mode_info), sizeof (*mode_info));
  *framebuf = (char *) framebuffer.ptr;

  grub_video_fb_fini ();

  return GRUB_ERR_NONE;
}

static struct grub_video_adapter grub_video_uga_adapter =
  {
    .name = "EFI UGA driver",
    .id = GRUB_VIDEO_DRIVER_EFI_UGA,

    .prio = GRUB_VIDEO_ADAPTER_PRIO_FIRMWARE_DIRTY,

    .init = grub_video_uga_init,
    .fini = grub_video_uga_fini,
    .setup = grub_video_uga_setup,
    .get_info = grub_video_fb_get_info,
    .get_info_and_fini = grub_video_uga_get_info_and_fini,
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
    .swap_buffers = grub_video_uga_swap_buffers,
    .create_render_target = grub_video_fb_create_render_target,
    .delete_render_target = grub_video_fb_delete_render_target,
    .set_active_render_target = grub_video_uga_set_active_render_target,
    .get_active_render_target = grub_video_fb_get_active_render_target,
  };

GRUB_MOD_INIT(efi_uga)
{
  if (check_protocol ())
    grub_video_register (&grub_video_uga_adapter);
}

GRUB_MOD_FINI(efi_uga)
{
  if (uga)
    grub_video_unregister (&grub_video_uga_adapter);
}
