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

#include <grub/net.h>
#include <grub/net/ip.h>
#include <grub/net/netbuff.h>

struct icmp_header
{
  grub_uint8_t type;
  grub_uint8_t code;
  grub_uint16_t checksum;
} GRUB_PACKED;

struct ping_header
{
  grub_uint16_t id;
  grub_uint16_t seq;
} GRUB_PACKED;

struct router_adv
{
  grub_uint8_t ttl;
  grub_uint8_t flags;
  grub_uint16_t router_lifetime;
  grub_uint32_t reachable_time;
  grub_uint32_t retrans_timer;
  grub_uint8_t options[0];
} GRUB_PACKED;

struct option_header
{
  grub_uint8_t type;
  grub_uint8_t len;
} GRUB_PACKED;

struct prefix_option
{
  struct option_header header;
  grub_uint8_t prefixlen;
  grub_uint8_t flags;
  grub_uint32_t valid_lifetime;
  grub_uint32_t preferred_lifetime;
  grub_uint32_t reserved;
  grub_uint64_t prefix[2];
} GRUB_PACKED;

struct neighbour_solicit
{
  grub_uint32_t reserved;
  grub_uint64_t target[2];
} GRUB_PACKED;

struct neighbour_advertise
{
  grub_uint32_t flags;
  grub_uint64_t target[2];
} GRUB_PACKED;

struct router_solicit
{
  grub_uint32_t reserved;
} GRUB_PACKED;

enum
  {
    FLAG_SLAAC = 0x40
  };

enum
  {
    ICMP6_ECHO = 128,
    ICMP6_ECHO_REPLY = 129,
    ICMP6_ROUTER_SOLICIT = 133,
    ICMP6_ROUTER_ADVERTISE = 134,
    ICMP6_NEIGHBOUR_SOLICIT = 135,
    ICMP6_NEIGHBOUR_ADVERTISE = 136,
  };

enum
  {
    OPTION_SOURCE_LINK_LAYER_ADDRESS = 1,
    OPTION_TARGET_LINK_LAYER_ADDRESS = 2,
    OPTION_PREFIX = 3
  };

enum
  {
    FLAG_SOLICITED = (1 << 30),
    FLAG_OVERRIDE = (1 << 29)
  };

grub_err_t
grub_net_recv_icmp6_packet (struct grub_net_buff *nb,
			    struct grub_net_card *card,
			    struct grub_net_network_level_interface *inf,
			    const grub_net_link_level_address_t *ll_src,
			    const grub_net_network_level_address_t *source,
			    const grub_net_network_level_address_t *dest,
			    grub_uint8_t ttl)
{
  struct icmp_header *icmph;
  struct grub_net_network_level_interface *orig_inf = inf;
  grub_err_t err;
  grub_uint16_t checksum;

  icmph = (struct icmp_header *) nb->data;

  if (nb->tail - nb->data < (grub_ssize_t) sizeof (*icmph))
    {
      grub_netbuff_free (nb);
      return GRUB_ERR_NONE;
    }

  checksum = icmph->checksum;
  icmph->checksum = 0;
  if (checksum != grub_net_ip_transport_checksum (nb,
						  GRUB_NET_IP_ICMPV6,
						  source,
						  dest))
    {
      grub_dprintf ("net", "invalid ICMPv6 checksum: %04x instead of %04x\n",
		    checksum,
		    grub_net_ip_transport_checksum (nb,
						    GRUB_NET_IP_ICMPV6,
						    source,
						    dest));
      icmph->checksum = checksum;
      grub_netbuff_free (nb);
      return GRUB_ERR_NONE;
    }
  icmph->checksum = checksum;

  err = grub_netbuff_pull (nb, sizeof (*icmph));
  if (err)
    {
      grub_netbuff_free (nb);
      return err;
    }

  grub_dprintf ("net", "ICMPv6 message: %02x, %02x\n",
		icmph->type, icmph->code);
  switch (icmph->type)
    {
    case ICMP6_ECHO:
      /* Don't accept multicast pings.  */
      if (!inf)
	break;
      {
	struct grub_net_buff *nb_reply;
	struct icmp_header *icmphr;
	if (icmph->code)
	  break;
	nb_reply = grub_netbuff_alloc (nb->tail - nb->data + 512);
	if (!nb_reply)
	  {
	    grub_netbuff_free (nb);
	    return grub_errno;
	  }
	err = grub_netbuff_reserve (nb_reply, nb->tail - nb->data + 512);
	if (err)
	  goto ping_fail;
	err = grub_netbuff_push (nb_reply, nb->tail - nb->data);
	if (err)
	  goto ping_fail;
	grub_memcpy (nb_reply->data, nb->data, nb->tail - nb->data);
	err = grub_netbuff_push (nb_reply, sizeof (*icmphr));
	if (err)
	  goto ping_fail;
	icmphr = (struct icmp_header *) nb_reply->data;
	icmphr->type = ICMP6_ECHO_REPLY;
	icmphr->code = 0;
	icmphr->checksum = 0;
	icmphr->checksum = grub_net_ip_transport_checksum (nb_reply,
							   GRUB_NET_IP_ICMPV6,
							   &inf->address,
							   source);
	err = grub_net_send_ip_packet (inf, source, ll_src, nb_reply,
				       GRUB_NET_IP_ICMPV6);

      ping_fail:
	grub_netbuff_free (nb);
	grub_netbuff_free (nb_reply);
	return err;
      }
    case ICMP6_NEIGHBOUR_SOLICIT:
      {
	struct neighbour_solicit *nbh;
	struct grub_net_buff *nb_reply;
	struct option_header *ohdr;
	struct neighbour_advertise *adv;
	struct icmp_header *icmphr;
	grub_uint8_t *ptr;

	if (icmph->code)
	  break;
	if (ttl != 0xff)
	  break;
	nbh = (struct neighbour_solicit *) nb->data;
	err = grub_netbuff_pull (nb, sizeof (*nbh));
	if (err)
	  {
	    grub_netbuff_free (nb);
	    return err;
	  }
	for (ptr = (grub_uint8_t *) nb->data; ptr < nb->tail;
	     ptr += ohdr->len * 8)
	  {
	    ohdr = (struct option_header *) ptr;
	    if (ohdr->len == 0 || ptr + 8 * ohdr->len > nb->tail)
	      {
		grub_netbuff_free (nb);
		return GRUB_ERR_NONE; 
	      }
	    if (ohdr->type == OPTION_SOURCE_LINK_LAYER_ADDRESS
		&& ohdr->len == 1)
	      {
		grub_net_link_level_address_t ll_address;
		ll_address.type = card->default_address.type;
		ll_address.len = card->default_address.len;
		grub_memcpy (ll_address.mac, ohdr + 1, ll_address.len);
		grub_net_link_layer_add_address (card, source, &ll_address, 0);
	      }
	  }
	FOR_NET_NETWORK_LEVEL_INTERFACES (inf)
	{
	  if (inf->card == card
	      && inf->address.type == GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6
	      && grub_memcmp (&inf->address.ipv6, &nbh->target, 16) == 0)
	    break;
	}
	if (!inf)
	  break;

	nb_reply = grub_netbuff_alloc (sizeof (struct neighbour_advertise)
				       + sizeof (struct option_header)
				       + 6
				       + sizeof (struct icmp_header)
				       + GRUB_NET_OUR_IPV6_HEADER_SIZE
				       + GRUB_NET_MAX_LINK_HEADER_SIZE);
	if (!nb_reply)
	  {
	    grub_netbuff_free (nb);
	    return grub_errno;
	  }
	err = grub_netbuff_reserve (nb_reply,
				    sizeof (struct neighbour_advertise)
				    + sizeof (struct option_header)
				    + 6
				    + sizeof (struct icmp_header)
				    + GRUB_NET_OUR_IPV6_HEADER_SIZE
				    + GRUB_NET_MAX_LINK_HEADER_SIZE);
	if (err)
	  goto ndp_fail;

	err = grub_netbuff_push (nb_reply, 6);
	if (err)
	  goto ndp_fail;
	grub_memcpy (nb_reply->data, inf->hwaddress.mac, 6);
	err = grub_netbuff_push (nb_reply, sizeof (*ohdr));
	if (err)
	  goto ndp_fail;
	ohdr = (struct option_header *) nb_reply->data;
	ohdr->type = OPTION_TARGET_LINK_LAYER_ADDRESS;
	ohdr->len = 1;
	err = grub_netbuff_push (nb_reply, sizeof (*adv));
	if (err)
	  goto ndp_fail;
	adv = (struct neighbour_advertise *) nb_reply->data;
	adv->flags = grub_cpu_to_be32_compile_time (FLAG_SOLICITED
						    | FLAG_OVERRIDE);
	grub_memcpy (&adv->target, &nbh->target, 16);

	err = grub_netbuff_push (nb_reply, sizeof (*icmphr));
	if (err)
	  goto ndp_fail;
	icmphr = (struct icmp_header *) nb_reply->data;
	icmphr->type = ICMP6_NEIGHBOUR_ADVERTISE;
	icmphr->code = 0;
	icmphr->checksum = 0;
	icmphr->checksum = grub_net_ip_transport_checksum (nb_reply,
							   GRUB_NET_IP_ICMPV6,
							   &inf->address,
							   source);
	err = grub_net_send_ip_packet (inf, source, ll_src, nb_reply,
				       GRUB_NET_IP_ICMPV6);

      ndp_fail:
	grub_netbuff_free (nb);
	grub_netbuff_free (nb_reply);
	return err;
      }
    case ICMP6_NEIGHBOUR_ADVERTISE:
      {
	struct neighbour_advertise *nbh;
	grub_uint8_t *ptr;
	struct option_header *ohdr;

	if (icmph->code)
	  break;
	if (ttl != 0xff)
	  break;
	nbh = (struct neighbour_advertise *) nb->data;
	err = grub_netbuff_pull (nb, sizeof (*nbh));
	if (err)
	  {
	    grub_netbuff_free (nb);
	    return err;
	  }

	for (ptr = (grub_uint8_t *) nb->data; ptr < nb->tail;
	     ptr += ohdr->len * 8)
	  {
	    ohdr = (struct option_header *) ptr;
	    if (ohdr->len == 0 || ptr + 8 * ohdr->len > nb->tail)
	      {
		grub_netbuff_free (nb);
		return GRUB_ERR_NONE; 
	      }
	    if (ohdr->type == OPTION_TARGET_LINK_LAYER_ADDRESS
		&& ohdr->len == 1)
	      {
		grub_net_link_level_address_t ll_address;
		ll_address.type = card->default_address.type;
		ll_address.len = card->default_address.len;
		grub_memcpy (ll_address.mac, ohdr + 1, ll_address.len);
		grub_net_link_layer_add_address (card, source, &ll_address, 0);
	      }
	  }
	break;
      }
    case ICMP6_ROUTER_ADVERTISE:
      {
	grub_uint8_t *ptr;
	struct option_header *ohdr;
	struct router_adv *radv;
	struct grub_net_network_level_interface *route_inf = NULL;
	int default_route = 0;
	if (icmph->code)
	  break;
	radv = (struct router_adv *)nb->data;
	err = grub_netbuff_pull (nb, sizeof (struct router_adv));
	if (err)
	  {
	    grub_netbuff_free (nb);
	    return err;
	  }
	if (grub_be_to_cpu16 (radv->router_lifetime) > 0)
	  {
	    struct grub_net_route *route;

	    FOR_NET_ROUTES (route)
	    {
	      if (!grub_memcmp (&route->gw, source, sizeof (route->gw)))
		break;
	    }
	    if (route == NULL)
	      default_route = 1;
	  }

	for (ptr = (grub_uint8_t *) nb->data; ptr < nb->tail;
	     ptr += ohdr->len * 8)
	  {
	    ohdr = (struct option_header *) ptr;
	    if (ohdr->len == 0 || ptr + 8 * ohdr->len > nb->tail)
	      {
		grub_netbuff_free (nb);
		return GRUB_ERR_NONE; 
	      }
	    if (ohdr->type == OPTION_SOURCE_LINK_LAYER_ADDRESS
		&& ohdr->len == 1)
	      {
		grub_net_link_level_address_t ll_address;
		ll_address.type = card->default_address.type;
		ll_address.len = card->default_address.len;
		grub_memcpy (ll_address.mac, ohdr + 1, ll_address.len);
		grub_net_link_layer_add_address (card, source, &ll_address, 0);
	      }
	    if (ohdr->type == OPTION_PREFIX && ohdr->len == 4)
	      {
		struct prefix_option *opt = (struct prefix_option *) ptr;
		struct grub_net_slaac_mac_list *slaac;
		if (!(opt->flags & FLAG_SLAAC)
		    || (grub_be_to_cpu64 (opt->prefix[0]) >> 48) == 0xfe80
		    || (grub_be_to_cpu32 (opt->preferred_lifetime)
			> grub_be_to_cpu32 (opt->valid_lifetime))
		    || opt->prefixlen != 64)
		  {
		    grub_dprintf ("net", "discarded prefix: %d, %d, %d, %d\n",
				  !(opt->flags & FLAG_SLAAC),
				  (grub_be_to_cpu64 (opt->prefix[0]) >> 48) == 0xfe80,
				  (grub_be_to_cpu32 (opt->preferred_lifetime)
				   > grub_be_to_cpu32 (opt->valid_lifetime)),
				  opt->prefixlen != 64);
		    continue;
		  }
		for (slaac = card->slaac_list; slaac; slaac = slaac->next)
		  {
		    grub_net_network_level_address_t addr;
		    grub_net_network_level_netaddress_t netaddr;

		    if (slaac->address.type
			!= GRUB_NET_LINK_LEVEL_PROTOCOL_ETHERNET)
		      continue;
		    addr.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6;
		    addr.ipv6[0] = opt->prefix[0];
		    addr.ipv6[1] = grub_net_ipv6_get_id (&slaac->address);
		    netaddr.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6;
		    netaddr.ipv6.base[0] = opt->prefix[0];
		    netaddr.ipv6.base[1] = 0;
		    netaddr.ipv6.masksize = 64;

		    FOR_NET_NETWORK_LEVEL_INTERFACES (inf)
		    {
		      if (inf->card == card
			  && grub_net_addr_cmp (&inf->address, &addr) == 0)
			break;
		    }
		    /* Update lease time if needed here once we have
		       lease times.  */
		    if (inf)
		      {
			if (!route_inf)
			  route_inf = inf;
			continue;
		      }

		    grub_dprintf ("net", "creating slaac\n");

		    {
		      char *name;
		      name = grub_xasprintf ("%s:%d",
					     slaac->name, slaac->slaac_counter++);
		      if (!name)
			{
			  grub_errno = GRUB_ERR_NONE;
			  continue;
			}
		      inf = grub_net_add_addr (name, 
					       card, &addr,
					       &slaac->address, 0);
		      if (!route_inf)
			route_inf = inf;
		      grub_net_add_route (name, netaddr, inf);
		      grub_free (name);
		    }
		  }
	      }
	  }
	if (default_route)
	  {
	    char *name;
	    grub_net_network_level_netaddress_t netaddr;
	    name = grub_xasprintf ("%s:ra:default6", card->name);
	    if (!name)
	      {
		grub_errno = GRUB_ERR_NONE;
		goto next;
	      }
	    /* Default routes take alll of the traffic, so make the mask huge */
	    netaddr.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6;
	    netaddr.ipv6.masksize = 0;
	    netaddr.ipv6.base[0] = 0;
	    netaddr.ipv6.base[1] = 0;

	    /* May not have gotten slaac info, find a global address on this
	      card.  */
	    if (route_inf == NULL)
	      {
		FOR_NET_NETWORK_LEVEL_INTERFACES (inf)
		{
		  if (inf->card == card && inf != orig_inf
		      && inf->address.type == GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6
		      && grub_net_hwaddr_cmp(&inf->hwaddress,
					     &orig_inf->hwaddress) == 0)
		    {
		      route_inf = inf;
		      break;
		    }
		}
	      }
	    if (route_inf != NULL)
	      grub_net_add_route_gw (name, netaddr, *source, route_inf);
	    grub_free (name);
	  }
next:
	if (ptr != nb->tail)
	  break;
      }
    };

  grub_netbuff_free (nb);
  return GRUB_ERR_NONE;
}

grub_err_t
grub_net_icmp6_send_request (struct grub_net_network_level_interface *inf,
			     const grub_net_network_level_address_t *proto_addr)
{
  struct grub_net_buff *nb;
  grub_err_t err = GRUB_ERR_NONE;
  int i;
  struct option_header *ohdr;
  struct neighbour_solicit *sol;
  struct icmp_header *icmphr;
  grub_net_network_level_address_t multicast;
  grub_net_link_level_address_t ll_multicast;
  grub_uint8_t *nbd;
  multicast.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6;
  multicast.ipv6[0] = grub_be_to_cpu64_compile_time (0xff02ULL << 48);
  multicast.ipv6[1] = (grub_be_to_cpu64_compile_time (0x01ff000000ULL)
		       | (proto_addr->ipv6[1]
			  & grub_be_to_cpu64_compile_time (0xffffff)));
  
  err = grub_net_link_layer_resolve (inf, &multicast, &ll_multicast);
  if (err)
    return err;

  nb = grub_netbuff_alloc (sizeof (struct neighbour_solicit)
			   + sizeof (struct option_header)
			   + 6
			   + sizeof (struct icmp_header)
			   + GRUB_NET_OUR_IPV6_HEADER_SIZE
			   + GRUB_NET_MAX_LINK_HEADER_SIZE);
  if (!nb)
    return grub_errno;
  err = grub_netbuff_reserve (nb,
			      sizeof (struct neighbour_solicit)
			      + sizeof (struct option_header)
			      + 6
			      + sizeof (struct icmp_header)
			      + GRUB_NET_OUR_IPV6_HEADER_SIZE
			      + GRUB_NET_MAX_LINK_HEADER_SIZE);
  err = grub_netbuff_push (nb, 6);
  if (err)
    goto fail;

  grub_memcpy (nb->data, inf->hwaddress.mac, 6);
  err = grub_netbuff_push (nb, sizeof (*ohdr));
  if (err)
    goto fail;

  ohdr = (struct option_header *) nb->data;
  ohdr->type = OPTION_SOURCE_LINK_LAYER_ADDRESS;
  ohdr->len = 1;
  err = grub_netbuff_push (nb, sizeof (*sol));  
  if (err)
    goto fail;

  sol = (struct neighbour_solicit *) nb->data;
  sol->reserved = 0;
  grub_memcpy (&sol->target, &proto_addr->ipv6, 16);

  err = grub_netbuff_push (nb, sizeof (*icmphr));
  if (err)
    goto fail;

  icmphr = (struct icmp_header *) nb->data;
  icmphr->type = ICMP6_NEIGHBOUR_SOLICIT;
  icmphr->code = 0;
  icmphr->checksum = 0;
  icmphr->checksum = grub_net_ip_transport_checksum (nb,
						     GRUB_NET_IP_ICMPV6,
						     &inf->address,
						     &multicast);
  nbd = nb->data;
  err = grub_net_send_ip_packet (inf, &multicast, &ll_multicast, nb,
				 GRUB_NET_IP_ICMPV6);
  if (err)
    goto fail;

  for (i = 0; i < GRUB_NET_TRIES; i++)
    {
      if (grub_net_link_layer_resolve_check (inf, proto_addr))
	break;
      grub_net_poll_cards (GRUB_NET_INTERVAL + (i * GRUB_NET_INTERVAL_ADDITION),
                           0);
      if (grub_net_link_layer_resolve_check (inf, proto_addr))
	break;
      nb->data = nbd;
      err = grub_net_send_ip_packet (inf, &multicast, &ll_multicast, nb,
				     GRUB_NET_IP_ICMPV6);
      if (err)
	break;
    }

 fail:
  grub_netbuff_free (nb);
  return err;
}

grub_err_t
grub_net_icmp6_send_router_solicit (struct grub_net_network_level_interface *inf)
{
  struct grub_net_buff *nb;
  grub_err_t err = GRUB_ERR_NONE;
  grub_net_network_level_address_t multicast;
  grub_net_link_level_address_t ll_multicast;
  struct option_header *ohdr;
  struct router_solicit *sol;
  struct icmp_header *icmphr;

  multicast.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6;
  multicast.ipv6[0] = grub_cpu_to_be64_compile_time (0xff02ULL << 48);
  multicast.ipv6[1] = grub_cpu_to_be64_compile_time (0x02ULL);

  err = grub_net_link_layer_resolve (inf, &multicast, &ll_multicast);
  if (err)
    return err;

  nb = grub_netbuff_alloc (sizeof (struct router_solicit)
			   + sizeof (struct option_header)
			   + 6
			   + sizeof (struct icmp_header)
			   + GRUB_NET_OUR_IPV6_HEADER_SIZE
			   + GRUB_NET_MAX_LINK_HEADER_SIZE);
  if (!nb)
    return grub_errno;
  err = grub_netbuff_reserve (nb,
			      sizeof (struct router_solicit)
			      + sizeof (struct option_header)
			      + 6
			      + sizeof (struct icmp_header)
			      + GRUB_NET_OUR_IPV6_HEADER_SIZE
			      + GRUB_NET_MAX_LINK_HEADER_SIZE);
  if (err)
    goto fail;

  err = grub_netbuff_push (nb, 6);
  if (err)
    goto fail;

  grub_memcpy (nb->data, inf->hwaddress.mac, 6);

  err = grub_netbuff_push (nb, sizeof (*ohdr));
  if (err)
    goto fail;

  ohdr = (struct option_header *) nb->data;
  ohdr->type = OPTION_SOURCE_LINK_LAYER_ADDRESS;
  ohdr->len = 1;

  err = grub_netbuff_push (nb, sizeof (*sol));
  if (err)
    goto fail;

  sol = (struct router_solicit *) nb->data;
  sol->reserved = 0;

  err = grub_netbuff_push (nb, sizeof (*icmphr));
  if (err)
    goto fail;

  icmphr = (struct icmp_header *) nb->data;
  icmphr->type = ICMP6_ROUTER_SOLICIT;
  icmphr->code = 0;
  icmphr->checksum = 0;
  icmphr->checksum = grub_net_ip_transport_checksum (nb,
						     GRUB_NET_IP_ICMPV6,
						     &inf->address,
						     &multicast);
  err = grub_net_send_ip_packet (inf, &multicast, &ll_multicast, nb,
				 GRUB_NET_IP_ICMPV6);
 fail:
  grub_netbuff_free (nb);
  return err;
}
