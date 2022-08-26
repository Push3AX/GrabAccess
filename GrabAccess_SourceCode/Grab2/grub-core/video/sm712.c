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

#if !defined (TEST) && !defined(GENINIT)
#include <grub/err.h>
#include <grub/types.h>
#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/video.h>
#include <grub/video_fb.h>
#include <grub/pci.h>
#include <grub/vga.h>
#include <grub/cache.h>
#else
typedef unsigned char grub_uint8_t;
typedef unsigned short grub_uint16_t;
typedef unsigned int grub_uint32_t;
typedef int grub_err_t;
#include <grub/vgaregs.h>
#include <stdio.h>
#define ARRAY_SIZE(array) (sizeof (array) / sizeof (array[0]))
#endif

#include "sm712_init.c"

#pragma GCC diagnostic ignored "-Wcast-align"

#define GRUB_SM712_TOTAL_MEMORY_SPACE  0x700400
#define GRUB_SM712_REG_BASE 0x700000
#define GRUB_SM712_PCIID 0x0712126f

enum
  {
    GRUB_SM712_SR_TV_CONTROL = 0x65,
    GRUB_SM712_SR_RAM_LUT = 0x66,
    GRUB_SM712_SR_CLOCK_CONTROL1 = 0x68,
    GRUB_SM712_SR_CLOCK_CONTROL2 = 0x69,
    GRUB_SM712_SR_VCLK_NUM = 0x6c,
    GRUB_SM712_SR_VCLK_DENOM = 0x6d,
    GRUB_SM712_SR_VCLK2_NUM = 0x6e,
    GRUB_SM712_SR_VCLK2_DENOM = 0x6f,
    GRUB_SM712_SR_POPUP_ICON_LOW = 0x80,
    GRUB_SM712_SR_POPUP_ICON_HIGH = 0x81,
    GRUB_SM712_SR_POPUP_ICON_CTRL = 0x82,
    GRUB_SM712_SR_POPUP_ICON_COLOR1 = 0x84,
    GRUB_SM712_SR_POPUP_ICON_COLOR2 = 0x85,
    GRUB_SM712_SR_POPUP_ICON_COLOR3 = 0x86,

    GRUB_SM712_SR_HW_CURSOR_UPPER_LEFT_X_LOW = 0x88,
    GRUB_SM712_SR_HW_CURSOR_UPPER_LEFT_X_HIGH = 0x89,
    GRUB_SM712_SR_HW_CURSOR_UPPER_LEFT_Y_LOW = 0x8a,
    GRUB_SM712_SR_HW_CURSOR_UPPER_LEFT_Y_HIGH = 0x8b,
    GRUB_SM712_SR_HW_CURSOR_FG_COLOR = 0x8c,
    GRUB_SM712_SR_HW_CURSOR_BG_COLOR = 0x8d,

    GRUB_SM712_SR_POPUP_ICON_X_LOW = 0x90,
    GRUB_SM712_SR_POPUP_ICON_X_HIGH = 0x91,
    GRUB_SM712_SR_POPUP_ICON_Y_LOW = 0x92,
    GRUB_SM712_SR_POPUP_ICON_Y_HIGH = 0x93,
    GRUB_SM712_SR_PANEL_HW_VIDEO_CONTROL = 0xa0,
    GRUB_SM712_SR_PANEL_HW_VIDEO_COLOR_KEY_LOW = 0xa1,
    GRUB_SM712_SR_PANEL_HW_VIDEO_COLOR_KEY_HIGH = 0xa2,
    GRUB_SM712_SR_PANEL_HW_VIDEO_COLOR_KEY_MASK_LOW = 0xa3,
    GRUB_SM712_SR_PANEL_HW_VIDEO_COLOR_KEY_MASK_HIGH = 0xa4,
    GRUB_SM712_SR_PANEL_HW_VIDEO_RED_CONSTANT = 0xa5,
    GRUB_SM712_SR_PANEL_HW_VIDEO_GREEN_CONSTANT = 0xa6,
    GRUB_SM712_SR_PANEL_HW_VIDEO_BLUE_CONSTANT = 0xa7,
    GRUB_SM712_SR_PANEL_HW_VIDEO_TOP_BOUNDARY = 0xa8,
    GRUB_SM712_SR_PANEL_HW_VIDEO_LEFT_BOUNDARY = 0xa9,
    GRUB_SM712_SR_PANEL_HW_VIDEO_BOTTOM_BOUNDARY = 0xaa,
    GRUB_SM712_SR_PANEL_HW_VIDEO_RIGHT_BOUNDARY = 0xab,
    GRUB_SM712_SR_PANEL_HW_VIDEO_TOP_LEFT_OVERFLOW_BOUNDARY = 0xac,
    GRUB_SM712_SR_PANEL_HW_VIDEO_BOTTOM_RIGHT_OVERFLOW_BOUNDARY = 0xad,
    GRUB_SM712_SR_PANEL_HW_VIDEO_VERTICAL_STRETCH_FACTOR = 0xae,
    GRUB_SM712_SR_PANEL_HW_VIDEO_HORIZONTAL_STRETCH_FACTOR = 0xaf,
  };
enum
  {
    GRUB_SM712_SR_TV_CRT_SRAM = 0x00,
    GRUB_SM712_SR_TV_LCD_SRAM = 0x08
  };
enum
  {
    GRUB_SM712_SR_TV_ALT_CLOCK = 0x00,
    GRUB_SM712_SR_TV_FREE_RUN_CLOCK = 0x04
  };
enum
  {
    GRUB_SM712_SR_TV_CLOCK_CKIN_NTSC = 0x00,
    GRUB_SM712_SR_TV_CLOCK_REFCLK_PAL = 0x04
  };
enum
  {
    GRUB_SM712_SR_TV_HSYNC = 0x00,
    GRUB_SM712_SR_TV_COMPOSITE_HSYNC = 0x01
  };
enum
  {
    GRUB_SM712_SR_RAM_LUT_NORMAL = 0,
    GRUB_SM712_SR_RAM_LUT_LCD_RAM_OFF = 0x80,
    GRUB_SM712_SR_RAM_LUT_CRT_RAM_OFF = 0x40,
    GRUB_SM712_SR_RAM_LUT_LCD_RAM_NO_WRITE = 0x20,
    GRUB_SM712_SR_RAM_LUT_CRT_RAM_NO_WRITE = 0x10,
    GRUB_SM712_SR_RAM_LUT_CRT_8BIT = 0x08,
    GRUB_SM712_SR_RAM_LUT_CRT_GAMMA = 0x04
  };

enum
  {
    GRUB_SM712_SR_CLOCK_CONTROL1_VCLK_FROM_CCR = 0x40,
    GRUB_SM712_SR_CLOCK_CONTROL1_8DOT_CLOCK = 0x10,
  };

enum
  {
    GRUB_SM712_SR_CLOCK_CONTROL2_PROGRAM_VCLOCK = 0x03
  };

#define GRUB_SM712_SR_POPUP_ICON_HIGH_MASK 0x7
#define GRUB_SM712_SR_POPUP_ICON_HIGH_HW_CURSOR_EN 0x80
  enum
  {
    GRUB_SM712_SR_POPUP_ICON_CTRL_DISABLED = 0,
    GRUB_SM712_SR_POPUP_ICON_CTRL_ZOOM_ENABLED = 0x40,
    GRUB_SM712_SR_POPUP_ICON_CTRL_ENABLED = 0x80
  };
#define RGB332_BLACK 0
#define RGB332_WHITE 0xff

  enum
  {
    GRUB_SM712_CR_OVERFLOW_INTERLACE = 0x30,
    GRUB_SM712_CR_INTERLACE_RETRACE = 0x31,
    GRUB_SM712_CR_TV_VDISPLAY_START = 0x32,
    GRUB_SM712_CR_TV_VDISPLAY_END_HIGH = 0x33,
    GRUB_SM712_CR_TV_VDISPLAY_END_LOW = 0x34,
    GRUB_SM712_CR_DDA_CONTROL_LOW = 0x35,
    GRUB_SM712_CR_DDA_CONTROL_HIGH = 0x36,
    GRUB_SM712_CR_TV_EQUALIZER = 0x38,
    GRUB_SM712_CR_TV_SERRATION = 0x39,
    GRUB_SM712_CR_HSYNC_CTRL = 0x3a,
    GRUB_SM712_CR_DEBUG = 0x3c,
    GRUB_SM712_CR_SHADOW_VGA_HTOTAL = 0x40,
    GRUB_SM712_CR_SHADOW_VGA_HBLANK_START = 0x41,
    GRUB_SM712_CR_SHADOW_VGA_HBLANK_END = 0x42,
    GRUB_SM712_CR_SHADOW_VGA_HRETRACE_START = 0x43,
    GRUB_SM712_CR_SHADOW_VGA_HRETRACE_END = 0x44,
    GRUB_SM712_CR_SHADOW_VGA_VERTICAL_TOTAL = 0x45,
    GRUB_SM712_CR_SHADOW_VGA_VBLANK_START = 0x46,
    GRUB_SM712_CR_SHADOW_VGA_VBLANK_END = 0x47,
    GRUB_SM712_CR_SHADOW_VGA_VRETRACE_START = 0x48,
    GRUB_SM712_CR_SHADOW_VGA_VRETRACE_END = 0x49,    
    GRUB_SM712_CR_SHADOW_VGA_OVERFLOW = 0x4a,
    GRUB_SM712_CR_SHADOW_VGA_CELL_HEIGHT = 0x4b,
    GRUB_SM712_CR_SHADOW_VGA_HDISPLAY_END = 0x4c,
    GRUB_SM712_CR_SHADOW_VGA_VDISPLAY_END = 0x4d,
    GRUB_SM712_CR_DDA_LOOKUP_REG3_START = 0x90,
    GRUB_SM712_CR_DDA_LOOKUP_REG2_START = 0x91,
    GRUB_SM712_CR_DDA_LOOKUP_REG1_START = 0xa0,
    GRUB_SM712_CR_VCENTERING_OFFSET = 0xa6,
    GRUB_SM712_CR_HCENTERING_OFFSET = 0xa7,
  };

#define GRUB_SM712_CR_DEBUG_NONE 0

#define SM712_DDA_REG3_COMPARE_SHIFT 2
#define SM712_DDA_REG3_COMPARE_MASK  0xfc
#define SM712_DDA_REG3_DDA_SHIFT 8
#define SM712_DDA_REG3_DDA_MASK  0x3
#define SM712_DDA_REG2_DDA_MASK  0xff
#define SM712_DDA_REG2_VCENTER_MASK 0x3f

static struct
{
  grub_uint8_t compare;
  grub_uint16_t dda;
  grub_uint8_t vcentering;
} dda_lookups[] = {
  { 21, 469,  2},
  { 23, 477,  2},
  { 33, 535,  2},
  { 35, 682, 21},
  { 34, 675,  2},
  { 55, 683,  6},
};

static struct
{
#if !defined (TEST) && !defined(GENINIT)
  struct grub_video_mode_info mode_info;
#endif

  volatile grub_uint8_t *ptr;
  grub_uint8_t *cached_ptr;
  int mapped;
  grub_uint32_t base;
#if !defined (TEST) && !defined(GENINIT)
  grub_pci_device_t dev;
#endif
} framebuffer;

#if !defined (TEST) && !defined(GENINIT)
static grub_err_t
grub_video_sm712_video_init (void)
{
  /* Reset frame buffer.  */
  grub_memset (&framebuffer, 0, sizeof(framebuffer));

  return grub_video_fb_init ();
}

static grub_err_t
grub_video_sm712_video_fini (void)
{
  if (framebuffer.mapped)
    {
      grub_pci_device_unmap_range (framebuffer.dev, framebuffer.ptr,
				   GRUB_SM712_TOTAL_MEMORY_SPACE);
      grub_pci_device_unmap_range (framebuffer.dev, framebuffer.cached_ptr,
				   GRUB_SM712_TOTAL_MEMORY_SPACE);
    }
  return grub_video_fb_fini ();
}
#endif

static inline void
grub_sm712_write_reg (grub_uint8_t val, grub_uint16_t addr)
{
#ifdef TEST
  printf ("  {1, 0x%x, 0x%x},\n", addr, val);
#elif defined (GENINIT)
  printf (" .byte 0x%02x, 0x%02x\n", (addr - 0x3c0), val);
  if ((addr - 0x3c0) & ~0x7f)
    printf ("FAIL\n");
#else
   *(volatile grub_uint8_t *) (framebuffer.ptr + GRUB_SM712_REG_BASE
			       + addr) = val;
#endif
}

static inline grub_uint8_t
grub_sm712_read_reg (grub_uint16_t addr)
{
#ifdef TEST
  printf ("  {-1, 0x%x, 0x5},\n", addr);
#elif defined (GENINIT)
  if ((addr - 0x3c0) & ~0x7f)
    printf ("FAIL\n");
  printf ("  .byte 0x%04x, 0x00\n", (addr - 0x3c0) | 0x80);
#else
  return *(volatile grub_uint8_t *) (framebuffer.ptr + GRUB_SM712_REG_BASE
				     + addr);
#endif
}

static inline grub_uint8_t
grub_sm712_sr_read (grub_uint8_t addr)
{
  grub_sm712_write_reg (addr, GRUB_VGA_IO_SR_INDEX);
  return grub_sm712_read_reg (GRUB_VGA_IO_SR_DATA);
}

static inline void
grub_sm712_sr_write (grub_uint8_t val, grub_uint8_t addr)
{
  grub_sm712_write_reg (addr, GRUB_VGA_IO_SR_INDEX);
  grub_sm712_write_reg (val, GRUB_VGA_IO_SR_DATA);
}

static inline void
grub_sm712_gr_write (grub_uint8_t val, grub_uint8_t addr)
{
  grub_sm712_write_reg (addr, GRUB_VGA_IO_GR_INDEX);
  grub_sm712_write_reg (val, GRUB_VGA_IO_GR_DATA);
}

static inline void
grub_sm712_cr_write (grub_uint8_t val, grub_uint8_t addr)
{
  grub_sm712_write_reg (addr, GRUB_VGA_IO_CR_INDEX);
  grub_sm712_write_reg (val, GRUB_VGA_IO_CR_DATA);
}

static inline void
grub_sm712_write_arx (grub_uint8_t val, grub_uint8_t addr)
{
  grub_sm712_read_reg (GRUB_VGA_IO_INPUT_STATUS1_REGISTER);
  grub_sm712_write_reg (addr, GRUB_VGA_IO_ARX);
  grub_sm712_read_reg (GRUB_VGA_IO_ARX_READ);
  grub_sm712_write_reg (val, GRUB_VGA_IO_ARX);
}

static inline void
grub_sm712_cr_shadow_write (grub_uint8_t val, grub_uint8_t addr)
{
  grub_uint8_t mapping[] =
    {
      [GRUB_VGA_CR_HTOTAL] = GRUB_SM712_CR_SHADOW_VGA_HTOTAL,
      [GRUB_VGA_CR_HORIZ_END] = 0xff,
      [GRUB_VGA_CR_HBLANK_START] = GRUB_SM712_CR_SHADOW_VGA_HBLANK_START,
      [GRUB_VGA_CR_HBLANK_END] = GRUB_SM712_CR_SHADOW_VGA_HBLANK_END,
      [GRUB_VGA_CR_HORIZ_SYNC_PULSE_START] = GRUB_SM712_CR_SHADOW_VGA_HRETRACE_START,
      [GRUB_VGA_CR_HORIZ_SYNC_PULSE_END] = GRUB_SM712_CR_SHADOW_VGA_HRETRACE_END,
      [GRUB_VGA_CR_VERT_TOTAL] = GRUB_SM712_CR_SHADOW_VGA_VERTICAL_TOTAL,
      [GRUB_VGA_CR_OVERFLOW] = GRUB_SM712_CR_SHADOW_VGA_OVERFLOW,
      [GRUB_VGA_CR_BYTE_PANNING] = 0xff,
      [GRUB_VGA_CR_CELL_HEIGHT] = GRUB_SM712_CR_SHADOW_VGA_CELL_HEIGHT,
      [GRUB_VGA_CR_CURSOR_START] = 0xff,
      [GRUB_VGA_CR_CURSOR_END] = 0xff,
      [GRUB_VGA_CR_START_ADDR_HIGH_REGISTER] = 0xff,
      [GRUB_VGA_CR_START_ADDR_LOW_REGISTER] = 0xff,
      [GRUB_VGA_CR_CURSOR_ADDR_HIGH] = 0xff,
      [GRUB_VGA_CR_CURSOR_ADDR_LOW] = 0xff,
      [GRUB_VGA_CR_VSYNC_START] = GRUB_SM712_CR_SHADOW_VGA_VRETRACE_START,
      [GRUB_VGA_CR_VSYNC_END] = GRUB_SM712_CR_SHADOW_VGA_VRETRACE_END,
      [GRUB_VGA_CR_VDISPLAY_END] = GRUB_SM712_CR_SHADOW_VGA_VDISPLAY_END,
      [GRUB_VGA_CR_PITCH] = GRUB_SM712_CR_SHADOW_VGA_HDISPLAY_END,
      [GRUB_VGA_CR_UNDERLINE_LOCATION] = 0xff,

      [GRUB_VGA_CR_VERTICAL_BLANK_START] = GRUB_SM712_CR_SHADOW_VGA_VBLANK_START,
      [GRUB_VGA_CR_VERTICAL_BLANK_END] = GRUB_SM712_CR_SHADOW_VGA_VBLANK_END,
      [GRUB_VGA_CR_MODE] = 0xff,
      [GRUB_VGA_CR_LINE_COMPARE] = 0xff
    };
  if (addr >= ARRAY_SIZE (mapping) || mapping[addr] == 0xff)
    return;
  grub_sm712_cr_write (val, mapping[addr]);
}

static inline void
grub_sm712_write_dda_lookup (int idx, grub_uint8_t compare, grub_uint16_t dda,
			     grub_uint8_t vcentering)
{
  grub_sm712_cr_write (((compare << SM712_DDA_REG3_COMPARE_SHIFT)
			& SM712_DDA_REG3_COMPARE_MASK)
		       | ((dda >> SM712_DDA_REG3_DDA_SHIFT)
			  & SM712_DDA_REG3_DDA_MASK),
		       GRUB_SM712_CR_DDA_LOOKUP_REG3_START + 2 * idx);
  grub_sm712_cr_write (dda & SM712_DDA_REG2_DDA_MASK,
		       GRUB_SM712_CR_DDA_LOOKUP_REG2_START + 2 * idx);
  grub_sm712_cr_write (vcentering & SM712_DDA_REG2_VCENTER_MASK,
		       GRUB_SM712_CR_DDA_LOOKUP_REG1_START + idx);
}

#if !defined (TEST) && !defined(GENINIT)
/* Helper for grub_video_sm712_setup.  */
static int
find_card (grub_pci_device_t dev, grub_pci_id_t pciid, void *data)
{
  int *found = data;
  grub_pci_address_t addr;
  grub_uint32_t class;

  addr = grub_pci_make_address (dev, GRUB_PCI_REG_CLASS);
  class = grub_pci_read (addr);

  if (((class >> 16) & 0xffff) != GRUB_PCI_CLASS_SUBCLASS_VGA
      || pciid != GRUB_SM712_PCIID)
    return 0;
  
  *found = 1;

  addr = grub_pci_make_address (dev, GRUB_PCI_REG_ADDRESS_REG0);
  framebuffer.base = grub_pci_read (addr);
  framebuffer.dev = dev;

  return 1;
}
#endif

static grub_err_t
grub_video_sm712_setup (unsigned int width, unsigned int height,
			unsigned int mode_type, unsigned int mode_mask __attribute__ ((unused)))
{
  unsigned i;
#if !defined (TEST) && !defined(GENINIT)
  int depth;
  grub_err_t err;
  int found = 0;

  /* Decode depth from mode_type.  If it is zero, then autodetect.  */
  depth = (mode_type & GRUB_VIDEO_MODE_TYPE_DEPTH_MASK)
          >> GRUB_VIDEO_MODE_TYPE_DEPTH_POS;

  if ((width != 1024 && width != 0) || (height != 600 && height != 0)
      || (depth != 16 && depth != 0))
    return grub_error (GRUB_ERR_NOT_IMPLEMENTED_YET,
		       "Only 1024x600x16 is supported");

  grub_pci_iterate (find_card, &found);
  if (!found)
    return grub_error (GRUB_ERR_IO, "Couldn't find graphics card");
  /* Fill mode info details.  */
  framebuffer.mode_info.width = 1024;
  framebuffer.mode_info.height = 600;
  framebuffer.mode_info.mode_type = (GRUB_VIDEO_MODE_TYPE_RGB
				     | GRUB_VIDEO_MODE_TYPE_DOUBLE_BUFFERED
				     | GRUB_VIDEO_MODE_TYPE_UPDATING_SWAP);
  framebuffer.mode_info.bpp = 16;
  framebuffer.mode_info.bytes_per_pixel = 2;
  framebuffer.mode_info.pitch = 1024 * 2;
  framebuffer.mode_info.number_of_colors = 256;
  framebuffer.mode_info.red_mask_size = 5;
  framebuffer.mode_info.red_field_pos = 11;
  framebuffer.mode_info.green_mask_size = 6;
  framebuffer.mode_info.green_field_pos = 5;
  framebuffer.mode_info.blue_mask_size = 5;
  framebuffer.mode_info.blue_field_pos = 0;
  framebuffer.mode_info.reserved_mask_size = 0;
  framebuffer.mode_info.reserved_field_pos = 0;
  framebuffer.mode_info.blit_format
    = grub_video_get_blit_format (&framebuffer.mode_info);
#endif

#if !defined (TEST) && !defined(GENINIT)
  if (found && framebuffer.base == 0)
    {
      grub_pci_address_t addr;
      /* FIXME: choose address dynamically if needed.   */
      framebuffer.base = 0x04000000;

      addr = grub_pci_make_address (framebuffer.dev, GRUB_PCI_REG_ADDRESS_REG0);
      grub_pci_write (addr, framebuffer.base);

      /* Set latency.  */
      addr = grub_pci_make_address (framebuffer.dev, GRUB_PCI_REG_CACHELINE);
      grub_pci_write (addr, 0x8);

      /* Enable address spaces.  */
      addr = grub_pci_make_address (framebuffer.dev, GRUB_PCI_REG_COMMAND);
      grub_pci_write (addr, 0x7);
    }
#endif

  /* We can safely discard volatile attribute.  */
#if !defined (TEST) && !defined(GENINIT)
  framebuffer.ptr
    = grub_pci_device_map_range (framebuffer.dev,
				 framebuffer.base,
				 GRUB_SM712_TOTAL_MEMORY_SPACE);
  framebuffer.cached_ptr
    = grub_pci_device_map_range_cached (framebuffer.dev,
					framebuffer.base,
					GRUB_SM712_TOTAL_MEMORY_SPACE);
#endif
  framebuffer.mapped = 1;

  /* Initialise SM712.  */
#if !defined (TEST) && !defined(GENINIT)
  /* FIXME */
  grub_vga_sr_write (0x11, 0x18);
#endif

#if !defined (TEST) && !defined(GENINIT)
  /* Prevent garbage from appearing on the screen.  */
  grub_memset ((void *) framebuffer.cached_ptr, 0, 
	       framebuffer.mode_info.height * framebuffer.mode_info.pitch);
#endif

  /* FIXME */
  grub_sm712_sr_write (0, 0x21);
  grub_sm712_sr_write (0x7a, 0x62);
  grub_sm712_sr_write (0x16, 0x6a);
  grub_sm712_sr_write (0x2, 0x6b);
  grub_sm712_write_reg (0, GRUB_VGA_IO_PIXEL_MASK);
  grub_sm712_sr_write (GRUB_VGA_SR_RESET_ASYNC, GRUB_VGA_SR_RESET);
  grub_sm712_write_reg (GRUB_VGA_IO_MISC_NEGATIVE_VERT_POLARITY 
			| GRUB_VGA_IO_MISC_NEGATIVE_HORIZ_POLARITY
			| GRUB_VGA_IO_MISC_UPPER_64K
			| GRUB_VGA_IO_MISC_EXTERNAL_CLOCK_0
			| GRUB_VGA_IO_MISC_ENABLE_VRAM_ACCESS
			| GRUB_VGA_IO_MISC_COLOR, GRUB_VGA_IO_MISC_WRITE);
  grub_sm712_sr_write (GRUB_VGA_SR_RESET_ASYNC | GRUB_VGA_SR_RESET_SYNC,
		       GRUB_VGA_SR_RESET);
  grub_sm712_sr_write (GRUB_VGA_SR_CLOCKING_MODE_8_DOT_CLOCK,
		       GRUB_VGA_SR_CLOCKING_MODE);
  grub_sm712_sr_write (GRUB_VGA_ALL_PLANES, GRUB_VGA_SR_MAP_MASK_REGISTER);
  grub_sm712_sr_write (0, GRUB_VGA_SR_CHAR_MAP_SELECT);
  grub_sm712_sr_write (GRUB_VGA_SR_MEMORY_MODE_CHAIN4
		       | GRUB_VGA_SR_MEMORY_MODE_SEQUENTIAL_ADDRESSING
		       | GRUB_VGA_SR_MEMORY_MODE_EXTERNAL_VIDEO_MEMORY,
		       GRUB_VGA_SR_MEMORY_MODE);

  for (i = 0; i < ARRAY_SIZE (sm712_sr_seq1); i++)
    grub_sm712_sr_write (sm712_sr_seq1[i], 0x10 + i);

  for (i = 0; i < ARRAY_SIZE (sm712_sr_seq2); i++)
    grub_sm712_sr_write (sm712_sr_seq2[i], 0x30 + i);

  /* Undocumented.  */
  grub_sm712_sr_write (0x1a, 0x63);
  /* Undocumented.  */
  grub_sm712_sr_write (0x1a, 0x64);

  grub_sm712_sr_write (GRUB_SM712_SR_TV_CRT_SRAM | GRUB_SM712_SR_TV_ALT_CLOCK
		       | GRUB_SM712_SR_TV_CLOCK_CKIN_NTSC
		       | GRUB_SM712_SR_TV_HSYNC,
		       GRUB_SM712_SR_TV_CONTROL);

  grub_sm712_sr_write (GRUB_SM712_SR_RAM_LUT_NORMAL, GRUB_SM712_SR_RAM_LUT);

  /* Undocumented.  */
  grub_sm712_sr_write (0x00, 0x67);

  grub_sm712_sr_write (GRUB_SM712_SR_CLOCK_CONTROL1_VCLK_FROM_CCR
		       | GRUB_SM712_SR_CLOCK_CONTROL1_8DOT_CLOCK,
		       GRUB_SM712_SR_CLOCK_CONTROL1);
  grub_sm712_sr_write (GRUB_SM712_SR_CLOCK_CONTROL2_PROGRAM_VCLOCK,
		       GRUB_SM712_SR_CLOCK_CONTROL2);

  grub_sm712_sr_write (82, GRUB_SM712_SR_VCLK_NUM);
  grub_sm712_sr_write (137, GRUB_SM712_SR_VCLK_DENOM);

  grub_sm712_sr_write (9, GRUB_SM712_SR_VCLK2_NUM);
  grub_sm712_sr_write (2, GRUB_SM712_SR_VCLK2_DENOM);
  /* FIXME */
  grub_sm712_sr_write (0x04, 0x70);
  /* FIXME */
  grub_sm712_sr_write (0x45, 0x71);
  /* Undocumented */
  grub_sm712_sr_write (0x30, 0x72);
  /* Undocumented */
  grub_sm712_sr_write (0x30, 0x73);
  /* Undocumented */
  grub_sm712_sr_write (0x40, 0x74);
  /* Undocumented */
  grub_sm712_sr_write (0x20, 0x75);

  grub_sm712_sr_write (0xff, GRUB_SM712_SR_POPUP_ICON_LOW);
  grub_sm712_sr_write (GRUB_SM712_SR_POPUP_ICON_HIGH_MASK,
		       GRUB_SM712_SR_POPUP_ICON_HIGH);
  grub_sm712_sr_write (GRUB_SM712_SR_POPUP_ICON_CTRL_DISABLED,
		       GRUB_SM712_SR_POPUP_ICON_CTRL);
  /* Undocumented */
  grub_sm712_sr_write (0x0, 0x83);

  grub_sm712_sr_write (8, GRUB_SM712_SR_POPUP_ICON_COLOR1);
  grub_sm712_sr_write (0, GRUB_SM712_SR_POPUP_ICON_COLOR2);
  grub_sm712_sr_write (0x42, GRUB_SM712_SR_POPUP_ICON_COLOR3);

  /* Undocumented */
  grub_sm712_sr_write (0x3a, 0x87);

  /* Why theese coordinates?  */
  grub_sm712_sr_write (0x59, GRUB_SM712_SR_HW_CURSOR_UPPER_LEFT_X_LOW);
  grub_sm712_sr_write (0x02, GRUB_SM712_SR_HW_CURSOR_UPPER_LEFT_X_HIGH);
  grub_sm712_sr_write (0x44, GRUB_SM712_SR_HW_CURSOR_UPPER_LEFT_Y_LOW);
  grub_sm712_sr_write (0x02, GRUB_SM712_SR_HW_CURSOR_UPPER_LEFT_Y_HIGH);

  grub_sm712_sr_write (RGB332_BLACK, GRUB_SM712_SR_HW_CURSOR_FG_COLOR);
  grub_sm712_sr_write (RGB332_WHITE, GRUB_SM712_SR_HW_CURSOR_BG_COLOR);

  /* Undocumented */
  grub_sm712_sr_write (0x3a, 0x8e);
  grub_sm712_sr_write (0x3a, 0x8f);

  grub_sm712_sr_write (0, GRUB_SM712_SR_POPUP_ICON_X_LOW);
  grub_sm712_sr_write (0, GRUB_SM712_SR_POPUP_ICON_X_HIGH);
  grub_sm712_sr_write (0, GRUB_SM712_SR_POPUP_ICON_Y_LOW);
  grub_sm712_sr_write (0, GRUB_SM712_SR_POPUP_ICON_Y_HIGH);

  grub_sm712_sr_write (0, GRUB_SM712_SR_PANEL_HW_VIDEO_CONTROL);
  grub_sm712_sr_write (0x10, GRUB_SM712_SR_PANEL_HW_VIDEO_COLOR_KEY_LOW);
  grub_sm712_sr_write (0x08, GRUB_SM712_SR_PANEL_HW_VIDEO_COLOR_KEY_HIGH);
  grub_sm712_sr_write (0x00, GRUB_SM712_SR_PANEL_HW_VIDEO_COLOR_KEY_MASK_LOW);
  grub_sm712_sr_write (0x02, GRUB_SM712_SR_PANEL_HW_VIDEO_COLOR_KEY_MASK_HIGH);
  grub_sm712_sr_write (0xed, GRUB_SM712_SR_PANEL_HW_VIDEO_RED_CONSTANT);
  grub_sm712_sr_write (0xed, GRUB_SM712_SR_PANEL_HW_VIDEO_GREEN_CONSTANT);
  grub_sm712_sr_write (0xed, GRUB_SM712_SR_PANEL_HW_VIDEO_BLUE_CONSTANT);

  grub_sm712_sr_write (0x7b, GRUB_SM712_SR_PANEL_HW_VIDEO_TOP_BOUNDARY);
  grub_sm712_sr_write (0xfb, GRUB_SM712_SR_PANEL_HW_VIDEO_LEFT_BOUNDARY);
  grub_sm712_sr_write (0xff, GRUB_SM712_SR_PANEL_HW_VIDEO_BOTTOM_BOUNDARY);
  grub_sm712_sr_write (0xff, GRUB_SM712_SR_PANEL_HW_VIDEO_RIGHT_BOUNDARY);
  /* Doesn't match documentation?  */
  grub_sm712_sr_write (0x97, GRUB_SM712_SR_PANEL_HW_VIDEO_TOP_LEFT_OVERFLOW_BOUNDARY);
  grub_sm712_sr_write (0xef, GRUB_SM712_SR_PANEL_HW_VIDEO_BOTTOM_RIGHT_OVERFLOW_BOUNDARY);

  grub_sm712_sr_write (0xbf, GRUB_SM712_SR_PANEL_HW_VIDEO_VERTICAL_STRETCH_FACTOR);
  grub_sm712_sr_write (0xdf, GRUB_SM712_SR_PANEL_HW_VIDEO_HORIZONTAL_STRETCH_FACTOR);

  grub_sm712_gr_write (GRUB_VGA_NO_PLANES, GRUB_VGA_GR_SET_RESET_PLANE);
  grub_sm712_gr_write (GRUB_VGA_NO_PLANES, GRUB_VGA_GR_SET_RESET_PLANE_ENABLE);
  grub_sm712_gr_write (GRUB_VGA_NO_PLANES, GRUB_VGA_GR_COLOR_COMPARE);
  grub_sm712_gr_write (GRUB_VGA_GR_DATA_ROTATE_NOP, GRUB_VGA_GR_DATA_ROTATE);
  grub_sm712_gr_write (GRUB_VGA_NO_PLANES, GRUB_VGA_GR_READ_MAP_REGISTER);
  grub_sm712_gr_write (GRUB_VGA_GR_MODE_256_COLOR, GRUB_VGA_GR_MODE);
  grub_sm712_gr_write (GRUB_VGA_GR_GR6_MMAP_A0
		       | GRUB_VGA_GR_GR6_GRAPHICS_MODE, GRUB_VGA_GR_GR6);
  grub_sm712_gr_write (GRUB_VGA_ALL_PLANES, GRUB_VGA_GR_COLOR_COMPARE_DISABLE);
  grub_sm712_gr_write (0xff, GRUB_VGA_GR_BITMASK);

  /* Write palette mapping.  */
  for (i = 0; i < 16; i++)
    grub_sm712_write_arx (i, i);

  grub_sm712_write_arx (GRUB_VGA_ARX_MODE_ENABLE_256COLOR
			| GRUB_VGA_ARX_MODE_GRAPHICS, GRUB_VGA_ARX_MODE);
  grub_sm712_write_arx (0, GRUB_VGA_ARX_OVERSCAN);
  grub_sm712_write_arx (GRUB_VGA_ALL_PLANES, GRUB_VGA_ARX_COLOR_PLANE_ENABLE);
  grub_sm712_write_arx (0, GRUB_VGA_ARX_HORIZONTAL_PANNING);
  grub_sm712_write_arx (0, GRUB_VGA_ARX_COLOR_SELECT);

  /* FIXME: compute this generically.  */
  {
    struct grub_video_hw_config config =
      {
	.vertical_total = 806,
	.vertical_blank_start = 0x300,
	.vertical_blank_end = 0,
	.vertical_sync_start = 0x303,
	.vertical_sync_end = 0x9,
	.line_compare = 0x3ff,
	.vdisplay_end = 0x300,
	.pitch = 0x80,
	.horizontal_total = 164,
	.horizontal_end = 128,
	.horizontal_blank_start = 128,
	.horizontal_blank_end = 0,
	.horizontal_sync_pulse_start = 133,
	.horizontal_sync_pulse_end = 22
      };
    grub_vga_set_geometry (&config, grub_sm712_cr_write);
    config.horizontal_sync_pulse_start = 134;
    config.horizontal_sync_pulse_end = 21;
    config.vertical_sync_start = 0x301;
    config.vertical_sync_end = 0x0;
    config.line_compare = 0x0ff;
    config.vdisplay_end = 0x258;
    config.pitch = 0x7f;
    grub_vga_set_geometry (&config, grub_sm712_cr_shadow_write);
  }

  grub_sm712_cr_write (GRUB_VGA_CR_BYTE_PANNING_NORMAL,
		       GRUB_VGA_CR_BYTE_PANNING);
  grub_sm712_cr_write (0, GRUB_VGA_CR_CURSOR_START);
  grub_sm712_cr_write (0, GRUB_VGA_CR_CURSOR_END);
  grub_sm712_cr_write (0, GRUB_VGA_CR_START_ADDR_HIGH_REGISTER);
  grub_sm712_cr_write (0, GRUB_VGA_CR_START_ADDR_LOW_REGISTER);
  grub_sm712_cr_write (0, GRUB_VGA_CR_CURSOR_ADDR_HIGH);
  grub_sm712_cr_write (0, GRUB_VGA_CR_CURSOR_ADDR_LOW);
  grub_sm712_cr_write (GRUB_VGA_CR_UNDERLINE_LOCATION_DWORD_MODE,
		       GRUB_VGA_CR_UNDERLINE_LOCATION);
  grub_sm712_cr_write (GRUB_VGA_CR_MODE_ADDRESS_WRAP
		       | GRUB_VGA_CR_MODE_BYTE_MODE
		       | GRUB_VGA_CR_MODE_TIMING_ENABLE
		       | GRUB_VGA_CR_MODE_NO_CGA
		       | GRUB_VGA_CR_MODE_NO_HERCULES,
		       GRUB_VGA_CR_MODE);

  grub_sm712_cr_write (0, GRUB_SM712_CR_OVERFLOW_INTERLACE);
  grub_sm712_cr_write (0, GRUB_SM712_CR_INTERLACE_RETRACE);
  grub_sm712_cr_write (0, GRUB_SM712_CR_TV_VDISPLAY_START);
  grub_sm712_cr_write (0, GRUB_SM712_CR_TV_VDISPLAY_END_HIGH);
  grub_sm712_cr_write (0, GRUB_SM712_CR_TV_VDISPLAY_END_LOW);
  grub_sm712_cr_write (0x80, GRUB_SM712_CR_DDA_CONTROL_LOW);
  grub_sm712_cr_write (0x02, GRUB_SM712_CR_DDA_CONTROL_HIGH);

  /* Undocumented */
  grub_sm712_cr_write (0x20, 0x37);

  grub_sm712_cr_write (0, GRUB_SM712_CR_TV_EQUALIZER);
  grub_sm712_cr_write (0, GRUB_SM712_CR_TV_SERRATION);
  grub_sm712_cr_write (0, GRUB_SM712_CR_HSYNC_CTRL);

  /* Undocumented */
  grub_sm712_cr_write (0x40, 0x3b);

  grub_sm712_cr_write (GRUB_SM712_CR_DEBUG_NONE, GRUB_SM712_CR_DEBUG);

  /* Undocumented */
  grub_sm712_cr_write (0xff, 0x3d);
  grub_sm712_cr_write (0x46, 0x3e);
  grub_sm712_cr_write (0x91, 0x3f);

  for (i = 0; i < ARRAY_SIZE (dda_lookups); i++)
    grub_sm712_write_dda_lookup (i, dda_lookups[i].compare, dda_lookups[i].dda,
				 dda_lookups[i].vcentering);
  
  /* Undocumented  */
  grub_sm712_cr_write (0, 0x9c);
  grub_sm712_cr_write (0, 0x9d);
  grub_sm712_cr_write (0, 0x9e);
  grub_sm712_cr_write (0, 0x9f);

  grub_sm712_cr_write (0, GRUB_SM712_CR_VCENTERING_OFFSET);
  grub_sm712_cr_write (0, GRUB_SM712_CR_HCENTERING_OFFSET);

  grub_sm712_write_reg (GRUB_VGA_IO_MISC_NEGATIVE_HORIZ_POLARITY
			| GRUB_VGA_IO_MISC_UPPER_64K
			| GRUB_VGA_IO_MISC_28MHZ
			| GRUB_VGA_IO_MISC_ENABLE_VRAM_ACCESS
			| GRUB_VGA_IO_MISC_COLOR,
			GRUB_VGA_IO_MISC_WRITE);

#if !defined (TEST) && !defined(GENINIT)
  /* Undocumented? */
  *(volatile grub_uint32_t *) ((char *) framebuffer.ptr + 0x40c00c) = 0;
  *(volatile grub_uint32_t *) ((char *) framebuffer.ptr + 0x40c040) = 0;
  *(volatile grub_uint32_t *) ((char *) framebuffer.ptr + 0x40c000) = 0x20000;
  *(volatile grub_uint32_t *) ((char *) framebuffer.ptr + 0x40c010) = 0x1020100;
#endif

  (void) grub_sm712_sr_read (0x16);

#if !defined (TEST) && !defined(GENINIT)
  err = grub_video_fb_setup (mode_type, mode_mask,
			     &framebuffer.mode_info,
			     framebuffer.cached_ptr, NULL, NULL);
  if (err)
    return err;

  /* Copy default palette to initialize emulated palette.  */
  err = grub_video_fb_set_palette (0, GRUB_VIDEO_FBSTD_NUMCOLORS,
				   grub_video_fbstd_colors);
  return err;
#else
  return 0;
#endif
}

#if !defined (TEST) && !defined(GENINIT)

static grub_err_t
grub_video_sm712_swap_buffers (void)
{
  grub_size_t s;
  s = (framebuffer.mode_info.height
       * framebuffer.mode_info.pitch
       * framebuffer.mode_info.bytes_per_pixel);
  grub_video_fb_swap_buffers ();
  grub_arch_sync_dma_caches (framebuffer.cached_ptr, s);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_video_sm712_get_info_and_fini (struct grub_video_mode_info *mode_info,
				    void **framebuf)
{
  grub_memcpy (mode_info, &(framebuffer.mode_info), sizeof (*mode_info));
  *framebuf = (char *) framebuffer.ptr;

  grub_video_fb_fini ();

  return GRUB_ERR_NONE;
}

static struct grub_video_adapter grub_video_sm712_adapter =
  {
    .name = "SM712 Video Driver",
    .id = GRUB_VIDEO_DRIVER_SM712,

    .prio = GRUB_VIDEO_ADAPTER_PRIO_NATIVE,

    .init = grub_video_sm712_video_init,
    .fini = grub_video_sm712_video_fini,
    .setup = grub_video_sm712_setup,
    .get_info = grub_video_fb_get_info,
    .get_info_and_fini = grub_video_sm712_get_info_and_fini,
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
    .swap_buffers = grub_video_sm712_swap_buffers,
    .create_render_target = grub_video_fb_create_render_target,
    .delete_render_target = grub_video_fb_delete_render_target,
    .set_active_render_target = grub_video_fb_set_active_render_target,
    .get_active_render_target = grub_video_fb_get_active_render_target,

    .next = 0
  };

GRUB_MOD_INIT(video_sm712)
{
  grub_video_register (&grub_video_sm712_adapter);
}

GRUB_MOD_FINI(video_sm712)
{
  grub_video_unregister (&grub_video_sm712_adapter);
}
#else
int
main ()
{
  grub_video_sm712_setup (1024, 600, 0, 0);
}
#endif
