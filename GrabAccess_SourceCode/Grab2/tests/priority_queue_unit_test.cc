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

#include <stdio.h>
#include <string.h>
#include <grub/test.h>
#include <grub/misc.h>
#include <grub/priority_queue.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <queue>

using namespace std;

static int 
compar (const void *a_, const void *b_)
{
  int a = *(int *) a_;
  int b = *(int *) b_;
  if (a < b)
    return -1;
  if (a > b)
    return +1;
  return 0;
}

static void
priority_queue_test (void)
{
  priority_queue <int> pq;
  grub_priority_queue_t pq2;
  int counter;
  int s = 0;
  pq2 = grub_priority_queue_new (sizeof (int), compar);
  if (!pq2)
    {
      grub_test_assert (0,
			"priority queue: queue creating failed\n");
      return;
    }
  srand (1);

  for (counter = 0; counter < 1000000; counter++)
    {
      int op = rand () % 10;
      if (s && *(int *) grub_priority_queue_top (pq2) != pq.top ())
	{
	  printf ("Error at %d\n", counter);
	  grub_test_assert (0,
			    "priority queue: error at %d\n", counter);
	  return;
	}
      if (op < 3 && s)
	{
	  grub_priority_queue_pop (pq2);
	  pq.pop ();
	  s--;
	}
      else
	{
	  int v = rand ();
	  pq.push (v);
	  if (grub_priority_queue_push (pq2, &v) != 0)
	    {
	      grub_test_assert (0,
				"priority queue: push failed");
	      return;
	    }
	  s++;
	}
    }
  while (s)
    {
      if (*(int *) grub_priority_queue_top (pq2) != pq.top ())
	{
	  grub_test_assert (0,
			    "priority queue: Error at the end. %d elements remaining.\n", s);
	  return;
	}
      grub_priority_queue_pop (pq2);
      pq.pop ();
      s--;
    }
  printf ("priority_queue: passed successfully\n");
}

GRUB_UNIT_TEST ("priority_queue_unit_test", priority_queue_test);
