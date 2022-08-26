/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2007,2008,2009  Free Software Foundation, Inc.
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

#ifndef GRUB_PS2_MOUSE_HEADER
#define GRUB_PS2_MOUSE_HEADER	1

/* Used for sending commands to the controller.  */


#define KEYBOARD_COMMAND_READ		0x20
#define KEYBOARD_COMMAND_WRITE		0x60
#define KEYBOARD_COMMAND_REBOOT		0xfe

#define KEYBOARD_AT_TRANSLATE		0x40

#define GRUB_AT_ACK                     0xfa
#define GRUB_AT_NACK                    0xfe
#define GRUB_AT_TRIES                   5

#define KEYBOARD_ISMAKE(x)	!((x) & 0x80)
#define KEYBOARD_SCANCODE(x)	((x) & 0x7f)

#define BSPLIT(code)  \
((int)code >> 7) & 1, \
((int)code >> 6) & 1, \
((int)code >> 5) & 1, \
((int)code >> 4) & 1, \
((int)code >> 3) & 1, \
((int)code >> 2) & 1, \
((int)code >> 1) & 1, \
((int)code >> 0) & 1

#define MMODE_DEFAULT 0
#define MMODE_MOUSE 1
#define MMODE_TOUCH 2

#define PS2_HAS_DATA(x)	((x) & 0x01)
#define PS2_COMMAND_ISREADY(x)	!((x) & 0x02)
#define PS2_ISMOUSE_EVENT(x)	((x) & 0x20)
#define PS2_ISKEYBOARD_EVENT(x)	!((x) & 0x20)

#define MOUSE_BUTTON_LEFT 1
#define MOUSE_BUTTON_RIGHT 2
#define MOUSE_BUTTON_MIDDLE 4

extern void grub_ps2mouse_init (void);
extern void grub_ps2mouse_fini (void);

#endif
