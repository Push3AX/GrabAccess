/* cstub.c - machine independent portion of remote GDB stub */
/*
 *  Copyright (C) 2006  Lubomir Kundrak
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <grub/misc.h>
#include <grub/cpu/gdb.h>
#include <grub/gdb.h>
#include <grub/serial.h>
#include <grub/backtrace.h>

static const char hexchars[] = "0123456789abcdef";
int grub_gdb_regs[GRUB_MACHINE_NR_REGS];

#define GRUB_GDB_COMBUF_SIZE 400	/* At least sizeof(grub_gdb_regs)*2 are needed for
					   register packets.  */
static char grub_gdb_inbuf[GRUB_GDB_COMBUF_SIZE + 1];
static char grub_gdb_outbuf[GRUB_GDB_COMBUF_SIZE + 1];

struct grub_serial_port *grub_gdb_port;

static int
hex (char ch)
{
  if ((ch >= 'a') && (ch <= 'f'))
    return (ch - 'a' + 10);
  if ((ch >= '0') && (ch <= '9'))
    return (ch - '0');
  if ((ch >= 'A') && (ch <= 'F'))
    return (ch - 'A' + 10);
  return (-1);
}

/* Scan for the sequence $<data>#<checksum>.  */
static char *
grub_gdb_getpacket (void)
{
  char *buffer = &grub_gdb_inbuf[0];
  unsigned char checksum;
  unsigned char xmitcsum;
  int count;
  int ch;

  while (1)
    {
      /* Wait around for the start character, ignore all other
         characters.  */
      while ((ch = grub_serial_port_fetch (grub_gdb_port)) != '$');

    retry:
      checksum = 0;
      xmitcsum = -1;
      count = 0;

      /* Now read until a # or end of buffer is found.  */
      while (count < GRUB_GDB_COMBUF_SIZE)
	{
	  do
	    ch = grub_serial_port_fetch (grub_gdb_port);
	  while (ch < 0);
	  if (ch == '$')
	    goto retry;
	  if (ch == '#')
	    break;
	  checksum += ch;
	  buffer[count] = ch;
	  count = count + 1;
	}
      buffer[count] = 0;
      if (ch == '#')
	{
	  do
	    ch = grub_serial_port_fetch (grub_gdb_port);
	  while (ch < 0);
	  xmitcsum = hex (ch) << 4;
	  do
	    ch = grub_serial_port_fetch (grub_gdb_port);
	  while (ch < 0);
	  xmitcsum += hex (ch);

	  if (checksum != xmitcsum)
	    grub_serial_port_put (grub_gdb_port, '-');	/* Failed checksum.  */
	  else
	    {
	      grub_serial_port_put (grub_gdb_port, '+');	/* Successful transfer.  */

	      /* If a sequence char is present, reply the sequence ID.  */
	      if (buffer[2] == ':')
		{
		  grub_serial_port_put (grub_gdb_port, buffer[0]);
		  grub_serial_port_put (grub_gdb_port, buffer[1]);

		  return &buffer[3];
		}
	      return &buffer[0];
	    }
	}
    }
}

/* Send the packet in buffer.  */
static void
grub_gdb_putpacket (char *buffer)
{
  grub_uint8_t checksum;

  /* $<packet info>#<checksum>.  */
  do
    {
      char *ptr;
      grub_serial_port_put (grub_gdb_port, '$');
      checksum = 0;

      for (ptr = buffer; *ptr; ptr++)
	{
	  grub_serial_port_put (grub_gdb_port, *ptr);
	  checksum += *ptr;
	}

      grub_serial_port_put (grub_gdb_port, '#');
      grub_serial_port_put (grub_gdb_port, hexchars[checksum >> 4]);
      grub_serial_port_put (grub_gdb_port, hexchars[checksum & 0xf]);
    }
  while (grub_serial_port_fetch (grub_gdb_port) != '+');
}

/* Convert the memory pointed to by mem into hex, placing result in buf.
   Return a pointer to the last char put in buf (NULL).  */
static char *
grub_gdb_mem2hex (char *mem, char *buf, grub_size_t count)
{
  grub_size_t i;
  unsigned char ch;

  for (i = 0; i < count; i++)
    {
      ch = *mem++;
      *buf++ = hexchars[ch >> 4];
      *buf++ = hexchars[ch % 16];
    }
  *buf = 0;
  return (buf);
}

/* Convert the hex array pointed to by buf into binary to be placed in mem.
   Return a pointer to the character after the last byte written.  */
static char *
grub_gdb_hex2mem (char *buf, char *mem, int count)
{
  int i;
  unsigned char ch;

  for (i = 0; i < count; i++)
    {
      ch = hex (*buf++) << 4;
      ch = ch + hex (*buf++);
      *mem++ = ch;
    }
  return (mem);
}

/* Convert hex characters to int and return the number of characters
   processed.  */
static int
grub_gdb_hex2int (char **ptr, grub_uint64_t *int_value)
{
  int num_chars = 0;
  int hex_value;

  *int_value = 0;

  while (**ptr)
    {
      hex_value = hex (**ptr);
      if (hex_value >= 0)
	{
	  *int_value = (*int_value << 4) | hex_value;
	  num_chars++;
	}
      else
	break;

      (*ptr)++;
    }

  return (num_chars);
}

/* This function does all command procesing for interfacing to gdb.  */
void __attribute__ ((regparm(3)))
grub_gdb_trap (int trap_no)
{
  unsigned int sig_no;
  int stepping;
  grub_uint64_t addr;
  grub_uint64_t length;
  char *ptr;

  if (!grub_gdb_port)
    {
      grub_printf ("Unhandled exception 0x%x at ", trap_no);
      grub_backtrace_print_address ((void *) grub_gdb_regs[PC]);
      grub_printf ("\n");
      grub_backtrace_pointer ((void *) grub_gdb_regs[EBP]);
      grub_fatal ("Unhandled exception");
    }

  sig_no = grub_gdb_trap2sig (trap_no);

  ptr = grub_gdb_outbuf;

  /* Reply to host that an exception has occurred.  */

  *ptr++ = 'T';	/* Notify gdb with signo, PC, FP and SP.  */

  *ptr++ = hexchars[sig_no >> 4];
  *ptr++ = hexchars[sig_no & 0xf];

  /* Stack pointer.  */
  *ptr++ = hexchars[SP];
  *ptr++ = ':';
  ptr = grub_gdb_mem2hex ((char *) &grub_gdb_regs[ESP], ptr, 4);
  *ptr++ = ';';

  /* Frame pointer.  */
  *ptr++ = hexchars[FP];
  *ptr++ = ':';
  ptr = grub_gdb_mem2hex ((char *) &grub_gdb_regs[EBP], ptr, 4);
  *ptr++ = ';';

  /* Program counter.  */
  *ptr++ = hexchars[PC];
  *ptr++ = ':';
  ptr = grub_gdb_mem2hex ((char *) &grub_gdb_regs[PC], ptr, 4);
  *ptr++ = ';';

  *ptr = '\0';

  grub_gdb_putpacket (grub_gdb_outbuf);

  stepping = 0;

  while (1)
    {
      grub_gdb_outbuf[0] = 0;
      ptr = grub_gdb_getpacket ();

      switch (*ptr++)
	{
	case '?':
	  grub_gdb_outbuf[0] = 'S';
	  grub_gdb_outbuf[1] = hexchars[sig_no >> 4];
	  grub_gdb_outbuf[2] = hexchars[sig_no & 0xf];
	  grub_gdb_outbuf[3] = 0;
	  break;

	/* Return values of the CPU registers.  */
	case 'g':
	  grub_gdb_mem2hex ((char *) grub_gdb_regs, grub_gdb_outbuf,
			    sizeof (grub_gdb_regs));
	  break;

	/* Set values of the CPU registers -- return OK.  */
	case 'G':
	  grub_gdb_hex2mem (ptr, (char *) grub_gdb_regs,
			    sizeof (grub_gdb_regs));
	  grub_strcpy (grub_gdb_outbuf, "OK");
	  break;

	/* Set the value of a single CPU register -- return OK.  */
	case 'P':
	  {
	    grub_uint64_t regno;

	    if (grub_gdb_hex2int (&ptr, &regno) && *ptr++ == '=')
	      if (regno < GRUB_MACHINE_NR_REGS)
		{
		  grub_gdb_hex2mem (ptr, (char *) &grub_gdb_regs[regno], 4);
		  grub_strcpy (grub_gdb_outbuf, "OK");
		  break;
		}
	    /* FIXME: GDB requires setting orig_eax. I don't know what's
	       this register is about. For now just simulate setting any
	       registers.  */
	    grub_strcpy (grub_gdb_outbuf, /*"E01"*/ "OK");
	    break;
	  }

	/* mAA..AA,LLLL: Read LLLL bytes at address AA..AA.  */
	case 'm':
	  /* Try to read %x,%x.  Set ptr = 0 if successful.  */
	  if (grub_gdb_hex2int (&ptr, &addr))
	    if (*(ptr++) == ',')
	      if (grub_gdb_hex2int (&ptr, &length))
		{
		  ptr = 0;
		  grub_gdb_mem2hex ((char *) (grub_addr_t) addr,
				    grub_gdb_outbuf, length);
		}
	  if (ptr)
	    grub_strcpy (grub_gdb_outbuf, "E01");
	  break;

	/* MAA..AA,LLLL: Write LLLL bytes at address AA.AA -- return OK.  */
	case 'M':
	  /* Try to read %x,%x.  Set ptr = 0 if successful.  */
	  if (grub_gdb_hex2int (&ptr, &addr))
	    if (*(ptr++) == ',')
	      if (grub_gdb_hex2int (&ptr, &length))
		if (*(ptr++) == ':')
		  {
		    grub_gdb_hex2mem (ptr, (char *) (grub_addr_t) addr, length);
		    grub_strcpy (grub_gdb_outbuf, "OK");
		    ptr = 0;
		  }
	  if (ptr)
	    {
	      grub_strcpy (grub_gdb_outbuf, "E02");
	    }
	  break;

	/* sAA..AA: Step one instruction from AA..AA(optional).  */
	case 's':
	  stepping = 1;
	  /* FALLTHROUGH */

	/* cAA..AA: Continue at address AA..AA(optional).  */
	case 'c':
	  /* try to read optional parameter, pc unchanged if no parm */
	  if (grub_gdb_hex2int (&ptr, &addr))
	    grub_gdb_regs[PC] = addr;

	  /* Clear the trace bit.  */
	  grub_gdb_regs[PS] &= 0xfffffeff;

	  /* Set the trace bit if we're stepping.  */
	  if (stepping)
	    grub_gdb_regs[PS] |= 0x100;

	  return;

	/* Kill the program.  */
	case 'k':
	  /* Do nothing.  */
	  return;
	}

      /* Reply to the request.  */
      grub_gdb_putpacket (grub_gdb_outbuf);
    }
}

