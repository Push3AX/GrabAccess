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

#include <grub/net/arp.h>
#include <grub/net/netbuff.h>
#include <grub/mm.h>
#include <grub/net.h>
#include <grub/net/ethernet.h>
#include <grub/net/ip.h>
#include <grub/time.h>

/* ARP header operation codes */
enum
  {
    ARP_REQUEST = 1,
    ARP_REPLY = 2
  };

struct arphdr {
  grub_uint16_t hrd;
  grub_uint16_t pro;
  grub_uint8_t hln;
  grub_uint8_t pln;
  grub_uint16_t op;
} GRUB_PACKED;

static int have_pending;
static grub_uint32_t pending_req;

grub_err_t
grub_net_arp_send_request (struct grub_net_network_level_interface *inf,
			   const grub_net_network_level_address_t *proto_addr)
{
  struct grub_net_buff nb;
  struct arphdr *arp_header;
  grub_net_link_level_address_t target_mac_addr;
  grub_err_t err;
  int i;
  grub_uint8_t *nbd;
  grub_uint8_t arp_data[128];
  grub_uint8_t hln;
  grub_uint8_t pln;
  grub_uint8_t arp_packet_len;
  grub_uint8_t *tmp_ptr;

  if (proto_addr->type != GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4)
    return grub_error (GRUB_ERR_BUG, "unsupported address family");

  /* Build a request packet.  */
  nb.head = arp_data;
  nb.end = arp_data + sizeof (arp_data);
  grub_netbuff_clear (&nb);
  grub_netbuff_reserve (&nb, 128);

  hln = inf->card->default_address.len;
  pln = sizeof (proto_addr->ipv4);
  arp_packet_len = sizeof (*arp_header) + 2 * (hln + pln);

  err = grub_netbuff_push (&nb, arp_packet_len);
  if (err)
    return err;

  arp_header = (struct arphdr *) nb.data;
  arp_header->hrd = grub_cpu_to_be16 (inf->card->default_address.type);
  arp_header->hln = hln;
  arp_header->pro = grub_cpu_to_be16_compile_time (GRUB_NET_ETHERTYPE_IP);
  arp_header->pln = pln;
  arp_header->op = grub_cpu_to_be16_compile_time (ARP_REQUEST);
  tmp_ptr = nb.data + sizeof (*arp_header);

  /* The source hardware address. */
  grub_memcpy (tmp_ptr, inf->hwaddress.mac, hln);
  tmp_ptr += hln;

  /* The source protocol address. */
  grub_memcpy (tmp_ptr, &inf->address.ipv4, pln);
  tmp_ptr += pln;

  /* The target hardware address. */
  grub_memset (tmp_ptr, 0, hln);
  tmp_ptr += hln;

  /* The target protocol address */
  grub_memcpy (tmp_ptr, &proto_addr->ipv4, pln);
  tmp_ptr += pln;

  grub_memset (&target_mac_addr.mac, 0xff, hln);

  nbd = nb.data;
  send_ethernet_packet (inf, &nb, target_mac_addr, GRUB_NET_ETHERTYPE_ARP);
  for (i = 0; i < GRUB_NET_TRIES; i++)
    {
      if (grub_net_link_layer_resolve_check (inf, proto_addr))
	return GRUB_ERR_NONE;
      pending_req = proto_addr->ipv4;
      have_pending = 0;
      grub_net_poll_cards (GRUB_NET_INTERVAL + (i * GRUB_NET_INTERVAL_ADDITION),
                           &have_pending);
      if (grub_net_link_layer_resolve_check (inf, proto_addr))
	return GRUB_ERR_NONE;
      nb.data = nbd;
      send_ethernet_packet (inf, &nb, target_mac_addr, GRUB_NET_ETHERTYPE_ARP);
    }

  return GRUB_ERR_NONE;
}

grub_err_t
grub_net_arp_receive (struct grub_net_buff *nb, struct grub_net_card *card,
                      grub_uint16_t *vlantag)
{
  struct arphdr *arp_header = (struct arphdr *) nb->data;
  grub_net_network_level_address_t sender_addr, target_addr;
  grub_net_link_level_address_t sender_mac_addr;
  struct grub_net_network_level_interface *inf;
  grub_uint16_t hw_type;
  grub_uint8_t hln;
  grub_uint8_t pln;
  grub_uint8_t arp_packet_len;
  grub_uint8_t *tmp_ptr;

  hw_type = card->default_address.type;
  hln = card->default_address.len;
  pln = sizeof(sender_addr.ipv4);
  arp_packet_len = sizeof (*arp_header) + 2 * (pln + hln);

  if (arp_header->pro != grub_cpu_to_be16_compile_time (GRUB_NET_ETHERTYPE_IP)
      || arp_header->hrd != grub_cpu_to_be16 (hw_type)
      || arp_header->hln != hln || arp_header->pln != pln
      || nb->tail - nb->data < (int) arp_packet_len) {
    return GRUB_ERR_NONE;
  }

  tmp_ptr =  nb->data + sizeof (*arp_header);

  /* The source hardware address. */
  sender_mac_addr.type = hw_type;
  sender_mac_addr.len = hln;
  grub_memcpy (sender_mac_addr.mac, tmp_ptr, hln);
  tmp_ptr += hln;

  /* The source protocol address. */
  sender_addr.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4;
  grub_memcpy(&sender_addr.ipv4, tmp_ptr, pln);
  tmp_ptr += pln;

  grub_net_link_layer_add_address (card, &sender_addr, &sender_mac_addr, 1);

  /* The target hardware address. */
  tmp_ptr += hln;

  /* The target protocol address. */
  target_addr.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4;
  grub_memcpy(&target_addr.ipv4, tmp_ptr, pln);

  if (sender_addr.ipv4 == pending_req)
    have_pending = 1;

  FOR_NET_NETWORK_LEVEL_INTERFACES (inf)
  {
    /* Verify vlantag id */
    if (inf->card == card && inf->vlantag != *vlantag)
      {
        grub_dprintf ("net", "invalid vlantag! %x != %x\n",
                      inf->vlantag, *vlantag);
        break;
      }

    /* Am I the protocol address target? */
    if (grub_net_addr_cmp (&inf->address, &target_addr) == 0
	&& arp_header->op == grub_cpu_to_be16_compile_time (ARP_REQUEST))
      {
	grub_net_link_level_address_t target;
	struct grub_net_buff nb_reply;
	struct arphdr *arp_reply;
	grub_uint8_t arp_data[128];
	grub_err_t err;

	nb_reply.head = arp_data;
	nb_reply.end = arp_data + sizeof (arp_data);
	grub_netbuff_clear (&nb_reply);
	grub_netbuff_reserve (&nb_reply, 128);

	err = grub_netbuff_push (&nb_reply, arp_packet_len);
	if (err)
	  return err;

	arp_reply = (struct arphdr *) nb_reply.data;

	arp_reply->hrd = grub_cpu_to_be16 (hw_type);
	arp_reply->pro = grub_cpu_to_be16_compile_time (GRUB_NET_ETHERTYPE_IP);
	arp_reply->pln = pln;
	arp_reply->hln = hln;
	arp_reply->op = grub_cpu_to_be16_compile_time (ARP_REPLY);

	tmp_ptr = nb_reply.data + sizeof (*arp_reply);

	/* The source hardware address. */
	grub_memcpy (tmp_ptr, inf->hwaddress.mac, hln);
	tmp_ptr += hln;

	/* The source protocol address. */
	grub_memcpy (tmp_ptr, &target_addr.ipv4, pln);
	tmp_ptr += pln;

	/* The target hardware address. */
	grub_memcpy (tmp_ptr, sender_mac_addr.mac, hln);
	tmp_ptr += hln;

	/* The target protocol address */
	grub_memcpy (tmp_ptr, &sender_addr.ipv4, pln);
	tmp_ptr += pln;

	target.type = hw_type;
	target.len = hln;
	grub_memcpy (target.mac, sender_mac_addr.mac, hln);

	/* Change operation to REPLY and send packet */
	send_ethernet_packet (inf, &nb_reply, target, GRUB_NET_ETHERTYPE_ARP);
      }
  }
  return GRUB_ERR_NONE;
}
