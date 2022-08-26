/* usb.c - USB Hub Support.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008  Free Software Foundation, Inc.
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

#include <grub/dl.h>
#include <grub/mm.h>
#include <grub/usb.h>
#include <grub/misc.h>
#include <grub/time.h>

#define GRUB_USBHUB_MAX_DEVICES 128

/* USB Supports 127 devices, with device 0 as special case.  */
static struct grub_usb_device *grub_usb_devs[GRUB_USBHUB_MAX_DEVICES];

static int rescan = 0;
static int npending = 0;

struct grub_usb_hub
{
  struct grub_usb_hub *next;
  grub_usb_controller_t controller;
  int nports;
  struct grub_usb_device **devices;
  struct grub_usb_hub_port *ports;
  grub_usb_device_t dev;
};

static struct grub_usb_hub *hubs;
static grub_usb_controller_dev_t grub_usb_list;

/* Add a device that currently has device number 0 and resides on
   CONTROLLER, the Hub reported that the device speed is SPEED.  */
static grub_usb_device_t
grub_usb_hub_add_dev (grub_usb_controller_t controller,
                      grub_usb_speed_t speed,
                      int split_hubport, int split_hubaddr,
                      int root_portno,
                      grub_uint32_t route)
{
  grub_usb_device_t dev;
  int i;
  grub_usb_err_t err;

  grub_boot_time ("Attaching USB device");

  dev = grub_zalloc (sizeof (struct grub_usb_device));
  if (! dev)
    return NULL;

  dev->controller = *controller;
  dev->speed = speed;
  dev->split_hubport = split_hubport;
  dev->split_hubaddr = split_hubaddr;
  dev->root_port = root_portno;
  dev->route = route;

  if (controller->dev->attach_dev) {
    err = controller->dev->attach_dev (controller, dev);
    if (err)
      {
       grub_free (dev);
       return NULL;
      }
  }

  err = grub_usb_device_initialize (dev);
  if (err)
    {
      grub_free (dev);
      return NULL;
    }

  /* Assign a new address to the device.  */
  for (i = 1; i < GRUB_USBHUB_MAX_DEVICES; i++)
    {
      if (! grub_usb_devs[i])
	break;
    }
  if (i == GRUB_USBHUB_MAX_DEVICES)
    {
      grub_error (GRUB_ERR_IO, "can't assign address to USB device");
      for (i = 0; i < GRUB_USB_MAX_CONF; i++)
      {
        int currif;

        for (currif = 0; currif < dev->config[i].descconf->numif; currif++)
          grub_free (dev->config[i].interf[currif].descendp);

        grub_free (dev->config[i].descconf);
      }
      grub_free (dev);
      return NULL;
    }

  err = grub_usb_control_msg (dev,
			      (GRUB_USB_REQTYPE_OUT
			       | GRUB_USB_REQTYPE_STANDARD
			       | GRUB_USB_REQTYPE_TARGET_DEV),
			      GRUB_USB_REQ_SET_ADDRESS,
			      i, 0, 0, NULL);
  if (err)
    {
      for (i = 0; i < GRUB_USB_MAX_CONF; i++)
      {
        int currif;

        for (currif = 0; currif < dev->config[i].descconf->numif; currif++)
          grub_free (dev->config[i].interf[currif].descendp);

        grub_free (dev->config[i].descconf);
      }
      grub_free (dev);
      return NULL;
    }

  dev->addr = i;
  dev->initialized = 1;
  grub_usb_devs[i] = dev;

  grub_dprintf ("usb", "Added new usb device: %p, addr=%d\n",
		dev, i);
  grub_dprintf ("usb", "speed=%d, split_hubport=%d, split_hubaddr=%d\n",
		speed, split_hubport, split_hubaddr);

  /* Wait "recovery interval", spec. says 2ms */
  grub_millisleep (2);

  grub_boot_time ("Probing USB device driver");

  grub_usb_device_attach (dev);

  grub_boot_time ("Attached USB device");

  return dev;
}

static grub_usb_err_t
grub_usb_set_hub_depth(grub_usb_device_t dev, grub_uint8_t depth)
{
  return grub_usb_control_msg (dev, (GRUB_USB_REQTYPE_OUT
                            | GRUB_USB_REQTYPE_CLASS
                            | GRUB_USB_REQTYPE_TARGET_DEV),
                             GRUB_USB_HUB_REQ_SET_HUB_DEPTH, depth,
                             0, 0, NULL);
}

static grub_usb_err_t
grub_usb_add_hub (grub_usb_device_t dev)
{
  struct grub_usb_usb_hubdesc hubdesc;
  grub_usb_err_t err;
  grub_uint16_t req;
  int i;

  req = (dev->speed == GRUB_USB_SPEED_SUPER) ? GRUB_USB_DESCRIPTOR_SS_HUB :
    GRUB_USB_DESCRIPTOR_HUB;

  err = grub_usb_control_msg (dev, (GRUB_USB_REQTYPE_IN
                        | GRUB_USB_REQTYPE_CLASS
                        | GRUB_USB_REQTYPE_TARGET_DEV),
                        GRUB_USB_REQ_GET_DESCRIPTOR,
                        (req << 8) | 0,
                        0, sizeof (hubdesc), (char *) &hubdesc);
  if (err)
    return err;
  grub_dprintf ("usb", "Hub descriptor:\n\t\t len:%d, typ:0x%02x, cnt:%d, char:0x%02x, pwg:%d, curr:%d\n",
                hubdesc.length, hubdesc.type, hubdesc.portcnt,
                hubdesc.characteristics, hubdesc.pwdgood,
                hubdesc.current);

  /* Activate the first configuration. Hubs should have only one conf. */
  grub_dprintf ("usb", "Hub set configuration\n");
  grub_usb_set_configuration (dev, 1);

  dev->nports = hubdesc.portcnt;
  dev->children = grub_calloc (hubdesc.portcnt, sizeof (dev->children[0]));
  dev->ports = grub_calloc (dev->nports, sizeof (dev->ports[0]));
  if (!dev->children || !dev->ports)
    {
      grub_free (dev->children);
      grub_free (dev->ports);
      return GRUB_USB_ERR_INTERNAL;
    }

  if (dev->speed == GRUB_USB_SPEED_SUPER)
    {
      grub_uint8_t depth;
      grub_uint32_t route;
      /* Depth maximum value is 5, but root hubs doesn't count */
      for (depth = 0, route = dev->route; (route & 0xf) > 0; route >>= 4)
       depth++;

      err = grub_usb_set_hub_depth(dev, depth);
      if (err)
        return err;
    }

  /* Power on all Hub ports.  */
  for (i = 1; i <= hubdesc.portcnt; i++)
    {
      grub_dprintf ("usb", "Power on - port %d\n", i);
      /* Power on the port and wait for possible device connect */
      grub_usb_control_msg (dev, (GRUB_USB_REQTYPE_OUT
				  | GRUB_USB_REQTYPE_CLASS
				  | GRUB_USB_REQTYPE_TARGET_OTHER),
			    GRUB_USB_REQ_SET_FEATURE,
			    GRUB_USB_HUB_FEATURE_PORT_POWER,
			    i, 0, NULL);
    }

  /* Rest will be done on next usb poll.  */
  for (i = 0; i < dev->config[0].interf[0].descif->endpointcnt;
       i++)
    {
      struct grub_usb_desc_endp *endp = NULL;
      endp = dev->config[0].interf[0].descendp[i];

      if ((endp->endp_addr & 128) && grub_usb_get_ep_type(endp)
	  == GRUB_USB_EP_INTERRUPT)
	{
	  grub_size_t len;
	  dev->hub_endpoint = endp;
	  len = endp->maxpacket;
	  if (len > sizeof (dev->statuschange))
	    len = sizeof (dev->statuschange);
	  dev->hub_transfer
	    = grub_usb_bulk_read_background (dev, endp, len,
					     (char *) &dev->statuschange);
	  break;
	}
    }

  rescan = 1;

  return GRUB_USB_ERR_NONE;
}

static void
attach_root_port (struct grub_usb_hub *hub, int portno,
		  grub_usb_speed_t speed)
{
  grub_usb_device_t dev;
  grub_usb_err_t err;

  grub_boot_time ("After detect_dev");

  /* Enable the port.  */
  err = hub->controller->dev->portstatus (hub->controller, portno, 1);
  if (err)
    return;
  hub->controller->dev->pending_reset = grub_get_time_ms () + 5000;
  npending++;

  grub_millisleep (10);

  grub_boot_time ("Port enabled");

  /* Enable the port and create a device.  */
  /* High speed device needs not transaction translation
     and full/low speed device cannot be connected to EHCI root hub
     and full/low speed device connected to OHCI/UHCI needs not
     transaction translation - e.g. hubport and hubaddr should be
     always none (zero) for any device connected to any root hub. */
  dev = grub_usb_hub_add_dev (hub->controller, speed, 0, 0, portno, 0);
  hub->controller->dev->pending_reset = 0;
  npending--;
  if (! dev)
    return;

  hub->devices[portno] = dev;

  /* If the device is a Hub, scan it for more devices.  */
  if (dev->descdev.class == 0x09)
    grub_usb_add_hub (dev);

  grub_boot_time ("Attached root port");
}

/* Iterate over all controllers found by the driver.  */
static int
grub_usb_controller_dev_register_iter (grub_usb_controller_t controller, void *data)
{
  grub_usb_controller_dev_t usb = data;
  struct grub_usb_hub *hub;

  controller->dev = usb;

  grub_boot_time ("Registering USB root hub");

  hub = grub_malloc (sizeof (*hub));
  if (!hub)
    return GRUB_USB_ERR_INTERNAL;

  hub->next = hubs;
  hubs = hub;
  hub->controller = grub_malloc (sizeof (*controller));
  if (!hub->controller)
    {
      grub_free (hub);
      return GRUB_USB_ERR_INTERNAL;
    }

  grub_memcpy (hub->controller, controller, sizeof (*controller));
  hub->dev = 0;

  /* Query the number of ports the root Hub has.  */
  hub->nports = controller->dev->hubports (controller);
  hub->devices = grub_calloc (hub->nports, sizeof (hub->devices[0]));
  hub->ports = grub_calloc (hub->nports, sizeof (hub->ports[0]));
  if (!hub->devices || !hub->ports)
    {
      grub_free (hub->devices);
      grub_free (hub->ports);
      grub_free (hub->controller);
      grub_free (hub);
      grub_print_error ();
      return 0;
    }

  return 0;
}

void
grub_usb_controller_dev_unregister (grub_usb_controller_dev_t usb)
{
  grub_usb_controller_dev_t *p, q;

  for (p = &grub_usb_list, q = *p; q; p = &(q->next), q = q->next)
    if (q == usb)
      {
	*p = q->next;
	break;
      }
}

void
grub_usb_controller_dev_register (grub_usb_controller_dev_t usb)
{
  int portno;
  int continue_waiting = 0;
  struct grub_usb_hub *hub;

  usb->next = grub_usb_list;
  grub_usb_list = usb;

  if (usb->iterate)
    usb->iterate (grub_usb_controller_dev_register_iter, usb);

  grub_boot_time ("waiting for stable power on USB root\n");

  while (1)
    {
      for (hub = hubs; hub; hub = hub->next)
	if (hub->controller->dev == usb)
	  {
	    /* Wait for completion of insertion and stable power (USB spec.)
	     * Should be at least 100ms, some devices requires more...
	     * There is also another thing - some devices have worse contacts
	     * and connected signal is unstable for some time - we should handle
	     * it - but prevent deadlock in case when device is too faulty... */
	    for (portno = 0; portno < hub->nports; portno++)
	      {
		grub_usb_speed_t speed;
		int changed = 0;

		speed = hub->controller->dev->detect_dev (hub->controller, portno,
							  &changed);
      
		if (hub->ports[portno].state == PORT_STATE_NORMAL
		    && speed != GRUB_USB_SPEED_NONE)
		  {
		    hub->ports[portno].soft_limit_time = grub_get_time_ms () + 250;
		    hub->ports[portno].hard_limit_time = hub->ports[portno].soft_limit_time + 1750;
		    hub->ports[portno].state = PORT_STATE_WAITING_FOR_STABLE_POWER;
		    grub_boot_time ("Scheduling stable power wait for port %p:%d",
				    usb, portno);
		    continue_waiting++;
		    continue;
		  }

		if (hub->ports[portno].state == PORT_STATE_WAITING_FOR_STABLE_POWER
		    && speed == GRUB_USB_SPEED_NONE)
		  {
		    hub->ports[portno].soft_limit_time = grub_get_time_ms () + 250;
		    continue;
		  }
		if (hub->ports[portno].state == PORT_STATE_WAITING_FOR_STABLE_POWER
		    && grub_get_time_ms () > hub->ports[portno].soft_limit_time)
		  {
		    hub->ports[portno].state = PORT_STATE_STABLE_POWER;
		    grub_boot_time ("Got stable power wait for port %p:%d",
				    usb, portno);
		    continue_waiting--;
		    continue;
		  }
		if (hub->ports[portno].state == PORT_STATE_WAITING_FOR_STABLE_POWER
		    && grub_get_time_ms () > hub->ports[portno].hard_limit_time)
		  {
		    hub->ports[portno].state = PORT_STATE_FAILED_DEVICE;
		    continue_waiting--;
		    continue;
		  }
	      }
	  }
      if (!continue_waiting)
	break;
      grub_millisleep (1);
    }

  grub_boot_time ("After the stable power wait on USB root");

  for (hub = hubs; hub; hub = hub->next)
    if (hub->controller->dev == usb)
      for (portno = 0; portno < hub->nports; portno++)
	if (hub->ports[portno].state == PORT_STATE_STABLE_POWER)
	  {
	    grub_usb_speed_t speed;
	    int changed = 0;
	    hub->ports[portno].state = PORT_STATE_NORMAL;
	    speed = hub->controller->dev->detect_dev (hub->controller, portno, &changed);
	    attach_root_port (hub, portno, speed);
	  }

  grub_boot_time ("USB root hub registered");
}

static void detach_device (grub_usb_device_t dev);

static void
detach_device (grub_usb_device_t dev)
{
  unsigned i;
  grub_usb_err_t err;

  int k;
  if (!dev)
    return;
  if (dev->descdev.class == GRUB_USB_CLASS_HUB)
    {
      if (dev->hub_transfer)
	grub_usb_cancel_transfer (dev->hub_transfer);

      for (i = 0; i < dev->nports; i++)
	detach_device (dev->children[i]);
      grub_free (dev->children);
    }
  for (i = 0; i < ARRAY_SIZE (dev->config); i++)
    if (dev->config[i].descconf)
      for (k = 0; k < dev->config[i].descconf->numif; k++)
	{
	  struct grub_usb_interface *inter = &dev->config[i].interf[k];
	  if (inter && inter->detach_hook)
	    inter->detach_hook (dev, i, k);
	}
  if (dev->controller.dev->detach_dev) {
    err = dev->controller.dev->detach_dev (&dev->controller, dev);
    if (err)
      {
       // XXX
      }
  }

  grub_usb_devs[dev->addr] = 0;
}

static int
wait_power_nonroot_hub (grub_usb_device_t dev)
{
  grub_usb_err_t err;
  int continue_waiting = 0;
  unsigned i;
  
  for (i = 1; i <= dev->nports; i++)
    if (dev->ports[i - 1].state == PORT_STATE_WAITING_FOR_STABLE_POWER)
      {
	grub_uint64_t tm;
	grub_uint32_t current_status = 0;

	/* Get the port status.  */
	err = grub_usb_control_msg (dev, (GRUB_USB_REQTYPE_IN
					  | GRUB_USB_REQTYPE_CLASS
					  | GRUB_USB_REQTYPE_TARGET_OTHER),
				    GRUB_USB_REQ_GET_STATUS,
				    0, i,
				    sizeof (current_status),
				    (char *) &current_status);
	if (err)
	  {
	    dev->ports[i - 1].state = PORT_STATE_FAILED_DEVICE;
	    continue;
	  }
	tm = grub_get_time_ms ();
	if (!(current_status & GRUB_USB_HUB_STATUS_PORT_CONNECTED))
	  dev->ports[i - 1].soft_limit_time = tm + 250;
	if (tm >= dev->ports[i - 1].soft_limit_time)
	  {
	    if (dev->controller.dev->pending_reset)
	      continue;
	    /* Now do reset of port. */
	    grub_usb_control_msg (dev, (GRUB_USB_REQTYPE_OUT
					| GRUB_USB_REQTYPE_CLASS
					| GRUB_USB_REQTYPE_TARGET_OTHER),
				  GRUB_USB_REQ_SET_FEATURE,
				  GRUB_USB_HUB_FEATURE_PORT_RESET,
				  i, 0, 0);
	    dev->ports[i - 1].state = PORT_STATE_NORMAL;
	    grub_boot_time ("Resetting port %p:%d", dev, i - 1);

	    rescan = 1;
	    /* We cannot reset more than one device at the same time !
	     * Resetting more devices together results in very bad
	     * situation: more than one device has default address 0
	     * at the same time !!!
	     * Additionaly, we cannot perform another reset
	     * anywhere on the same OHCI controller until
	     * we will finish addressing of reseted device ! */
	    dev->controller.dev->pending_reset = grub_get_time_ms () + 5000;
	    npending++;
	    continue;
	  }
	if (tm >= dev->ports[i - 1].hard_limit_time)
	  {
	    dev->ports[i - 1].state = PORT_STATE_FAILED_DEVICE;
	    continue;
	  }
	continue_waiting = 1;
      }
  return continue_waiting && dev->controller.dev->pending_reset == 0;
}

static void
poll_nonroot_hub (grub_usb_device_t dev)
{
  grub_usb_err_t err;
  unsigned i;
  grub_uint32_t changed;
  grub_size_t actual, len;

  if (!dev->hub_transfer)
    return;

  err = grub_usb_check_transfer (dev->hub_transfer, &actual);

  if (err == GRUB_USB_ERR_WAIT)
    return;

  changed = dev->statuschange;

  len = dev->hub_endpoint->maxpacket;
  if (len > sizeof (dev->statuschange))
    len = sizeof (dev->statuschange);
  dev->hub_transfer
    = grub_usb_bulk_read_background (dev, dev->hub_endpoint, len,
				     (char *) &dev->statuschange);

  if (err || actual == 0 || changed == 0)
    return;

  /* Iterate over the Hub ports.  */
  for (i = 1; i <= dev->nports; i++)
    {
      grub_uint32_t status;

      if (!(changed & (1 << i))
	  || dev->ports[i - 1].state == PORT_STATE_WAITING_FOR_STABLE_POWER)
	continue;

      /* Get the port status.  */
      err = grub_usb_control_msg (dev, (GRUB_USB_REQTYPE_IN
					| GRUB_USB_REQTYPE_CLASS
					| GRUB_USB_REQTYPE_TARGET_OTHER),
				  GRUB_USB_REQ_GET_STATUS,
				  0, i, sizeof (status), (char *) &status);

      grub_dprintf ("usb", "dev = %p, i = %d, status = %08x\n",
                   dev, i, status);

      if (err)
	continue;

      /* FIXME: properly handle these conditions.  */
      if (status & GRUB_USB_HUB_STATUS_C_PORT_ENABLED)
	grub_usb_control_msg (dev, (GRUB_USB_REQTYPE_OUT
				    | GRUB_USB_REQTYPE_CLASS
				    | GRUB_USB_REQTYPE_TARGET_OTHER),
			      GRUB_USB_REQ_CLEAR_FEATURE,
			      GRUB_USB_HUB_FEATURE_C_PORT_ENABLED, i, 0, 0);

      if (status & GRUB_USB_HUB_STATUS_C_PORT_SUSPEND)
	grub_usb_control_msg (dev, (GRUB_USB_REQTYPE_OUT
				    | GRUB_USB_REQTYPE_CLASS
				    | GRUB_USB_REQTYPE_TARGET_OTHER),
			      GRUB_USB_REQ_CLEAR_FEATURE,
			      GRUB_USB_HUB_FEATURE_C_PORT_SUSPEND, i, 0, 0);

      if (status & GRUB_USB_HUB_STATUS_C_PORT_OVERCURRENT)
	grub_usb_control_msg (dev, (GRUB_USB_REQTYPE_OUT
				    | GRUB_USB_REQTYPE_CLASS
				    | GRUB_USB_REQTYPE_TARGET_OTHER),
			      GRUB_USB_REQ_CLEAR_FEATURE,
			      GRUB_USB_HUB_FEATURE_C_PORT_OVERCURRENT, i, 0, 0);

      if (!dev->controller.dev->pending_reset &&
          (status & GRUB_USB_HUB_STATUS_C_PORT_CONNECTED))
	{
	  grub_usb_control_msg (dev, (GRUB_USB_REQTYPE_OUT
				      | GRUB_USB_REQTYPE_CLASS
				      | GRUB_USB_REQTYPE_TARGET_OTHER),
				GRUB_USB_REQ_CLEAR_FEATURE,
				GRUB_USB_HUB_FEATURE_C_PORT_CONNECTED, i, 0, 0);

	  detach_device (dev->children[i - 1]);
	  dev->children[i - 1] = NULL;

	  /* Connected and status of connection changed ? */
	  if (status & GRUB_USB_HUB_STATUS_PORT_CONNECTED)
	    {
	      grub_boot_time ("Before the stable power wait portno=%d", i);
	      /* A device is actually connected to this port. */
	      /* Wait for completion of insertion and stable power (USB spec.)
	       * Should be at least 100ms, some devices requires more...
	       * There is also another thing - some devices have worse contacts
	       * and connected signal is unstable for some time - we should handle
	       * it - but prevent deadlock in case when device is too faulty... */
	      dev->ports[i - 1].soft_limit_time = grub_get_time_ms () + 250;
	      dev->ports[i - 1].hard_limit_time = dev->ports[i - 1].soft_limit_time + 1750;
	      dev->ports[i - 1].state = PORT_STATE_WAITING_FOR_STABLE_POWER;
	      grub_boot_time ("Scheduling stable power wait for port %p:%d",
			      dev, i - 1);
	      continue;
	    }
	}

      if (status & GRUB_USB_HUB_STATUS_C_PORT_RESET)
	{
	  grub_usb_control_msg (dev, (GRUB_USB_REQTYPE_OUT
				      | GRUB_USB_REQTYPE_CLASS
				      | GRUB_USB_REQTYPE_TARGET_OTHER),
				GRUB_USB_REQ_CLEAR_FEATURE,
				GRUB_USB_HUB_FEATURE_C_PORT_RESET, i, 0, 0);

	  grub_boot_time ("Port %d reset", i);

	  if (status & GRUB_USB_HUB_STATUS_PORT_CONNECTED)
	    {
	      grub_usb_speed_t speed;
	      grub_usb_device_t next_dev;
	      int split_hubport = 0;
	      int split_hubaddr = 0;

	      /* Determine the device speed.  */
	      if (dev->speed == GRUB_USB_SPEED_SUPER)
               speed = GRUB_USB_SPEED_SUPER;
             else if (status & GRUB_USB_HUB_STATUS_PORT_LOWSPEED)
		speed = GRUB_USB_SPEED_LOW;
	      else
		{
		  if (status & GRUB_USB_HUB_STATUS_PORT_HIGHSPEED)
		    speed = GRUB_USB_SPEED_HIGH;
		  else
		    speed = GRUB_USB_SPEED_FULL;
		}

	      /* Wait a recovery time after reset, spec. says 10ms */
	      grub_millisleep (10);

              /* Find correct values for SPLIT hubport and hubaddr */
	      if (speed == GRUB_USB_SPEED_HIGH || speed == GRUB_USB_SPEED_SUPER)
	        {
		  /* HIGH speed device needs not transaction translation */
		  split_hubport = 0;
		  split_hubaddr = 0;
		}
	      else
	        /* FULL/LOW device needs hub port and hub address
		   for transaction translation (if connected to EHCI) */
	        if (dev->speed == GRUB_USB_SPEED_HIGH)
	          {
		    /* This port is the first FULL/LOW speed port
		       in the chain from root hub. Attached device
		       should use its port number and hub address */
		    split_hubport = i;
		    split_hubaddr = dev->addr;
		  }
	        else
	          {
		    /* This port is NOT the first FULL/LOW speed port
		       in the chain from root hub. Attached device
		       should use values inherited from some parent
		       HIGH speed hub - if any. */
		    split_hubport = dev->split_hubport;
		    split_hubaddr = dev->split_hubaddr;
		  }


	      /* Add the device and assign a device address to it.  */
	      next_dev = grub_usb_hub_add_dev (&dev->controller, speed,
					       split_hubport, split_hubaddr, dev->root_port,
					       dev->route << 4 | (i & 0xf));
	      if (dev->controller.dev->pending_reset)
		{
		  dev->controller.dev->pending_reset = 0;
		  npending--;
		}
	      if (! next_dev)
		continue;

	      dev->children[i - 1] = next_dev;

	      /* If the device is a Hub, scan it for more devices.  */
	      if (next_dev->descdev.class == 0x09)
		grub_usb_add_hub (next_dev);
	    }
	}
    }
}

void
grub_usb_poll_devices (int wait_for_completion)
{
  struct grub_usb_hub *hub;
  int i;

  for (hub = hubs; hub; hub = hub->next)
    {
      /* Do we have to recheck number of ports?  */
      /* No, it should be never changed, it should be constant. */
      for (i = 0; i < hub->nports; i++)
	{
	  grub_usb_speed_t speed = GRUB_USB_SPEED_NONE;
	  int changed = 0;

          if (hub->controller->dev->pending_reset)
            {
              /* Check for possible timeout */
              if (grub_get_time_ms () > hub->controller->dev->pending_reset)
                {
                  /* Something went wrong, reset device was not
                   * addressed properly, timeout happened */
	          hub->controller->dev->pending_reset = 0;
		  npending--;
                }
            }
          if (!hub->controller->dev->pending_reset)
	    speed = hub->controller->dev->detect_dev (hub->controller,
						      i, &changed);

	  if (changed)
	    {
	      detach_device (hub->devices[i]);
	      hub->devices[i] = NULL;
	      if (speed != GRUB_USB_SPEED_NONE)
                attach_root_port (hub, i, speed);
	    }
	}
    }

  while (1)
    {
      rescan = 0;

      /* We should check changes of non-root hubs too. */
      for (i = 0; i < GRUB_USBHUB_MAX_DEVICES; i++)
	{
	  grub_usb_device_t dev = grub_usb_devs[i];

	  if (dev && dev->descdev.class == 0x09)
	    poll_nonroot_hub (dev);
	}

      while (1)
	{
	  int continue_waiting = 0;
	  for (i = 0; i < GRUB_USBHUB_MAX_DEVICES; i++)
	    {
	      grub_usb_device_t dev = grub_usb_devs[i];
	    
	      if (dev && dev->descdev.class == 0x09)
		continue_waiting = continue_waiting || wait_power_nonroot_hub (dev);
	    }
	  if (!continue_waiting)
	    break;
	  grub_millisleep (1);
	}

      if (!(rescan || (npending && wait_for_completion)))
	break;
      grub_millisleep (25);
    }
}

int
grub_usb_iterate (grub_usb_iterate_hook_t hook, void *hook_data)
{
  int i;

  for (i = 0; i < GRUB_USBHUB_MAX_DEVICES; i++)
    {
      if (grub_usb_devs[i])
	{
	  if (hook (grub_usb_devs[i], hook_data))
	      return 1;
	}
    }

  return 0;
}
