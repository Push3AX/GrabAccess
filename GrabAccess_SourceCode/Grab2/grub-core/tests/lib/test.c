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
#include <grub/test.h>

struct grub_test_failure
{
  /* The next failure.  */
  struct grub_test_failure *next;
  struct grub_test_failure **prev;

  /* The test source file name.  */
  char *file;

  /* The test function name.  */
  char *funp;

  /* The test call line number.  */
  grub_uint32_t line;

  /* The test failure message.  */
  char *message;
};
typedef struct grub_test_failure *grub_test_failure_t;

grub_test_t grub_test_list;
static grub_test_failure_t failure_list;

static grub_test_failure_t
failure_start(const char *file, const char *funp, grub_uint32_t line);
static grub_test_failure_t
failure_start(const char *file, const char *funp, grub_uint32_t line)
{
  grub_test_failure_t failure;

  failure = (grub_test_failure_t) grub_malloc (sizeof (*failure));
  if (!failure)
    return NULL;

  failure->file = grub_strdup (file ? : "<unknown_file>");
  if (!failure->file)
    {
      grub_free(failure);
      return NULL;
    }

  failure->funp = grub_strdup (funp ? : "<unknown_function>");
  if (!failure->funp)
    {
      grub_free(failure->file);
      grub_free(failure);
      return NULL;
    }

  failure->line = line;

  failure->message = NULL;

  return failure;
}

static void
failure_append_vtext(grub_test_failure_t failure, const char *fmt, va_list args);
static void
failure_append_vtext(grub_test_failure_t failure, const char *fmt, va_list args)
{
  char *msg = grub_xvasprintf(fmt, args);
  if (failure->message)
    {
      char *oldmsg = failure->message;

      failure->message = grub_xasprintf("%s%s", oldmsg, msg);
      grub_free (oldmsg);
      grub_free (msg);
    }
  else
    {
      failure->message = msg;
    }
}

static void
failure_append_text(grub_test_failure_t failure, const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  failure_append_vtext(failure, fmt, args);
  va_end(args);
}

static void
add_failure (const char *file,
	     const char *funp,
	     grub_uint32_t line, const char *fmt, va_list args)
{
  grub_test_failure_t failure = failure_start(file, funp, line);
  failure_append_text(failure, fmt, args);
  grub_list_push (GRUB_AS_LIST_P (&failure_list), GRUB_AS_LIST (failure));
}

static void
free_failures (void)
{
  grub_test_failure_t item;

  while (failure_list)
    {
      item = failure_list;
      failure_list = item->next;
      if (item->message)
	grub_free (item->message);

      if (item->funp)
	grub_free (item->funp);

      if (item->file)
	grub_free (item->file);

      grub_free (item);
    }
  failure_list = 0;
}

void
grub_test_nonzero (int cond,
		   const char *file,
		   const char *funp, grub_uint32_t line, const char *fmt, ...)
{
  va_list ap;

  if (cond)
    return;

  va_start (ap, fmt);
  add_failure (file, funp, line, fmt, ap);
  va_end (ap);
}

void
grub_test_assert_helper (int cond, const char *file, const char *funp,
			 grub_uint32_t line, const char *condstr,
			 const char *fmt, ...)
{
  va_list ap;
  grub_test_failure_t failure;

  if (cond)
    return;

  failure = failure_start(file, funp, line);
  failure_append_text(failure, "assert failed: %s ", condstr);

  va_start(ap, fmt);

  failure_append_vtext(failure, fmt, ap);

  va_end(ap);

  grub_list_push (GRUB_AS_LIST_P (&failure_list), GRUB_AS_LIST (failure));
}

void
grub_test_register (const char *name, void (*test_main) (void))
{
  grub_test_t test;

  test = (grub_test_t) grub_malloc (sizeof (*test));
  if (!test)
    return;

  test->name = grub_strdup (name);
  test->main = test_main;

  grub_list_push (GRUB_AS_LIST_P (&grub_test_list), GRUB_AS_LIST (test));
}

void
grub_test_unregister (const char *name)
{
  grub_test_t test;

  test = (grub_test_t) grub_named_list_find
    (GRUB_AS_NAMED_LIST (grub_test_list), name);

  if (test)
    {
      grub_list_remove (GRUB_AS_LIST (test));

      if (test->name)
	grub_free (test->name);

      grub_free (test);
    }
}

int
grub_test_run (grub_test_t test)
{
  grub_test_failure_t failure;

  test->main ();

  grub_printf ("%s:\n", test->name);
  FOR_LIST_ELEMENTS (failure, failure_list)
    grub_printf (" %s:%s:%u: %s\n",
		 (failure->file ? : "<unknown_file>"),
		 (failure->funp ? : "<unknown_function>"),
		 failure->line, (failure->message ? : "<no message>"));

  if (!failure_list)
    {
      grub_printf ("%s: PASS\n", test->name);
      return GRUB_ERR_NONE;
    }
  else
    {
      grub_printf ("%s: FAIL\n", test->name);
      free_failures ();
      return GRUB_ERR_TEST_FAILURE;
    }
}
