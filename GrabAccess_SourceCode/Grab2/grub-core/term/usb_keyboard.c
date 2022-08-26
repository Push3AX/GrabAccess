/* Support for the HID Boot Protocol.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008, 2009  Free Software Foundation, Inc.
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
#include <grub/time.h>
#include <grub/misc.h>
#include <grub/usb.h>
#include <grub/dl.h>
#include <grub/keyboard_layouts.h>

GRUB_MOD_LICENSE ("GPLv3+");

enum
  {
    KEY_NO_KEY = 0x00,
    KEY_ERR_BUFFER = 0x01,
    KEY_ERR_POST  = 0x02,
    KEY_ERR_UNDEF = 0x03,
    KEY_CAPS_LOCK = 0x39,
    KEY_NUM_LOCK  = 0x53,
  };

enum
  {
    LED_NUM_LOCK = 0x01,
    LED_CAPS_LOCK = 0x02
  };

/* Valid values for bRequest.  See HID definition version 1.11 section 7.2. */
#define USB_HID_GET_REPORT	0x01
#define USB_HID_GET_IDLE	0x02
#define USB_HID_GET_PROTOCOL	0x03
#define USB_HID_SET_REPORT	0x09
#define USB_HID_SET_IDLE	0x0A
#define USB_HID_SET_PROTOCOL	0x0B

#define USB_HID_BOOT_SUBCLASS	0x01
#define USB_HID_KBD_PROTOCOL	0x01

#define GRUB_USB_KEYBOARD_LEFT_CTRL   0x01
#define GRUB_USB_KEYBOARD_LEFT_SHIFT  0x02
#define GRUB_USB_KEYBOARD_LEFT_ALT    0x04
#define GRUB_USB_KEYBOARD_RIGHT_CTRL  0x10
#define GRUB_USB_KEYBOARD_RIGHT_SHIFT 0x20
#define GRUB_USB_KEYBOARD_RIGHT_ALT   0x40

struct grub_usb_keyboard_data
{
  grub_usb_device_t usbdev;
  grub_uint8_t status;
  grub_uint16_t mods;
  int interfno;
  struct grub_usb_desc_endp *endp;
  grub_usb_transfer_t transfer;
  grub_uint8_t report[8];
  int dead;
  int last_key;
  grub_uint64_t repeat_time;
  grub_uint8_t current_report[8];
  grub_uint8_t last_report[8];
  int index;
  int max_index;
};

static int grub_usb_keyboard_getkey (struct grub_term_input *term);
static int grub_usb_keyboard_getkeystatus (struct grub_term_input *term);

static struct grub_term_input grub_usb_keyboard_term =
  {
    .getkey = grub_usb_keyboard_getkey,
    .getkeystatus = grub_usb_keyboard_getkeystatus,
    .next = 0
  };

static struct grub_term_input grub_usb_keyboards[16];

static int
interpret_status (grub_uint8_t data0)
{
  int mods = 0;

  /* Check Shift, Control, and Alt status.  */
  if (data0 & GRUB_USB_KEYBOARD_LEFT_SHIFT)
    mods |= GRUB_TERM_STATUS_LSHIFT;
  if (data0 & GRUB_USB_KEYBOARD_RIGHT_SHIFT)
    mods |= GRUB_TERM_STATUS_RSHIFT;
  if (data0 & GRUB_USB_KEYBOARD_LEFT_CTRL)
    mods |= GRUB_TERM_STATUS_LCTRL;
  if (data0 & GRUB_USB_KEYBOARD_RIGHT_CTRL)
    mods |= GRUB_TERM_STATUS_RCTRL;
  if (data0 & GRUB_USB_KEYBOARD_LEFT_ALT)
    mods |= GRUB_TERM_STATUS_LALT;
  if (data0 & GRUB_USB_KEYBOARD_RIGHT_ALT)
    mods |= GRUB_TERM_STATUS_RALT;

  return mods;
}

static void
grub_usb_keyboard_detach (grub_usb_device_t usbdev,
			  int config __attribute__ ((unused)),
			  int interface __attribute__ ((unused)))
{
  unsigned i;
  for (i = 0; i < ARRAY_SIZE (grub_usb_keyboards); i++)
    {
      struct grub_usb_keyboard_data *data = grub_usb_keyboards[i].data;

      if (!data)
	continue;

      if (data->usbdev != usbdev)
	continue;

      if (data->transfer)
	grub_usb_cancel_transfer (data->transfer);

      grub_term_unregister_input (&grub_usb_keyboards[i]);
      grub_free ((char *) grub_usb_keyboards[i].name);
      grub_usb_keyboards[i].name = NULL;
      grub_free (grub_usb_keyboards[i].data);
      grub_usb_keyboards[i].data = 0;
    }
}

static int
grub_usb_keyboard_attach (grub_usb_device_t usbdev, int configno, int interfno)
{
  unsigned curnum;
  struct grub_usb_keyboard_data *data;
  struct grub_usb_desc_endp *endp = NULL;
  int j;

  grub_dprintf ("usb_keyboard", "%x %x %x %d %d\n",
		usbdev->descdev.class, usbdev->descdev.subclass,
		usbdev->descdev.protocol, configno, interfno);

  for (curnum = 0; curnum < ARRAY_SIZE (grub_usb_keyboards); curnum++)
    if (!grub_usb_keyboards[curnum].data)
      break;

  if (curnum == ARRAY_SIZE (grub_usb_keyboards))
    return 0;

  if (usbdev->descdev.class != 0 
      || usbdev->descdev.subclass != 0 || usbdev->descdev.protocol != 0)
    return 0;

  if (usbdev->config[configno].interf[interfno].descif->subclass
      != USB_HID_BOOT_SUBCLASS
      || usbdev->config[configno].interf[interfno].descif->protocol
      != USB_HID_KBD_PROTOCOL)
    return 0;

  for (j = 0; j < usbdev->config[configno].interf[interfno].descif->endpointcnt;
       j++)
    {
      endp = usbdev->config[configno].interf[interfno].descendp[j];

      if ((endp->endp_addr & 128) && grub_usb_get_ep_type(endp)
	  == GRUB_USB_EP_INTERRUPT)
	break;
    }
  if (j == usbdev->config[configno].interf[interfno].descif->endpointcnt)
    return 0;

  grub_dprintf ("usb_keyboard", "HID found!\n");

  data = grub_malloc (sizeof (*data));
  if (!data)
    {
      grub_print_error ();
      return 0;
    }

  data->usbdev = usbdev;
  data->interfno = interfno;
  data->endp = endp;

  /* Configure device */
  grub_usb_set_configuration (usbdev, configno + 1);
  
  /* Place the device in boot mode.  */
  grub_usb_control_msg (usbdev, GRUB_USB_REQTYPE_CLASS_INTERFACE_OUT,
  			USB_HID_SET_PROTOCOL, 0, interfno, 0, 0);

  /* Reports every time an event occurs and not more often than that.  */
  grub_usb_control_msg (usbdev, GRUB_USB_REQTYPE_CLASS_INTERFACE_OUT,
  			USB_HID_SET_IDLE, 0<<8, interfno, 0, 0);

  grub_memcpy (&grub_usb_keyboards[curnum], &grub_usb_keyboard_term,
	       sizeof (grub_usb_keyboards[curnum]));
  grub_usb_keyboards[curnum].data = data;
  usbdev->config[configno].interf[interfno].detach_hook
    = grub_usb_keyboard_detach;
  grub_usb_keyboards[curnum].name = grub_xasprintf ("usb_keyboard%d", curnum);
  if (!grub_usb_keyboards[curnum].name)
    {
      grub_print_error ();
      return 0;
    }

  /* Test showed that getting report may make the keyboard go nuts.
     Moreover since we're reattaching keyboard it usually sends
     an initial message on interrupt pipe and so we retrieve
     the same keystatus.
   */
#if 0
  {
    grub_uint8_t report[8];
    grub_usb_err_t err;
    grub_memset (report, 0, sizeof (report));
    err = grub_usb_control_msg (usbdev, GRUB_USB_REQTYPE_CLASS_INTERFACE_IN,
    				USB_HID_GET_REPORT, 0x0100, interfno,
				sizeof (report), (char *) report);
    if (err)
      data->status = 0;
    else
      data->status = report[0];
  }
#else
  data->status = 0;
#endif

  data->transfer = grub_usb_bulk_read_background (usbdev,
						  data->endp,
						  sizeof (data->report),
						  (char *) data->report);
  if (!data->transfer)
    {
      grub_print_error ();
      return 0;
    }

  data->last_key = -1;
  data->mods = 0;
  data->dead = 0;

  grub_term_register_input_active ("usb_keyboard", &grub_usb_keyboards[curnum]);

  return 1;
}



static void
send_leds (struct grub_usb_keyboard_data *termdata)
{
  char report[1];
  report[0] = 0;
  if (termdata->mods & GRUB_TERM_STATUS_CAPS)
    report[0] |= LED_CAPS_LOCK;
  if (termdata->mods & GRUB_TERM_STATUS_NUM)
    report[0] |= LED_NUM_LOCK;
  grub_usb_control_msg (termdata->usbdev, GRUB_USB_REQTYPE_CLASS_INTERFACE_OUT,
			USB_HID_SET_REPORT, 0x0200, termdata->interfno,
			sizeof (report), (char *) report);
  grub_errno = GRUB_ERR_NONE;
}

static int
parse_keycode (struct grub_usb_keyboard_data *termdata)
{
  int index = termdata->index;
  int i, keycode;

  /* Sanity check */
  if (index < 2)
    index = 2;

  for ( ; index < termdata->max_index; index++)
    {
      keycode = termdata->current_report[index];
      
      if (keycode == KEY_NO_KEY
          || keycode == KEY_ERR_BUFFER
          || keycode == KEY_ERR_POST
          || keycode == KEY_ERR_UNDEF)
        {
          /* Don't parse (rest of) this report */
          termdata->index = 0;
          if (keycode != KEY_NO_KEY)
          /* Don't replace last report with current faulty report
           * in future ! */
            grub_memcpy (termdata->current_report,
                         termdata->last_report,
                         sizeof (termdata->report));
          return GRUB_TERM_NO_KEY;
        }

      /* Try to find current keycode in last report. */
      for (i = 2; i < 8; i++)
        if (keycode == termdata->last_report[i])
          break;
      if (i < 8)
        /* Keycode is in last report, it means it was not released,
         * ignore it. */
        continue;
        
      if (keycode == KEY_CAPS_LOCK)
        {
          termdata->mods ^= GRUB_TERM_STATUS_CAPS;
          send_leds (termdata);
          continue;
        }

      if (keycode == KEY_NUM_LOCK)
        {
          termdata->mods ^= GRUB_TERM_STATUS_NUM;
          send_leds (termdata);
          continue;
        }

      termdata->last_key = grub_term_map_key (keycode,
                                              interpret_status (termdata->current_report[0])
					        | termdata->mods);
      termdata->repeat_time = grub_get_time_ms () + GRUB_TERM_REPEAT_PRE_INTERVAL;

      grub_errno = GRUB_ERR_NONE;

      index++;
      if (index >= termdata->max_index)
        termdata->index = 0;
      else
        termdata->index = index;

      return termdata->last_key;
    }

  /* All keycodes parsed */
  termdata->index = 0;
  return GRUB_TERM_NO_KEY;
}

static int
grub_usb_keyboard_getkey (struct grub_term_input *term)
{
  grub_usb_err_t err;
  struct grub_usb_keyboard_data *termdata = term->data;
  grub_size_t actual;
  int keycode = GRUB_TERM_NO_KEY;

  if (termdata->dead)
    return GRUB_TERM_NO_KEY;

  if (termdata->index)
    keycode = parse_keycode (termdata);
  if (keycode != GRUB_TERM_NO_KEY)
    return keycode;
    
  /* Poll interrupt pipe.  */
  err = grub_usb_check_transfer (termdata->transfer, &actual);

  if (err == GRUB_USB_ERR_WAIT)
    {
      if (termdata->last_key != -1
	  && grub_get_time_ms () > termdata->repeat_time)
	{
	  termdata->repeat_time = grub_get_time_ms ()
	    + GRUB_TERM_REPEAT_INTERVAL;
	  return termdata->last_key;
	}
      return GRUB_TERM_NO_KEY;
    }

  if (!err && (actual >= 3))
    grub_memcpy (termdata->last_report,
                 termdata->current_report,
                 sizeof (termdata->report));
                 
  grub_memcpy (termdata->current_report,
               termdata->report,
               sizeof (termdata->report));

  termdata->transfer = grub_usb_bulk_read_background (termdata->usbdev,
						      termdata->endp,
						      sizeof (termdata->report),
						      (char *) termdata->report);
  if (!termdata->transfer)
    {
      grub_printf ("%s failed. Stopped\n", term->name);
      termdata->dead = 1;
    }

  termdata->last_key = -1;

  grub_dprintf ("usb_keyboard",
		"err = %d, actual = %" PRIuGRUB_SIZE
		" report: 0x%02x 0x%02x 0x%02x 0x%02x"
		" 0x%02x 0x%02x 0x%02x 0x%02x\n",
		err, actual,
		termdata->current_report[0], termdata->current_report[1],
		termdata->current_report[2], termdata->current_report[3],
		termdata->current_report[4], termdata->current_report[5],
		termdata->current_report[6], termdata->current_report[7]);

  if (err || actual < 1)
    return GRUB_TERM_NO_KEY;

  termdata->status = termdata->current_report[0];

  if (actual < 3)
    return GRUB_TERM_NO_KEY;

  termdata->index = 2; /* New data received. */
  termdata->max_index = actual;
  
  return parse_keycode (termdata);
}

static int
grub_usb_keyboard_getkeystatus (struct grub_term_input *term)
{
  struct grub_usb_keyboard_data *termdata = term->data;

  return interpret_status (termdata->status) | termdata->mods;
}

static struct grub_usb_attach_desc attach_hook =
{
  .class = GRUB_USB_CLASS_HID,
  .hook = grub_usb_keyboard_attach
};

GRUB_MOD_INIT(usb_keyboard)
{
  grub_usb_register_attach_hook_class (&attach_hook);
}

GRUB_MOD_FINI(usb_keyboard)
{
  unsigned i;
  for (i = 0; i < ARRAY_SIZE (grub_usb_keyboards); i++)
    if (grub_usb_keyboards[i].data)
      {
	struct grub_usb_keyboard_data *data = grub_usb_keyboards[i].data;

	if (!data)
	  continue;
	
	if (data->transfer)
	  grub_usb_cancel_transfer (data->transfer);

	grub_term_unregister_input (&grub_usb_keyboards[i]);
	grub_free ((char *) grub_usb_keyboards[i].name);
	grub_usb_keyboards[i].name = NULL;
	grub_free (grub_usb_keyboards[i].data);
	grub_usb_keyboards[i].data = 0;
      }
  grub_usb_unregister_attach_hook_class (&attach_hook);
}
