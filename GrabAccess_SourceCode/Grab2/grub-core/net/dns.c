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
#include <grub/net/udp.h>
#include <grub/command.h>
#include <grub/i18n.h>
#include <grub/err.h>
#include <grub/time.h>
#include <grub/safemath.h>

struct dns_cache_element
{
  char *name;
  grub_size_t naddresses;
  struct grub_net_network_level_address *addresses;
  grub_uint64_t limit_time;
};

#define DNS_CACHE_SIZE 1021
#define DNS_HASH_BASE 423

typedef enum grub_dns_qtype_id
  {
    GRUB_DNS_QTYPE_A = 1,
    GRUB_DNS_QTYPE_AAAA = 28
  } grub_dns_qtype_id_t;

static struct dns_cache_element dns_cache[DNS_CACHE_SIZE];
static struct grub_net_network_level_address *dns_servers;
static grub_size_t dns_nservers, dns_servers_alloc;

grub_err_t
grub_net_add_dns_server (const struct grub_net_network_level_address *s)
{
  if (dns_servers_alloc <= dns_nservers)
    {
      int na = dns_servers_alloc * 2;
      struct grub_net_network_level_address *ns;
      grub_size_t sz;

      if (na < 8)
	na = 8;

      if (grub_mul (na, sizeof (ns[0]), &sz))
	return GRUB_ERR_OUT_OF_RANGE;

      ns = grub_realloc (dns_servers, sz);
      if (!ns)
	return grub_errno;
      dns_servers_alloc = na;
      dns_servers = ns;
    }
  dns_servers[dns_nservers++] = *s;
  return GRUB_ERR_NONE;
}

void
grub_net_remove_dns_server (const struct grub_net_network_level_address *s)
{
  grub_size_t i;
  for (i = 0; i < dns_nservers; i++)
    if (grub_net_addr_cmp (s, &dns_servers[i]) == 0)
      break;
  if (i < dns_nservers)
    {
      dns_servers[i] = dns_servers[dns_nservers - 1];
      dns_nservers--;
    }
}

struct dns_header
{
  grub_uint16_t id;
  grub_uint8_t flags;
  grub_uint8_t ra_z_r_code;
  grub_uint16_t qdcount;
  grub_uint16_t ancount;
  grub_uint16_t nscount;
  grub_uint16_t arcount;
} GRUB_PACKED;

enum
  {
    FLAGS_RESPONSE = 0x80,
    FLAGS_OPCODE = 0x78,
    FLAGS_RD = 0x01
  };

enum
  {
    ERRCODE_MASK = 0x0f
  };

enum
  {
    DNS_PORT = 53
  };

struct recv_data
{
  grub_size_t *naddresses;
  struct grub_net_network_level_address **addresses;
  int cache;
  grub_uint16_t id;
  int dns_err;
  char *name;
  const char *oname;
  int stop;
};

static inline int
hash (const char *str)
{
  unsigned v = 0, xn = 1;
  const char *ptr;
  for (ptr = str; *ptr; )
    {
      v = (v + xn * *ptr);
      xn = (DNS_HASH_BASE * xn) % DNS_CACHE_SIZE;
      ptr++;
      if (((ptr - str) & 0x3ff) == 0)
	v %= DNS_CACHE_SIZE;
    }
  return v % DNS_CACHE_SIZE;
}

static int
check_name_real (const grub_uint8_t *name_at, const grub_uint8_t *head,
		 const grub_uint8_t *tail, const char *check_with,
		 int *length, char *set)
{
  const char *readable_ptr = check_with;
  const grub_uint8_t *ptr;
  char *optr = set;
  int bytes_processed = 0;
  if (length)
    *length = 0;
  for (ptr = name_at; ptr < tail && bytes_processed < tail - head + 2; )
    {
      /* End marker.  */
      if (!*ptr)
	{
	  if (length && *length)
	    (*length)--;
	  if (optr && optr != set)
	    optr--;
	  if (optr)
	    *optr = 0;
	  return !readable_ptr || (*readable_ptr == 0);
	}
      if (*ptr & 0xc0)
	{
	  bytes_processed += 2;
	  if (ptr + 1 >= tail)
	    return 0;
	  ptr = head + (((ptr[0] & 0x3f) << 8) | ptr[1]);
	  continue;
	}
      if (readable_ptr && grub_memcmp (ptr + 1, readable_ptr, *ptr) != 0)
	return 0;
      if (grub_memchr (ptr + 1, 0, *ptr) 
	  || grub_memchr (ptr + 1, '.', *ptr))
	return 0;
      if (readable_ptr)
	readable_ptr += *ptr;
      if (readable_ptr && *readable_ptr != '.' && *readable_ptr != 0)
	return 0;
      bytes_processed += *ptr + 1;
      if (length)
	*length += *ptr + 1;
      if (optr)
	{
	  grub_memcpy (optr, ptr + 1, *ptr);
	  optr += *ptr;
	}
      if (optr)
	*optr++ = '.';
      if (readable_ptr && *readable_ptr)
	readable_ptr++;
      ptr += *ptr + 1;
    }
  return 0;
}

static int
check_name (const grub_uint8_t *name_at, const grub_uint8_t *head,
	    const grub_uint8_t *tail, const char *check_with)
{
  return check_name_real (name_at, head, tail, check_with, NULL, NULL);
}

static char *
get_name (const grub_uint8_t *name_at, const grub_uint8_t *head,
	  const grub_uint8_t *tail)
{
  int length;
  char *ret;

  if (!check_name_real (name_at, head, tail, NULL, &length, NULL))
    return NULL;
  ret = grub_malloc (length + 1);
  if (!ret)
    return NULL;
  if (!check_name_real (name_at, head, tail, NULL, NULL, ret))
    {
      grub_free (ret);
      return NULL;
    }
  return ret;
}

enum
  {
    DNS_CLASS_A = 1,
    DNS_CLASS_CNAME = 5,
    DNS_CLASS_AAAA = 28
  };

static grub_err_t 
recv_hook (grub_net_udp_socket_t sock __attribute__ ((unused)),
	   struct grub_net_buff *nb,
	   void *data_)
{
  struct dns_header *head;
  struct recv_data *data = data_;
  int i, j;
  grub_uint8_t *ptr, *reparse_ptr;
  int redirect_cnt = 0;
  char *redirect_save = NULL;
  grub_uint32_t ttl_all = ~0U;

  /* Code apparently assumed that only one packet is received as response.
     We may get multiple responses due to network condition, so check here
     and quit early. */
  if (*data->addresses)
    {
      grub_netbuff_free (nb);
      return GRUB_ERR_NONE;
    }

  head = (struct dns_header *) nb->data;
  ptr = (grub_uint8_t *) (head + 1);
  if (ptr >= nb->tail)
    {
      grub_netbuff_free (nb);
      return GRUB_ERR_NONE;
    }
  
  if (head->id != data->id)
    {
      grub_netbuff_free (nb);
      return GRUB_ERR_NONE;
    }
  if (!(head->flags & FLAGS_RESPONSE) || (head->flags & FLAGS_OPCODE))
    {
      grub_netbuff_free (nb);
      return GRUB_ERR_NONE;
    }
  if (head->ra_z_r_code & ERRCODE_MASK)
    {
      data->dns_err = 1;
      grub_netbuff_free (nb);
      return GRUB_ERR_NONE;
    }
  for (i = 0; i < grub_be_to_cpu16 (head->qdcount); i++)
    {
      if (ptr >= nb->tail)
	{
	  grub_netbuff_free (nb);
	  return GRUB_ERR_NONE;
	}
      while (ptr < nb->tail && !((*ptr & 0xc0) || *ptr == 0))
	ptr += *ptr + 1;
      if (ptr < nb->tail && (*ptr & 0xc0))
	ptr++;
      ptr++;
      ptr += 4;
    }
  *data->addresses = grub_calloc (grub_be_to_cpu16 (head->ancount),
				  sizeof ((*data->addresses)[0]));
  if (!*data->addresses)
    {
      grub_errno = GRUB_ERR_NONE;
      grub_netbuff_free (nb);
      return GRUB_ERR_NONE;
    }
  reparse_ptr = ptr;
 reparse:
  for (i = 0, ptr = reparse_ptr; i < grub_be_to_cpu16 (head->ancount); i++)
    {
      int ignored = 0;
      grub_uint8_t class;
      grub_uint32_t ttl = 0;
      grub_uint16_t length;
      if (ptr >= nb->tail)
	{
	  if (!*data->naddresses)
	    grub_free (*data->addresses);
	  return GRUB_ERR_NONE;
	}
      ignored = !check_name (ptr, nb->data, nb->tail, data->name);
      while (ptr < nb->tail && !((*ptr & 0xc0) || *ptr == 0))
	ptr += *ptr + 1;
      if (ptr < nb->tail && (*ptr & 0xc0))
	ptr++;
      ptr++;
      if (ptr + 10 >= nb->tail)
	{
	  if (!*data->naddresses)
	    grub_free (*data->addresses);
	  grub_netbuff_free (nb);
	  return GRUB_ERR_NONE;
	}
      if (*ptr++ != 0)
	ignored = 1;
      class = *ptr++;
      if (*ptr++ != 0)
	ignored = 1;
      if (*ptr++ != 1)
	ignored = 1;
      for (j = 0; j < 4; j++)
	{
	  ttl <<= 8;
	  ttl |= *ptr++;
	}
      length = *ptr++ << 8;
      length |= *ptr++;
      if (ptr + length > nb->tail)
	{
	  if (!*data->naddresses)
	    grub_free (*data->addresses);
	  grub_netbuff_free (nb);
	  return GRUB_ERR_NONE;
	}
      if (!ignored)
	{
	  if (ttl_all > ttl)
	    ttl_all = ttl;
	  switch (class)
	    {
	    case DNS_CLASS_A:
	      if (length != 4)
		break;
	      (*data->addresses)[*data->naddresses].type
		= GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4;
	      grub_memcpy (&(*data->addresses)[*data->naddresses].ipv4,
			   ptr, 4);
	      (*data->naddresses)++;
	      data->stop = 1;
	      break;
	    case DNS_CLASS_AAAA:
	      if (length != 16)
		break;
	      (*data->addresses)[*data->naddresses].type
		= GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6;
	      grub_memcpy (&(*data->addresses)[*data->naddresses].ipv6,
			   ptr, 16);
	      (*data->naddresses)++;
	      data->stop = 1;
	      break;
	    case DNS_CLASS_CNAME:
	      if (!(redirect_cnt & (redirect_cnt - 1)))
		{
		  grub_free (redirect_save);
		  redirect_save = data->name;
		}
	      else
		grub_free (data->name);
	      redirect_cnt++;
	      data->name = get_name (ptr, nb->data, nb->tail);
	      if (!data->name)
		{
		  data->dns_err = 1;
		  grub_errno = 0;
		  return GRUB_ERR_NONE;
		}
	      grub_dprintf ("dns", "CNAME %s\n", data->name);
	      if (grub_strcmp (redirect_save, data->name) == 0)
		{
		  data->dns_err = 1;
		  grub_free (redirect_save);
		  return GRUB_ERR_NONE;
		}
	      goto reparse;
	    }
	}
      ptr += length;
    }
  if (ttl_all && *data->naddresses && data->cache)
    {
      int h;
      grub_dprintf ("dns", "caching for %d seconds\n", ttl_all);
      h = hash (data->oname);
      grub_free (dns_cache[h].name);
      dns_cache[h].name = 0;
      grub_free (dns_cache[h].addresses);
      dns_cache[h].addresses = 0;
      dns_cache[h].name = grub_strdup (data->oname);
      dns_cache[h].naddresses = *data->naddresses;
      dns_cache[h].addresses = grub_calloc (*data->naddresses,
					    sizeof (dns_cache[h].addresses[0]));
      dns_cache[h].limit_time = grub_get_time_ms () + 1000 * ttl_all;
      if (!dns_cache[h].addresses || !dns_cache[h].name)
	{
	  grub_free (dns_cache[h].name);
	  dns_cache[h].name = 0;
	  grub_free (dns_cache[h].addresses);
	  dns_cache[h].addresses = 0;
	}
      grub_memcpy (dns_cache[h].addresses, *data->addresses,
		   *data->naddresses
		   * sizeof (dns_cache[h].addresses[0]));
    }
  grub_netbuff_free (nb);
  grub_free (redirect_save);
  return GRUB_ERR_NONE;
}

grub_err_t
grub_net_dns_lookup (const char *name,
		     const struct grub_net_network_level_address *servers,
		     grub_size_t n_servers,
		     grub_size_t *naddresses,
		     struct grub_net_network_level_address **addresses,
		     int cache)
{
  grub_size_t send_servers = 0;
  grub_size_t i, j;
  struct grub_net_buff *nb;
  grub_net_udp_socket_t *sockets;
  grub_uint8_t *optr;
  const char *iptr;
  struct dns_header *head;
  static grub_uint16_t id = 1;
  grub_uint8_t *qtypeptr;
  grub_err_t err = GRUB_ERR_NONE;
  struct recv_data data = {naddresses, addresses, cache,
			   grub_cpu_to_be16 (id++), 0, 0, name, 0};
  grub_uint8_t *nbd;
  grub_size_t try_server = 0;

  if (!servers)
    {
      servers = dns_servers;
      n_servers = dns_nservers;
    }

  if (!n_servers)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       N_("no DNS servers configured"));

  *naddresses = 0;
  if (cache)
    {
      int h;
      h = hash (name);
      if (dns_cache[h].name && grub_strcmp (dns_cache[h].name, name) == 0
	  && grub_get_time_ms () < dns_cache[h].limit_time)
	{
	  grub_dprintf ("dns", "retrieved from cache\n");
	  *addresses = grub_malloc (dns_cache[h].naddresses
				    * sizeof ((*addresses)[0]));
	  if (!*addresses)
	    return grub_errno;
	  *naddresses = dns_cache[h].naddresses;
	  grub_memcpy (*addresses, dns_cache[h].addresses,
		       dns_cache[h].naddresses
		       * sizeof ((*addresses)[0]));
	  return GRUB_ERR_NONE;
	}
    }

  sockets = grub_calloc (n_servers, sizeof (sockets[0]));
  if (!sockets)
    return grub_errno;

  data.name = grub_strdup (name);
  if (!data.name)
    {
      grub_free (sockets);
      return grub_errno;
    }

  nb = grub_netbuff_alloc (GRUB_NET_OUR_MAX_IP_HEADER_SIZE
			   + GRUB_NET_MAX_LINK_HEADER_SIZE
			   + GRUB_NET_UDP_HEADER_SIZE
			   + sizeof (struct dns_header)
			   + grub_strlen (name) + 2 + 4);
  if (!nb)
    {
      grub_free (sockets);
      grub_free (data.name);
      return grub_errno;
    }
  grub_netbuff_reserve (nb, GRUB_NET_OUR_MAX_IP_HEADER_SIZE
			+ GRUB_NET_MAX_LINK_HEADER_SIZE
			+ GRUB_NET_UDP_HEADER_SIZE);
  grub_netbuff_put (nb, sizeof (struct dns_header)
		    + grub_strlen (name) + 2 + 4);
  head = (struct dns_header *) nb->data;
  optr = (grub_uint8_t *) (head + 1);
  for (iptr = name; *iptr; )
    {
      const char *dot;
      dot = grub_strchr (iptr, '.');
      if (!dot)
	dot = iptr + grub_strlen (iptr);
      if ((dot - iptr) >= 64)
	{
	  grub_free (sockets);
	  grub_free (data.name);
	  return grub_error (GRUB_ERR_BAD_ARGUMENT,
			     N_("domain name component is too long"));
	}
      *optr = (dot - iptr);
      optr++;
      grub_memcpy (optr, iptr, dot - iptr);
      optr += dot - iptr;
      iptr = dot;
      if (*iptr)
	iptr++;
    }
  *optr++ = 0;

  /* Type.  */
  *optr++ = 0;
  qtypeptr = optr++;

  /* Class.  */
  *optr++ = 0;
  *optr++ = 1;

  head->id = data.id;
  head->flags = FLAGS_RD;
  head->ra_z_r_code = 0;
  head->qdcount = grub_cpu_to_be16_compile_time (1);
  head->ancount = grub_cpu_to_be16_compile_time (0);
  head->nscount = grub_cpu_to_be16_compile_time (0);
  head->arcount = grub_cpu_to_be16_compile_time (0);

  nbd = nb->data;

  for (i = 0; i < n_servers * 4; i++)
    {
      /* Connect to a next server.  */
      while (!(i & 1) && try_server < n_servers)
	{
	  sockets[send_servers] = grub_net_udp_open (servers[try_server++],
						     DNS_PORT,
						     recv_hook,
						     &data);
	  if (!sockets[send_servers])
	    {
	      err = grub_errno;
	      grub_errno = GRUB_ERR_NONE;
	    }
	  else
	    {
	      send_servers++;
	      break;
	    }
	}
      if (!send_servers)
	goto out;
      if (*data.naddresses)
	goto out;
      for (j = 0; j < send_servers; j++)
	{
          grub_err_t err2;

          grub_size_t t = 0;
          do
            {
              nb->data = nbd;
              if (servers[j].option == DNS_OPTION_IPV4 ||
                 ((servers[j].option == DNS_OPTION_PREFER_IPV4) && (t++ == 0)) ||
                 ((servers[j].option == DNS_OPTION_PREFER_IPV6) && (t++ == 1)))
                *qtypeptr = GRUB_DNS_QTYPE_A;
              else
                *qtypeptr = GRUB_DNS_QTYPE_AAAA;

              grub_dprintf ("dns", "QTYPE: %u QNAME: %s\n", *qtypeptr, name);

              err2 = grub_net_send_udp_packet (sockets[j], nb);
              if (err2)
                {
                  grub_errno = GRUB_ERR_NONE;
                  err = err2;
                }
              if (*data.naddresses)
                goto out;
            }
          while (t == 1);
	}
      grub_net_poll_cards (200, &data.stop);
    }
 out:
  grub_free (data.name);
  grub_netbuff_free (nb);
  for (j = 0; j < send_servers; j++)
    grub_net_udp_close (sockets[j]);
  
  grub_free (sockets);

  if (*data.naddresses)
    return GRUB_ERR_NONE;
  if (data.dns_err)
    return grub_error (GRUB_ERR_NET_NO_DOMAIN,
		       N_("no DNS record found"));
    
  if (err)
    {
      grub_errno = err;
      return err;
    }
  return grub_error (GRUB_ERR_TIMEOUT,
		     N_("no DNS reply received"));
}

static grub_err_t
grub_cmd_nslookup (struct grub_command *cmd __attribute__ ((unused)),
		   int argc, char **args)
{
  grub_err_t err;
  struct grub_net_network_level_address cmd_server;
  struct grub_net_network_level_address *servers;
  grub_size_t nservers, i, naddresses = 0;
  struct grub_net_network_level_address *addresses = 0;
  if (argc != 2 && argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("two arguments expected"));
  if (argc == 2)
    {
      err = grub_net_resolve_address (args[1], &cmd_server);
      if (err)
	return err;
      servers = &cmd_server;
      nservers = 1;
    }
  else
    {
      servers = dns_servers;
      nservers = dns_nservers;
    }

  grub_net_dns_lookup (args[0], servers, nservers, &naddresses,
                       &addresses, 0);

  for (i = 0; i < naddresses; i++)
    {
      char buf[GRUB_NET_MAX_STR_ADDR_LEN];
      grub_net_addr_to_str (&addresses[i], buf);
      grub_printf ("%s\n", buf);
    }
  grub_free (addresses);
  if (naddresses)
    return GRUB_ERR_NONE;
  return grub_error (GRUB_ERR_NET_NO_DOMAIN, N_("no DNS record found"));
}

static grub_err_t
grub_cmd_list_dns (struct grub_command *cmd __attribute__ ((unused)),
		   int argc __attribute__ ((unused)),
		   char **args __attribute__ ((unused)))
{
  grub_size_t i;
  const char *strtype = "";

  for (i = 0; i < dns_nservers; i++)
    {
      switch (dns_servers[i].option)
        {
        case DNS_OPTION_IPV4:
          strtype = _("only ipv4");
          break;

        case DNS_OPTION_IPV6:
          strtype = _("only ipv6");
          break;

        case DNS_OPTION_PREFER_IPV4:
          strtype = _("prefer ipv4");
          break;

        case DNS_OPTION_PREFER_IPV6:
          strtype = _("prefer ipv6");
          break;
        }

      char buf[GRUB_NET_MAX_STR_ADDR_LEN];
      grub_net_addr_to_str (&dns_servers[i], buf);
      grub_printf ("%s (%s)\n", buf, strtype);
    }
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_add_dns (struct grub_command *cmd __attribute__ ((unused)),
		  int argc, char **args)
{
  grub_err_t err;
  struct grub_net_network_level_address server;

  if ((argc < 1) || (argc > 2))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("one argument expected"));
  else if (argc == 1)
    server.option = DNS_OPTION_PREFER_IPV4;
  else
    {
      if (grub_strcmp (args[1], "--only-ipv4") == 0)
          server.option = DNS_OPTION_IPV4;
      else if (grub_strcmp (args[1], "--only-ipv6") == 0)
          server.option = DNS_OPTION_IPV6;
      else if (grub_strcmp (args[1], "--prefer-ipv4") == 0)
          server.option = DNS_OPTION_PREFER_IPV4;
      else if (grub_strcmp (args[1], "--prefer-ipv6") == 0)
          server.option = DNS_OPTION_PREFER_IPV6;
      else
        return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("invalid argument"));
    }

  err = grub_net_resolve_address (args[0], &server);
  if (err)
    return err;

  return grub_net_add_dns_server (&server);
}

static grub_err_t
grub_cmd_del_dns (struct grub_command *cmd __attribute__ ((unused)),
		  int argc, char **args)
{
  grub_err_t err;
  struct grub_net_network_level_address server;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("one argument expected"));
  err = grub_net_resolve_address (args[1], &server);
  if (err)
    return err;

  return grub_net_add_dns_server (&server);
}

static grub_command_t cmd, cmd_add, cmd_del, cmd_list;

void
grub_dns_init (void)
{
  cmd = grub_register_command ("net_nslookup", grub_cmd_nslookup,
			       N_("ADDRESS DNSSERVER"),
			       N_("Perform a DNS lookup"));
  cmd_add = grub_register_command ("net_add_dns", grub_cmd_add_dns,
				   N_("DNSSERVER"),
				   N_("Add a DNS server"));
  cmd_del = grub_register_command ("net_del_dns", grub_cmd_del_dns,
				   N_("DNSSERVER"),
				   N_("Remove a DNS server"));
  cmd_list = grub_register_command ("net_ls_dns", grub_cmd_list_dns,
				   NULL, N_("List DNS servers"));
}

void
grub_dns_fini (void)
{
  grub_unregister_command (cmd);
  grub_unregister_command (cmd_add);
  grub_unregister_command (cmd_del);
  grub_unregister_command (cmd_list);
}
