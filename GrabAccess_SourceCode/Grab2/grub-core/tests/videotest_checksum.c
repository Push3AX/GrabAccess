/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013 Free Software Foundation, Inc.
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

/* All tests need to include test.h for GRUB testing framework.  */
#include <grub/test.h>
#include <grub/dl.h>
#include <grub/video.h>
#include <grub/video_fb.h>
#include <grub/command.h>
#include <grub/font.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define FONT_NAME "Unknown Regular 16"

/* Functional test main method.  */
static void
videotest_checksum (void)
{
  unsigned i;
  grub_font_t font;

  font = grub_font_get (FONT_NAME);
  if (font && grub_strcmp (font->name, FONT_NAME) != 0)
    font = 0;
  if (!font)
    font = grub_font_load ("unicode");

  if (!font)
    {
      grub_test_assert (0, "unicode font not found: %s", grub_errmsg);
      return;
    }

  for (i = 0; i < ARRAY_SIZE (grub_test_video_modes); i++)
    {
      grub_err_t err;
#if defined (GRUB_MACHINE_MIPS_QEMU_MIPS) || defined (GRUB_MACHINE_IEEE1275)
      if (grub_test_video_modes[i].width > 1024)
	continue;
#endif
      err = grub_video_capture_start (&grub_test_video_modes[i],
				      grub_video_fbstd_colors,
				      grub_test_video_modes[i].number_of_colors);
      if (err)
	{
	  grub_test_assert (0, "can't start capture: %s", grub_errmsg);
	  grub_print_error ();
	  continue;
	}
      grub_terminal_input_fake_sequence ((int []) { '\n' }, 1);

      grub_video_checksum ("videotest");

      char *args[] = { 0 };
      grub_command_execute ("videotest", 0, args);

      grub_terminal_input_fake_sequence_end ();
      grub_video_checksum_end ();
      grub_video_capture_end ();
    }
}

/* Register example_test method as a functional test.  */
GRUB_FUNCTIONAL_TEST (videotest_checksum, videotest_checksum);
