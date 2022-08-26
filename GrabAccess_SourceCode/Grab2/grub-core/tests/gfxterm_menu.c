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
  "menuentry \"test\" {\n"
  "\ttrue\n"
  "}\n"
  "menuentry \"s̛ ơ t o̒ s̒ u o̕̚ 8.04 m̂ñåh̊z̆x̣ a̡ b̢g̢ u᷎ô᷎ ô᷎ O̷ a̖̣ ȃ̐\" --class ubuntu --class linux --class os {\n"
  "\ttrue\n"
  "}\n"
  "menuentry \" הַרמלל(טוֹבָ) לֶךְ\" --class opensuse --class linux --class os {\n"
  "\ttrue\n"
  "}\n"
  "menuentry \"الرملل جِداً لِكَ\" --class gentoo --class linux --class os {\n"
  "\ttrue\n"
  "}\n"
  "menuentry \"ὑπόγυͅον\" --class kubuntu --class linux --class os {\n"
  "\ttrue\n"
  "}\n"
  "menuentry \"سَّ نِّ نَّ نٌّ نّْ\" --class linuxmint --class linux --class os {\n"
  "\ttrue\n"
  "}\n"
  /* Chinese & UTF-8 test from Carbon Jiao. */
  "menuentry \"从硬盘的第一主分区启动\" --class \"windows xp\" --class windows --class os {\n"
  "\ttrue\n"
  "}\n"
  "timeout=3\n";

static char *
get_test_cfg (grub_size_t *sz)
{
  *sz = grub_strlen (testfile);
  return grub_strdup (testfile);
}

struct grub_procfs_entry test_cfg = 
{
  .name = "test.cfg",
  .get_contents = get_test_cfg
};

struct
{
  const char *name;
  const char *var;
  const char *val;  
} tests[] =
  {
    { "gfxterm_menu", NULL, NULL },
    { "gfxmenu", "theme", "starfield/theme.txt" },
    { "gfxterm_ar", "lang", "en@arabic" },
    { "gfxterm_cyr", "lang", "en@cyrillic" },
    { "gfxterm_heb", "lang", "en@hebrew" },
    { "gfxterm_gre", "lang", "en@greek" },
    { "gfxterm_ru", "lang", "ru" },
    { "gfxterm_fr", "lang", "fr" },
    { "gfxterm_quot", "lang", "en@quot" },
    { "gfxterm_piglatin", "lang", "en@piglatin" },
    { "gfxterm_ch", "lang", "de_CH" },
    { "gfxterm_red", "menu_color_normal", "red/blue" },
    { "gfxterm_high", "menu_color_highlight", "blue/red" },
  };

#define FONT_NAME "Unknown Regular 16"

/* Functional test main method.  */
static void
gfxterm_menu (void)
{
  unsigned i, j;
  grub_font_t font;

  grub_dl_load ("png");
  grub_dl_load ("gettext");
  grub_dl_load ("gfxterm");

  grub_errno = GRUB_ERR_NONE;

  grub_dl_load ("gfxmenu");

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

  grub_procfs_register ("test.cfg", &test_cfg);
  
  for (j = 0; j < ARRAY_SIZE (tests); j++)
    for (i = 0; i < GRUB_TEST_VIDEO_SMALL_N_MODES; i++)
      {
	grub_uint64_t start;

#if defined (GRUB_MACHINE_MIPS_QEMU_MIPS) || defined (GRUB_MACHINE_IEEE1275)
	if (grub_test_video_modes[i].width > 1024)
	  continue;
	if (grub_strcmp (tests[j].name, "gfxmenu") == 0
	    && grub_test_video_modes[i].width > 800)
	  continue;
#endif
	start = grub_get_time_ms ();

	grub_video_capture_start (&grub_test_video_modes[i],
				  grub_video_fbstd_colors,
				  grub_test_video_modes[i].number_of_colors);
	if (grub_errno)
	  {
	    grub_test_assert (0, "can't start capture: %d: %s",
			      grub_errno, grub_errmsg);
	    return;
	  }
	grub_terminal_input_fake_sequence ((int []) { -1, -1, -1, GRUB_TERM_KEY_DOWN, -1, 'e',
	      -1, GRUB_TERM_KEY_RIGHT, -1, 'x', -1,  GRUB_TERM_ESC, -1, GRUB_TERM_ESC }, 14);

	grub_video_checksum (tests[j].name);

	if (grub_test_use_gfxterm ())
	  return;

	grub_env_context_open ();
	if (tests[j].var)
	  grub_env_set (tests[j].var, tests[j].val);
	grub_normal_execute ("(proc)/test.cfg", 1, 0);
	grub_env_context_close ();

	grub_test_use_gfxterm_end ();

	grub_terminal_input_fake_sequence_end ();
	grub_video_checksum_end ();
	grub_video_capture_end ();

	if (tests[j].var)
	  grub_env_unset (tests[j].var);
	grub_printf ("%s %dx%dx%s done %lld ms\n", tests[j].name,
		     grub_test_video_modes[i].width,
		     grub_test_video_modes[i].height,
		     grub_video_checksum_get_modename (), (long long) (grub_get_time_ms () - start));
      }

  grub_procfs_unregister (&test_cfg);
}

/* Register example_test method as a functional test.  */
GRUB_FUNCTIONAL_TEST (gfxterm_menu, gfxterm_menu);
