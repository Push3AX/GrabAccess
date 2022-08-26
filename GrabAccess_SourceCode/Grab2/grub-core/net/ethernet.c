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

#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/env.h>
#include <grub/net/ethernet.h>
#include <grub/net/ip.h>
#include <grub/net/arp.h>
#include <grub/net/netbuff.h>
#include <grub/net.h>
#include <grub/time.h>
#include <grub/net/arp.h>

#define LLCADDRMASK 0x7f

#pragma GCC diagnostic ignored "-Wcast-align"

struct llchdr
{
  grub_uint8_t dsap;
  grub_uint8_t ssap;
  grub_uint8_t ctrl;
} GRUB_PACKED;

struct snaphdr
{
  grub_uint8_t oui[3]; 
  grub_uint16_t type;
} GRUB_PACKED;

grub_err_t
send_ethernet_packet (struct grub_net_network_level_interface *inf,
		      struct grub_net_buff *nb,
		      grub_net_link_level_address_t target_addr,
		      grub_net_ethertype_t ethertype)
{
  grub_uint8_t *eth;
  grub_err_t err;
  grub_uint32_t vlantag = 0;
  grub_uint8_t hw_addr_len = inf->card->default_address.len;
  grub_uint8_t etherhdr_size = 2 * hw_addr_len + 2;

  /* Source and destination link addresses + ethertype + vlan tag */
  COMPILE_TIME_ASSERT ((GRUB_NET_MAX_LINK_ADDRESS_SIZE * 2 + 2 + 4) <
		       GRUB_NET_MAX_LINK_HEADER_SIZE);

  /* Increase ethernet header in case of vlantag */
  if (inf->vlantag != 0)
    etherhdr_size += 4;

  err = grub_netbuff_push (nb, etherhdr_size);
  if (err)
    return err;
  eth = nb->data;
  grub_memcpy (eth, target_addr.mac, hw_addr_len);
  eth += hw_addr_len;
  grub_memcpy (eth, inf->hwaddress.mac, hw_addr_len);
  eth += hw_addr_len;

  /* Check if a vlan-tag is present. */
  if (vlantag != 0)
    {
      *((grub_uint32_t *)eth) = grub_cpu_to_be32 (vlantag);
      eth += sizeof (vlantag);
    }

  /* Write ethertype */
  *((grub_uint16_t*) eth) = grub_cpu_to_be16 (ethertype);

  if (!inf->card->opened)
    {
      err = GRUB_ERR_NONE;
      if (inf->card->driver->open)
	err = inf->card->driver->open (inf->card);
      if (err)
	return err;
      inf->card->opened = 1;
    }

  return inf->card->driver->send (inf->card, nb);
}

grub_err_t
grub_net_recv_ethernet_packet (struct grub_net_buff *nb,
			       struct grub_net_card *card)
{
  grub_uint8_t *eth;
  struct llchdr *llch;
  struct snaphdr *snaph;
  grub_net_ethertype_t type;
  grub_net_link_level_address_t hwaddress;
  grub_net_link_level_address_t src_hwaddress;
  grub_err_t err;
  grub_uint8_t hw_addr_len = card->default_address.len;
  grub_uint8_t etherhdr_size = 2 * hw_addr_len + 2;
  grub_uint16_t vlantag = 0;

  eth = nb->data;

  hwaddress.type = card->default_address.type;
  hwaddress.len = hw_addr_len;
  grub_memcpy (hwaddress.mac, eth, hw_addr_len);
  eth += hw_addr_len;

  src_hwaddress.type = card->default_address.type;
  src_hwaddress.len = hw_addr_len;
  grub_memcpy (src_hwaddress.mac, eth, hw_addr_len);
  eth += hw_addr_len;

  type = grub_be_to_cpu16 (*(grub_uint16_t*)(eth));
  if (type == VLANTAG_IDENTIFIER)
    {
      /* Skip vlan tag */
      eth += 2;
      vlantag = grub_be_to_cpu16 (*(grub_uint16_t*)(eth));
      etherhdr_size += 4;
      eth += 2;
      type = grub_be_to_cpu16 (*(grub_uint16_t*)(eth));
    }

  err = grub_netbuff_pull (nb, etherhdr_size);
  if (err)
    return err;

  if (type <= 1500)
    {
      llch = (struct llchdr *) nb->data;
      type = llch->dsap & LLCADDRMASK;

      if (llch->dsap == 0xaa && llch->ssap == 0xaa && llch->ctrl == 0x3)
	{
	  err = grub_netbuff_pull (nb, sizeof (*llch));
	  if (err)
	    return err;
	  snaph = (struct snaphdr *) nb->data;
	  type = snaph->type;
	}
    }

  switch (type)
    {
      /* ARP packet. */
    case GRUB_NET_ETHERTYPE_ARP:
      grub_net_arp_receive (nb, card, &vlantag);
      grub_netbuff_free (nb);
      return GRUB_ERR_NONE;
      /* IP packet.  */
    case GRUB_NET_ETHERTYPE_IP:
    case GRUB_NET_ETHERTYPE_IP6:
      return grub_net_recv_ip_packets (nb, card, &hwaddress, &src_hwaddress,
                                       &vlantag);
    }
  grub_netbuff_free (nb);
  return GRUB_ERR_NONE;
}
