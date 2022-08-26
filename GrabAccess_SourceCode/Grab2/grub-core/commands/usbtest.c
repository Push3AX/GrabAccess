/* usbtest.c - test module for USB */
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

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/charset.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/usb.h>
#include <grub/command.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const char *usb_classes[] =
  {
    "Unknown",
    "Audio",
    "Communication Interface",
    "HID",
    "Unknown",
    "Physical",
    "Image",
    "Printer",
    "Mass Storage",
    "Hub",
    "Data Interface",
    "Smart Card",
    "Content Security",
    "Video"
  };

static const char *usb_endp_type[] =
  {
    "Control",
    "Isochronous",
    "Bulk",
    "Interrupt"
  };

static const char *usb_devspeed[] =
  {
    "",
    "Low",
    "Full",
    "High"
  };

#if __GNUC__ >= 9
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#endif

static grub_usb_err_t
grub_usb_get_string (grub_usb_device_t dev, grub_uint8_t index, int langid,
		     char **string)
{
  struct grub_usb_desc_str descstr;
  struct grub_usb_desc_str *descstrp;
  grub_usb_err_t err;

  /* Only get the length.  */
  err = grub_usb_control_msg (dev, 1 << 7,
			      0x06, (3 << 8) | index,
			      langid, 1, (char *) &descstr);
  if (err)
    return err;

  descstrp = grub_malloc (descstr.length);
  if (! descstrp)
    return GRUB_USB_ERR_INTERNAL;
  err = grub_usb_control_msg (dev, 1 << 7,
			      0x06, (3 << 8) | index,
			      langid, descstr.length, (char *) descstrp);

  if (descstrp->length == 0)
    {
      grub_free (descstrp);
      *string = grub_strdup ("");
      if (! *string)
	return GRUB_USB_ERR_INTERNAL;
      return GRUB_USB_ERR_NONE;
    }

  *string = grub_malloc (descstr.length * 2 + 1);
  if (! *string)
    {
      grub_free (descstrp);
      return GRUB_USB_ERR_INTERNAL;
    }

  *grub_utf16_to_utf8 ((grub_uint8_t *) *string, descstrp->str,
		       descstrp->length / 2 - 1) = 0;
  grub_free (descstrp);

  return GRUB_USB_ERR_NONE;
}

#if __GNUC__ >= 9
#pragma GCC diagnostic pop
#endif

static void
usb_print_str (const char *description, grub_usb_device_t dev, int idx)
{
  char *name = NULL;
  grub_usb_err_t err;
  /* XXX: LANGID  */

  if (! idx)
    return;

  err = grub_usb_get_string (dev, idx, 0x0409, &name);
  if (err)
    grub_printf ("Error %d retrieving %s\n", err, description);
  else
    {
      grub_printf ("%s: `%s'\n", description, name);
      grub_free (name);
    }
}

static int
usb_iterate (grub_usb_device_t dev, void *data __attribute__ ((unused)))
{
  struct grub_usb_desc_device *descdev;
  int i;

  descdev = &dev->descdev;

  usb_print_str ("Product", dev, descdev->strprod);
  usb_print_str ("Vendor", dev, descdev->strvendor);
  usb_print_str ("Serial", dev, descdev->strserial);

  grub_printf ("Class: (0x%02x) %s, Subclass: 0x%02x, Protocol: 0x%02x\n",
	       descdev->class, descdev->class < ARRAY_SIZE (usb_classes)
	       ? usb_classes[descdev->class] : "Unknown",
	       descdev->subclass, descdev->protocol);
  grub_printf ("USB version %d.%d, VendorID: 0x%02x, ProductID: 0x%02x, #conf: %d\n",
	       descdev->usbrel >> 8, (descdev->usbrel >> 4) & 0x0F,
	       descdev->vendorid, descdev->prodid, descdev->configcnt);

  grub_printf ("%s speed device\n", usb_devspeed[dev->speed]);

  for (i = 0; i < descdev->configcnt; i++)
    {
      struct grub_usb_desc_config *config;

      config = dev->config[i].descconf;
      usb_print_str ("Configuration:", dev, config->strconfig);
    }

  for (i = 0; i < dev->config[0].descconf->numif; i++)
    {
      int j;
      struct grub_usb_desc_if *interf;
      interf = dev->config[0].interf[i].descif;

      grub_printf ("Interface #%d: #Endpoints: %d   ",
		   i, interf->endpointcnt);
      grub_printf ("Class: (0x%02x) %s, Subclass: 0x%02x, Protocol: 0x%02x\n",
		   interf->class, interf->class < ARRAY_SIZE (usb_classes)
		   ? usb_classes[interf->class] : "Unknown",
		   interf->subclass, interf->protocol);

      usb_print_str ("Interface", dev, interf->strif);

      for (j = 0; j < interf->endpointcnt; j++)
	{
	  struct grub_usb_desc_endp *endp;
	  endp = dev->config[0].interf[i].descendp[j];

	  grub_printf ("Endpoint #%d: %s, max packed size: %d, transfer type: %s, latency: %d\n",
		       endp->endp_addr & 15,
		       (endp->endp_addr & 128) ? "IN" : "OUT",
		       endp->maxpacket, usb_endp_type[endp->attrib & 3],
		       endp->interval);
	}
    }

  grub_printf("\n");

  return 0;
}

static grub_err_t
grub_cmd_usbtest (grub_command_t cmd __attribute__ ((unused)),
		  int argc __attribute__ ((unused)),
		  char **args __attribute__ ((unused)))
{
  grub_usb_poll_devices (1);

  grub_printf ("USB devices:\n\n");
  grub_usb_iterate (usb_iterate, NULL);

  return 0;
}

static grub_command_t cmd;

GRUB_MOD_INIT(usbtest)
{
  cmd = grub_register_command ("usb", grub_cmd_usbtest,
			       0, N_("Test USB support."));
}

GRUB_MOD_FINI(usbtest)
{
  grub_unregister_command (cmd);
}
