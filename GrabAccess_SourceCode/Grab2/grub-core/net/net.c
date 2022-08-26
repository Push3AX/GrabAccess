/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010,2011,2012,2013  Free Software Foundation, Inc.
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
#include <grub/net/netbuff.h>
#include <grub/time.h>
#include <grub/file.h>
#include <grub/i18n.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/dl.h>
#include <grub/command.h>
#include <grub/env.h>
#include <grub/net/ethernet.h>
#include <grub/net/arp.h>
#include <grub/net/ip.h>
#include <grub/loader.h>
#include <grub/bufio.h>
#include <grub/kernel.h>
#ifdef GRUB_MACHINE_EFI
#include <grub/net/efi.h>
#endif

GRUB_MOD_LICENSE ("GPLv3+");

char *grub_net_default_server;

struct grub_net_route *grub_net_routes = NULL;
struct grub_net_network_level_interface *grub_net_network_level_interfaces = NULL;
struct grub_net_card *grub_net_cards = NULL;
struct grub_net_network_level_protocol *grub_net_network_level_protocols = NULL;
static struct grub_fs grub_net_fs;

struct grub_net_link_layer_entry {
  int avail;
  grub_net_network_level_address_t nl_address;
  grub_net_link_level_address_t ll_address;
};

#define LINK_LAYER_CACHE_SIZE 256

static struct grub_net_link_layer_entry *
link_layer_find_entry (const grub_net_network_level_address_t *proto,
		       const struct grub_net_card *card)
{
  unsigned i;
  if (!card->link_layer_table)
    return NULL;
  for (i = 0; i < LINK_LAYER_CACHE_SIZE; i++)
    {
      if (card->link_layer_table[i].avail == 1 
	  && grub_net_addr_cmp (&card->link_layer_table[i].nl_address,
				proto) == 0)
	return &card->link_layer_table[i];
    }
  return NULL;
}

void
grub_net_link_layer_add_address (struct grub_net_card *card,
				 const grub_net_network_level_address_t *nl,
				 const grub_net_link_level_address_t *ll,
				 int override)
{
  struct grub_net_link_layer_entry *entry;

  /* Check if the sender is in the cache table.  */
  entry = link_layer_find_entry (nl, card);
  /* Update sender hardware address.  */
  if (entry && override)
    grub_memcpy (&entry->ll_address, ll, sizeof (entry->ll_address));
  if (entry)
    return;

  /* Add sender to cache table.  */
  if (card->link_layer_table == NULL)
    {
      card->link_layer_table = grub_zalloc (LINK_LAYER_CACHE_SIZE
					    * sizeof (card->link_layer_table[0]));
      if (card->link_layer_table == NULL)
	return;
    }

  entry = &(card->link_layer_table[card->new_ll_entry]);
  entry->avail = 1;
  grub_memcpy (&entry->ll_address, ll, sizeof (entry->ll_address));
  grub_memcpy (&entry->nl_address, nl, sizeof (entry->nl_address));
  card->new_ll_entry++;
  if (card->new_ll_entry == LINK_LAYER_CACHE_SIZE)
    card->new_ll_entry = 0;
}

int
grub_net_link_layer_resolve_check (struct grub_net_network_level_interface *inf,
				   const grub_net_network_level_address_t *proto_addr)
{
  struct grub_net_link_layer_entry *entry;

  if (proto_addr->type == GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4
      && proto_addr->ipv4 == 0xffffffff)
    return 1;
  entry = link_layer_find_entry (proto_addr, inf->card);
  if (entry)
    return 1;
  return 0;
}

grub_err_t
grub_net_link_layer_resolve (struct grub_net_network_level_interface *inf,
			     const grub_net_network_level_address_t *proto_addr,
			     grub_net_link_level_address_t *hw_addr)
{
  struct grub_net_link_layer_entry *entry;
  grub_err_t err;

  if ((proto_addr->type == GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4
       && proto_addr->ipv4 == 0xffffffff)
      || proto_addr->type == GRUB_NET_NETWORK_LEVEL_PROTOCOL_DHCP_RECV
      || (proto_addr->type == GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6
	  && proto_addr->ipv6[0] == grub_be_to_cpu64_compile_time (0xff02ULL
								   << 48)
	  && proto_addr->ipv6[1] == (grub_be_to_cpu64_compile_time (1))))
    {
      hw_addr->type = inf->card->default_address.type;
      hw_addr->len = inf->card->default_address.len;
      grub_memset (hw_addr->mac, -1, hw_addr->len);
      return GRUB_ERR_NONE;
    }

  if (proto_addr->type == GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6
      && ((grub_be_to_cpu64 (proto_addr->ipv6[0]) >> 56) == 0xff))
    {
      hw_addr->type = GRUB_NET_LINK_LEVEL_PROTOCOL_ETHERNET;
      hw_addr->len = inf->card->default_address.len;
      hw_addr->mac[0] = 0x33;
      hw_addr->mac[1] = 0x33;
      hw_addr->mac[2] = ((grub_be_to_cpu64 (proto_addr->ipv6[1]) >> 24) & 0xff);
      hw_addr->mac[3] = ((grub_be_to_cpu64 (proto_addr->ipv6[1]) >> 16) & 0xff);
      hw_addr->mac[4] = ((grub_be_to_cpu64 (proto_addr->ipv6[1]) >> 8) & 0xff);
      hw_addr->mac[5] = ((grub_be_to_cpu64 (proto_addr->ipv6[1]) >> 0) & 0xff);
      return GRUB_ERR_NONE;
    }

  /* Check cache table.  */
  entry = link_layer_find_entry (proto_addr, inf->card);
  if (entry)
    {
      *hw_addr = entry->ll_address;
      return GRUB_ERR_NONE;
    }
  switch (proto_addr->type)
    {
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4:
      err = grub_net_arp_send_request (inf, proto_addr);
      break;
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6:
      err = grub_net_icmp6_send_request (inf, proto_addr);
      break;
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_DHCP_RECV:
      return grub_error (GRUB_ERR_BUG, "shouldn't reach here");
    default:
      return grub_error (GRUB_ERR_BUG,
			 "unsupported address type %d", proto_addr->type);
    }
  if (err)
    return err;
  entry = link_layer_find_entry (proto_addr, inf->card);
  if (entry)
    {
      *hw_addr = entry->ll_address;
      return GRUB_ERR_NONE;
    }
  return grub_error (GRUB_ERR_TIMEOUT, 
		     N_("timeout: could not resolve hardware address"));
}

void
grub_net_card_unregister (struct grub_net_card *card)
{
  struct grub_net_network_level_interface *inf, *next;
  FOR_NET_NETWORK_LEVEL_INTERFACES_SAFE(inf, next)
    if (inf->card == card)
      grub_net_network_level_interface_unregister (inf);
  if (card->opened)
    {
      if (card->driver->close)
	card->driver->close (card);
      card->opened = 0;
    }
  grub_list_remove (GRUB_AS_LIST (card));
}

static struct grub_net_slaac_mac_list *
grub_net_ipv6_get_slaac (struct grub_net_card *card,
			 const grub_net_link_level_address_t *hwaddr)
{
  struct grub_net_slaac_mac_list *slaac;
  char *ptr;

  for (slaac = card->slaac_list; slaac; slaac = slaac->next)
    if (grub_net_hwaddr_cmp (&slaac->address, hwaddr) == 0)
      return slaac;

  slaac = grub_zalloc (sizeof (*slaac));
  if (!slaac)
    return NULL;

  slaac->name = grub_malloc (grub_strlen (card->name)
			     + GRUB_NET_MAX_STR_HWADDR_LEN
			     + sizeof (":slaac"));
  ptr = grub_stpcpy (slaac->name, card->name);
  if (grub_net_hwaddr_cmp (&card->default_address, hwaddr) != 0)
    {
      ptr = grub_stpcpy (ptr, ":");
      grub_net_hwaddr_to_str (hwaddr, ptr);
      ptr += grub_strlen (ptr);
    }
  ptr = grub_stpcpy (ptr, ":slaac");

  grub_memcpy (&slaac->address, hwaddr, sizeof (slaac->address));
  slaac->next = card->slaac_list;
  card->slaac_list = slaac;
  return slaac;
}

static void
grub_net_network_level_interface_register (struct grub_net_network_level_interface *inter);

static struct grub_net_network_level_interface *
grub_net_add_addr_real (char *name, 
			struct grub_net_card *card,
			const grub_net_network_level_address_t *addr,
			const grub_net_link_level_address_t *hwaddress,
			grub_net_interface_flags_t flags)
{
  struct grub_net_network_level_interface *inter;

  inter = grub_zalloc (sizeof (*inter));
  if (!inter)
    return NULL;

  inter->name = name;
  grub_memcpy (&(inter->address), addr, sizeof (inter->address));
  grub_memcpy (&(inter->hwaddress), hwaddress, sizeof (inter->hwaddress));
  inter->flags = flags;
  inter->card = card;
  inter->dhcp_ack = NULL;
  inter->dhcp_acklen = 0;

  grub_net_network_level_interface_register (inter);

  return inter;
}

struct grub_net_network_level_interface *
grub_net_add_addr (const char *name, 
		   struct grub_net_card *card,
		   const grub_net_network_level_address_t *addr,
		   const grub_net_link_level_address_t *hwaddress,
		   grub_net_interface_flags_t flags)
{
  char *name_dup = grub_strdup (name);
  struct grub_net_network_level_interface *ret;
 
  if (!name_dup)
    return NULL;
  ret = grub_net_add_addr_real (name_dup, card, addr, hwaddress, flags);
  if (!ret)
    grub_free (name_dup);
  return ret;
}

struct grub_net_network_level_interface *
grub_net_ipv6_get_link_local (struct grub_net_card *card,
			      const grub_net_link_level_address_t *hwaddr)
{
  struct grub_net_network_level_interface *inf;
  char *name;
  char *ptr;
  grub_net_network_level_address_t addr;

  addr.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6;
  addr.ipv6[0] = grub_cpu_to_be64_compile_time (0xfe80ULL << 48);
  addr.ipv6[1] = grub_net_ipv6_get_id (hwaddr);

  FOR_NET_NETWORK_LEVEL_INTERFACES (inf)
  {
    if (inf->card == card
	&& grub_net_hwaddr_cmp (&inf->hwaddress, hwaddr) == 0
	&& grub_net_addr_cmp (&inf->address, &addr) == 0)
      return inf;
  }

  name = grub_malloc (grub_strlen (card->name)
		      + GRUB_NET_MAX_STR_HWADDR_LEN
		      + sizeof (":link"));
  if (!name)
    return NULL;

  ptr = grub_stpcpy (name, card->name);
  if (grub_net_hwaddr_cmp (&card->default_address, hwaddr) != 0)
    {
      ptr = grub_stpcpy (ptr, ":");
      grub_net_hwaddr_to_str (hwaddr, ptr);
      ptr += grub_strlen (ptr);
    }
  ptr = grub_stpcpy (ptr, ":link");
  return grub_net_add_addr_real (name, card, &addr, hwaddr, 0);
}

/* FIXME: allow to specify mac address.  */
static grub_err_t
grub_cmd_ipv6_autoconf (struct grub_command *cmd __attribute__ ((unused)),
			int argc, char **args)
{
  struct grub_net_card *card;
  struct grub_net_network_level_interface **ifaces;
  grub_size_t ncards = 0;
  unsigned j = 0;
  int interval;
  grub_err_t err;
  struct grub_net_slaac_mac_list **slaacs;

  FOR_NET_CARDS (card)
  {
    if (argc > 0 && grub_strcmp (card->name, args[0]) != 0)
      continue;
    ncards++;
  }

  ifaces = grub_calloc (ncards, sizeof (ifaces[0]));
  slaacs = grub_calloc (ncards, sizeof (slaacs[0]));
  if (!ifaces || !slaacs)
    {
      grub_free (ifaces);
      grub_free (slaacs);
      return grub_errno;
    }

  FOR_NET_CARDS (card)
  {
    if (argc > 0 && grub_strcmp (card->name, args[0]) != 0)
      continue;
    ifaces[j] = grub_net_ipv6_get_link_local (card, &card->default_address);
    if (!ifaces[j])
      {
	grub_free (ifaces);
	grub_free (slaacs);
	return grub_errno;
      }
    slaacs[j] = grub_net_ipv6_get_slaac (card, &card->default_address);
    if (!slaacs[j])
      {
	grub_free (ifaces);
	grub_free (slaacs);
	return grub_errno;
      }
    j++;
  }

  for (interval = 200; interval < 10000; interval *= 2)
    {
      int done = 1;
      for (j = 0; j < ncards; j++)
	{
	  if (slaacs[j]->slaac_counter)
	    continue;
	  err = grub_net_icmp6_send_router_solicit (ifaces[j]);
	  if (err)
	    err = GRUB_ERR_NONE;
	  done = 0;
	}
      if (done)
	break;
      grub_net_poll_cards (interval, 0);
    }

  err = GRUB_ERR_NONE;
  for (j = 0; j < ncards; j++)
    {
      if (slaacs[j]->slaac_counter)
	continue;
      err = grub_error (GRUB_ERR_FILE_NOT_FOUND,
			N_("couldn't autoconfigure %s"),
			ifaces[j]->card->name);
    }

  grub_free (ifaces);
  grub_free (slaacs);
  return err;
}


static int
parse_ip (const char *val, grub_uint32_t *ip, const char **rest)
{
  grub_uint32_t newip = 0;
  int i;
  const char *ptr = val;

  for (i = 0; i < 4; i++)
    {
      unsigned long t;
      t = grub_strtoul (ptr, &ptr, 0);
      if (grub_errno)
	{
	  grub_errno = GRUB_ERR_NONE;
	  return 0;
	}
      if (*ptr != '.' && i == 0)
	{
	  newip = t;
	  break;
	}
      if (t & ~0xff)
	return 0;
      newip >>= 8;
      newip |= (t << 24);
      if (i != 3 && *ptr != '.')
	return 0;
      ptr++;
    }
  *ip = grub_cpu_to_le32 (newip);
  if (rest)
    *rest = (ptr - 1);
  return 1;
}

static int
parse_ip6 (const char *val, grub_uint64_t *ip, const char **rest)
{
  grub_uint16_t newip[8];
  const char *ptr = val;
  int word, quaddot = -1;
  int bracketed = 0;

  if (ptr[0] == '[')
    {
      bracketed = 1;
      ptr++;
    }

  if (ptr[0] == ':' && ptr[1] != ':')
    return 0;
  if (ptr[0] == ':')
    ptr++;

  for (word = 0; word < 8; word++)
    {
      unsigned long t;
      if (*ptr == ':')
	{
	  quaddot = word;
	  word--;
	  ptr++;
	  continue;
	}
      t = grub_strtoul (ptr, &ptr, 16);
      if (grub_errno)
	{
	  grub_errno = GRUB_ERR_NONE;
	  break;
	}
      if (t & ~0xffff)
	return 0;
      newip[word] = grub_cpu_to_be16 (t);
      if (*ptr != ':')
	break;
      ptr++;
    }
  if (quaddot == -1 && word < 7)
    return 0;
  if (quaddot != -1)
    {
      grub_memmove (&newip[quaddot + 7 - word], &newip[quaddot],
		    (word - quaddot + 1) * sizeof (newip[0]));
      grub_memset (&newip[quaddot], 0, (7 - word) * sizeof (newip[0]));
    }
  grub_memcpy (ip, newip, 16);
  if (bracketed && *ptr == ']')
    ptr++;
  if (rest)
    *rest = ptr;
  return 1;
}

static int
match_net (const grub_net_network_level_netaddress_t *net,
	   const grub_net_network_level_address_t *addr)
{
  if (net->type != addr->type)
    return 0;
  switch (net->type)
    {
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_DHCP_RECV:
      return 0;
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4:
      {
	grub_uint32_t mask = (0xffffffffU << (32 - net->ipv4.masksize));
	if (net->ipv4.masksize == 0)
	  mask = 0;
	return ((grub_be_to_cpu32 (net->ipv4.base) & mask)
		== (grub_be_to_cpu32 (addr->ipv4) & mask));
      }
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6:
      {
	grub_uint64_t mask[2];
	if (net->ipv6.masksize == 0)
	  return 1;
	if (net->ipv6.masksize <= 64)
	  {
	    mask[0] = 0xffffffffffffffffULL << (64 - net->ipv6.masksize);
	    mask[1] = 0;
	  }
	else 
	  {
	    mask[0] = 0xffffffffffffffffULL;
	    mask[1] = 0xffffffffffffffffULL << (128 - net->ipv6.masksize);
	  }
	return (((grub_be_to_cpu64 (net->ipv6.base[0]) & mask[0])
		== (grub_be_to_cpu64 (addr->ipv6[0]) & mask[0]))
		&& ((grub_be_to_cpu64 (net->ipv6.base[1]) & mask[1])
		    == (grub_be_to_cpu64 (addr->ipv6[1]) & mask[1])));
      }
    }
  return 0;
}

grub_err_t
grub_net_resolve_address (const char *name,
			  grub_net_network_level_address_t *addr)
{
  const char *rest;
  grub_err_t err;
  grub_size_t naddresses;
  struct grub_net_network_level_address *addresses = 0;

  if (parse_ip (name, &addr->ipv4, &rest) && *rest == 0)
    {
      addr->type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4;
      return GRUB_ERR_NONE;
    }
  if (parse_ip6 (name, addr->ipv6, &rest) && *rest == 0)
    {
      addr->type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6;
      return GRUB_ERR_NONE;
    }
  err = grub_net_dns_lookup (name, 0, 0, &naddresses, &addresses, 1);
  if (err)
    return err;
  if (!naddresses)
    grub_error (GRUB_ERR_NET_BAD_ADDRESS, N_("unresolvable address %s"),
		name);
  /* FIXME: use other results as well.  */
  *addr = addresses[0];
  grub_free (addresses);
  return GRUB_ERR_NONE;
}

grub_err_t
grub_net_resolve_net_address (const char *name,
			      grub_net_network_level_netaddress_t *addr)
{
  const char *rest;
  if (parse_ip (name, &addr->ipv4.base, &rest))
    {
      addr->type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4;
      if (*rest == '/')
	{
	  addr->ipv4.masksize = grub_strtoul (rest + 1, &rest, 0);
	  if (!grub_errno && *rest == 0)
	    return GRUB_ERR_NONE;
	  grub_errno = GRUB_ERR_NONE;
	}
      else if (*rest == 0)
	{
	  addr->ipv4.masksize = 32;
	  return GRUB_ERR_NONE;
	}
    }
  if (parse_ip6 (name, addr->ipv6.base, &rest))
    {
      addr->type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6;
      if (*rest == '/')
	{
	  addr->ipv6.masksize = grub_strtoul (rest + 1, &rest, 0);
	  if (!grub_errno && *rest == 0)
	    return GRUB_ERR_NONE;
	  grub_errno = GRUB_ERR_NONE;
	}
      else if (*rest == 0)
	{
	  addr->ipv6.masksize = 128;
	  return GRUB_ERR_NONE;
	}
    }
  return grub_error (GRUB_ERR_NET_BAD_ADDRESS,
		     N_("unrecognised network address `%s'"),
		     name);
}

static int
route_cmp (const struct grub_net_route *a, const struct grub_net_route *b)
{
  if (a == NULL && b == NULL)
    return 0;
  if (b == NULL)
    return +1;
  if (a == NULL)
    return -1;
  if (a->target.type < b->target.type)
    return -1;
  if (a->target.type > b->target.type)
    return +1;
  switch (a->target.type)
    {
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_DHCP_RECV:
      break;
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6:
      if (a->target.ipv6.masksize > b->target.ipv6.masksize)
	return +1;
      if (a->target.ipv6.masksize < b->target.ipv6.masksize)
	return -1;
      break;
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4:
      if (a->target.ipv4.masksize > b->target.ipv4.masksize)
	return +1;
      if (a->target.ipv4.masksize < b->target.ipv4.masksize)
	return -1;
      break;
    }
  return 0;
}

grub_err_t
grub_net_route_address (grub_net_network_level_address_t addr,
			grub_net_network_level_address_t *gateway,
			struct grub_net_network_level_interface **interf)
{
  struct grub_net_route *route;
  unsigned int depth = 0;
  unsigned int routecnt = 0;
  struct grub_net_network_level_protocol *prot = NULL;
  grub_net_network_level_address_t curtarget = addr;

  *gateway = addr;

  FOR_NET_ROUTES(route)
    routecnt++;

  for (depth = 0; depth < routecnt + 2 && depth < GRUB_UINT_MAX; depth++)
    {
      struct grub_net_route *bestroute = NULL;
      FOR_NET_ROUTES(route)
      {
	if (depth && prot != route->prot)
	  continue;
	if (!match_net (&route->target, &curtarget))
	  continue;
	if (route_cmp (route, bestroute) > 0)
	  bestroute = route;
      }
      if (bestroute == NULL)
	return grub_error (GRUB_ERR_NET_NO_ROUTE,
			   N_("destination unreachable"));

      if (!bestroute->is_gateway)
	{
	  *interf = bestroute->interface;
	  return GRUB_ERR_NONE;
	}
      if (depth == 0)
	{
	  *gateway = bestroute->gw;
	  if (bestroute->interface != NULL)
	    {
	      *interf = bestroute->interface;
	      return GRUB_ERR_NONE;
	    }
	}
      curtarget = bestroute->gw;
    }

  return grub_error (GRUB_ERR_NET_ROUTE_LOOP,
		     /* TRANSLATORS: route loop is a condition when e.g.
			to contact server A you need to go through B
			and to contact B you need to go through A.  */
		     N_("route loop detected"));
}

static grub_err_t
grub_cmd_deladdr (struct grub_command *cmd __attribute__ ((unused)),
		  int argc, char **args)
{
  struct grub_net_network_level_interface *inter;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("one argument expected"));

  FOR_NET_NETWORK_LEVEL_INTERFACES (inter)
    if (grub_strcmp (inter->name, args[0]) == 0)
      break;
  if (inter == NULL)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("address not found"));

  if (inter->flags & GRUB_NET_INTERFACE_PERMANENT)
    return grub_error (GRUB_ERR_IO,
		       N_("you can't delete this address"));

  grub_net_network_level_interface_unregister (inter);
  grub_free (inter->name);
  grub_free (inter);

  return GRUB_ERR_NONE;  
}

void
grub_net_addr_to_str (const grub_net_network_level_address_t *target, char *buf)
{
  switch (target->type)
    {
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_DHCP_RECV:
      COMPILE_TIME_ASSERT (sizeof ("temporary") < GRUB_NET_MAX_STR_ADDR_LEN);
      grub_strcpy (buf, "temporary");
      return;
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6:
      {
	char *ptr = buf;
	grub_uint64_t n = grub_be_to_cpu64 (target->ipv6[0]);
	int i;
	for (i = 0; i < 4; i++)
	  {
	    grub_snprintf (ptr, 6, "%" PRIxGRUB_UINT64_T ":",
			   (n >> (48 - 16 * i)) & 0xffff);
	    ptr += grub_strlen (ptr); 
	  }
	n  = grub_be_to_cpu64 (target->ipv6[1]);
	for (i = 0; i < 3; i++)
	  {
	    grub_snprintf (ptr, 6, "%" PRIxGRUB_UINT64_T ":",
			   (n >> (48 - 16 * i)) & 0xffff);
	    ptr += grub_strlen (ptr); 
	  }
	grub_snprintf (ptr, 5, "%" PRIxGRUB_UINT64_T, n & 0xffff);
	return;
      }
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4:
      {
	grub_uint32_t n = grub_be_to_cpu32 (target->ipv4);
	grub_snprintf (buf, GRUB_NET_MAX_STR_ADDR_LEN, "%d.%d.%d.%d",
		       ((n >> 24) & 0xff), ((n >> 16) & 0xff),
		       ((n >> 8) & 0xff), ((n >> 0) & 0xff));
      }
      return;
    }
  grub_snprintf (buf, GRUB_NET_MAX_STR_ADDR_LEN,
		 "Unknown address type %d", target->type);
}


void
grub_net_hwaddr_to_str (const grub_net_link_level_address_t *addr, char *str)
{
  char *ptr;
  unsigned i;
  int maxstr;

  if (addr->len > GRUB_NET_MAX_LINK_ADDRESS_SIZE)
    {
       str[0] = 0;
       grub_printf (_("Unsupported hw address type %d len %d\n"),
		    addr->type, addr->len);
       return;
    }
  maxstr = addr->len * grub_strlen ("XX:");
  for (ptr = str, i = 0; i < addr->len; i++)
    {
      ptr += grub_snprintf (ptr, maxstr - (ptr - str),
		     "%02x:", addr->mac[i] & 0xff);
    }
}

int
grub_net_hwaddr_cmp (const grub_net_link_level_address_t *a,
		     const grub_net_link_level_address_t *b)
{
  if (a->type < b->type)
    return -1;
  if (a->type > b->type)
    return +1;
  if (a->len < b->len)
    return -1;
  if (a->len > b->len)
    return +1;
  if (a->len > GRUB_NET_MAX_LINK_ADDRESS_SIZE)
    {
      grub_printf (_("Unsupported hw address type %d len %d\n"),
		   a->type, a->len);
      return + 1;
    }
  return grub_memcmp (a->mac, b->mac, a->len);
}

int
grub_net_addr_cmp (const grub_net_network_level_address_t *a,
		   const grub_net_network_level_address_t *b)
{
  if (a->type < b->type)
    return -1;
  if (a->type > b->type)
    return +1;
  switch (a->type)
    {
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4:
      return grub_memcmp (&a->ipv4, &b->ipv4, sizeof (a->ipv4));
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6:
      return grub_memcmp (&a->ipv6, &b->ipv6, sizeof (a->ipv6));
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_DHCP_RECV:
      return 0;
    }
  grub_printf (_("Unsupported address type %d\n"), a->type);
  return 1;
}

/* FIXME: implement this. */
static char *
hwaddr_set_env (struct grub_env_var *var __attribute__ ((unused)),
		const char *val __attribute__ ((unused)))
{
  return NULL;
}

/* FIXME: implement this. */
static char *
addr_set_env (struct grub_env_var *var __attribute__ ((unused)),
	      const char *val __attribute__ ((unused)))
{
  return NULL;
}

static char *
defserver_set_env (struct grub_env_var *var __attribute__ ((unused)),
		   const char *val)
{
  grub_free (grub_net_default_server);
  grub_net_default_server = grub_strdup (val);
  return grub_strdup (val);
}

static const char *
defserver_get_env (struct grub_env_var *var __attribute__ ((unused)),
		   const char *val __attribute__ ((unused)))
{
  if (!grub_net_default_server)
    grub_net_default_server = grub_strdup (grub_env_get ("net_pxe_next_server"));
  return grub_net_default_server ? : "";
}

static const char *
defip_get_env (struct grub_env_var *var __attribute__ ((unused)),
	       const char *val __attribute__ ((unused)))
{
  const char *intf = grub_env_get ("net_default_interface");
  const char *ret = NULL;
  if (intf)
    {
      char *buf = grub_xasprintf ("net_%s_ip", intf);
      if (buf)
	ret = grub_env_get (buf);
      grub_free (buf);
    }
  return ret;
}

static char *
defip_set_env (struct grub_env_var *var __attribute__ ((unused)),
	       const char *val)
{
  const char *intf = grub_env_get ("net_default_interface");
  if (intf)
    {
      char *buf = grub_xasprintf ("net_%s_ip", intf);
      if (buf)
	grub_env_set (buf, val);
      grub_free (buf);
    }
  return NULL;
}


static const char *
defmac_get_env (struct grub_env_var *var __attribute__ ((unused)),
	       const char *val __attribute__ ((unused)))
{
  const char *intf = grub_env_get ("net_default_interface");
  const char *ret = NULL;
  if (intf)
    {
      char *buf = grub_xasprintf ("net_%s_mac", intf);
      if (buf)
	ret = grub_env_get (buf);
      grub_free (buf);
    }
  return ret;
}

static char *
defmac_set_env (struct grub_env_var *var __attribute__ ((unused)),
	       const char *val)
{
  const char *intf = grub_env_get ("net_default_interface");
  if (intf)
    {
      char *buf = grub_xasprintf ("net_%s_mac", intf);
      if (buf)
	grub_env_set (buf, val);
      grub_free (buf);
    }
  return NULL;
}


static void
grub_net_network_level_interface_register (struct grub_net_network_level_interface *inter)
{
  {
    char buf[GRUB_NET_MAX_STR_HWADDR_LEN];
    char *name;
    char *ptr;
    grub_net_hwaddr_to_str (&inter->hwaddress, buf);
    name = grub_xasprintf ("net_%s_mac", inter->name);
    if (!name)
      return;
    for (ptr = name; *ptr; ptr++)
      if (*ptr == ':')
	*ptr = '_';    
    grub_env_set (name, buf);
    grub_register_variable_hook (name, 0, hwaddr_set_env);
    grub_env_export (name);
    grub_free (name);
  }

  {
    char buf[GRUB_NET_MAX_STR_ADDR_LEN];
    char *name;
    char *ptr;
    grub_net_addr_to_str (&inter->address, buf);
    name = grub_xasprintf ("net_%s_ip", inter->name);
    if (!name)
      return;
    for (ptr = name; *ptr; ptr++)
      if (*ptr == ':')
	*ptr = '_';    
    grub_env_set (name, buf);
    grub_register_variable_hook (name, 0, addr_set_env);
    grub_env_export (name);
    grub_free (name);
  }

  inter->card->num_ifaces++;
  inter->prev = &grub_net_network_level_interfaces;
  inter->next = grub_net_network_level_interfaces;
  if (inter->next)
    inter->next->prev = &inter->next;
  grub_net_network_level_interfaces = inter;
}

int
grub_ipv6_get_masksize (grub_uint16_t *be_mask)
{
  grub_uint8_t *mask;
  grub_uint16_t mask16[8];
  int x, y;
  int ret = 128;

  grub_memcpy (mask16, be_mask, sizeof (mask16));
  for (x = 0; x < 8; x++)
    mask16[x] = grub_be_to_cpu16 (mask16[x]);

  mask = (grub_uint8_t *)mask16;

  for (x = 15; x >= 0; x--)
    {
      grub_uint8_t octet = mask[x];
      if (!octet)
	{
	  ret -= 8;
	  continue;
	}
      for (y = 0; y < 8; y++)
	{
	  if (octet & (1 << y))
	    break;
	  else
	    ret--;
	}
      break;
    }

  return ret;
}

grub_err_t
grub_net_add_ipv6_local (struct grub_net_network_level_interface *inter,
			 int mask)
{
  struct grub_net_route *route;

  if (inter->address.type != GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6)
    return 0;

  if (mask == -1)
      mask = grub_ipv6_get_masksize ((grub_uint16_t *)inter->address.ipv6);

  if (mask == -1)
    return 0;

  route = grub_zalloc (sizeof (*route));
  if (!route)
    return grub_errno;

  route->name = grub_xasprintf ("%s:local", inter->name);
  if (!route->name)
    {
      grub_free (route);
      return grub_errno;
    }

  route->target.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6;
  grub_memcpy (route->target.ipv6.base, inter->address.ipv6,
	       sizeof (inter->address.ipv6));
  route->target.ipv6.masksize = mask;
  route->is_gateway = 0;
  route->interface = inter;

  grub_net_route_register (route);

  return 0;
}

grub_err_t
grub_net_add_ipv4_local (struct grub_net_network_level_interface *inter,
			 int mask)
{
  grub_uint32_t ip_cpu;
  struct grub_net_route *route;

  if (inter->address.type != GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4)
    return 0;

  ip_cpu = grub_be_to_cpu32 (inter->address.ipv4);

  if (mask == -1)
    {
      if (!(ip_cpu & 0x80000000))
	mask = 8;
      else if (!(ip_cpu & 0x40000000))
	mask = 16;
      else if (!(ip_cpu & 0x20000000))
	mask = 24;
    }
  if (mask == -1)
    return 0;

  route = grub_zalloc (sizeof (*route));
  if (!route)
    return grub_errno;

  route->name = grub_xasprintf ("%s:local", inter->name);
  if (!route->name)
    {
      grub_free (route);
      return grub_errno;
    }

  route->target.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4;
  route->target.ipv4.base = grub_cpu_to_be32 (ip_cpu & (0xffffffff << (32 - mask)));
  route->target.ipv4.masksize = mask;
  route->is_gateway = 0;
  route->interface = inter;

  grub_net_route_register (route);

  return 0;
}

/* FIXME: support MAC specifying.  */
static grub_err_t
grub_cmd_addaddr (struct grub_command *cmd __attribute__ ((unused)),
		  int argc, char **args)
{
  struct grub_net_card *card;
  grub_net_network_level_address_t addr;
  grub_err_t err;
  grub_net_interface_flags_t flags = 0;
  struct grub_net_network_level_interface *inf;

  if (argc != 3)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("three arguments expected"));
  
  FOR_NET_CARDS (card)
    if (grub_strcmp (card->name, args[1]) == 0)
      break;
  if (card == NULL)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("card not found")); 

  err = grub_net_resolve_address (args[2], &addr);
  if (err)
    return err;

  if (card->flags & GRUB_NET_CARD_NO_MANUAL_INTERFACES)
    return grub_error (GRUB_ERR_IO,
		       "this card doesn't support address addition");

  if (card->flags & GRUB_NET_CARD_HWADDRESS_IMMUTABLE)
    flags |= GRUB_NET_INTERFACE_HWADDRESS_IMMUTABLE;

  inf = grub_net_add_addr (args[0], card, &addr, &card->default_address,
			   flags);
  if (inf)
    grub_net_add_ipv4_local (inf, -1);

  return grub_errno;
}

static grub_err_t
grub_cmd_delroute (struct grub_command *cmd __attribute__ ((unused)),
		   int argc, char **args)
{
  struct grub_net_route *route;
  struct grub_net_route **prev;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("one argument expected"));
  
  for (prev = &grub_net_routes, route = *prev; route; prev = &((*prev)->next),
	 route = *prev)
    if (grub_strcmp (route->name, args[0]) == 0)
      {
	*prev = route->next;
	grub_free (route->name);
	grub_free (route);
	if (!*prev)
	  break;
      }

  return GRUB_ERR_NONE;
}

grub_err_t
grub_net_add_route (const char *name,
		    grub_net_network_level_netaddress_t target,
		    struct grub_net_network_level_interface *inter)
{
  struct grub_net_route *route;

  route = grub_zalloc (sizeof (*route));
  if (!route)
    return grub_errno;

  route->name = grub_strdup (name);
  if (!route->name)
    {
      grub_free (route);
      return grub_errno;
    }

  route->target = target;
  route->is_gateway = 0;
  route->interface = inter;

  grub_net_route_register (route);

  return GRUB_ERR_NONE;
}

grub_err_t
grub_net_add_route_gw (const char *name,
		       grub_net_network_level_netaddress_t target,
		       grub_net_network_level_address_t gw,
		       struct grub_net_network_level_interface *inter)
{
  struct grub_net_route *route;

  route = grub_zalloc (sizeof (*route));
  if (!route)
    return grub_errno;

  route->name = grub_strdup (name);
  if (!route->name)
    {
      grub_free (route);
      return grub_errno;
    }

  route->target = target;
  route->is_gateway = 1;
  route->gw = gw;
  route->interface = inter;

  grub_net_route_register (route);

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_addroute (struct grub_command *cmd __attribute__ ((unused)),
		  int argc, char **args)
{
  grub_net_network_level_netaddress_t target;
  if (argc < 3)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       N_("three arguments expected"));

  grub_net_resolve_net_address  (args[1], &target);
  
  if (grub_strcmp (args[2], "gw") == 0 && argc >= 4)
    {
      grub_err_t err;
      grub_net_network_level_address_t gw;

      err = grub_net_resolve_address (args[3], &gw);
      if (err)
	return err;
      return grub_net_add_route_gw (args[0], target, gw, NULL);
    }
  else
    {
      struct grub_net_network_level_interface *inter;

      FOR_NET_NETWORK_LEVEL_INTERFACES (inter)
	if (grub_strcmp (inter->name, args[2]) == 0)
	  break;

      if (!inter)
	return grub_error (GRUB_ERR_BAD_ARGUMENT,
			   N_("unrecognised network interface `%s'"), args[2]);
      return grub_net_add_route (args[0], target, inter);
    }
}

static void
print_net_address (const grub_net_network_level_netaddress_t *target)
{
  switch (target->type)
    {
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_DHCP_RECV:
      /* TRANSLATORS: it refers to the network address.  */
      grub_printf ("%s\n", _("temporary"));
      return;
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4:
      {
	grub_uint32_t n = grub_be_to_cpu32 (target->ipv4.base);
	grub_printf ("%d.%d.%d.%d/%d ", ((n >> 24) & 0xff),
		     ((n >> 16) & 0xff),
		     ((n >> 8) & 0xff),
		     ((n >> 0) & 0xff),
		     target->ipv4.masksize);
      }
      return;
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6:
      {
	char buf[GRUB_NET_MAX_STR_ADDR_LEN];
	struct grub_net_network_level_address base;
	base.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6;
	grub_memcpy (&base.ipv6, &target->ipv6, 16);
	grub_net_addr_to_str (&base, buf);
	grub_printf ("%s/%d ", buf, target->ipv6.masksize);
      }
      return;
    }
  grub_printf (_("Unknown address type %d\n"), target->type);
}

static void
print_address (const grub_net_network_level_address_t *target)
{
  char buf[GRUB_NET_MAX_STR_ADDR_LEN];
  grub_net_addr_to_str (target, buf);
  grub_xputs (buf);
}

static grub_err_t
grub_cmd_listroutes (struct grub_command *cmd __attribute__ ((unused)),
		     int argc __attribute__ ((unused)),
		     char **args __attribute__ ((unused)))
{
  struct grub_net_route *route;
  FOR_NET_ROUTES(route)
  {
    grub_printf ("%s ", route->name);
    print_net_address (&route->target);
    if (route->is_gateway)
      {
	grub_printf ("gw ");
	print_address (&route->gw);	
      }
    else
      grub_printf ("%s", route->interface->name);      
    grub_printf ("\n");
  }
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_listcards (struct grub_command *cmd __attribute__ ((unused)),
		    int argc __attribute__ ((unused)),
		    char **args __attribute__ ((unused)))
{
  struct grub_net_card *card;
  FOR_NET_CARDS(card)
  {
    char buf[GRUB_NET_MAX_STR_HWADDR_LEN];
    grub_net_hwaddr_to_str (&card->default_address, buf);
    grub_printf ("%s %s\n", card->name, buf);
  }
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_listaddrs (struct grub_command *cmd __attribute__ ((unused)),
		    int argc __attribute__ ((unused)),
		    char **args __attribute__ ((unused)))
{
  struct grub_net_network_level_interface *inf;
  FOR_NET_NETWORK_LEVEL_INTERFACES (inf)
  {
    char bufh[GRUB_NET_MAX_STR_HWADDR_LEN];
    char bufn[GRUB_NET_MAX_STR_ADDR_LEN];
    grub_net_hwaddr_to_str (&inf->hwaddress, bufh);
    grub_net_addr_to_str (&inf->address, bufn);
    grub_printf ("%s %s %s\n", inf->name, bufh, bufn);
  }
  return GRUB_ERR_NONE;
}

grub_net_app_level_t grub_net_app_level_list;
struct grub_net_socket *grub_net_sockets;

static grub_net_t
grub_net_open_real (const char *name)
{
  grub_net_app_level_t proto;
  const char *protname, *server;
  char *host;
  grub_size_t protnamelen;
  int try;
  int port = 0;

  if (grub_strncmp (name, "pxe:", sizeof ("pxe:") - 1) == 0)
    {
      protname = "tftp";
      protnamelen = sizeof ("tftp") - 1;
      server = name + sizeof ("pxe:") - 1;
    }
  else if (grub_strcmp (name, "pxe") == 0)
    {
      protname = "tftp";
      protnamelen = sizeof ("tftp") - 1;
      server = grub_net_default_server;
    }
  else
    {
      const char *comma;
      comma = grub_strchr (name, ',');
      if (comma)
	{
	  protnamelen = comma - name;
	  server = comma + 1;
	  protname = name;
	}
      else
	{
	  protnamelen = grub_strlen (name);
	  server = grub_net_default_server;
	  protname = name;
	}
    }
  if (!server)
    {
      grub_error (GRUB_ERR_NET_BAD_ADDRESS,
		  N_("no server is specified"));
      return NULL;
    }  

  char* port_start;
  /* ipv6 or port specified? */
  if ((port_start = grub_strchr (server, ':')))
    {
      char* ipv6_begin;
      if((ipv6_begin = grub_strchr (server, '[')))
	{
	  char* ipv6_end = grub_strchr (server, ']');
	  if(!ipv6_end)
	    {
	      grub_error (GRUB_ERR_NET_BAD_ADDRESS,
		      N_("mismatched [ in address"));
	      return NULL;
	    }
	  /* port number after bracketed ipv6 addr */
	  if(ipv6_end[1] == ':')
	    {
	      port = grub_strtoul (ipv6_end + 2, NULL, 10);
	      if(port > 65535)
		{
		  grub_error (GRUB_ERR_NET_BAD_ADDRESS,
			  N_("bad port number"));
		  return NULL;
		}
	    }
	  host = grub_strndup (ipv6_begin, (ipv6_end - ipv6_begin) + 1);
	}
      else
	{
	  if (grub_strchr (port_start + 1, ':'))
	    {
	      int iplen = grub_strlen (server);
	      /* bracket bare ipv6 addrs */
	      host = grub_malloc (iplen + 3);
	      if(!host)
		{
		  return NULL;
		}
	      host[0] = '[';
	      grub_memcpy (host + 1, server, iplen);
	      host[iplen + 1] = ']';
	      host[iplen + 2] = '\0';
	    }
	  else
	    {
	      /* hostname:port or ipv4:port */
	      port = grub_strtol (port_start + 1, NULL, 10);
	      if(port > 65535)
		{
		  grub_error (GRUB_ERR_NET_BAD_ADDRESS,
			  N_("bad port number"));
		  return NULL;
		}
	      host = grub_strndup (server, port_start - server);
	    }
	}
    }
  else
    {
      host = grub_strdup (server);
    }
  if (!host)
    {
      return NULL;
    }

  for (try = 0; try < 2; try++)
    {
      FOR_NET_APP_LEVEL (proto)
      {
	if (grub_memcmp (proto->name, protname, protnamelen) == 0
	    && proto->name[protnamelen] == 0)
	  {
	    grub_net_t ret = grub_zalloc (sizeof (*ret));
	    if (!ret)
	      {
		grub_free (host);
		return NULL;
	      }
	    ret->protocol = proto;
	    ret->port = port;
	    ret->server = host;
	    ret->fs = &grub_net_fs;
	    return ret;
	  }
      }
      if (try == 0)
	{
	  const char *prefix, *root;
	  char *prefdev, *comma;
	  int skip = 0;
	  grub_size_t devlen;

	  /* Do not attempt to load module if it requires protocol provided
	     by this module - it results in infinite recursion. Just continue,
	     fail and cleanup on next iteration.
	   */
	  prefix = grub_env_get ("prefix");
	  if (!prefix)
	    continue;

	  prefdev = grub_file_get_device_name (prefix);
	  if (!prefdev)
	    {
	      root = grub_env_get ("root");
	      if (!root)
		continue;
	      prefdev = grub_strdup (root);
	      if (!prefdev)
		continue;
	    }

	  if (grub_strncmp (prefdev, "pxe", sizeof ("pxe") - 1) == 0 &&
	      (!prefdev[sizeof ("pxe") - 1] || (prefdev[sizeof("pxe") - 1] == ':')))
	    {
	      grub_free (prefdev);
	      prefdev = grub_strdup ("tftp");
	      if (!prefdev)
		continue;
	    }

	  comma = grub_strchr (prefdev, ',');
	  if (comma)
	    *comma = '\0';
	  devlen = grub_strlen (prefdev);

	  if (protnamelen == devlen && grub_memcmp (prefdev, protname, devlen) == 0)
	    skip = 1;

	  grub_free (prefdev);

	  if (skip)
	    continue;

	  if (sizeof ("http") - 1 == protnamelen
	      && grub_memcmp ("http", protname, protnamelen) == 0)
	    {
	      grub_dl_load ("http");
	      grub_errno = GRUB_ERR_NONE;
	      continue;
	    }
	  if (sizeof ("tftp") - 1 == protnamelen
	      && grub_memcmp ("tftp", protname, protnamelen) == 0)
	    {
	      grub_dl_load ("tftp");
	      grub_errno = GRUB_ERR_NONE;
	      continue;
	    }
	}
      break;
    }

  /* Restore original error.  */
  grub_error (GRUB_ERR_UNKNOWN_DEVICE, N_("disk `%s' not found"),
	      name);

  grub_free (host);
  return NULL;
}

static grub_err_t
grub_net_fs_dir (grub_device_t device, const char *path __attribute__ ((unused)),
		 grub_fs_dir_hook_t hook __attribute__ ((unused)),
		 void *hook_data __attribute__ ((unused)))
{
  if (!device->net)
    return grub_error (GRUB_ERR_BUG, "invalid net device");
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_net_fs_open (struct grub_file *file_out, const char *name)
{
  grub_err_t err;
  struct grub_file *file, *bufio;

  file = grub_malloc (sizeof (*file));
  if (!file)
    return grub_errno;

  grub_memcpy (file, file_out, sizeof (struct grub_file));
  file->device->net->packs.first = NULL;
  file->device->net->packs.last = NULL;
  file->device->net->name = grub_strdup (name);
  if (!file->device->net->name)
    {
      grub_free (file);
      return grub_errno;
    }

  err = file->device->net->protocol->open (file, name);
  if (err)
    {
      while (file->device->net->packs.first)
	{
	  grub_netbuff_free (file->device->net->packs.first->nb);
	  grub_net_remove_packet (file->device->net->packs.first);
	}
      grub_free (file->device->net->name);
      grub_free (file);
      return err;
    }
  bufio = grub_bufio_open (file, 32768);
  if (! bufio)
    {
      while (file->device->net->packs.first)
	{
	  grub_netbuff_free (file->device->net->packs.first->nb);
	  grub_net_remove_packet (file->device->net->packs.first);
	}
      file->device->net->protocol->close (file);
      grub_free (file->device->net->name);
      grub_free (file);
      return grub_errno;
    }

  grub_memcpy (file_out, bufio, sizeof (struct grub_file));
  grub_free (bufio);
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_net_fs_close (grub_file_t file)
{
  while (file->device->net->packs.first)
    {
      grub_netbuff_free (file->device->net->packs.first->nb);
      grub_net_remove_packet (file->device->net->packs.first);
    }
  file->device->net->protocol->close (file);
  grub_free (file->device->net->name);
  return GRUB_ERR_NONE;
}

static void
receive_packets (struct grub_net_card *card, int *stop_condition)
{
  int received = 0;
  if (card->num_ifaces == 0)
    return;
  if (!card->opened)
    {
      grub_err_t err = GRUB_ERR_NONE;
      if (card->driver->open)
	err = card->driver->open (card);
      if (err)
	{
	  grub_errno = GRUB_ERR_NONE;
	  return;
	}
      card->opened = 1;
    }
  while (received < 100)
    {
      /* Maybe should be better have a fixed number of packets for each card
	 and just mark them as used and not used.  */ 
      struct grub_net_buff *nb;

      if (received > 10 && stop_condition && *stop_condition)
	break;

      nb = card->driver->recv (card);
      if (!nb)
	{
	  card->last_poll = grub_get_time_ms ();
	  break;
	}
      received++;
      grub_net_recv_ethernet_packet (nb, card);
      if (grub_errno)
	{
	  grub_dprintf ("net", "error receiving: %d: %s\n", grub_errno,
			grub_errmsg);
	  grub_errno = GRUB_ERR_NONE;
	}
    }
  grub_print_error ();
}

static char *
grub_env_write_readonly (struct grub_env_var *var __attribute__ ((unused)),
			 const char *val __attribute__ ((unused)))
{
  return NULL;
}

grub_err_t
grub_env_set_net_property (const char *intername, const char *suffix,
                           const char *value, grub_size_t len)
{
  char *varname, *varvalue;
  char *ptr;

  varname = grub_xasprintf ("net_%s_%s", intername, suffix);
  if (!varname)
    return grub_errno;
  for (ptr = varname; *ptr; ptr++)
    if (*ptr == ':')
      *ptr = '_';
  varvalue = grub_malloc (len + 1);
  if (!varvalue)
    {
      grub_free (varname);
      return grub_errno;
    }

  grub_memcpy (varvalue, value, len);
  varvalue[len] = 0;
  grub_err_t ret = grub_env_set (varname, varvalue);
  grub_register_variable_hook (varname, 0, grub_env_write_readonly);
  grub_env_export (varname);
  grub_free (varname);
  grub_free (varvalue);

  return ret;
}

void
grub_net_poll_cards (unsigned time, int *stop_condition)
{
  struct grub_net_card *card;
  grub_uint64_t start_time;
  start_time = grub_get_time_ms ();
  while ((grub_get_time_ms () - start_time) < time
	 && (!stop_condition || !*stop_condition))
    FOR_NET_CARDS (card)
      receive_packets (card, stop_condition);
  grub_net_tcp_retransmit ();
}

static void
grub_net_poll_cards_idle_real (void)
{
  struct grub_net_card *card;
  FOR_NET_CARDS (card)
  {
    grub_uint64_t ctime = grub_get_time_ms ();

    if (ctime < card->last_poll
	|| ctime >= card->last_poll + card->idle_poll_delay_ms)
      receive_packets (card, 0);
  }
  grub_net_tcp_retransmit ();
}

/*  Read from the packets list*/
static grub_ssize_t
grub_net_fs_read_real (grub_file_t file, char *buf, grub_size_t len)
{
  grub_net_t net = file->device->net;
  struct grub_net_buff *nb;
  char *ptr = buf;
  grub_size_t amount, total = 0;
  int try = 0;

  while (try <= GRUB_NET_TRIES)
    {
      while (net->packs.first)
	{
	  try = 0;
	  nb = net->packs.first->nb;
	  amount = nb->tail - nb->data;
	  if (amount > len)
	    amount = len;
	  len -= amount;
	  total += amount;
	  file->device->net->offset += amount;
	  if (grub_file_progress_hook)
	    grub_file_progress_hook (0, 0, amount, file);
	  if (buf)
	    {
	      grub_memcpy (ptr, nb->data, amount);
	      ptr += amount;
	    }
	  if (amount == (grub_size_t) (nb->tail - nb->data))
	    {
	      grub_netbuff_free (nb);
	      grub_net_remove_packet (net->packs.first);
	    }
	  else
	    nb->data += amount;

	  if (!len)
	    {
	      if (net->protocol->packets_pulled)
		net->protocol->packets_pulled (file);
	      return total;
	    }
	}
      if (net->protocol->packets_pulled)
	net->protocol->packets_pulled (file);

      if (!net->eof)
	{
	  try++;
	  grub_net_poll_cards (GRUB_NET_INTERVAL +
                               (try * GRUB_NET_INTERVAL_ADDITION), &net->stall);
        }
      else
	return total;
    }
  grub_error (GRUB_ERR_TIMEOUT, N_("timeout reading `%s'"), net->name);
  return -1;
}

static grub_off_t
have_ahead (struct grub_file *file)
{
  grub_net_t net = file->device->net;
  grub_off_t ret = net->offset;
  struct grub_net_packet *pack;
  for (pack = net->packs.first; pack; pack = pack->next)
    ret += pack->nb->tail - pack->nb->data;
  return ret;
}

static grub_err_t 
grub_net_seek_real (struct grub_file *file, grub_off_t offset)
{
  if (offset == file->device->net->offset)
    return GRUB_ERR_NONE;

  if (offset > file->device->net->offset)
    {
      if (!file->device->net->protocol->seek || have_ahead (file) >= offset)
	{
	  grub_net_fs_read_real (file, NULL,
				 offset - file->device->net->offset);
	  return grub_errno;
	}
      return file->device->net->protocol->seek (file, offset);
    }

  {
    grub_err_t err;
    if (file->device->net->protocol->seek)
      return file->device->net->protocol->seek (file, offset);
    while (file->device->net->packs.first)
      {
	grub_netbuff_free (file->device->net->packs.first->nb);
	grub_net_remove_packet (file->device->net->packs.first);
      }
    file->device->net->protocol->close (file);

    file->device->net->packs.first = NULL;
    file->device->net->packs.last = NULL;
    file->device->net->offset = 0;
    file->device->net->eof = 0;
    file->device->net->stall = 0;
    err = file->device->net->protocol->open (file, file->device->net->name);
    if (err)
      return err;
    grub_net_fs_read_real (file, NULL, offset);
    return grub_errno;
  }
}

static grub_ssize_t
grub_net_fs_read (grub_file_t file, char *buf, grub_size_t len)
{
  if (file->offset != file->device->net->offset)
    {
      grub_err_t err;
      err = grub_net_seek_real (file, file->offset);
      if (err)
	return err;
    }
  return grub_net_fs_read_real (file, buf, len);
}

static struct grub_fs grub_net_fs =
  {
    .name = "netfs",
    .fs_dir = grub_net_fs_dir,
    .fs_open = grub_net_fs_open,
    .fs_read = grub_net_fs_read,
    .fs_close = grub_net_fs_close,
    .fs_label = NULL,
    .fs_uuid = NULL,
    .fs_mtime = NULL,
  };

static grub_err_t
grub_net_fini_hw (int noreturn __attribute__ ((unused)))
{
  struct grub_net_card *card;
  FOR_NET_CARDS (card) 
    if (card->opened)
      {
	if (card->driver->close)
	  card->driver->close (card);
	card->opened = 0;
      }
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_net_restore_hw (void)
{
  return GRUB_ERR_NONE;
}


static int
grub_config_search_through (char *config, char *suffix,
			    grub_size_t num_tries, grub_size_t slice_size)
{
  while (num_tries-- > 0)
    {
      grub_file_t file;

      grub_dprintf ("net", "attempt to fetch config %s\n", config);

      file = grub_file_open (config, GRUB_FILE_TYPE_CONFIG);

      if (file)
        {
          grub_file_close (file);
          return 0;
        }
      else
        {
          if (grub_errno == GRUB_ERR_IO)
            grub_errno = GRUB_ERR_NONE;
        }

      if (grub_strlen (suffix) < slice_size)
        break;

      config[grub_strlen (config) - slice_size] = '\0';
    }

  return 1;
}

grub_err_t
grub_net_search_config_file (char *config)
{
  grub_size_t config_len;
  char *suffix;

  config_len = grub_strlen (config);
  config[config_len] = '-';
  suffix = config + config_len + 1;

  struct grub_net_network_level_interface *inf;
  FOR_NET_NETWORK_LEVEL_INTERFACES (inf)
    {
      /* By the Client UUID. */
      char *ptr;
      int client_uuid_len;
      char *client_uuid_var;
      const char *client_uuid;

      client_uuid_len = sizeof ("net_") + grub_strlen (inf->name) +
                        sizeof ("_clientuuid") + 1;

      client_uuid_var = grub_zalloc (client_uuid_len);
      if (!client_uuid_var)
        return grub_errno;

      grub_snprintf (client_uuid_var, client_uuid_len,
                     "net_%s_clientuuid", inf->name);

      client_uuid = grub_env_get (client_uuid_var);
      grub_free (client_uuid_var);

      if (client_uuid)
        {
          grub_strcpy (suffix, client_uuid);
          if (grub_config_search_through (config, suffix, 1, 0) == 0)
            return GRUB_ERR_NONE;
        }

      /* By the MAC address. */

      /* Add ethernet type */
      grub_strcpy (suffix, "01-");

      grub_net_hwaddr_to_str (&inf->hwaddress, suffix + 3);

      for (ptr = suffix; *ptr; ptr++)
        if (*ptr == ':')
          *ptr = '-';

      if (grub_config_search_through (config, suffix, 1, 0) == 0)
        return GRUB_ERR_NONE;

      /* By IP address */

      switch ((&inf->address)->type)
        {
        case GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4:
          {
            grub_uint32_t n = grub_be_to_cpu32 ((&inf->address)->ipv4);

            grub_snprintf (suffix, GRUB_NET_MAX_STR_ADDR_LEN, "%02X%02X%02X%02X", \
                           ((n >> 24) & 0xff), ((n >> 16) & 0xff),      \
                           ((n >> 8) & 0xff), ((n >> 0) & 0xff));

            if (grub_config_search_through (config, suffix, 8, 1) == 0)
              return GRUB_ERR_NONE;
            break;
          }
        case GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6:
          {
            char buf[GRUB_NET_MAX_STR_ADDR_LEN];
            struct grub_net_network_level_address base;
            base.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6;
            grub_memcpy (&base.ipv6, ((&inf->address)->ipv6), 16);
            grub_net_addr_to_str (&base, buf);

            for (ptr = buf; *ptr; ptr++)
              if (*ptr == ':')
                *ptr = '-';

            grub_snprintf (suffix, GRUB_NET_MAX_STR_ADDR_LEN, "%s", buf);
            if (grub_config_search_through (config, suffix, 1, 0) == 0)
              return GRUB_ERR_NONE;
            break;
          }
        case GRUB_NET_NETWORK_LEVEL_PROTOCOL_DHCP_RECV:
          return grub_error (GRUB_ERR_BUG, "shouldn't reach here");
        default:
          return grub_error (GRUB_ERR_BUG,
                             "unsupported address type %d", (&inf->address)->type);
        }
    }

  /* Remove the remaining minus sign at the end. */
  config[config_len] = '\0';

  return GRUB_ERR_NONE;
}

static struct grub_preboot *fini_hnd;

static grub_command_t cmd_addaddr, cmd_deladdr, cmd_addroute, cmd_delroute;
static grub_command_t cmd_lsroutes, cmd_lscards;
static grub_command_t cmd_lsaddr, cmd_slaac;

#ifdef GRUB_MACHINE_EFI

static enum {
  INIT_MODE_NONE,
  INIT_MODE_GRUB,
  INIT_MODE_EFI
} init_mode;

static grub_command_t cmd_bootp, cmd_bootp6;

#endif

GRUB_MOD_INIT(net)
{
#ifdef GRUB_MACHINE_EFI
  if (grub_net_open)
    return;

  if ((grub_efi_net_boot_from_https () || grub_efi_net_boot_from_opa ())
      && grub_efi_net_fs_init ())
    {
      cmd_lsroutes = grub_register_command ("net_ls_routes", grub_efi_net_list_routes,
					    "", N_("list network routes"));
      cmd_lscards = grub_register_command ("net_ls_cards", grub_efi_net_list_cards,
					   "", N_("list network cards"));
      cmd_lsaddr = grub_register_command ("net_ls_addr", grub_efi_net_list_addrs,
					  "", N_("list network addresses"));
      cmd_addaddr = grub_register_command ("net_add_addr", grub_efi_net_add_addr,
					    /* TRANSLATORS: HWADDRESS stands for
					       "hardware address".  */
					  N_("SHORTNAME CARD ADDRESS [HWADDRESS]"),
					  N_("Add a network address."));
      cmd_bootp = grub_register_command ("net_bootp", grub_efi_net_bootp,
					 N_("[CARD]"),
					 N_("perform a bootp autoconfiguration"));
      cmd_bootp6 = grub_register_command ("net_bootp6", grub_efi_net_bootp6,
					 N_("[CARD]"),
					 N_("perform a bootp autoconfiguration"));
      init_mode = INIT_MODE_EFI;
      return;
    }
#endif

  grub_register_variable_hook ("net_default_server", defserver_get_env,
			       defserver_set_env);
  grub_env_export ("net_default_server");
  grub_register_variable_hook ("pxe_default_server", defserver_get_env,
			       defserver_set_env);
  grub_env_export ("pxe_default_server");
  grub_register_variable_hook ("net_default_ip", defip_get_env,
			       defip_set_env);
  grub_env_export ("net_default_ip");
  grub_register_variable_hook ("net_default_mac", defmac_get_env,
			       defmac_set_env);
  grub_env_export ("net_default_mac");

  cmd_addaddr = grub_register_command ("net_add_addr", grub_cmd_addaddr,
					/* TRANSLATORS: HWADDRESS stands for
					   "hardware address".  */
				       N_("SHORTNAME CARD ADDRESS [HWADDRESS]"),
				       N_("Add a network address."));
  cmd_slaac = grub_register_command ("net_ipv6_autoconf",
				     grub_cmd_ipv6_autoconf,
				     N_("[CARD [HWADDRESS]]"),
				     N_("Perform an IPV6 autoconfiguration"));

  cmd_deladdr = grub_register_command ("net_del_addr", grub_cmd_deladdr,
				       N_("SHORTNAME"),
				       N_("Delete a network address."));
  cmd_addroute = grub_register_command ("net_add_route", grub_cmd_addroute,
					/* TRANSLATORS: "gw" is a keyword.  */
					N_("SHORTNAME NET [INTERFACE| gw GATEWAY]"),
					N_("Add a network route."));
  cmd_delroute = grub_register_command ("net_del_route", grub_cmd_delroute,
					N_("SHORTNAME"),
					N_("Delete a network route."));
  cmd_lsroutes = grub_register_command ("net_ls_routes", grub_cmd_listroutes,
					"", N_("list network routes"));
  cmd_lscards = grub_register_command ("net_ls_cards", grub_cmd_listcards,
				       "", N_("list network cards"));
  cmd_lsaddr = grub_register_command ("net_ls_addr", grub_cmd_listaddrs,
				       "", N_("list network addresses"));
  grub_bootp_init ();
  grub_dns_init ();

  grub_net_open = grub_net_open_real;
  fini_hnd = grub_loader_register_preboot_hook (grub_net_fini_hw,
						grub_net_restore_hw,
						GRUB_LOADER_PREBOOT_HOOK_PRIO_DISK);
  grub_net_poll_cards_idle = grub_net_poll_cards_idle_real;

#ifdef GRUB_MACHINE_EFI
  grub_env_set ("grub_netfs_type", "grub");
  grub_register_variable_hook ("grub_netfs_type", 0, grub_env_write_readonly);
  grub_env_export ("grub_netfs_type");
  init_mode = INIT_MODE_GRUB;
#endif

}

GRUB_MOD_FINI(net)
{

#ifdef GRUB_MACHINE_EFI
  if (init_mode == INIT_MODE_NONE)
    return;

  if (init_mode == INIT_MODE_EFI)
    {
      grub_unregister_command (cmd_lsroutes);
      grub_unregister_command (cmd_lscards);
      grub_unregister_command (cmd_lsaddr);
      grub_unregister_command (cmd_addaddr);
      grub_unregister_command (cmd_bootp);
      grub_unregister_command (cmd_bootp6);
      grub_efi_net_fs_fini ();
      init_mode = INIT_MODE_NONE;
      return;
    }
#endif

  grub_register_variable_hook ("net_default_server", 0, 0);
  grub_register_variable_hook ("pxe_default_server", 0, 0);

  grub_bootp_fini ();
  grub_dns_fini ();
  grub_unregister_command (cmd_addaddr);
  grub_unregister_command (cmd_deladdr);
  grub_unregister_command (cmd_addroute);
  grub_unregister_command (cmd_delroute);
  grub_unregister_command (cmd_lsroutes);
  grub_unregister_command (cmd_lscards);
  grub_unregister_command (cmd_lsaddr);
  grub_unregister_command (cmd_slaac);
  grub_fs_unregister (&grub_net_fs);
  grub_net_open = NULL;
  grub_net_fini_hw (0);
  grub_loader_unregister_preboot_hook (fini_hnd);
  grub_net_poll_cards_idle = grub_net_poll_cards_idle_real;
#ifdef GRUB_MACHINE_EFI
  init_mode = INIT_MODE_NONE;
#endif
}
