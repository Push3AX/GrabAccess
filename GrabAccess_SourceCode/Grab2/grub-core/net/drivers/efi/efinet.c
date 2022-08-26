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
#include <grub/dl.h>
#include <grub/env.h>
#include <grub/net.h>
#include <grub/net/url.h>
#include <grub/time.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/i18n.h>
#include <grub/lib/hexdump.h>
#include <grub/types.h>
#include <grub/net/netbuff.h>
#include <grub/env.h>

GRUB_MOD_LICENSE ("GPLv3+");

/* GUID.  */
static grub_efi_guid_t net_io_guid = GRUB_EFI_SIMPLE_NETWORK_GUID;
static grub_efi_guid_t pxe_io_guid = GRUB_EFI_PXE_GUID;
static grub_efi_guid_t ip4_config_guid = GRUB_EFI_IP4_CONFIG2_PROTOCOL_GUID;
static grub_efi_guid_t ip6_config_guid = GRUB_EFI_IP6_CONFIG_PROTOCOL_GUID;

static grub_err_t
send_card_buffer (struct grub_net_card *dev,
		  struct grub_net_buff *pack)
{
  grub_efi_status_t st;
  grub_efi_simple_network_t *net = dev->efi_net;
  grub_uint64_t limit_time = grub_get_time_ms () + 4000;
  void *txbuf;

  if (net == NULL)
    return grub_error (GRUB_ERR_IO,
		       N_("network protocol not available, can't send packet"));
  if (dev->txbusy)
    while (1)
      {
	txbuf = NULL;
	st = efi_call_3 (net->get_status, net, 0, &txbuf);
	if (st != GRUB_EFI_SUCCESS)
	  return grub_error (GRUB_ERR_IO,
			     N_("couldn't send network packet"));
	/*
	   Some buggy firmware could return an arbitrary address instead of the
	   txbuf address we trasmitted, so just check that txbuf is non NULL
	   for success.  This is ok because we open the SNP protocol in
	   exclusive mode so we know we're the only ones transmitting on this
	   box and since we only transmit one packet at a time we know our
	   transmit was successfull.
	 */
	if (txbuf)
	  {
	    dev->txbusy = 0;
	    break;
	  }
	if (limit_time < grub_get_time_ms ())
	  return grub_error (GRUB_ERR_TIMEOUT,
			     N_("couldn't send network packet"));
      }

  dev->last_pkt_size = (pack->tail - pack->data);
  if (dev->last_pkt_size > dev->mtu)
    dev->last_pkt_size = dev->mtu;

  grub_memcpy (dev->txbuf, pack->data, dev->last_pkt_size);

  st = efi_call_7 (net->transmit, net, 0, dev->last_pkt_size,
		   dev->txbuf, NULL, NULL, NULL);
  if (st != GRUB_EFI_SUCCESS)
    return grub_error (GRUB_ERR_IO, N_("couldn't send network packet"));

  /*
     The card may have sent out the packet immediately - set txbusy
     to 0 in this case.
     Cases were observed where checking txbuf at the next call
     of send_card_buffer() is too late: 0 is returned in txbuf and
     we run in the GRUB_ERR_TIMEOUT case above.
     Perhaps a timeout in the FW has discarded the recycle buffer.
   */
  txbuf = NULL;
  st = efi_call_3 (net->get_status, net, 0, &txbuf);
  dev->txbusy = !(st == GRUB_EFI_SUCCESS && txbuf);

  return GRUB_ERR_NONE;
}

static struct grub_net_buff *
get_card_packet (struct grub_net_card *dev)
{
  grub_efi_simple_network_t *net = dev->efi_net;
  grub_err_t err;
  grub_efi_status_t st;
  grub_efi_uintn_t bufsize = dev->rcvbufsize;
  struct grub_net_buff *nb;
  int i;

  if (net == NULL)
    return NULL;

  for (i = 0; i < 2; i++)
    {
      if (!dev->rcvbuf)
	dev->rcvbuf = grub_malloc (dev->rcvbufsize);
      if (!dev->rcvbuf)
	return NULL;

      st = efi_call_7 (net->receive, net, NULL, &bufsize,
		       dev->rcvbuf, NULL, NULL, NULL);
      if (st != GRUB_EFI_BUFFER_TOO_SMALL)
	break;
      dev->rcvbufsize = 2 * ALIGN_UP (dev->rcvbufsize > bufsize
				      ? dev->rcvbufsize : bufsize, 64);
      grub_free (dev->rcvbuf);
      dev->rcvbuf = 0;
    }

  if (st != GRUB_EFI_SUCCESS)
    return NULL;

  nb = grub_netbuff_alloc (bufsize + 2);
  if (!nb)
    return NULL;

  /* Reserve 2 bytes so that 2 + 14/18 bytes of ethernet header is divisible
     by 4. So that IP header is aligned on 4 bytes. */
  if (grub_netbuff_reserve (nb, 2))
    {
      grub_netbuff_free (nb);
      return NULL;
    }
  grub_memcpy (nb->data, dev->rcvbuf, bufsize);
  err = grub_netbuff_put (nb, bufsize);
  if (err)
    {
      grub_netbuff_free (nb);
      return NULL;
    }

  return nb;
}

static grub_err_t
open_card (struct grub_net_card *dev)
{
  grub_efi_simple_network_t *net;

  if (dev->efi_net != NULL)
    {
      grub_efi_close_protocol (dev->efi_handle, &net_io_guid);
      dev->efi_net = NULL;
    }
  /*
   * Try to reopen SNP exlusively to close any active MNP protocol instance
   * that may compete for packet polling.
   */
  net = grub_efi_open_protocol (dev->efi_handle, &net_io_guid,
				GRUB_EFI_OPEN_PROTOCOL_BY_EXCLUSIVE);
  if (net != NULL)
    {
      if (net->mode->state == GRUB_EFI_NETWORK_STOPPED
	  && efi_call_1 (net->start, net) != GRUB_EFI_SUCCESS)
	return grub_error (GRUB_ERR_NET_NO_CARD, "%s: net start failed",
			   dev->name);

      if (net->mode->state == GRUB_EFI_NETWORK_STOPPED)
	return grub_error (GRUB_ERR_NET_NO_CARD, "%s: card stopped",
			   dev->name);

      if (net->mode->state == GRUB_EFI_NETWORK_STARTED
	  && efi_call_3 (net->initialize, net, 0, 0) != GRUB_EFI_SUCCESS)
	return grub_error (GRUB_ERR_NET_NO_CARD, "%s: net initialize failed",
			   dev->name);

      /* Enable hardware receive filters if driver declares support for it.
	 We need unicast and broadcast and additionaly all nodes and
	 solicited multicast for IPv6. Solicited multicast is per-IPv6
	 address and we currently do not have API to do it so simply
	 try to enable receive of all multicast packets or evertyhing in
	 the worst case (i386 PXE driver always enables promiscuous too).

	 This does trust firmware to do what it claims to do.
       */
      if (net->mode->receive_filter_mask)
	{
	  grub_uint32_t filters = GRUB_EFI_SIMPLE_NETWORK_RECEIVE_UNICAST   |
				  GRUB_EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST |
				  GRUB_EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS_MULTICAST;

	  filters &= net->mode->receive_filter_mask;
	  if (!(filters & GRUB_EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS_MULTICAST))
	    filters |= (net->mode->receive_filter_mask &
			GRUB_EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS);

	  efi_call_6 (net->receive_filters, net, filters, 0, 0, 0, NULL);
	}

      dev->efi_net = net;
    } else {
      return grub_error (GRUB_ERR_NET_NO_CARD, "%s: can't open protocol",
			 dev->name);
    }

  return GRUB_ERR_NONE;
}

static void
close_card (struct grub_net_card *dev)
{
  efi_call_1 (dev->efi_net->shutdown, dev->efi_net);
  efi_call_1 (dev->efi_net->stop, dev->efi_net);
  grub_efi_close_protocol (dev->efi_handle, &net_io_guid);
}

static struct grub_net_card_driver efidriver =
  {
    .name = "efinet",
    .open = open_card,
    .close = close_card,
    .send = send_card_buffer,
    .recv = get_card_packet
  };

grub_efi_handle_t
grub_efinet_get_device_handle (struct grub_net_card *card)
{
  if (!card || card->driver != &efidriver)
    return 0;
  return card->efi_handle;
}

static void
grub_efinet_findcards (void)
{
  grub_efi_uintn_t num_handles;
  grub_efi_handle_t *handles;
  grub_efi_handle_t *handle;
  int i = 0;

  /* Find handles which support the disk io interface.  */
  handles = grub_efi_locate_handle (GRUB_EFI_BY_PROTOCOL, &net_io_guid,
				    0, &num_handles);
  if (! handles)
    return;
  for (handle = handles; num_handles--; handle++)
    {
      grub_efi_simple_network_t *net;
      struct grub_net_card *card;
      grub_efi_device_path_t *dp, *parent = NULL, *child = NULL;

      /* EDK2 UEFI PXE driver creates IPv4 and IPv6 messaging devices as
	 children of main MAC messaging device. We only need one device with
	 bound SNP per physical card, otherwise they compete with each other
	 when polling for incoming packets.
       */
      dp = grub_efi_get_device_path (*handle);
      if (!dp)
	continue;
      for (; ! GRUB_EFI_END_ENTIRE_DEVICE_PATH (dp); dp = GRUB_EFI_NEXT_DEVICE_PATH (dp))
	{
	  parent = child;
	  child = dp;
	}
      if (child
	  && GRUB_EFI_DEVICE_PATH_TYPE (child) == GRUB_EFI_MESSAGING_DEVICE_PATH_TYPE
	  && (GRUB_EFI_DEVICE_PATH_SUBTYPE (child) == GRUB_EFI_IPV4_DEVICE_PATH_SUBTYPE
	      || GRUB_EFI_DEVICE_PATH_SUBTYPE (child) == GRUB_EFI_IPV6_DEVICE_PATH_SUBTYPE)
	  && parent
	  && GRUB_EFI_DEVICE_PATH_TYPE (parent) == GRUB_EFI_MESSAGING_DEVICE_PATH_TYPE
	  && GRUB_EFI_DEVICE_PATH_SUBTYPE (parent) == GRUB_EFI_MAC_ADDRESS_DEVICE_PATH_SUBTYPE)
	continue;

      net = grub_efi_open_protocol (*handle, &net_io_guid,
				    GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
      if (! net)
	/* This should not happen... Why?  */
	continue;

      if (net->mode->hwaddr_size > GRUB_NET_MAX_LINK_ADDRESS_SIZE)
	continue;

      if (net->mode->state == GRUB_EFI_NETWORK_STOPPED
	  && efi_call_1 (net->start, net) != GRUB_EFI_SUCCESS)
	continue;

      if (net->mode->state == GRUB_EFI_NETWORK_STOPPED)
	continue;

      if (net->mode->state == GRUB_EFI_NETWORK_STARTED
	  && efi_call_3 (net->initialize, net, 0, 0) != GRUB_EFI_SUCCESS)
	continue;

      card = grub_zalloc (sizeof (struct grub_net_card));
      if (!card)
	{
	  grub_print_error ();
	  grub_free (handles);
	  return;
	}

      card->mtu = net->mode->max_packet_size;
      card->txbufsize = ALIGN_UP (card->mtu, 64) + 256;
      card->txbuf = grub_zalloc (card->txbufsize);
      if (!card->txbuf)
	{
	  grub_print_error ();
	  grub_free (handles);
	  grub_free (card);
	  return;
	}
      card->txbusy = 0;

      card->rcvbufsize = ALIGN_UP (card->mtu, 64) + 256;

      card->name = grub_xasprintf ("efinet%d", i++);
      card->driver = &efidriver;
      card->flags = 0;
      card->default_address.type = net->mode->if_type;
      card->default_address.len = net->mode->hwaddr_size;
      grub_memcpy (card->default_address.mac,
		   net->mode->current_address,
		   net->mode->hwaddr_size);
      card->efi_net = net;
      card->efi_handle = *handle;

      grub_net_card_register (card);
    }
  grub_free (handles);
}

static grub_efi_handle_t
grub_efi_locate_device_path (grub_efi_guid_t *protocol, grub_efi_device_path_t *device_path,
			    grub_efi_device_path_t **r_device_path)
{
  grub_efi_handle_t handle;
  grub_efi_status_t status;

  status = efi_call_3 (grub_efi_system_table->boot_services->locate_device_path,
		      protocol, &device_path, &handle);

  if (status != GRUB_EFI_SUCCESS)
    return 0;

  if (r_device_path)
    *r_device_path = device_path;

  return handle;
}

static grub_efi_ipv4_address_t *
grub_dns_server_ip4_address (grub_efi_device_path_t *dp, grub_efi_uintn_t *num_dns)
{
  grub_efi_handle_t hnd;
  grub_efi_status_t status;
  grub_efi_ip4_config2_protocol_t *conf;
  grub_efi_ipv4_address_t *addrs;
  grub_efi_uintn_t data_size = 1 * sizeof (grub_efi_ipv4_address_t);

  hnd = grub_efi_locate_device_path (&ip4_config_guid, dp, NULL);

  if (!hnd)
    return 0;

  conf = grub_efi_open_protocol (hnd, &ip4_config_guid,
				GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);

  if (!conf)
    return 0;

  addrs  = grub_malloc (data_size);
  if (!addrs)
    return 0;

  status = efi_call_4 (conf->get_data, conf,
		      GRUB_EFI_IP4_CONFIG2_DATA_TYPE_DNSSERVER,
		      &data_size, addrs);

  if (status == GRUB_EFI_BUFFER_TOO_SMALL)
    {
      grub_free (addrs);
      addrs  = grub_malloc (data_size);
      if (!addrs)
	return 0;

      status = efi_call_4 (conf->get_data,  conf,
			  GRUB_EFI_IP4_CONFIG2_DATA_TYPE_DNSSERVER,
			  &data_size, addrs);
    }

  if (status != GRUB_EFI_SUCCESS)
    {
      grub_free (addrs);
      return 0;
    }

  *num_dns = data_size / sizeof (grub_efi_ipv4_address_t);
  return addrs;
}

static grub_efi_ipv6_address_t *
grub_dns_server_ip6_address (grub_efi_device_path_t *dp, grub_efi_uintn_t *num_dns)
{
  grub_efi_handle_t hnd;
  grub_efi_status_t status;
  grub_efi_ip6_config_protocol_t *conf;
  grub_efi_ipv6_address_t *addrs;
  grub_efi_uintn_t data_size = 1 * sizeof (grub_efi_ipv6_address_t);

  hnd = grub_efi_locate_device_path (&ip6_config_guid, dp, NULL);

  if (!hnd)
    return 0;

  conf = grub_efi_open_protocol (hnd, &ip6_config_guid,
				GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);

  if (!conf)
    return 0;

  addrs  = grub_malloc (data_size);
  if (!addrs)
    return 0;

  status = efi_call_4 (conf->get_data, conf,
		      GRUB_EFI_IP6_CONFIG_DATA_TYPE_DNSSERVER,
		      &data_size, addrs);

  if (status == GRUB_EFI_BUFFER_TOO_SMALL)
    {
      grub_free (addrs);
      addrs  = grub_malloc (data_size);
      if (!addrs)
	return 0;

      status = efi_call_4 (conf->get_data,  conf,
			  GRUB_EFI_IP6_CONFIG_DATA_TYPE_DNSSERVER,
			  &data_size, addrs);
    }

  if (status != GRUB_EFI_SUCCESS)
    {
      grub_free (addrs);
      return 0;
    }

  *num_dns = data_size / sizeof (grub_efi_ipv6_address_t);
  return addrs;
}

static struct grub_net_buff *
grub_efinet_create_dhcp_ack_from_device_path (grub_efi_device_path_t *dp, int *use_ipv6)
{
  grub_efi_uint16_t uri_len;
  grub_efi_device_path_t *ldp, *ddp;
  grub_efi_uri_device_path_t *uri_dp;
  struct grub_net_buff *nb;
  grub_err_t err;

  ddp = grub_efi_duplicate_device_path (dp);
  if (!ddp)
    return NULL;

  ldp = grub_efi_find_last_device_path (ddp);

  if (GRUB_EFI_DEVICE_PATH_TYPE (ldp) != GRUB_EFI_MESSAGING_DEVICE_PATH_TYPE
      || GRUB_EFI_DEVICE_PATH_SUBTYPE (ldp) != GRUB_EFI_URI_DEVICE_PATH_SUBTYPE)
    {
      grub_free (ddp);
      return NULL;
    }

  uri_len = GRUB_EFI_DEVICE_PATH_LENGTH (ldp) > 4 ? GRUB_EFI_DEVICE_PATH_LENGTH (ldp) - 4  : 0;

  if (!uri_len)
    {
      grub_free (ddp);
      return NULL;
    }

  uri_dp = (grub_efi_uri_device_path_t *) ldp;

  ldp->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
  ldp->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
  ldp->length = sizeof (*ldp);

  ldp = grub_efi_find_last_device_path (ddp);

  /* Skip the DNS Device */
  if (GRUB_EFI_DEVICE_PATH_TYPE (ldp) == GRUB_EFI_MESSAGING_DEVICE_PATH_TYPE
      && GRUB_EFI_DEVICE_PATH_SUBTYPE (ldp) == GRUB_EFI_DNS_DEVICE_PATH_SUBTYPE)
    {
      ldp->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
      ldp->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
      ldp->length = sizeof (*ldp);

      ldp = grub_efi_find_last_device_path (ddp);
    }

  if (GRUB_EFI_DEVICE_PATH_TYPE (ldp) != GRUB_EFI_MESSAGING_DEVICE_PATH_TYPE
      || (GRUB_EFI_DEVICE_PATH_SUBTYPE (ldp) != GRUB_EFI_IPV4_DEVICE_PATH_SUBTYPE
          && GRUB_EFI_DEVICE_PATH_SUBTYPE (ldp) != GRUB_EFI_IPV6_DEVICE_PATH_SUBTYPE))
    {
      grub_free (ddp);
      return NULL;
    }

  nb = grub_netbuff_alloc (512);
  if (!nb)
    {
      grub_free (ddp);
      return NULL;
    }

  if (GRUB_EFI_DEVICE_PATH_SUBTYPE (ldp) == GRUB_EFI_IPV4_DEVICE_PATH_SUBTYPE)
    {
      grub_efi_ipv4_device_path_t *ipv4 = (grub_efi_ipv4_device_path_t *) ldp;
      struct grub_net_bootp_packet *bp;
      grub_uint8_t *ptr;
      grub_efi_ipv4_address_t *dns;
      grub_efi_uintn_t num_dns;

      bp = (struct grub_net_bootp_packet *) nb->tail;
      err = grub_netbuff_put (nb, sizeof (*bp) + 4);
      if (err)
	{
	  grub_free (ddp);
	  grub_netbuff_free (nb);
	  return NULL;
	}

      if (sizeof(bp->boot_file) < uri_len)
	{
	  grub_free (ddp);
	  grub_netbuff_free (nb);
	  return NULL;
	}
      grub_memcpy (bp->boot_file, uri_dp->uri, uri_len);
      grub_memcpy (&bp->your_ip, ipv4->local_ip_address, sizeof (bp->your_ip));
      grub_memcpy (&bp->server_ip, ipv4->remote_ip_address, sizeof (bp->server_ip));

      bp->vendor[0] = GRUB_NET_BOOTP_RFC1048_MAGIC_0;
      bp->vendor[1] = GRUB_NET_BOOTP_RFC1048_MAGIC_1;
      bp->vendor[2] = GRUB_NET_BOOTP_RFC1048_MAGIC_2;
      bp->vendor[3] = GRUB_NET_BOOTP_RFC1048_MAGIC_3;

      ptr = nb->tail;
      err = grub_netbuff_put (nb, sizeof (ipv4->subnet_mask) + 2);
      if (err)
	{
	  grub_free (ddp);
	  grub_netbuff_free (nb);
	  return NULL;
	}
      *ptr++ = GRUB_NET_BOOTP_NETMASK;
      *ptr++ = sizeof (ipv4->subnet_mask);
      grub_memcpy (ptr, ipv4->subnet_mask, sizeof (ipv4->subnet_mask));

      ptr = nb->tail;
      err = grub_netbuff_put (nb, sizeof (ipv4->gateway_ip_address) + 2);
      if (err)
	{
	  grub_free (ddp);
	  grub_netbuff_free (nb);
	  return NULL;
	}
      *ptr++ = GRUB_NET_BOOTP_ROUTER;
      *ptr++ = sizeof (ipv4->gateway_ip_address);
      grub_memcpy (ptr, ipv4->gateway_ip_address, sizeof (ipv4->gateway_ip_address));

      ptr = nb->tail;
      err = grub_netbuff_put (nb, sizeof ("HTTPClient") + 1);
      if (err)
	{
	  grub_free (ddp);
	  grub_netbuff_free (nb);
	  return NULL;
	}
      *ptr++ = GRUB_NET_BOOTP_VENDOR_CLASS_IDENTIFIER;
      *ptr++ = sizeof ("HTTPClient") - 1;
      grub_memcpy (ptr, "HTTPClient", sizeof ("HTTPClient") - 1);

      dns = grub_dns_server_ip4_address (dp, &num_dns);
      if (dns)
	{
	  grub_efi_uintn_t size_dns = sizeof (*dns) * num_dns;

	  ptr = nb->tail;
	  err = grub_netbuff_put (nb, size_dns + 2);
	  if (err)
	    {
	      grub_free (ddp);
	      grub_netbuff_free (nb);
	      return NULL;
	    }
	  *ptr++ = GRUB_NET_BOOTP_DNS;
	  *ptr++ = size_dns;
	  grub_memcpy (ptr, dns, size_dns);
	  grub_free (dns);
	}

      ptr = nb->tail;
      err = grub_netbuff_put (nb, 1);
      if (err)
	{
	  grub_free (ddp);
	  grub_netbuff_free (nb);
	  return NULL;
	}
      *ptr = GRUB_NET_BOOTP_END;
      *use_ipv6 = 0;

      ldp->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
      ldp->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
      ldp->length = sizeof (*ldp);
      ldp = grub_efi_find_last_device_path (ddp);

      if (GRUB_EFI_DEVICE_PATH_SUBTYPE (ldp) ==  GRUB_EFI_MAC_ADDRESS_DEVICE_PATH_SUBTYPE)
	{
	  grub_efi_mac_address_device_path_t *mac = (grub_efi_mac_address_device_path_t *) ldp;
	  bp->hw_type = mac->if_type;
	  bp->hw_len = sizeof (bp->mac_addr);
	  grub_memcpy (bp->mac_addr, mac->mac_address, bp->hw_len);
	}
    }
  else
    {
      grub_efi_ipv6_device_path_t *ipv6 = (grub_efi_ipv6_device_path_t *) ldp;

      struct grub_net_dhcp6_packet *d6p;
      struct grub_net_dhcp6_option *opt;
      struct grub_net_dhcp6_option_iana *iana;
      struct grub_net_dhcp6_option_iaaddr *iaaddr;
      grub_efi_ipv6_address_t *dns;
      grub_efi_uintn_t num_dns;

      d6p = (struct grub_net_dhcp6_packet *)nb->tail;
      err = grub_netbuff_put (nb, sizeof(*d6p));
      if (err)
	{
	  grub_free (ddp);
	  grub_netbuff_free (nb);
	  return NULL;
	}
      d6p->message_type = GRUB_NET_DHCP6_REPLY;

      opt = (struct grub_net_dhcp6_option *)nb->tail;
      err = grub_netbuff_put (nb, sizeof(*opt));
      if (err)
	{
	  grub_free (ddp);
	  grub_netbuff_free (nb);
	  return NULL;
	}
      opt->code = grub_cpu_to_be16_compile_time (GRUB_NET_DHCP6_OPTION_IA_NA);
      opt->len = grub_cpu_to_be16_compile_time (sizeof(*iana) + sizeof(*opt) + sizeof(*iaaddr));

      err = grub_netbuff_put (nb, sizeof(*iana));
      if (err)
	{
	  grub_free (ddp);
	  grub_netbuff_free (nb);
	  return NULL;
	}

      opt = (struct grub_net_dhcp6_option *)nb->tail;
      err = grub_netbuff_put (nb, sizeof(*opt));
      if (err)
	{
	  grub_free (ddp);
	  grub_netbuff_free (nb);
	  return NULL;
	}
      opt->code = grub_cpu_to_be16_compile_time (GRUB_NET_DHCP6_OPTION_IAADDR);
      opt->len = grub_cpu_to_be16_compile_time (sizeof (*iaaddr));

      iaaddr = (struct grub_net_dhcp6_option_iaaddr *)nb->tail;
      err = grub_netbuff_put (nb, sizeof(*iaaddr));
      if (err)
	{
	  grub_free (ddp);
	  grub_netbuff_free (nb);
	  return NULL;
	}
      grub_memcpy (iaaddr->addr, ipv6->local_ip_address, sizeof(ipv6->local_ip_address));

      opt = (struct grub_net_dhcp6_option *)nb->tail;
      err = grub_netbuff_put (nb, sizeof(*opt) + uri_len);
      if (err)
	{
	  grub_free (ddp);
	  grub_netbuff_free (nb);
	  return NULL;
	}
      opt->code = grub_cpu_to_be16_compile_time (GRUB_NET_DHCP6_OPTION_BOOTFILE_URL);
      opt->len = grub_cpu_to_be16 (uri_len);
      grub_memcpy (opt->data, uri_dp->uri, uri_len);

      dns = grub_dns_server_ip6_address (dp, &num_dns);
      if (dns)
	{
	  grub_efi_uintn_t size_dns = sizeof (*dns) * num_dns;

	  opt = (struct grub_net_dhcp6_option *)nb->tail;
	  err = grub_netbuff_put (nb, sizeof(*opt) + size_dns);
	  if (err)
	  {
	    grub_free (ddp);
	    grub_netbuff_free (nb);
	    return NULL;
	  }
	  opt->code = grub_cpu_to_be16_compile_time (GRUB_NET_DHCP6_OPTION_DNS_SERVERS);
	  opt->len = grub_cpu_to_be16 (size_dns);
	  grub_memcpy (opt->data, dns, size_dns);
	  grub_free (dns);
	}

      *use_ipv6 = 1;
    }

  grub_free (ddp);
  return nb;
}

static void
grub_efi_net_config_real (grub_efi_handle_t hnd, char **device,
			  char **path)
{
  struct grub_net_card *card;
  grub_efi_device_path_t *dp, *ldp = NULL;

  dp = grub_efi_get_device_path (hnd);
  if (! dp)
    return;

  FOR_NET_CARDS (card)
  {
    grub_efi_device_path_t *cdp;
    struct grub_efi_pxe *pxe;
    struct grub_efi_pxe_mode *pxe_mode = NULL;
    grub_uint8_t *packet_buf;
    grub_size_t packet_bufsz ;
    int ipv6;
    struct grub_net_buff *nb = NULL;

    if (card->driver != &efidriver)
      continue;

    cdp = grub_efi_get_device_path (card->efi_handle);
    if (! cdp)
      continue;

    ldp = grub_efi_find_last_device_path (dp);

    if (grub_efi_compare_device_paths (dp, cdp) != 0)
      {
	grub_efi_device_path_t *dup_dp, *dup_ldp;
	int match;

	/* EDK2 UEFI PXE driver creates pseudo devices with type IPv4/IPv6
	   as children of Ethernet card and binds PXE and Load File protocols
	   to it. Loaded Image Device Path protocol will point to these pseudo
	   devices. We skip them when enumerating cards, so here we need to
	   find matching MAC device.
         */
	if (GRUB_EFI_DEVICE_PATH_TYPE (ldp) != GRUB_EFI_MESSAGING_DEVICE_PATH_TYPE
	    || (GRUB_EFI_DEVICE_PATH_SUBTYPE (ldp) != GRUB_EFI_IPV4_DEVICE_PATH_SUBTYPE
		&& GRUB_EFI_DEVICE_PATH_SUBTYPE (ldp) != GRUB_EFI_IPV6_DEVICE_PATH_SUBTYPE
		&& GRUB_EFI_DEVICE_PATH_SUBTYPE (ldp) != GRUB_EFI_DNS_DEVICE_PATH_SUBTYPE
		&& GRUB_EFI_DEVICE_PATH_SUBTYPE (ldp) != GRUB_EFI_URI_DEVICE_PATH_SUBTYPE))
	  continue;
	dup_dp = grub_efi_duplicate_device_path (dp);
	if (!dup_dp)
	  continue;

	if (GRUB_EFI_DEVICE_PATH_SUBTYPE (ldp) == GRUB_EFI_URI_DEVICE_PATH_SUBTYPE)
	  {
	    dup_ldp = grub_efi_find_last_device_path (dup_dp);
	    dup_ldp->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
	    dup_ldp->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
	    dup_ldp->length = sizeof (*dup_ldp);
	  }

	dup_ldp = grub_efi_find_last_device_path (dup_dp);
	if (GRUB_EFI_DEVICE_PATH_SUBTYPE (dup_ldp) == GRUB_EFI_DNS_DEVICE_PATH_SUBTYPE)
	  {
	    dup_ldp = grub_efi_find_last_device_path (dup_dp);
	    dup_ldp->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
	    dup_ldp->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
	    dup_ldp->length = sizeof (*dup_ldp);
	  }

	dup_ldp = grub_efi_find_last_device_path (dup_dp);
	dup_ldp->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
	dup_ldp->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
	dup_ldp->length = sizeof (*dup_ldp);
	match = grub_efi_compare_device_paths (dup_dp, cdp) == 0;
	grub_free (dup_dp);
	if (!match)
	  continue;
      }

    pxe = grub_efi_open_protocol (hnd, &pxe_io_guid,
				  GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (!pxe)
      {
	nb = grub_efinet_create_dhcp_ack_from_device_path (dp, &ipv6);
	if (!nb)
	  {
	    grub_print_error ();
	    continue;
	  }
	packet_buf = nb->head;
	packet_bufsz = nb->tail - nb->head;
      }
    else
      {
	pxe_mode = pxe->mode;
	packet_buf = (grub_uint8_t *) &pxe_mode->dhcp_ack;
	packet_bufsz = sizeof (pxe_mode->dhcp_ack);
	ipv6 = pxe_mode->using_ipv6;
      }

    if (ipv6)
      {
	grub_dprintf ("efinet", "using ipv6 and dhcpv6\n");
	if (pxe_mode)
	  grub_dprintf ("efinet", "dhcp_ack_received: %s%s\n",
			pxe_mode->dhcp_ack_received ? "yes" : "no",
			pxe_mode->dhcp_ack_received ? "" : " cannot continue");

	grub_net_configure_by_dhcpv6_reply (card->name, card, 0,
					    (struct grub_net_dhcp6_packet *)
					    packet_buf,
					    packet_bufsz,
					    1, device, path);
	if (grub_errno)
	  grub_print_error ();
	if (device && path)
	  grub_dprintf ("efinet", "device: `%s' path: `%s'\n", *device, *path);
	if (grub_errno)
	  grub_print_error ();
      }
    else
      {
	grub_dprintf ("efinet", "using ipv4 and dhcp\n");

    struct grub_net_bootp_packet *dhcp_ack =
        (struct grub_net_bootp_packet *)&pxe_mode->dhcp_ack;

        if (pxe_mode->proxy_offer_received)
          {
            grub_dprintf ("efinet", "proxy offer receive");
            struct grub_net_bootp_packet *proxy_offer =
                (struct grub_net_bootp_packet *)&pxe_mode->proxy_offer;

            if (proxy_offer && dhcp_ack->boot_file[0] == '\0')
              {
                grub_dprintf ("efinet", "setting values from proxy offer");
                /* Here we got a proxy offer and the dhcp_ack has a nil boot_file
                 * Copy the proxy DHCP offer details into the bootp_packet we are
                 * sending forward as they are the deatils we need.
                 */
                grub_memcpy (dhcp_ack->server_name, proxy_offer->server_name, 64);
                grub_memcpy (dhcp_ack->boot_file, proxy_offer->boot_file, 128);
                dhcp_ack->server_ip = proxy_offer->server_ip;
              }
          }

	grub_net_configure_by_dhcp_ack (card->name, card, 0,
					(struct grub_net_bootp_packet *)
					&pxe_mode->dhcp_ack,
					sizeof (pxe_mode->dhcp_ack),
					1, device, path);
	grub_dprintf ("efinet", "device: `%s' path: `%s'\n", *device, *path);
      }

    if (nb)
      grub_netbuff_free (nb);

    return;
  }
}

GRUB_MOD_INIT(efinet)
{
  if (grub_efi_net_config)
    return;

  grub_efinet_findcards ();
  grub_efi_net_config = grub_efi_net_config_real;
}

GRUB_MOD_FINI(efinet)
{
  struct grub_net_card *card, *next;

  FOR_NET_CARDS_SAFE (card, next) 
    if (card->driver == &efidriver)
      grub_net_card_unregister (card);

  grub_efi_net_config = NULL;
}

