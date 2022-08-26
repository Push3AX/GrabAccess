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
#include <grub/setjmp.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_jmp_buf jmp_point;
static int expected, ctr;

/* This fixes GCC7 "unintentional fallthrough" warning */
static void jmp0 (void) __attribute__ ((noreturn));
static void jmp1 (void) __attribute__ ((noreturn));
static void jmp2 (void) __attribute__ ((noreturn));

static void
jmp0 (void)
{
  grub_longjmp (jmp_point, 0);
}

static void
jmp1 (void)
{
  grub_longjmp (jmp_point, 1);
}

static void
jmp2 (void)
{
  grub_longjmp (jmp_point, 2);
}

static void
setjmp_test (void)
{
  int val;

  expected = 0;
  ctr = 0;
  val = grub_setjmp (jmp_point);

  grub_test_assert (val == expected, "setjmp returned %d instead of %d",
		    val, expected);
  switch (ctr++)
    {
    case 0:
      expected = 1;
      jmp0 ();
    case 1:
      expected = 1;
      jmp1 ();
    case 2:
      expected = 2;
      jmp2 ();
    case 3:
      return;
    }
  grub_test_assert (0, "setjmp didn't return enough times");
}

/* Register example_test method as a functional test.  */
GRUB_FUNCTIONAL_TEST (setjmp_test, setjmp_test);
