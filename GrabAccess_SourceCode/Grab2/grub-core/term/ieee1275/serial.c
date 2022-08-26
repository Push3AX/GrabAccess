/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2000,2001,2002,2003,2004,2005,2006,2007,2008,2009,2010,2012  Free Software Foundation, Inc.
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

#include <grub/serial.h>
#include <grub/types.h>
#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/time.h>
#include <grub/i18n.h>
#include <grub/ieee1275/console.h>

#define IEEE1275_IHANDLE_INVALID  ((grub_ieee1275_cell_t) 0)

struct ofserial_hash_ent
{
  char *devpath;
  /* Pointer to shortest available name on nodes representing canonical names,
     otherwise NULL.  */
  const char *shortest;
  struct ofserial_hash_ent *next;
};

static void
do_real_config (struct grub_serial_port *port)
{
  if (port->configured)
    return;

  if (grub_ieee1275_open (port->elem->devpath, &port->handle)
      || port->handle == (grub_ieee1275_ihandle_t) -1)
    port->handle = IEEE1275_IHANDLE_INVALID;

  port->configured = 1;
}

/* Fetch a key.  */
static int
serial_hw_fetch (struct grub_serial_port *port)
{
  grub_ssize_t actual;
  char c;

  do_real_config (port);

  if (port->handle == IEEE1275_IHANDLE_INVALID)
    return -1;
  grub_ieee1275_read (port->handle, &c, 1, &actual);

  if (actual <= 0)
    return -1;
  return c;
}

/* Put a character.  */
static void
serial_hw_put (struct grub_serial_port *port, const int c)
{
  grub_ssize_t actual;
  char c0 = c;

  do_real_config (port);

  if (port->handle == IEEE1275_IHANDLE_INVALID)
    return;

  grub_ieee1275_write (port->handle, &c0, 1, &actual);
}

/* Initialize a serial device. PORT is the port number for a serial device.
   SPEED is a DTE-DTE speed which must be one of these: 2400, 4800, 9600,
   19200, 38400, 57600 and 115200. WORD_LEN is the word length to be used
   for the device. Likewise, PARITY is the type of the parity and
   STOP_BIT_LEN is the length of the stop bit. The possible values for
   WORD_LEN, PARITY and STOP_BIT_LEN are defined in the header file as
   macros.  */
static grub_err_t
serial_hw_configure (struct grub_serial_port *port __attribute__ ((unused)),
		     struct grub_serial_config *config __attribute__ ((unused)))
{
  /*  FIXME: no IEEE1275 serial config available.  */

  return GRUB_ERR_NONE;
}

struct grub_serial_driver grub_ofserial_driver =
  {
    .configure = serial_hw_configure,
    .fetch = serial_hw_fetch,
    .put = serial_hw_put
  };

#define OFSERIAL_HASH_SZ	8
static struct ofserial_hash_ent *ofserial_hash[OFSERIAL_HASH_SZ];

static int
ofserial_hash_fn (const char *devpath)
{
  int hash = 0;
  while (*devpath)
    hash ^= *devpath++;
  return (hash & (OFSERIAL_HASH_SZ - 1));
}

static struct ofserial_hash_ent *
ofserial_hash_find (const char *devpath)
{
  struct ofserial_hash_ent *p = ofserial_hash[ofserial_hash_fn(devpath)];

  while (p)
    {
      if (!grub_strcmp (p->devpath, devpath))
	break;
      p = p->next;
    }
  return p;
}

static struct ofserial_hash_ent *
ofserial_hash_add_real (char *devpath)
{
  struct ofserial_hash_ent *p;
  struct ofserial_hash_ent **head = &ofserial_hash[ofserial_hash_fn(devpath)];

  p = grub_malloc(sizeof (*p));
  if (!p)
    return NULL;

  p->devpath = devpath;
  p->next = *head;
  p->shortest = 0;
  *head = p;
  return p;
}

static struct ofserial_hash_ent *
ofserial_hash_add (char *devpath, char *curcan)
{
  struct ofserial_hash_ent *p, *pcan;

  p = ofserial_hash_add_real (devpath);

  grub_dprintf ("serial", "devpath = %s, canonical = %s\n", devpath, curcan);

  if (!curcan)
    {
      p->shortest = devpath;
      return p;
    }

  pcan = ofserial_hash_find (curcan);
  if (!pcan)
    pcan = ofserial_hash_add_real (curcan);
  else
    grub_free (curcan);

  if (!pcan)
    grub_errno = GRUB_ERR_NONE;
  else
    {
      if (!pcan->shortest
	  || grub_strlen (pcan->shortest) > grub_strlen (devpath))
	pcan->shortest = devpath;
    }

  return p;
}

static void
dev_iterate_real (struct grub_ieee1275_devalias *alias,
		  int use_name)
{
  struct ofserial_hash_ent *op;

  if (grub_strcmp (alias->type, "serial") != 0)
    return;

  grub_dprintf ("serial", "serial name = %s, path = %s\n", alias->name,
		alias->path);

  op = ofserial_hash_find (alias->path);
  if (!op)
    {
      char *name = grub_strdup (use_name ? alias->name : alias->path);
      char *can = grub_strdup (alias->path);
      if (!name || !can)
	{
	  grub_errno = GRUB_ERR_NONE;
	  grub_free (name);
	  grub_free (can);
	  return;
	}
      op = ofserial_hash_add (name, can);
    }
  return;
}

static int
dev_iterate (struct grub_ieee1275_devalias *alias)
{
  dev_iterate_real (alias, 0);
  return 0;
}

static const char *
add_port (struct ofserial_hash_ent *ent)
{
  struct grub_serial_port *port;
  char *ptr;
  grub_err_t err;

  if (!ent->shortest)
    return NULL;

  port = grub_zalloc (sizeof (*port));
  if (!port)
    return NULL;
  port->name = grub_malloc (sizeof ("ieee1275/")
			    + grub_strlen (ent->shortest));
  port->elem = ent;
  if (!port->name)
    return NULL;
  ptr = grub_stpcpy (port->name, "ieee1275/");
  grub_strcpy (ptr, ent->shortest);

  port->driver = &grub_ofserial_driver;
  err = grub_serial_config_defaults (port);
  if (err)
    grub_print_error ();

  grub_serial_register (port);

  return port->name;
}

const char *
grub_ofserial_add_port (const char *path)
{
  struct ofserial_hash_ent *ent;
  char *name = grub_strdup (path);
  char *can = grub_strdup (path);

  if (!name || ! can)
    {
      grub_free (name);
      grub_free (can);
      return NULL;
    }

  ent = ofserial_hash_add (name, can);
  return add_port (ent);
}

void
grub_ofserial_init (void)
{
  unsigned i;
  struct grub_ieee1275_devalias alias;

  FOR_IEEE1275_DEVALIASES(alias)
    dev_iterate_real (&alias, 1);

  grub_ieee1275_devices_iterate (dev_iterate);
  
  for (i = 0; i < ARRAY_SIZE (ofserial_hash); i++)
    {
      struct ofserial_hash_ent *ent;
      for (ent = ofserial_hash[i]; ent; ent = ent->next)
	add_port (ent);
    }
}

