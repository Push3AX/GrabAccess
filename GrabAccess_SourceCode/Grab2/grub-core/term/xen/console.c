/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2011  Free Software Foundation, Inc.
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

#include <grub/term.h>
#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/time.h>
#include <grub/terminfo.h>
#include <grub/xen.h>

static int
readkey (struct grub_term_input *term __attribute__ ((unused)))
{
  grub_size_t prod, cons;
  int r;
  mb ();
  prod = grub_xen_xcons->in_prod;
  cons = grub_xen_xcons->in_cons;
  if (prod <= cons)
    return -1;
  r = grub_xen_xcons->in[cons];
  cons++;
  mb ();
  grub_xen_xcons->in_cons = cons;
  return r;
}

static int signal_sent = 1;

static void
refresh (struct grub_term_output *term __attribute__ ((unused)))
{
  struct evtchn_send send;
  send.port = grub_xen_start_page_addr->console.domU.evtchn;
  grub_xen_event_channel_op (EVTCHNOP_send, &send);
  signal_sent = 1;
  while (grub_xen_xcons->out_prod != grub_xen_xcons->out_cons)
    {
      grub_xen_sched_op (SCHEDOP_yield, 0);
    }
}

static void
put (struct grub_term_output *term __attribute__ ((unused)), const int c)
{
  grub_size_t prod, cons;

  while (1)
    {
      mb ();
      prod = grub_xen_xcons->out_prod;
      cons = grub_xen_xcons->out_cons;
      if (prod < cons + sizeof (grub_xen_xcons->out))
	break;
      if (!signal_sent)
	refresh (term);
      grub_xen_sched_op (SCHEDOP_yield, 0);
    }
  grub_xen_xcons->out[prod++ & (sizeof (grub_xen_xcons->out) - 1)] = c;
  mb ();
  grub_xen_xcons->out_prod = prod;
  signal_sent = 0;
}


struct grub_terminfo_input_state grub_console_terminfo_input = {
  .readkey = readkey
};

struct grub_terminfo_output_state grub_console_terminfo_output = {
  .put = put,
  .size = {80, 24}
};

static struct grub_term_input grub_console_term_input = {
  .name = "console",
  .init = 0,
  .getkey = grub_terminfo_getkey,
  .data = &grub_console_terminfo_input
};

static struct grub_term_output grub_console_term_output = {
  .name = "console",
  .init = 0,
  .putchar = grub_terminfo_putchar,
  .getxy = grub_terminfo_getxy,
  .getwh = grub_terminfo_getwh,
  .gotoxy = grub_terminfo_gotoxy,
  .cls = grub_terminfo_cls,
  .refresh = refresh,
  .setcolorstate = grub_terminfo_setcolorstate,
  .setcursor = grub_terminfo_setcursor,
  .flags = GRUB_TERM_CODE_TYPE_ASCII,
  .data = &grub_console_terminfo_output,
};


void
grub_console_init (void)
{
  grub_term_register_input ("console", &grub_console_term_input);
  grub_term_register_output ("console", &grub_console_term_output);

  grub_terminfo_init ();
  grub_terminfo_output_register (&grub_console_term_output, "vt100-color");
}
