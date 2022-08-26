/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2007, 2008, 2010  Free Software Foundation, Inc.
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

#include <grub/dl.h>
#include <grub/cpu/io.h>
#include <grub/types.h>
#include <grub/vga.h>
#include <grub/term.h>

#if defined (GRUB_MACHINE_COREBOOT)
#include <grub/machine/console.h>
#endif

/* MODESET is used for testing to force monochrome or colour mode.
   You shouldn't use mda_text on vga.
 */
#ifdef MODESET
#include <grub/machine/int.h>
#endif

#if defined (GRUB_MACHINE_COREBOOT) || defined (GRUB_MACHINE_QEMU) || defined (GRUB_MACHINE_MIPS_QEMU_MIPS) || defined (GRUB_MACHINE_MULTIBOOT)
#include <grub/machine/console.h>
#endif

GRUB_MOD_LICENSE ("GPLv3+");

#define COLS	80
#define ROWS	25

static struct grub_term_coordinate grub_curr_pos;

#ifdef __mips__
#define VGA_TEXT_SCREEN		((grub_uint16_t *) 0xb00b8000)
#define cr_read grub_vga_cr_read
#define cr_write grub_vga_cr_write
#elif defined (MODE_MDA)
#define VGA_TEXT_SCREEN		((grub_uint16_t *) 0xb0000)
#define cr_read grub_vga_cr_bw_read
#define cr_write grub_vga_cr_bw_write
#else
#define VGA_TEXT_SCREEN		((grub_uint16_t *) 0xb8000)
#define cr_read grub_vga_cr_read
#define cr_write grub_vga_cr_write
#endif

static grub_uint8_t cur_color = 0x7;

static void
screen_write_char (int x, int y, short c)
{
  VGA_TEXT_SCREEN[y * COLS + x] = grub_cpu_to_le16 (c);
}

static short
screen_read_char (int x, int y)
{
  return grub_le_to_cpu16 (VGA_TEXT_SCREEN[y * COLS + x]);
}

static void
update_cursor (void)
{
  unsigned int pos = grub_curr_pos.y * COLS + grub_curr_pos.x;
  cr_write (pos >> 8, GRUB_VGA_CR_CURSOR_ADDR_HIGH);
  cr_write (pos & 0xFF, GRUB_VGA_CR_CURSOR_ADDR_LOW);
}

static void
inc_y (void)
{
  grub_curr_pos.x = 0;
  if (grub_curr_pos.y < ROWS - 1)
    grub_curr_pos.y++;
  else
    {
      int x, y;
      for (y = 0; y < ROWS - 1; y++)
        for (x = 0; x < COLS; x++)
          screen_write_char (x, y, screen_read_char (x, y + 1));
      for (x = 0; x < COLS; x++)
	screen_write_char (x, ROWS - 1, ' ' | (cur_color << 8));
    }
}

static void
inc_x (void)
{
  if (grub_curr_pos.x >= COLS - 1)
    inc_y ();
  else
    grub_curr_pos.x++;
}

static void
grub_vga_text_putchar (struct grub_term_output *term __attribute__ ((unused)),
		       const struct grub_unicode_glyph *c)
{
  switch (c->base)
    {
      case '\b':
	if (grub_curr_pos.x != 0)
	  screen_write_char (grub_curr_pos.x--, grub_curr_pos.y, ' ');
	break;
      case '\n':
	inc_y ();
	break;
      case '\r':
	grub_curr_pos.x = 0;
	break;
      default:
	screen_write_char (grub_curr_pos.x, grub_curr_pos.y,
			   c->base | (cur_color << 8));
	inc_x ();
    }

  update_cursor ();
}

static struct grub_term_coordinate
grub_vga_text_getxy (struct grub_term_output *term __attribute__ ((unused)))
{
  return grub_curr_pos;
}

static void
grub_vga_text_gotoxy (struct grub_term_output *term __attribute__ ((unused)),
		      struct grub_term_coordinate pos)
{
  grub_curr_pos = pos;
  update_cursor ();
}

static void
grub_vga_text_cls (struct grub_term_output *term)
{
  int i;
  for (i = 0; i < ROWS * COLS; i++)
    VGA_TEXT_SCREEN[i] = grub_cpu_to_le16 (' ' | (cur_color << 8));
  grub_vga_text_gotoxy (term, (struct grub_term_coordinate) { 0, 0 });
}

static void
grub_vga_text_setcursor (struct grub_term_output *term __attribute__ ((unused)),
			 int on)
{
  grub_uint8_t old;
  old = cr_read (GRUB_VGA_CR_CURSOR_START);
  if (on)
    cr_write (old & ~GRUB_VGA_CR_CURSOR_START_DISABLE,
	      GRUB_VGA_CR_CURSOR_START);
  else
    cr_write (old | GRUB_VGA_CR_CURSOR_START_DISABLE,
	      GRUB_VGA_CR_CURSOR_START);
}

static grub_err_t
grub_vga_text_init_real (struct grub_term_output *term)
{
#ifdef MODESET
  struct grub_bios_int_registers regs;
  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;

#ifdef MODE_MDA
  regs.eax = 7;
#else
  regs.eax = 3;
#endif
  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;
  grub_bios_interrupt (0x10, &regs);
#endif
  grub_vga_text_cls (term);
  return 0;
}

static grub_err_t
grub_vga_text_fini_real (struct grub_term_output *term)
{
#ifdef MODESET
  struct grub_bios_int_registers regs;
  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;

  regs.eax = 3;
  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;
  grub_bios_interrupt (0x10, &regs);
#endif
  grub_vga_text_cls (term);
  return 0;
}

static struct grub_term_coordinate
grub_vga_text_getwh (struct grub_term_output *term __attribute__ ((unused)))
{
  return (struct grub_term_coordinate) { 80, 25 };
}

#ifndef MODE_MDA

static void
grub_vga_text_setcolorstate (struct grub_term_output *term __attribute__ ((unused)),
			     grub_term_color_state state)
{
  switch (state) {
    case GRUB_TERM_COLOR_STANDARD:
      cur_color = GRUB_TERM_DEFAULT_STANDARD_COLOR & 0x7f;
      break;
    case GRUB_TERM_COLOR_NORMAL:
      cur_color = grub_term_normal_color & 0x7f;
      break;
    case GRUB_TERM_COLOR_HIGHLIGHT:
      cur_color = grub_term_highlight_color & 0x7f;
      break;
    default:
      break;
  }
}

#else
static void
grub_vga_text_setcolorstate (struct grub_term_output *term __attribute__ ((unused)),
			     grub_term_color_state state)
{
  switch (state) {
    case GRUB_TERM_COLOR_STANDARD:
      cur_color = 0x07;
      break;
    case GRUB_TERM_COLOR_NORMAL:
      cur_color = 0x07;
      break;
    case GRUB_TERM_COLOR_HIGHLIGHT:
      cur_color = 0x70;
      break;
    default:
      break;
  }
}
#endif

static struct grub_term_output grub_vga_text_term =
  {
#ifdef MODE_MDA
    .name = "mda_text",
#else
    .name = "vga_text",
#endif
    .init = grub_vga_text_init_real,
    .fini = grub_vga_text_fini_real,
    .putchar = grub_vga_text_putchar,
    .getwh = grub_vga_text_getwh,
    .getxy = grub_vga_text_getxy,
    .gotoxy = grub_vga_text_gotoxy,
    .cls = grub_vga_text_cls,
    .setcolorstate = grub_vga_text_setcolorstate,
    .setcursor = grub_vga_text_setcursor,
    .flags = GRUB_TERM_CODE_TYPE_CP437,
    .progress_update_divisor = GRUB_PROGRESS_FAST
  };

#ifndef MODE_MDA

GRUB_MOD_INIT(vga_text)
{
#ifdef GRUB_MACHINE_COREBOOT
  if (!grub_video_coreboot_fbtable)
#endif
    grub_term_register_output ("vga_text", &grub_vga_text_term);
}

GRUB_MOD_FINI(vga_text)
{
  grub_term_unregister_output (&grub_vga_text_term);
}

#endif
