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

/* Unit tests are normal programs, so they can include C library.  */
#include <string.h>

/* All tests need to include test.h for GRUB testing framework.  */
#include <grub/test.h>

/* Unit test main method.  */
static void
example_test (void)
{
  /* Check if 1st argument is true and report with default error message.  */
  grub_test_assert (1 == 1, "1 equal 1 expected");

  /* Check if 1st argument is true and report with custom error message.  */
  grub_test_assert (2 == 2, "2 equal 2 expected");
  grub_test_assert (2 != 3, "2 matches %d", 3);
}

/* Register example_test method as a unit test.  */
GRUB_UNIT_TEST ("example_unit_test", example_test);
