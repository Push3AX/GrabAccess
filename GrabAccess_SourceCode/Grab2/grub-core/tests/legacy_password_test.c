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

#include <grub/test.h>
#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/crypto.h>
#include <grub/legacy_parse.h>
#include <grub/auth.h>

GRUB_MOD_LICENSE ("GPLv3+");

static struct
{
  char **args;
  int argc;
  char entered[GRUB_AUTH_MAX_PASSLEN];
  int exp;
} vectors[] = {
  { (char * []) { (char *) "hello", NULL }, 1, "hello", 1 },
  { (char * []) { (char *) "hello", NULL }, 1, "hi", 0 },
  { (char * []) { (char *) "hello", NULL }, 1, "hillo", 0 },
  { (char * []) { (char *) "hello", NULL }, 1, "hellw", 0 },
  { (char * []) { (char *) "hello", NULL }, 1, "hell", 0 },
  { (char * []) { (char *) "hello", NULL }, 1, "h", 0 },
  { (char * []) { (char *) "--md5", (char *) "$1$maL$OKEF0PD2k6eQ0Po8u4Gjr/",
		  NULL }, 2, "hello", 1 },
  { (char * []) { (char *) "--md5", (char *) "$1$maL$OKEF0PD2k6eQ0Po8u4Gjr/",
		  NULL }, 2, "hell", 0 },
  { (char * []) { (char *) "--md5", (char *) "$1$naL$BaFO8zGgmss1E76GsrAec1",
		  NULL }, 2, "hello", 1 },
  { (char * []) { (char *) "--md5", (char *) "$1$naL$BaFO8zGgmss1E76GsrAec1",
		  NULL }, 2, "hell", 0 },
  { (char * []) { (char *) "--md5", (char *) "$1$oaL$eyrazuM7TkxVkKgBim1WH1",
		  NULL }, 2, "hi", 1 },
  { (char * []) { (char *) "--md5", (char *) "$1$oaL$eyrazuM7TkxVkKgBim1WH1",
		  NULL }, 2, "hello", 0 },
};

static void
legacy_password_test (void)
{
  grub_size_t i;

  for (i = 0; i < ARRAY_SIZE (vectors); i++)
    grub_test_assert (grub_legacy_check_md5_password (vectors[i].argc,
						      vectors[i].args,
						      vectors[i].entered)
		      == vectors[i].exp, "Bad password check (%d)", (int) i);
}

/* Register example_test method as a functional test.  */
GRUB_FUNCTIONAL_TEST (legacy_password_test, legacy_password_test);
