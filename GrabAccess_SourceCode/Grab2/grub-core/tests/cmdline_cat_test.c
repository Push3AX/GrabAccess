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
#include <grub/procfs.h>
#include <grub/env.h>
#include <grub/normal.h>
#include <grub/time.h>

GRUB_MOD_LICENSE ("GPLv3+");


static const char testfile[] =
  /* Chinese & UTF-8 test from Carbon Jiao. */
  "从硬盘的第一主分区启动\n"
  "The quick brown fox jumped over the lazy dog.\n"
  /* Characters used:
     Code point  Description                    UTF-8 encoding
     ----------- ------------------------------ --------------
     U+263A      unfilled smiley face           E2 98 BA
     U+00A1      inverted exclamation point     C2 A1
     U+00A3      British pound currency symbol  C2 A3
     U+03C4      Greek tau                      CF 84
     U+00E4      lowercase letter a with umlaut C3 A4
     U+2124      set 'Z' symbol (integers)      E2 84 A4
     U+2286      subset symbol                  E2 8A 86
     U+211D      set 'R' symbol (real numbers)  E2 84 9D  */
  "Unicode test: happy\xE2\x98\xBA \xC2\xA3 5.00"
  " \xC2\xA1\xCF\x84\xC3\xA4u! "
  " \xE2\x84\xA4\xE2\x8A\x86\xE2\x84\x9D\n"
  /* Test handling of bad (non-UTF8) sequences*/
  "\x99Hello\xc2Hello\xc1\x81Hello\n";
;

static char *
get_test_txt (grub_size_t *sz)
{
  *sz = grub_strlen (testfile);
  return grub_strdup (testfile);
}

struct grub_procfs_entry test_txt = 
{
  .name = "test.txt",
  .get_contents = get_test_txt
};

#define FONT_NAME "Unknown Regular 16"

/* Functional test main method.  */
static void
cmdline_cat_test (void)
{
  unsigned i;
  grub_font_t font;

  grub_dl_load ("gfxterm");
  grub_errno = GRUB_ERR_NONE;

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

  grub_procfs_register ("test.txt", &test_txt);
  
  for (i = 0; i < GRUB_TEST_VIDEO_SMALL_N_MODES; i++)
    {
      grub_video_capture_start (&grub_test_video_modes[i],
				grub_video_fbstd_colors,
				grub_test_video_modes[i].number_of_colors);
      grub_terminal_input_fake_sequence ((int []) 
					 {  'c', 'a', 't', ' ', 
					     '(', 'p', 'r', 'o', 'c', ')',
					     '/', 't', 'e', 's', 't', '.',
					     't', 'x', 't', '\n',
					     GRUB_TERM_NO_KEY,
					     GRUB_TERM_NO_KEY, GRUB_TERM_ESC},
					 23);

      grub_video_checksum ("cmdline_cat");

      if (!grub_test_use_gfxterm ())
	grub_cmdline_run (1, 0);

      grub_test_use_gfxterm_end ();

      grub_terminal_input_fake_sequence_end ();
      grub_video_checksum_end ();
      grub_video_capture_end ();
    }

  grub_procfs_unregister (&test_txt);
}

/* Register example_test method as a functional test.  */
GRUB_FUNCTIONAL_TEST (cmdline_cat_test, cmdline_cat_test);
