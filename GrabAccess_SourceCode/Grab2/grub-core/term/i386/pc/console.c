/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2003,2005,2007,2008,2009  Free Software Foundation, Inc.
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

#include <grub/machine/memory.h>
#include <grub/machine/kernel.h>
#include <grub/machine/console.h>
#include <grub/term.h>
#include <grub/types.h>
#include <grub/machine/int.h>

static grub_uint8_t grub_console_cur_color = 0x7;

static void
int10_9 (grub_uint8_t ch, grub_uint16_t n)
{
  struct grub_bios_int_registers regs;

  regs.eax = ch | 0x0900;
  regs.ebx = grub_console_cur_color & 0xff;
  regs.ecx = n;
  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;  
  grub_bios_interrupt (0x10, &regs);
}

/*
 * BIOS call "INT 10H Function 03h" to get cursor position
 *	Call with	%ah = 0x03
 *			%bh = page
 *      Returns         %ch = starting scan line
 *                      %cl = ending scan line
 *                      %dh = row (0 is top)
 *                      %dl = column (0 is left)
 */


static struct grub_term_coordinate
grub_console_getxy (struct grub_term_output *term __attribute__ ((unused)))
{
  struct grub_bios_int_registers regs;

  regs.eax = 0x0300;
  regs.ebx = 0;
  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;  
  grub_bios_interrupt (0x10, &regs);

  return (struct grub_term_coordinate) {
    (regs.edx & 0xff), ((regs.edx & 0xff00) >> 8) };
}

/*
 * BIOS call "INT 10H Function 02h" to set cursor position
 *	Call with	%ah = 0x02
 *			%bh = page
 *                      %dh = row (0 is top)
 *                      %dl = column (0 is left)
 */
static void
grub_console_gotoxy (struct grub_term_output *term __attribute__ ((unused)),
		     struct grub_term_coordinate pos)
{
  struct grub_bios_int_registers regs;

  /* set page to 0 */
  regs.ebx = 0;
  regs.eax = 0x0200;
  regs.edx = (pos.y << 8) | pos.x;
  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;  
  grub_bios_interrupt (0x10, &regs);
}

/*
 *
 * Put the character C on the console. Because GRUB wants to write a
 * character with an attribute, this implementation is a bit tricky.
 * If C is a control character (CR, LF, BEL, BS), use INT 10, AH = 0Eh
 * (TELETYPE OUTPUT). Otherwise, use INT 10, AH = 9 to write character
 * with attributes and advance cursor. If we are on the last column,
 * let BIOS to wrap line correctly.
 */
static void
grub_console_putchar_real (grub_uint8_t c)
{
  struct grub_bios_int_registers regs;
  struct grub_term_coordinate pos;

  if (c == 7 || c == 8 || c == 0xa || c == 0xd)
    {
      regs.eax = c | 0x0e00;
      regs.ebx = 0x0001;
      regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;  
      grub_bios_interrupt (0x10, &regs);
      return;
    }

  /* get the current position */
  pos = grub_console_getxy (NULL);
  
  /* write the character with the attribute */
  int10_9 (c, 1);

  /* check the column with the width */
  if (pos.x >= 79)
    {
      grub_console_putchar_real (0x0d);
      grub_console_putchar_real (0x0a);
    }
  else
    grub_console_gotoxy (NULL, (struct grub_term_coordinate) { pos.x + 1,
	  pos.y });
}

static void
grub_console_putchar (struct grub_term_output *term __attribute__ ((unused)),
		      const struct grub_unicode_glyph *c)
{
  grub_console_putchar_real (c->base);
}

/*
 * BIOS call "INT 10H Function 09h" to write character and attribute
 *	Call with	%ah = 0x09
 *                      %al = (character)
 *                      %bh = (page number)
 *                      %bl = (attribute)
 *                      %cx = (number of times)
 */
static void
grub_console_cls (struct grub_term_output *term)
{
  /* move the cursor to the beginning */
  grub_console_gotoxy (term, (struct grub_term_coordinate) { 0, 0 });

  /* write spaces to the entire screen */
  int10_9 (' ', 80 * 25);

  /* move back the cursor */
  grub_console_gotoxy (term, (struct grub_term_coordinate) { 0, 0 });
}

/*
 * void grub_console_setcursor (int on)
 * BIOS call "INT 10H Function 01h" to set cursor type
 *      Call with       %ah = 0x01
 *                      %ch = cursor starting scanline
 *                      %cl = cursor ending scanline
 */
static void 
grub_console_setcursor (struct grub_term_output *term __attribute__ ((unused)),
			int on)
{
  static grub_uint16_t console_cursor_shape = 0;
  struct grub_bios_int_registers regs;

  /* check if the standard cursor shape has already been saved */
  if (!console_cursor_shape)
    {
      regs.eax = 0x0300;
      regs.ebx = 0;
      regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;  
      grub_bios_interrupt (0x10, &regs);
      console_cursor_shape = regs.ecx;
      if ((console_cursor_shape >> 8) >= (console_cursor_shape & 0xff))
	console_cursor_shape = 0x0d0e;
    }
  /* set %cx to the designated cursor shape */
  regs.ecx = on ? console_cursor_shape : 0x2000;
  regs.eax = 0x0100;
  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;  
  grub_bios_interrupt (0x10, &regs);
}

/*
 *	if there is a character pending, return it; otherwise return -1
 * BIOS call "INT 16H Function 01H" to check whether a character is pending
 *	Call with	%ah = 0x1
 *	Return:
 *		If key waiting to be input:
 *			%ah = keyboard scan code
 *			%al = ASCII character
 *			Zero flag = clear
 *		else
 *			Zero flag = set
 * BIOS call "INT 16H Function 00H" to read character from keyboard
 *	Call with	%ah = 0x0
 *	Return:		%ah = keyboard scan code
 *			%al = ASCII character
 */

static int
grub_console_getkey (struct grub_term_input *term __attribute__ ((unused)))
{
  const grub_uint16_t bypass_table[] = {
    0x0100 | GRUB_TERM_ESC, 0x0f00 | GRUB_TERM_TAB, 0x0e00 | GRUB_TERM_BACKSPACE, 0x1c00 | '\r', 0x1c00 | '\n'
  };
  struct grub_bios_int_registers regs;
  unsigned i;

  /*
   * Due to a bug in apple's bootcamp implementation, INT 16/AH = 0 would
   * cause the machine to hang at the second keystroke. However, we can
   * work around this problem by ensuring the presence of keystroke with
   * INT 16/AH = 1 before calling INT 16/AH = 0.
   */

  regs.eax = 0x0100;
  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;
  grub_bios_interrupt (0x16, &regs);
  if (regs.flags & GRUB_CPU_INT_FLAGS_ZERO)
    return GRUB_TERM_NO_KEY;

  regs.eax = 0x0000;
  regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;
  grub_bios_interrupt (0x16, &regs);
  if (!(regs.eax & 0xff))
    return ((regs.eax >> 8) & 0xff) | GRUB_TERM_EXTENDED;

  if ((regs.eax & 0xff) >= ' ')
    return regs.eax & 0xff;

  for (i = 0; i < ARRAY_SIZE (bypass_table); i++)
    if (bypass_table[i] == (regs.eax & 0xffff))
      return regs.eax & 0xff;

  return (regs.eax & 0xff) + (('a' - 1) | GRUB_TERM_CTRL);
}

static const struct grub_machine_bios_data_area *bios_data_area =
  (struct grub_machine_bios_data_area *) GRUB_MEMORY_MACHINE_BIOS_DATA_AREA_ADDR;

static int
grub_console_getkeystatus (struct grub_term_input *term __attribute__ ((unused)))
{
  /* conveniently GRUB keystatus is modelled after BIOS one.  */
  return bios_data_area->keyboard_flag_lower & ~0x80;
}

static struct grub_term_coordinate
grub_console_getwh (struct grub_term_output *term __attribute__ ((unused)))
{
  return (struct grub_term_coordinate) { 80, 25 };
}

static void
grub_console_setcolorstate (struct grub_term_output *term
			    __attribute__ ((unused)),
			    grub_term_color_state state)
{
  switch (state) {
    case GRUB_TERM_COLOR_STANDARD:
      grub_console_cur_color = GRUB_TERM_DEFAULT_STANDARD_COLOR & 0x7f;
      break;
    case GRUB_TERM_COLOR_NORMAL:
      grub_console_cur_color = grub_term_normal_color & 0x7f;
      break;
    case GRUB_TERM_COLOR_HIGHLIGHT:
      grub_console_cur_color = grub_term_highlight_color & 0x7f;
      break;
    default:
      break;
  }
}

static struct grub_term_input grub_console_term_input =
  {
    .name = "console",
    .getkey = grub_console_getkey,
    .getkeystatus = grub_console_getkeystatus
  };

static struct grub_term_output grub_console_term_output =
  {
    .name = "console",
    .putchar = grub_console_putchar,
    .getwh = grub_console_getwh,
    .getxy = grub_console_getxy,
    .gotoxy = grub_console_gotoxy,
    .cls = grub_console_cls,
    .setcolorstate = grub_console_setcolorstate,
    .setcursor = grub_console_setcursor,
    .flags = GRUB_TERM_CODE_TYPE_CP437,
    .progress_update_divisor = GRUB_PROGRESS_FAST
  };

void
#ifdef GRUB_MACHINE_MULTIBOOT
grub_console_pcbios_init (void)
#else
grub_console_init (void)
#endif
{
#ifdef GRUB_MACHINE_MULTIBOOT
  if (grub_mb_check_bios_int (0x10) && grub_mb_check_bios_int (0x16))
  {
#endif
    grub_term_register_output ("console", &grub_console_term_output);
    grub_term_register_input ("console", &grub_console_term_input);
#ifdef GRUB_MACHINE_MULTIBOOT
  }
#endif
}

void
#ifdef GRUB_MACHINE_MULTIBOOT
grub_console_pcbios_fini (void)
#else
grub_console_fini (void)
#endif
{
#ifdef GRUB_MACHINE_MULTIBOOT
  if (grub_mb_check_bios_int (0x10) && grub_mb_check_bios_int (0x16))
  {
#endif
    grub_term_unregister_input (&grub_console_term_input);
    grub_term_unregister_output (&grub_console_term_output);
#ifdef GRUB_MACHINE_MULTIBOOT
  }
#endif
}
