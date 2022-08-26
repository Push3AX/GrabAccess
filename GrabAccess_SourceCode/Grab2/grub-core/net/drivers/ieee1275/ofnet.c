/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010,2011  Free Software Foundation, Inc.
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

#include <grub/net/netbuff.h>
#include <grub/ieee1275/ieee1275.h>
#include <grub/dl.h>
#include <grub/net.h>
#include <grub/time.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

struct grub_ofnetcard_data
{
  char *path;
  char *suffix;
  grub_ieee1275_ihandle_t handle;
};

static grub_err_t
card_open (struct grub_net_card *dev)
{
  int status;
  struct grub_ofnetcard_data *data = dev->data;

  status = grub_ieee1275_open (data->path, &(data->handle));

  if (status)
    return grub_error (GRUB_ERR_IO, "Couldn't open network card.");

  return GRUB_ERR_NONE;
}

static void
card_close (struct grub_net_card *dev)
{
  struct grub_ofnetcard_data *data = dev->data;

  if (data->handle)
    grub_ieee1275_close (data->handle);
}

static grub_err_t
send_card_buffer (struct grub_net_card *dev, struct grub_net_buff *pack)
{
  grub_ssize_t actual;
  int status;
  struct grub_ofnetcard_data *data = dev->data;
  grub_size_t len;

  len = (pack->tail - pack->data);
  if (len > dev->mtu)
    len = dev->mtu;

  grub_memcpy (dev->txbuf, pack->data, len);
  status = grub_ieee1275_write (data->handle, dev->txbuf,
				len, &actual);

  if (status)
    return grub_error (GRUB_ERR_IO, N_("couldn't send network packet"));
  return GRUB_ERR_NONE;
}

static struct grub_net_buff *
get_card_packet (struct grub_net_card *dev)
{
  grub_ssize_t actual;
  int rc;
  struct grub_ofnetcard_data *data = dev->data;
  grub_uint64_t start_time;
  struct grub_net_buff *nb;

  start_time = grub_get_time_ms ();
  do
    rc = grub_ieee1275_read (data->handle, dev->rcvbuf, dev->rcvbufsize, &actual);
  while ((actual <= 0 || rc < 0) && (grub_get_time_ms () - start_time < 200));

  if (actual <= 0)
    return NULL;

  nb = grub_netbuff_alloc (actual + 2);
  if (!nb)
    return NULL;
  /* Reserve 2 bytes so that 2 + 14/18 bytes of ethernet header is divisible
     by 4. So that IP header is aligned on 4 bytes. */
  grub_netbuff_reserve (nb, 2);

  grub_memcpy (nb->data, dev->rcvbuf, actual);

  if (grub_netbuff_put (nb, actual))
    {
      grub_netbuff_free (nb);
      return NULL;
    }

  return nb;
}

static struct grub_net_card_driver ofdriver =
  {
    .name = "ofnet",
    .open = card_open,
    .close = card_close,
    .send = send_card_buffer,
    .recv = get_card_packet
  };

static const struct
{
  const char *name;
  int offset;
}

bootp_response_properties[] =
  {
    { .name = "bootp-response", .offset = 0},
    { .name = "dhcp-response", .offset = 0},
    { .name = "bootpreply-packet", .offset = 0x2a},
  };

enum
{
  BOOTARGS_SERVER_ADDR,
  BOOTARGS_FILENAME,
  BOOTARGS_CLIENT_ADDR,
  BOOTARGS_GATEWAY_ADDR,
  BOOTARGS_BOOTP_RETRIES,
  BOOTARGS_TFTP_RETRIES,
  BOOTARGS_SUBNET_MASK,
  BOOTARGS_BLOCKSIZE
};

static int
grub_ieee1275_parse_bootpath (const char *devpath, char *bootpath,
                              char **device, struct grub_net_card **card)
{
  char *args;
  char *comma_char = 0;
  char *equal_char = 0;
  grub_size_t field_counter = 0;
  grub_net_network_level_address_t client_addr = {0, {0}, 0}, gateway_addr = {0, {0}, 0}, subnet_mask = {0, {0}, 0};
  grub_net_link_level_address_t hw_addr = {0, {{0, 0, 0, 0, 0, 0}}};
  grub_net_interface_flags_t flags = 0;
  struct grub_net_network_level_interface *inter = NULL;
  grub_uint16_t vlantag = 0;

  hw_addr.type = GRUB_NET_LINK_LEVEL_PROTOCOL_ETHERNET;
  hw_addr.len = 6;

  args = bootpath + grub_strlen (devpath) + 1;
  do
    {
      comma_char = grub_strchr (args, ',');
      if (comma_char != 0)
        *comma_char = 0;

      /* Check if it's an option (like speed=auto) and not a default parameter */
      equal_char = grub_strchr (args, '=');
      if (equal_char != 0)
        {
          *equal_char = 0;
          grub_env_set_net_property ((*card)->name, args, equal_char + 1,
                                     grub_strlen(equal_char + 1));

          if ((grub_strcmp (args, "vtag") == 0) &&
              (grub_strlen (equal_char + 1) == 8))
            vlantag = grub_strtoul (equal_char + 1 + 4, 0, 16);

          *equal_char = '=';
        }
      else
        {
          switch (field_counter++)
            {
            case BOOTARGS_SERVER_ADDR:
              *device = grub_xasprintf ("tftp,%s", args);
              if (!*device)
                return grub_errno;
              break;

            case BOOTARGS_CLIENT_ADDR:
              grub_net_resolve_address (args, &client_addr);
              break;

            case BOOTARGS_GATEWAY_ADDR:
              grub_net_resolve_address (args, &gateway_addr);
              break;

            case BOOTARGS_SUBNET_MASK:
              grub_net_resolve_address (args, &subnet_mask);
              break;
            }
        }
      args = comma_char + 1;
      if (comma_char != 0)
        *comma_char = ',';
    } while (comma_char != 0);

  if ((client_addr.ipv4 != 0) && (subnet_mask.ipv4 != 0))
    {
      grub_ieee1275_phandle_t devhandle;
      grub_ieee1275_finddevice (devpath, &devhandle);
      grub_ieee1275_get_property (devhandle, "mac-address",
                                  hw_addr.mac, sizeof(hw_addr.mac), 0);
      inter = grub_net_add_addr ((*card)->name, *card, &client_addr, &hw_addr,
                                 flags);
      inter->vlantag = vlantag;
      grub_net_add_ipv4_local (inter,
                          __builtin_clz (~grub_swap_bytes32(grub_le_to_cpu32 (subnet_mask.ipv4))));
    }

  if (gateway_addr.ipv4 != 0)
    {
      grub_net_network_level_netaddress_t target;
      char *rname;

      target.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4;
      target.ipv4.base = 0;
      target.ipv4.masksize = 0;
      rname = grub_xasprintf ("%s:default", ((*card)->name));
      if (rname)
        grub_net_add_route_gw (rname, target, gateway_addr, inter);
      else
        return grub_errno;
    }

  return 0;
}

static void
grub_ieee1275_net_config_real (const char *devpath, char **device, char **path,
                               char *bootpath)
{
  struct grub_net_card *card;

  /* FIXME: Check that it's the right card.  */
  FOR_NET_CARDS (card)
  {
    char *bootp_response;
    char *canon;
    char c;
    struct grub_ofnetcard_data *data;

    grub_ssize_t size = -1;
    unsigned int i;

    if (card->driver != &ofdriver)
      continue;

    data = card->data;
    c = *data->suffix;
    *data->suffix = '\0';
    canon = grub_ieee1275_canonicalise_devname (data->path);
    *data->suffix = c;
    if (grub_strcmp (devpath, canon) != 0)
      {
	grub_free (canon);
	continue;
      }
    grub_free (canon);

    grub_ieee1275_parse_bootpath (devpath, bootpath, device, &card);

    for (i = 0; i < ARRAY_SIZE (bootp_response_properties); i++)
      if (grub_ieee1275_get_property_length (grub_ieee1275_chosen,
					     bootp_response_properties[i].name,
					     &size) >= 0)
	break;

    if (size < 0)
      return;

    bootp_response = grub_malloc (size);
    if (!bootp_response)
      {
	grub_print_error ();
	return;
      }
    if (grub_ieee1275_get_property (grub_ieee1275_chosen,
				    bootp_response_properties[i].name,
				    bootp_response, size, 0) < 0)
      return;

    grub_net_configure_by_dhcp_ack (card->name, card, 0,
				    (struct grub_net_bootp_packet *)
				    (bootp_response
				     + bootp_response_properties[i].offset),
				    size - bootp_response_properties[i].offset,
				    1, device, path);
    grub_free (bootp_response);
    return;
  }
}

/* Allocate memory with alloc-mem */
static void *
grub_ieee1275_alloc_mem (grub_size_t len)
{
  struct alloc_args
  {
    struct grub_ieee1275_common_hdr common;
    grub_ieee1275_cell_t method;
    grub_ieee1275_cell_t len;
    grub_ieee1275_cell_t catch;
    grub_ieee1275_cell_t result;
  }
  args;

  INIT_IEEE1275_COMMON (&args.common, "interpret", 2, 2);
  args.len = len;
  args.method = (grub_ieee1275_cell_t) "alloc-mem";

  if (IEEE1275_CALL_ENTRY_FN (&args) == -1 || args.catch)
    {
      grub_error (GRUB_ERR_INVALID_COMMAND, N_("alloc-mem failed"));
      return NULL;
    }
  else
    return (void *)args.result;
}

/* Free memory allocated by alloc-mem */
static grub_err_t
grub_ieee1275_free_mem (void *addr, grub_size_t len)
{
  struct free_args
  {
    struct grub_ieee1275_common_hdr common;
    grub_ieee1275_cell_t method;
    grub_ieee1275_cell_t len;
    grub_ieee1275_cell_t addr;
    grub_ieee1275_cell_t catch;
  }
  args;

  INIT_IEEE1275_COMMON (&args.common, "interpret", 3, 1);
  args.addr = (grub_ieee1275_cell_t)addr;
  args.len = len;
  args.method = (grub_ieee1275_cell_t) "free-mem";

  if (IEEE1275_CALL_ENTRY_FN(&args) == -1 || args.catch)
    {
      grub_error (GRUB_ERR_INVALID_COMMAND, N_("free-mem failed"));
      return grub_errno;
    }

  return GRUB_ERR_NONE;
}

static void *
ofnet_alloc_netbuf (grub_size_t len)
{
  if (grub_ieee1275_test_flag (GRUB_IEEE1275_FLAG_VIRT_TO_REAL_BROKEN))
    return grub_ieee1275_alloc_mem (len);
  else
    return grub_zalloc (len);
}

static void
ofnet_free_netbuf (void *addr, grub_size_t len)
{
  if (grub_ieee1275_test_flag (GRUB_IEEE1275_FLAG_VIRT_TO_REAL_BROKEN))
    grub_ieee1275_free_mem (addr, len);
  else
    grub_free (addr);
}

static int
search_net_devices (struct grub_ieee1275_devalias *alias)
{
  struct grub_ofnetcard_data *ofdata;
  struct grub_net_card *card;
  grub_ieee1275_phandle_t devhandle;
  grub_net_link_level_address_t lla;
  grub_ssize_t prop_size;
  grub_uint64_t prop;
  grub_uint8_t *pprop;
  char *shortname;
  char need_suffix = 1;

  if (grub_strcmp (alias->type, "network") != 0)
    return 0;

  ofdata = grub_malloc (sizeof (struct grub_ofnetcard_data));
  if (!ofdata)
    {
      grub_print_error ();
      return 1;
    }
  card = grub_zalloc (sizeof (struct grub_net_card));
  if (!card)
    {
      grub_free (ofdata);
      grub_print_error ();
      return 1;
    }

#define SUFFIX ":speed=auto,duplex=auto,1.1.1.1,dummy,1.1.1.1,1.1.1.1,5,5,1.1.1.1,512"

  if (grub_ieee1275_test_flag (GRUB_IEEE1275_FLAG_NO_OFNET_SUFFIX))
    need_suffix = 0;

  /* sun4v vnet devices do not support setting duplex/speed */
  {
    char *ptr;

    grub_ieee1275_finddevice (alias->path, &devhandle);

    grub_ieee1275_get_property_length (devhandle, "compatible", &prop_size);
    if (prop_size > 0)
      {
	pprop = grub_malloc (prop_size);
	if (!pprop)
	  {
	    grub_free (card);
	    grub_free (ofdata);
	    grub_print_error ();
	    return 1;
	  }

	if (!grub_ieee1275_get_property (devhandle, "compatible",
					 pprop, prop_size, NULL))
	  {
	    for (ptr = (char *) pprop; ptr - (char *) pprop < prop_size;
		 ptr += grub_strlen (ptr) + 1)
	      {
		if (!grub_strcmp(ptr, "SUNW,sun4v-network"))
		  need_suffix = 0;
	      }
	}

	grub_free (pprop);
      }
  }

  if (need_suffix)
    ofdata->path = grub_malloc (grub_strlen (alias->path) + sizeof (SUFFIX));
  else
    ofdata->path = grub_malloc (grub_strlen (alias->path) + 1);
  if (!ofdata->path)
    {
      grub_print_error ();
      return 0;
    }
  ofdata->suffix = grub_stpcpy (ofdata->path, alias->path);
  if (need_suffix)
    grub_memcpy (ofdata->suffix, SUFFIX, sizeof (SUFFIX));
  else
    *ofdata->suffix = '\0';

  grub_ieee1275_finddevice (ofdata->path, &devhandle);

  {
    grub_uint32_t t;
    if (grub_ieee1275_get_integer_property (devhandle,
					    "max-frame-size", &t,
					    sizeof (t), 0))
      card->mtu = 1500;
    else
      card->mtu = t;
  }

  pprop = (grub_uint8_t *) &prop;
  if (grub_ieee1275_get_property (devhandle, "mac-address",
				  pprop, sizeof(prop), &prop_size)
      && grub_ieee1275_get_property (devhandle, "local-mac-address",
				     pprop, sizeof(prop), &prop_size))
    {
      grub_error (GRUB_ERR_IO, "Couldn't retrieve mac address.");
      grub_print_error ();
      return 0;
    }

  if (prop_size == 8)
    grub_memcpy (&lla.mac, pprop+2, 6);
  else
    grub_memcpy (&lla.mac, pprop, 6);

  lla.type = GRUB_NET_LINK_LEVEL_PROTOCOL_ETHERNET;
  lla.len = 6;
  card->default_address = lla;

  card->txbufsize = ALIGN_UP (card->mtu, 64) + 256;
  card->rcvbufsize = ALIGN_UP (card->mtu, 64) + 256;

  card->txbuf = ofnet_alloc_netbuf (card->txbufsize);
  if (!card->txbuf)
    goto fail_netbuf;

  card->rcvbuf = ofnet_alloc_netbuf (card->rcvbufsize);
  if (!card->rcvbuf)
    {
      grub_error_push ();
      ofnet_free_netbuf (card->txbuf, card->txbufsize);
      grub_error_pop ();
      goto fail_netbuf;
    }
  card->driver = NULL;
  card->data = ofdata;
  card->flags = 0;
  shortname = grub_ieee1275_get_devname (alias->path);
  card->name = grub_xasprintf ("ofnet_%s", shortname ? : alias->path);
  card->idle_poll_delay_ms = 10;
  grub_free (shortname);

  card->driver = &ofdriver;
  grub_net_card_register (card);
  return 0;

fail_netbuf:
  grub_free (ofdata->path);
  grub_free (ofdata);
  grub_free (card);
  grub_print_error ();
  return 0;
}

static void
grub_ofnet_findcards (void)
{
  /* Look at all nodes for devices of the type network.  */
  grub_ieee1275_devices_iterate (search_net_devices);
}

GRUB_MOD_INIT(ofnet)
{
  grub_ofnet_findcards ();
  grub_ieee1275_net_config = grub_ieee1275_net_config_real;
}

GRUB_MOD_FINI(ofnet)
{
  struct grub_net_card *card, *next;

  FOR_NET_CARDS_SAFE (card, next) 
    if (card->driver && grub_strcmp (card->driver->name, "ofnet") == 0)
      grub_net_card_unregister (card);
  grub_ieee1275_net_config = 0;
}
