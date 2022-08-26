/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010 Free Software Foundation, Inc.
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

#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/extcmd.h>
#include <grub/test.h>
#include <grub/dl.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_err_t
grub_functional_test (grub_extcmd_context_t ctxt __attribute__ ((unused)),
		      int argc,
		      char **args)
{
  grub_test_t test;
  int ok = 1;
  int i;

  FOR_LIST_ELEMENTS (test, grub_test_list)
    {
      if (argc != 0)
	{
	  for (i = 0; i < argc; i++)
	    if (grub_strcmp(args[i], test->name) == 0)
	      break;
	  if (i == argc)
	    continue;
	}
      grub_errno = 0;
      ok = ok && !grub_test_run (test);
      grub_errno = 0;
    }
  if (ok)
    grub_printf ("ALL TESTS PASSED\n");
  else
    grub_printf ("TEST FAILURE\n");
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_functional_all_tests (grub_extcmd_context_t ctxt __attribute__ ((unused)),
			   int argc __attribute__ ((unused)),
			   char **args __attribute__ ((unused)))
{
  grub_test_t test;
  int ok = 1;

  grub_dl_load ("legacy_password_test");
  grub_errno = GRUB_ERR_NONE;
  grub_dl_load ("exfctest");
  grub_dl_load ("videotest_checksum");
  grub_dl_load ("gfxterm_menu");
  grub_dl_load ("setjmp_test");
  grub_dl_load ("cmdline_cat_test");
  grub_dl_load ("div_test");
  grub_dl_load ("xnu_uuid_test");
  grub_dl_load ("pbkdf2_test");
  grub_dl_load ("signature_test");
  grub_dl_load ("sleep_test");
  grub_dl_load ("bswap_test");
  grub_dl_load ("ctz_test");
  grub_dl_load ("cmp_test");
  grub_dl_load ("mul_test");
  grub_dl_load ("shift_test");

  FOR_LIST_ELEMENTS (test, grub_test_list)
    ok = !grub_test_run (test) && ok;
  if (ok)
    grub_printf ("ALL TESTS PASSED\n");
  else
    grub_printf ("TEST FAILURE\n");
  return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT (functional_test)
{
  cmd = grub_register_extcmd ("functional_test", grub_functional_test, 0, 0,
			      "Run all loaded functional tests.", 0);
  cmd = grub_register_extcmd ("all_functional_test", grub_functional_all_tests, 0, 0,
			      "Run all functional tests.", 0);
}

GRUB_MOD_FINI (functional_test)
{
  grub_unregister_extcmd (cmd);
}
