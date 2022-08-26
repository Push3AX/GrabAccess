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
#include <grub/env.h>
#include <grub/i18n.h>
#include <grub/command.h>
#include <grub/net.h>
#include <grub/net/ip.h>
#include <grub/net/netbuff.h>
#include <grub/net/udp.h>
#include <grub/net/url.h>
#include <grub/datetime.h>
#include <grub/time.h>
#include <grub/list.h>

static int
dissect_url (const char *url, char **proto, char **host, char **path)
{
  const char *p, *ps;
  grub_size_t l;

  *proto = *host = *path = NULL;
  ps = p = url;

  while ((p = grub_strchr (p, ':')))
    {
      if (grub_strlen (p) < sizeof ("://") - 1)
	break;
      if (grub_memcmp (p, "://", sizeof ("://") - 1) == 0)
	{
	  l = p - ps;
	  *proto = grub_malloc (l + 1);
	  if (!*proto)
	    {
	      grub_print_error ();
	      return 0;
	    }

	  grub_memcpy (*proto, ps, l);
	  (*proto)[l] = '\0';
	  p +=  sizeof ("://") - 1;
	  break;
	}
      ++p;
    }

  if (!*proto)
    {
      grub_dprintf ("bootp", "url: %s is not valid, protocol not found\n", url);
      return 0;
    }

  ps = p;
  p = grub_strchr (p, '/');

  if (!p)
    {
      grub_dprintf ("bootp", "url: %s is not valid, host/path not found\n", url);
      grub_free (*proto);
      *proto = NULL;
      return 0;
    }

  l = p - ps;

  if (l > 2 && ps[0] == '[' && ps[l - 1] == ']')
    {
      *host = grub_malloc (l - 1);
      if (!*host)
	{
	  grub_print_error ();
	  grub_free (*proto);
	  *proto = NULL;
	  return 0;
	}
      grub_memcpy (*host, ps + 1, l - 2);
      (*host)[l - 2] = 0;
    }
  else
    {
      *host = grub_malloc (l + 1);
      if (!*host)
	{
	  grub_print_error ();
	  grub_free (*proto);
	  *proto = NULL;
	  return 0;
	}
      grub_memcpy (*host, ps, l);
      (*host)[l] = 0;
    }

  *path = grub_strdup (p);
  if (!*path)
    {
      grub_print_error ();
      grub_free (*host);
      grub_free (*proto);
      *host = NULL;
      *proto = NULL;
      return 0;
    }
  return 1;
}

struct grub_dhcp_discover_options
{
  grub_uint8_t magic[4];
  struct
  {
    grub_uint8_t code;
    grub_uint8_t len;
    grub_uint8_t data;
  } GRUB_PACKED  message_type;
  grub_uint8_t end;
} GRUB_PACKED;

struct grub_dhcp_request_options
{
  grub_uint8_t magic[4];
  struct
  {
    grub_uint8_t code;
    grub_uint8_t len;
    grub_uint8_t data;
  } GRUB_PACKED  message_type;
  struct
  {
    grub_uint8_t type;
    grub_uint8_t len;
    grub_uint32_t data;
  } GRUB_PACKED 	server_identifier;
  struct
  {
    grub_uint8_t type;
    grub_uint8_t len;
    grub_uint32_t data;
  } GRUB_PACKED 	requested_ip;
  struct
  {
    grub_uint8_t type;
    grub_uint8_t len;
    grub_uint8_t data[7];
  } GRUB_PACKED 	parameter_request;
  grub_uint8_t end;
} GRUB_PACKED;

enum
{
  GRUB_DHCP_OPT_OVERLOAD_FILE = 1,
  GRUB_DHCP_OPT_OVERLOAD_SNAME = 2,
};
enum
{
  GRUB_DHCP_MESSAGE_UNKNOWN,
  GRUB_DHCP_MESSAGE_DISCOVER,
  GRUB_DHCP_MESSAGE_OFFER,
  GRUB_DHCP_MESSAGE_REQUEST,
  GRUB_DHCP_MESSAGE_DECLINE,
  GRUB_DHCP_MESSAGE_ACK,
  GRUB_DHCP_MESSAGE_NAK,
  GRUB_DHCP_MESSAGE_RELEASE,
  GRUB_DHCP_MESSAGE_INFORM,
};

#define GRUB_BOOTP_MAX_OPTIONS_SIZE 64

/* Max timeout when waiting for BOOTP/DHCP reply */
#define GRUB_DHCP_MAX_PACKET_TIMEOUT 32

#define GRUB_BOOTP_MAX_OPTIONS_SIZE 64

/* Max timeout when waiting for BOOTP/DHCP reply */
#define GRUB_DHCP_MAX_PACKET_TIMEOUT 32

static char *
grub_env_write_readonly (struct grub_env_var *var __attribute__ ((unused)),
                         const char *val __attribute__ ((unused)))
{
  return NULL;
}

static void
set_env_limn_ro (const char *intername, const char *suffix,
                 const char *value, grub_size_t len)
{
  char *varname, *varvalue;
  char *ptr;
  varname = grub_xasprintf ("net_%s_%s", intername, suffix);
  if (!varname)
    return;
  for (ptr = varname; *ptr; ptr++)
    if (*ptr == ':')
      *ptr = '_';
  varvalue = grub_malloc (len + 1);
  if (!varvalue)
    {
      grub_free (varname);
      return;
    }

  grub_memcpy (varvalue, value, len);
  varvalue[len] = 0;
  grub_env_set (varname, varvalue);
  grub_register_variable_hook (varname, 0, grub_env_write_readonly);
  grub_env_export (varname);
  grub_free (varname);
  grub_free (varvalue);
}

static char
hexdigit (grub_uint8_t val)
{
  if (val < 10)
    return val + '0';
  return val + 'a' - 10;
}

static const void *
find_dhcp_option (const struct grub_net_bootp_packet *bp, grub_size_t size,
		  grub_uint8_t opt_code, grub_uint8_t *opt_len)
{
  const grub_uint8_t *ptr;
  grub_uint8_t overload = 0;
  int end = 0;
  grub_size_t i;

  if (opt_len)
    *opt_len = 0;

  /* Is the packet big enough to hold at least the magic cookie? */
  if (size < sizeof (*bp) + sizeof (grub_uint32_t))
    return NULL;

  /*
   * Pointer arithmetic to point behind the common stub packet, where
   * the options start.
   */
  ptr = (grub_uint8_t *) (bp + 1);

  if (ptr[0] != GRUB_NET_BOOTP_RFC1048_MAGIC_0
      || ptr[1] != GRUB_NET_BOOTP_RFC1048_MAGIC_1
      || ptr[2] != GRUB_NET_BOOTP_RFC1048_MAGIC_2
      || ptr[3] != GRUB_NET_BOOTP_RFC1048_MAGIC_3)
    return NULL;

  size -= sizeof (*bp);
  i = sizeof (grub_uint32_t);

again:
  while (i < size)
    {
      grub_uint8_t tagtype;
      grub_uint8_t taglength;

      tagtype = ptr[i++];

      /* Pad tag.  */
      if (tagtype == GRUB_NET_BOOTP_PAD)
	continue;

      /* End tag.  */
      if (tagtype == GRUB_NET_BOOTP_END)
	{
	  end = 1;
	  break;
	}

      if (i >= size)
	return NULL;

      taglength = ptr[i++];
      if (i + taglength >= size)
	return NULL;

      grub_dprintf("net", "DHCP option %u (0x%02x) found with length %u.\n",
                   tagtype, tagtype, taglength);

      /* FIXME RFC 3396 options concatentation */
      if (tagtype == opt_code)
	{
	  if (opt_len)
	    *opt_len = taglength;
	  return &ptr[i];
	}

      if (tagtype == GRUB_NET_DHCP_OVERLOAD && taglength == 1)
	overload = ptr[i];

      i += taglength;
    }

  if (!end)
    return NULL;

  /* RFC2131, 4.1, 23ff:
   * If the options in a DHCP message extend into the 'sname' and 'file'
   * fields, the 'option overload' option MUST appear in the 'options'
   * field, with value 1, 2 or 3, as specified in RFC 1533.  If the
   * 'option overload' option is present in the 'options' field, the
   * options in the 'options' field MUST be terminated by an 'end' option,
   * and MAY contain one or more 'pad' options to fill the options field.
   * The options in the 'sname' and 'file' fields (if in use as indicated
   * by the 'options overload' option) MUST begin with the first octet of
   * the field, MUST be terminated by an 'end' option, and MUST be
   * followed by 'pad' options to fill the remainder of the field.  Any
   * individual option in the 'options', 'sname' and 'file' fields MUST be
   * entirely contained in that field.  The options in the 'options' field
   * MUST be interpreted first, so that any 'option overload' options may
   * be interpreted.  The 'file' field MUST be interpreted next (if the
   * 'option overload' option indicates that the 'file' field contains
   * DHCP options), followed by the 'sname' field.
   *
   * FIXME: We do not explicitly check for trailing 'pad' options here.
   */
  end = 0;
  if (overload & GRUB_DHCP_OPT_OVERLOAD_FILE)
  {
    overload &= ~GRUB_DHCP_OPT_OVERLOAD_FILE;
    ptr = (grub_uint8_t *) &bp->boot_file[0];
    size = sizeof (bp->boot_file);
    i = 0;
    goto again;
  }

  if (overload & GRUB_DHCP_OPT_OVERLOAD_SNAME)
  {
    overload &= ~GRUB_DHCP_OPT_OVERLOAD_SNAME;
    ptr = (grub_uint8_t *) &bp->server_name[0];
    size = sizeof (bp->server_name);
    i = 0;
    goto again;
  }

  return NULL;
}

#define OFFSET_OF(x, y) ((grub_size_t)((grub_uint8_t *)((y)->x) - (grub_uint8_t *)(y)))

void
grub_net_process_dhcp_ack (struct grub_net_network_level_interface *inter,
			   const struct grub_net_bootp_packet *bp,
			   grub_size_t size,
			   int is_def, char **device, char **path)
{
  int mask = -1;
  char server_ip[sizeof ("xxx.xxx.xxx.xxx")];
  const grub_uint8_t *opt;
  grub_uint8_t opt_len, overload = 0;
  const char *boot_file = 0, *server_name = 0;
  grub_size_t boot_file_len, server_name_len;

  opt = find_dhcp_option (bp, size, GRUB_NET_DHCP_OVERLOAD, &opt_len);
  if (opt && opt_len == 1)
    overload = *opt;

  opt = find_dhcp_option (bp, size, GRUB_NET_DHCP_TFTP_SERVER_NAME, &opt_len);
  if (opt && opt_len)
    {
      server_name = (const char *) opt;
      server_name_len = opt_len;
    }
  else if (size > OFFSET_OF (server_name, bp) && !(overload & GRUB_DHCP_OPT_OVERLOAD_SNAME) &&
          bp->server_name[0])
    {
      server_name = bp->server_name;
      server_name_len = sizeof (bp->server_name);
    }

  opt = find_dhcp_option (bp, size, GRUB_NET_DHCP_BOOTFILE_NAME, &opt_len);
  if (opt && opt_len)
    {
      boot_file = (const char *) opt;
      boot_file_len = opt_len;
    }
  else if (size > OFFSET_OF (boot_file, bp) && !(overload && GRUB_DHCP_OPT_OVERLOAD_FILE) &&
          bp->boot_file[0])
    {
      boot_file = bp->boot_file;
      boot_file_len = sizeof (bp->boot_file);
    }

  if (bp->server_ip)
    {
      grub_snprintf (server_ip, sizeof (server_ip), "%d.%d.%d.%d",
		     ((grub_uint8_t *) &bp->server_ip)[0],
		     ((grub_uint8_t *) &bp->server_ip)[1],
		     ((grub_uint8_t *) &bp->server_ip)[2],
		     ((grub_uint8_t *) &bp->server_ip)[3]);
      grub_env_set_net_property (inter->name, "next_server", server_ip, sizeof (server_ip));
      grub_print_error ();
    }

  if (is_def)
    grub_net_default_server = 0;
  if (is_def && !grub_net_default_server && bp->server_ip)
    {
      grub_net_default_server = grub_strdup (server_ip);
      grub_print_error ();
    }

  if (device && !*device && bp->server_ip)
    {
      *device = grub_xasprintf ("tftp,%s", server_ip);
      grub_print_error ();
    }

  if (server_name)
    {
      grub_env_set_net_property (inter->name, "dhcp_server_name", server_name, server_name_len);
      if (is_def && !grub_net_default_server)
	{
	  grub_net_default_server = grub_strdup (server_name);
	  grub_print_error ();
	}
      if (device && !*device)
	{
	  *device = grub_xasprintf ("tftp,%s", server_name);
	  grub_print_error ();
	}
    }

  if (!grub_net_default_server)
  {
    grub_net_default_server = grub_strdup (grub_env_get ("net_pxe_next_server"));
    grub_env_set ("net_default_server", grub_net_default_server);
  }

  if (boot_file)
    {
      grub_env_set_net_property (inter->name, "boot_file", boot_file, boot_file_len);
      if (path)
	{
	  *path = grub_strndup (boot_file, boot_file_len);
	  grub_print_error ();
	  if (*path)
	    {
	      char *slash;
	      slash = grub_strrchr (*path, '/');
	      if (slash)
		*slash = 0;
	      else
		**path = 0;
	    }
	}
    }

  opt = find_dhcp_option (bp, size, GRUB_NET_BOOTP_NETMASK, &opt_len);
  if (opt && opt_len == 4)
    {
      int i;
      for (i = 0; i < 32; i++)
	if (!(opt[i / 8] & (1 << (7 - (i % 8)))))
	  break;
      mask = i;
    }
  grub_net_add_ipv4_local (inter, mask);

  opt = find_dhcp_option (bp, size, GRUB_NET_BOOTP_CLIENT_ID, &opt_len);
  if (opt)
    {
      set_env_limn_ro (inter->name, "clientid", (char *) opt, opt_len);
    }

  opt = find_dhcp_option (bp, size, GRUB_NET_BOOTP_CLIENT_UUID, &opt_len);
  if (opt && opt_len == 17)
    {
      /* The format is 9cfe245e-d0c8-bd45-a79f-54ea5fbd3d97 */

      opt += 1;
      opt_len -= 1;

      char *val = grub_malloc (2 * opt_len + 4 + 1);
      int i = 0;
      int j = 0;
      for (i = 0; i < opt_len; i++)
        {
          val[2 * i + j] = hexdigit (opt[i] >> 4);
          val[2 * i + 1 + j] = hexdigit (opt[i] & 0xf);

          if ((i == 3) || (i == 5) || (i == 7) || (i == 9))
            {
              j++;
              val[2 * i + 1+ j] = '-';
            }
        }
      set_env_limn_ro (inter->name, "clientuuid", (char *) val, 2 * opt_len + 4);
    }

  /* We do not implement dead gateway detection and the first entry SHOULD
     be preferred one */
  opt = find_dhcp_option (bp, size, GRUB_NET_BOOTP_ROUTER, &opt_len);
  if (opt && opt_len && !(opt_len & 3))
    {
      grub_net_network_level_netaddress_t target;
      grub_net_network_level_address_t gw;
      char *rname;

      target.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4;
      target.ipv4.base = 0;
      target.ipv4.masksize = 0;
      gw.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4;
      gw.ipv4 = grub_get_unaligned32 (opt);
      rname = grub_xasprintf ("%s:default", inter->name);
      if (rname)
	grub_net_add_route_gw (rname, target, gw, 0);
      grub_free (rname);
    }

  opt = find_dhcp_option (bp, size, GRUB_NET_BOOTP_DNS, &opt_len);
  if (opt && opt_len && !(opt_len & 3))
    {
      int i;
      for (i = 0; i < opt_len / 4; i++)
	{
	  struct grub_net_network_level_address s;

	  s.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4;
	  s.ipv4 = grub_get_unaligned32 (opt);
	  s.option = DNS_OPTION_PREFER_IPV4;
	  grub_net_add_dns_server (&s);
	  opt += 4;
	}
    }

  opt = find_dhcp_option (bp, size, GRUB_NET_BOOTP_HOSTNAME, &opt_len);
  if (opt && opt_len)
    grub_env_set_net_property (inter->name, "hostname", (const char *) opt, opt_len);

  opt = find_dhcp_option (bp, size, GRUB_NET_BOOTP_DOMAIN, &opt_len);
  if (opt && opt_len)
    grub_env_set_net_property (inter->name, "domain", (const char *) opt, opt_len);

  opt = find_dhcp_option (bp, size, GRUB_NET_BOOTP_ROOT_PATH, &opt_len);
  if (opt && opt_len)
    grub_env_set_net_property (inter->name, "rootpath", (const char *) opt, opt_len);

  opt = find_dhcp_option (bp, size, GRUB_NET_BOOTP_VENDOR_CLASS_IDENTIFIER, &opt_len);
  if (opt && opt_len)
    {
      grub_env_set_net_property (inter->name, "vendor_class_identifier",
                                 (const char *) opt, opt_len);
      if (opt && grub_strcmp ((const char *)opt, "HTTPClient") == 0)
        {
          char *proto, *ip, *pa;
          if (dissect_url (bp->boot_file, &proto, &ip, &pa))
          {
            grub_env_set_net_property (inter->name, "boot_file",
                                       pa, grub_strlen (pa));

            grub_free (proto);
            grub_free (ip);
            grub_free (pa);
          }
        }
    }

  opt = find_dhcp_option (bp, size, GRUB_NET_BOOTP_EXTENSIONS_PATH, &opt_len);
  if (opt && opt_len)
    grub_env_set_net_property (inter->name, "extensionspath", (const char *) opt, opt_len);

  opt = find_dhcp_option (bp, size, GRUB_NET_BOOTP_CLIENT_ID, &opt_len);
  if (opt && opt_len)
    grub_env_set_net_property (inter->name, "clientid", (const char *) opt, opt_len);

  opt = find_dhcp_option (bp, size, GRUB_NET_BOOTP_CLIENT_UUID, &opt_len);
  if (opt && opt_len == 17)
    {
      /* The format is 9cfe245e-d0c8-bd45-a79f-54ea5fbd3d97 */
      char *val;
      int i, j = 0;

      opt += 1;
      opt_len -= 1;

      val = grub_malloc (2 * opt_len + 4 + 1);
      if (!val)
          return;

      for (i = 0; i < opt_len; i++)
        {
          val[2 * i + j] = hexdigit (opt[i] >> 4);
          val[2 * i + 1 + j] = hexdigit (opt[i] & 0xf);

          if ((i == 3) || (i == 5) || (i == 7) || (i == 9))
            {
              j++;
              val[2 * i + 1+ j] = '-';
            }
        }
      grub_env_set_net_property (inter->name, "clientuuid", (char *) val, 2 * opt_len + 4);
      grub_free (val);
    }
}

struct grub_net_network_level_interface *
grub_net_configure_by_dhcp_ack (const char *name,
				struct grub_net_card *card,
				grub_net_interface_flags_t flags,
				const struct grub_net_bootp_packet *bp,
				grub_size_t size,
				int is_def, char **device, char **path)
{
  grub_net_network_level_address_t addr;
  struct grub_net_network_level_interface *inter;

  addr.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4;
  addr.ipv4 = bp->your_ip;

  if (device)
    *device = 0;
  if (path)
    *path = 0;

  grub_dprintf("dhcp", "configuring dhcp for %s\n", name);
  inter = grub_net_add_addr (name, card, &addr, &card->default_address, flags);
  if (!inter)
    return 0;

  if (is_def)
    {
      grub_env_set ("net_default_interface", name);
      grub_env_export ("net_default_interface");
      grub_net_default_server = 0;
    }

  grub_net_process_dhcp_ack (inter, bp, size, is_def, device, path);

  inter->dhcp_ack = grub_malloc (size);
  if (inter->dhcp_ack)
    {
      grub_memcpy (inter->dhcp_ack, bp, size);
      inter->dhcp_acklen = size;
    }
  else
    grub_errno = GRUB_ERR_NONE;

  return inter;
}

static grub_err_t
send_dhcp_packet (struct grub_net_network_level_interface *iface)
{
  grub_err_t err;
  struct grub_net_bootp_packet *pack;
  struct grub_datetime date;
  grub_int64_t t = 0;
  struct grub_net_buff *nb;
  struct udphdr *udph;
  grub_net_network_level_address_t target;
  grub_net_link_level_address_t ll_target;

  static struct grub_dhcp_discover_options discover_options =
    {
      {
	GRUB_NET_BOOTP_RFC1048_MAGIC_0,
	GRUB_NET_BOOTP_RFC1048_MAGIC_1,
	GRUB_NET_BOOTP_RFC1048_MAGIC_2,
	GRUB_NET_BOOTP_RFC1048_MAGIC_3,
      },
      {
	GRUB_NET_DHCP_MESSAGE_TYPE,
	sizeof (discover_options.message_type.data),
	GRUB_DHCP_MESSAGE_DISCOVER,
      },
      GRUB_NET_BOOTP_END,
    };

  static struct grub_dhcp_request_options request_options =
    {
      {
	GRUB_NET_BOOTP_RFC1048_MAGIC_0,
	GRUB_NET_BOOTP_RFC1048_MAGIC_1,
	GRUB_NET_BOOTP_RFC1048_MAGIC_2,
	GRUB_NET_BOOTP_RFC1048_MAGIC_3,
      },
      {
	GRUB_NET_DHCP_MESSAGE_TYPE,
	sizeof (request_options.message_type.data),
	GRUB_DHCP_MESSAGE_REQUEST,
      },
      {
	GRUB_NET_DHCP_SERVER_IDENTIFIER,
	sizeof (request_options.server_identifier.data),
	0,
      },
      {
	GRUB_NET_DHCP_REQUESTED_IP_ADDRESS,
	sizeof (request_options.requested_ip.data),
	0,
      },
      {
	GRUB_NET_DHCP_PARAMETER_REQUEST_LIST,
	sizeof (request_options.parameter_request.data),
	{
	  GRUB_NET_BOOTP_NETMASK,
	  GRUB_NET_BOOTP_ROUTER,
	  GRUB_NET_BOOTP_DNS,
	  GRUB_NET_BOOTP_DOMAIN,
	  GRUB_NET_BOOTP_HOSTNAME,
	  GRUB_NET_BOOTP_ROOT_PATH,
	  GRUB_NET_BOOTP_EXTENSIONS_PATH,
	},
      },
      GRUB_NET_BOOTP_END,
    };

  COMPILE_TIME_ASSERT (sizeof (discover_options) <= GRUB_BOOTP_MAX_OPTIONS_SIZE);
  COMPILE_TIME_ASSERT (sizeof (request_options) <= GRUB_BOOTP_MAX_OPTIONS_SIZE);

  nb = grub_netbuff_alloc (sizeof (*pack) + GRUB_BOOTP_MAX_OPTIONS_SIZE + 128);
  if (!nb)
    return grub_errno;

  err = grub_netbuff_reserve (nb, sizeof (*pack) + GRUB_BOOTP_MAX_OPTIONS_SIZE + 128);
  if (err)
    goto out;

  err = grub_netbuff_push (nb, GRUB_BOOTP_MAX_OPTIONS_SIZE);
  if (err)
    goto out;

  grub_memset (nb->data, 0, GRUB_BOOTP_MAX_OPTIONS_SIZE);
  if (!iface->srv_id)
    {
      grub_memcpy (nb->data, &discover_options, sizeof (discover_options));
    }
  else
    {
      struct grub_dhcp_request_options *ro = (struct grub_dhcp_request_options *) nb->data;

      grub_memcpy (nb->data, &request_options, sizeof (request_options));
      /* my_ip and srv_id are stored in network order so do not need conversion. */
      grub_set_unaligned32 (&ro->server_identifier.data, iface->srv_id);
      grub_set_unaligned32 (&ro->requested_ip.data, iface->my_ip);
    }

  err = grub_netbuff_push (nb, sizeof (*pack));
  if (err)
    goto out;

  pack = (void *) nb->data;
  grub_memset (pack, 0, sizeof (*pack));
  pack->opcode = 1;
  pack->hw_type = 1;
  pack->hw_len = iface->hwaddress.len > 16 ? 0 : iface->hwaddress.len;

  err = grub_get_datetime (&date);
  if (err || !grub_datetime2unixtime (&date, &t))
    {
      grub_errno = GRUB_ERR_NONE;
      t = 0;
    }
  pack->seconds = grub_cpu_to_be16 (t);
  if (!iface->srv_id)
    iface->xid = pack->ident = grub_cpu_to_be32 (t);
  else
    pack->ident = iface->xid;

  grub_memcpy (&pack->mac_addr, &iface->hwaddress.mac, pack->hw_len);

  grub_netbuff_push (nb, sizeof (*udph));

  udph = (struct udphdr *) nb->data;
  udph->src = grub_cpu_to_be16_compile_time (68);
  udph->dst = grub_cpu_to_be16_compile_time (67);
  udph->chksum = 0;
  udph->len = grub_cpu_to_be16 (nb->tail - nb->data);
  target.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4;
  target.ipv4 = 0xffffffff;
  err = grub_net_link_layer_resolve (iface, &target, &ll_target);
  if (err)
    goto out;

  udph->chksum = grub_net_ip_transport_checksum (nb, GRUB_NET_IP_UDP,
						 &iface->address,
						 &target);

  err = grub_net_send_ip_packet (iface, &target, &ll_target, nb,
				 GRUB_NET_IP_UDP);

out:
  grub_netbuff_free (nb);
  return err;
}

/* The default netbuff size for sending DHCPv6 packets which should be
   large enough to hold the information */
#define GRUB_DHCP6_DEFAULT_NETBUFF_ALLOC_SIZE 512

struct grub_dhcp6_options
{
  grub_uint8_t *client_duid;
  grub_uint16_t client_duid_len;
  grub_uint8_t *server_duid;
  grub_uint16_t server_duid_len;
  grub_uint32_t iaid;
  grub_uint32_t t1;
  grub_uint32_t t2;
  grub_net_network_level_address_t *ia_addr;
  grub_uint32_t preferred_lifetime;
  grub_uint32_t valid_lifetime;
  grub_net_network_level_address_t *dns_server_addrs;
  grub_uint16_t num_dns_server;
  char *boot_file_proto;
  char *boot_file_server_ip;
  char *boot_file_path;
};

typedef struct grub_dhcp6_options *grub_dhcp6_options_t;

struct grub_dhcp6_session
{
  struct grub_dhcp6_session *next;
  struct grub_dhcp6_session **prev;
  grub_uint32_t iaid;
  grub_uint32_t transaction_id:24;
  grub_uint64_t start_time;
  struct grub_net_dhcp6_option_duid_ll duid;
  struct grub_net_network_level_interface *iface;

  /* The associated dhcpv6 options */
  grub_dhcp6_options_t adv;
  grub_dhcp6_options_t reply;
};

typedef struct grub_dhcp6_session *grub_dhcp6_session_t;

typedef void (*dhcp6_option_hook_fn) (const struct grub_net_dhcp6_option *opt, void *data);

static void
foreach_dhcp6_option (const struct grub_net_dhcp6_option *opt, grub_size_t size,
		      dhcp6_option_hook_fn hook, void *hook_data);

static void
parse_dhcp6_iaaddr (const struct grub_net_dhcp6_option *opt, void *data)
{
  grub_dhcp6_options_t dhcp6 = (grub_dhcp6_options_t )data;

  grub_uint16_t code = grub_be_to_cpu16 (opt->code);
  grub_uint16_t len = grub_be_to_cpu16 (opt->len);

  if (code == GRUB_NET_DHCP6_OPTION_IAADDR)
    {
      const struct grub_net_dhcp6_option_iaaddr *iaaddr;
      iaaddr = (const struct grub_net_dhcp6_option_iaaddr *)opt->data;

      if (len < sizeof (*iaaddr))
	{
	  grub_dprintf ("bootp", "DHCPv6: code %u with insufficient length %u\n", code, len);
	  return;
	}
      if (!dhcp6->ia_addr)
	{
	  dhcp6->ia_addr = grub_malloc (sizeof(*dhcp6->ia_addr));
	  dhcp6->ia_addr->type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6;
	  dhcp6->ia_addr->ipv6[0] = grub_get_unaligned64 (iaaddr->addr);
	  dhcp6->ia_addr->ipv6[1] = grub_get_unaligned64 (iaaddr->addr + 8);
	  dhcp6->preferred_lifetime = grub_be_to_cpu32 (iaaddr->preferred_lifetime);
	  dhcp6->valid_lifetime = grub_be_to_cpu32 (iaaddr->valid_lifetime);
	}
    }
}

static void
parse_dhcp6_option (const struct grub_net_dhcp6_option *opt, void *data)
{
  grub_dhcp6_options_t dhcp6 = (grub_dhcp6_options_t)data;
  grub_uint16_t code = grub_be_to_cpu16 (opt->code);
  grub_uint16_t len = grub_be_to_cpu16 (opt->len);

  switch (code)
    {
      case GRUB_NET_DHCP6_OPTION_CLIENTID:

	if (dhcp6->client_duid || !len)
	  {
	    grub_dprintf ("bootp", "Skipped DHCPv6 CLIENTID with length %u\n", len);
	    break;
	  }
	dhcp6->client_duid = grub_malloc (len);
	grub_memcpy (dhcp6->client_duid, opt->data, len);
	dhcp6->client_duid_len = len;
	break;

      case GRUB_NET_DHCP6_OPTION_SERVERID:

	if (dhcp6->server_duid || !len)
	  {
	    grub_dprintf ("bootp", "Skipped DHCPv6 SERVERID with length %u\n", len);
	    break;
	  }
	dhcp6->server_duid = grub_malloc (len);
	grub_memcpy (dhcp6->server_duid, opt->data, len);
	dhcp6->server_duid_len = len;
	break;

      case GRUB_NET_DHCP6_OPTION_IA_NA:
	{
	  const struct grub_net_dhcp6_option_iana *ia_na;
	  grub_uint16_t data_len;

	  if (dhcp6->iaid || len < sizeof (*ia_na))
	    {
	      grub_dprintf ("bootp", "Skipped DHCPv6 IA_NA with length %u\n", len);
	      break;
	    }
	  ia_na = (const struct grub_net_dhcp6_option_iana *)opt->data;
	  dhcp6->iaid = grub_be_to_cpu32 (ia_na->iaid);
	  dhcp6->t1 = grub_be_to_cpu32 (ia_na->t1);
	  dhcp6->t2 = grub_be_to_cpu32 (ia_na->t2);

	  data_len = len - sizeof (*ia_na);
	  if (data_len)
	    foreach_dhcp6_option ((const struct grub_net_dhcp6_option *)ia_na->data, data_len, parse_dhcp6_iaaddr, dhcp6);
	}
	break;

      case GRUB_NET_DHCP6_OPTION_DNS_SERVERS:
	{
	  const grub_uint8_t *po;
	  grub_uint16_t ln;
	  grub_net_network_level_address_t *la;

	  if (!len || len & 0xf)
	    {
	      grub_dprintf ("bootp", "Skip invalid length DHCPv6 DNS_SERVERS \n");
	      break;
	    }
	  dhcp6->num_dns_server = ln = len >> 4;
	  dhcp6->dns_server_addrs = la = grub_zalloc (ln * sizeof (*la));

	  for (po = opt->data; ln > 0; po += 0x10, la++, ln--)
	    {
	      la->type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6;
	      la->ipv6[0] = grub_get_unaligned64 (po);
	      la->ipv6[1] = grub_get_unaligned64 (po + 8);
	      la->option = DNS_OPTION_PREFER_IPV6;
	    }
	}
	break;

      case GRUB_NET_DHCP6_OPTION_BOOTFILE_URL:
	dissect_url ((const char *)opt->data,
		      &dhcp6->boot_file_proto,
		      &dhcp6->boot_file_server_ip,
		      &dhcp6->boot_file_path);
	break;

      default:
	break;
    }
}

static void
foreach_dhcp6_option (const struct grub_net_dhcp6_option *opt, grub_size_t size, dhcp6_option_hook_fn hook, void *hook_data)
{
  while (size)
    {
      grub_uint16_t code, len;

      if (size < sizeof (*opt))
	{
	  grub_dprintf ("bootp", "DHCPv6: Options stopped with remaining size %" PRIxGRUB_SIZE "\n", size);
	  break;
	}
      size -= sizeof (*opt);
      len = grub_be_to_cpu16 (opt->len);
      code = grub_be_to_cpu16 (opt->code);
      if (size < len)
	{
	  grub_dprintf ("bootp", "DHCPv6: Options stopped at out of bound length %u for option %u\n", len, code);
	  break;
	}
      if (!len)
	{
	  grub_dprintf ("bootp", "DHCPv6: Options stopped at zero length option %u\n", code);
	  break;
	}
      else
	{
	  if (hook)
	    hook (opt, hook_data);
	  size -= len;
	  opt = (const struct grub_net_dhcp6_option *)((grub_uint8_t *)opt + len + sizeof (*opt));
	}
    }
}

static grub_dhcp6_options_t
grub_dhcp6_options_get (const struct grub_net_dhcp6_packet *v6h,
			grub_size_t size)
{
  grub_dhcp6_options_t options;

  if (size < sizeof (*v6h))
    {
      grub_error (GRUB_ERR_OUT_OF_RANGE, N_("DHCPv6 packet size too small"));
      return NULL;
    }

  options = grub_zalloc (sizeof(*options));
  if (!options)
    return NULL;

  foreach_dhcp6_option ((const struct grub_net_dhcp6_option *)v6h->dhcp_options,
		       size - sizeof (*v6h), parse_dhcp6_option, options);

  return options;
}

static void
grub_dhcp6_options_free (grub_dhcp6_options_t options)
{
  if (options->client_duid)
    grub_free (options->client_duid);
  if (options->server_duid)
    grub_free (options->server_duid);
  if (options->ia_addr)
    grub_free (options->ia_addr);
  if (options->dns_server_addrs)
    grub_free (options->dns_server_addrs);
  if (options->boot_file_proto)
    grub_free (options->boot_file_proto);
  if (options->boot_file_server_ip)
    grub_free (options->boot_file_server_ip);
  if (options->boot_file_path)
    grub_free (options->boot_file_path);

  grub_free (options);
}

static grub_dhcp6_session_t grub_dhcp6_sessions;
#define FOR_DHCP6_SESSIONS_SAFE(var, next) FOR_LIST_ELEMENTS_SAFE (var, next, grub_dhcp6_sessions)
#define FOR_DHCP6_SESSIONS(var) FOR_LIST_ELEMENTS (var, grub_dhcp6_sessions)

static void
grub_net_configure_by_dhcp6_info (const char *name,
	  struct grub_net_card *card,
	  grub_dhcp6_options_t dhcp6,
	  int is_def,
	  int flags,
	  struct grub_net_network_level_interface **ret_inf)
{
  grub_net_network_level_netaddress_t netaddr;
  struct grub_net_network_level_interface *inf;

  if (dhcp6->ia_addr)
    {
      inf = grub_net_add_addr (name, card, dhcp6->ia_addr, &card->default_address, flags);

      netaddr.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6;
      netaddr.ipv6.base[0] = dhcp6->ia_addr->ipv6[0];
      netaddr.ipv6.base[1] = 0;
      netaddr.ipv6.masksize = 64;
      grub_net_add_route (name, netaddr, inf);

      if (ret_inf)
	*ret_inf = inf;
    }

  if (dhcp6->dns_server_addrs)
    {
      grub_uint16_t i;

      for (i = 0; i < dhcp6->num_dns_server; ++i)
	grub_net_add_dns_server (dhcp6->dns_server_addrs + i);
    }

  if (dhcp6->boot_file_path)
    grub_env_set_net_property (name, "boot_file", dhcp6->boot_file_path,
			  grub_strlen (dhcp6->boot_file_path));

  if (is_def && dhcp6->boot_file_server_ip)
    {
      grub_net_default_server = grub_strdup (dhcp6->boot_file_server_ip);
      grub_env_set ("net_default_interface", name);
      grub_env_export ("net_default_interface");
    }
}

static void
grub_dhcp6_session_add (struct grub_net_network_level_interface *iface,
			grub_uint32_t iaid)
{
  grub_dhcp6_session_t se;
  struct grub_datetime date;
  grub_err_t err;
  grub_int64_t t = 0;

  se = grub_malloc (sizeof (*se));

  err = grub_get_datetime (&date);
  if (err || !grub_datetime2unixtime (&date, &t))
    {
      grub_errno = GRUB_ERR_NONE;
      t = 0;
    }

  se->iface = iface;
  se->iaid = iaid;
  se->transaction_id = t;
  se->start_time = grub_get_time_ms ();
  se->duid.type = grub_cpu_to_be16_compile_time (3) ;
  se->duid.hw_type = grub_cpu_to_be16_compile_time (1);
  grub_memcpy (&se->duid.hwaddr, &iface->hwaddress.mac, sizeof (se->duid.hwaddr));
  se->adv = NULL;
  se->reply = NULL;
  grub_list_push (GRUB_AS_LIST_P (&grub_dhcp6_sessions), GRUB_AS_LIST (se));
}

static void
grub_dhcp6_session_remove (grub_dhcp6_session_t se)
{
  grub_list_remove (GRUB_AS_LIST (se));
  if (se->adv)
    grub_dhcp6_options_free (se->adv);
  if (se->reply)
    grub_dhcp6_options_free (se->reply);
  grub_free (se);
}

static void
grub_dhcp6_session_remove_all (void)
{
  grub_dhcp6_session_t se, next;

  FOR_DHCP6_SESSIONS_SAFE (se, next)
    {
      grub_dhcp6_session_remove (se);
    }
  grub_dhcp6_sessions = NULL;
}

static grub_err_t
grub_dhcp6_session_configure_network (grub_dhcp6_session_t se)
{
  char *name;

  name = grub_xasprintf ("%s:dhcp6", se->iface->card->name);
  if (!name)
    return grub_errno;

  grub_net_configure_by_dhcp6_info (name, se->iface->card, se->reply, 1, 0, 0);
  grub_free (name);

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_dhcp6_session_send_request (grub_dhcp6_session_t se)
{
  struct grub_net_buff *nb;
  struct grub_net_dhcp6_option *opt;
  struct grub_net_dhcp6_packet *v6h;
  struct grub_net_dhcp6_option_iana *ia_na;
  struct grub_net_dhcp6_option_iaaddr *iaaddr;
  struct udphdr *udph;
  grub_net_network_level_address_t multicast;
  grub_net_link_level_address_t ll_multicast;
  grub_uint64_t elapsed;
  struct grub_net_network_level_interface *inf = se->iface;
  grub_dhcp6_options_t dhcp6 = se->adv;
  grub_err_t err = GRUB_ERR_NONE;

  multicast.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6;
  multicast.ipv6[0] = grub_cpu_to_be64_compile_time (0xff02ULL << 48);
  multicast.ipv6[1] = grub_cpu_to_be64_compile_time (0x10002ULL);

  err = grub_net_link_layer_resolve (inf, &multicast, &ll_multicast);
  if (err)
    return err;

  nb = grub_netbuff_alloc (GRUB_DHCP6_DEFAULT_NETBUFF_ALLOC_SIZE);

  if (!nb)
    return grub_errno;

  err = grub_netbuff_reserve (nb, GRUB_DHCP6_DEFAULT_NETBUFF_ALLOC_SIZE);
  if (err)
    {
      grub_netbuff_free (nb);
      return err;
    }

  err = grub_netbuff_push (nb, dhcp6->client_duid_len + sizeof (*opt));
  if (err)
    {
      grub_netbuff_free (nb);
      return err;
    }
  opt = (struct grub_net_dhcp6_option *)nb->data;
  opt->code = grub_cpu_to_be16_compile_time (GRUB_NET_DHCP6_OPTION_CLIENTID);
  opt->len = grub_cpu_to_be16 (dhcp6->client_duid_len);
  grub_memcpy (opt->data, dhcp6->client_duid , dhcp6->client_duid_len);

  err = grub_netbuff_push (nb, dhcp6->server_duid_len + sizeof (*opt));
  if (err)
    {
      grub_netbuff_free (nb);
      return err;
    }
  opt = (struct grub_net_dhcp6_option *)nb->data;
  opt->code = grub_cpu_to_be16_compile_time (GRUB_NET_DHCP6_OPTION_SERVERID);
  opt->len = grub_cpu_to_be16 (dhcp6->server_duid_len);
  grub_memcpy (opt->data, dhcp6->server_duid , dhcp6->server_duid_len);

  err = grub_netbuff_push (nb, sizeof (*ia_na) + sizeof (*opt));
  if (err)
    {
      grub_netbuff_free (nb);
      return err;
    }

  if (dhcp6->ia_addr)
    {
      err = grub_netbuff_push (nb, sizeof(*iaaddr) + sizeof (*opt));
      if (err)
	{
	  grub_netbuff_free (nb);
	  return err;
	}
    }
  opt = (struct grub_net_dhcp6_option *)nb->data;
  opt->code = grub_cpu_to_be16_compile_time (GRUB_NET_DHCP6_OPTION_IA_NA);
  opt->len = grub_cpu_to_be16 (sizeof (*ia_na));
  if (dhcp6->ia_addr)
    opt->len += grub_cpu_to_be16 (sizeof(*iaaddr) + sizeof (*opt));

  ia_na = (struct grub_net_dhcp6_option_iana *)opt->data;
  ia_na->iaid = grub_cpu_to_be32 (dhcp6->iaid);

  ia_na->t1 = grub_cpu_to_be32 (dhcp6->t1);
  ia_na->t2 = grub_cpu_to_be32 (dhcp6->t2);

  if (dhcp6->ia_addr)
    {
      opt = (struct grub_net_dhcp6_option *)ia_na->data;
      opt->code = grub_cpu_to_be16_compile_time (GRUB_NET_DHCP6_OPTION_IAADDR);
      opt->len = grub_cpu_to_be16 (sizeof (*iaaddr));
      iaaddr = (struct grub_net_dhcp6_option_iaaddr *)opt->data;
      grub_set_unaligned64 (iaaddr->addr, dhcp6->ia_addr->ipv6[0]);
      grub_set_unaligned64 (iaaddr->addr + 8, dhcp6->ia_addr->ipv6[1]);

      iaaddr->preferred_lifetime = grub_cpu_to_be32 (dhcp6->preferred_lifetime);
      iaaddr->valid_lifetime = grub_cpu_to_be32 (dhcp6->valid_lifetime);
    }

  err = grub_netbuff_push (nb, sizeof (*opt) + 2 * sizeof (grub_uint16_t));
  if (err)
    {
      grub_netbuff_free (nb);
      return err;
    }

  opt = (struct grub_net_dhcp6_option*) nb->data;
  opt->code = grub_cpu_to_be16_compile_time (GRUB_NET_DHCP6_OPTION_ORO);
  opt->len = grub_cpu_to_be16_compile_time (2 * sizeof (grub_uint16_t));
  grub_set_unaligned16 (opt->data, grub_cpu_to_be16_compile_time (GRUB_NET_DHCP6_OPTION_BOOTFILE_URL));
  grub_set_unaligned16 (opt->data + 2, grub_cpu_to_be16_compile_time (GRUB_NET_DHCP6_OPTION_DNS_SERVERS));

  err = grub_netbuff_push (nb, sizeof (*opt) + sizeof (grub_uint16_t));
  if (err)
    {
      grub_netbuff_free (nb);
      return err;
    }
  opt = (struct grub_net_dhcp6_option*) nb->data;
  opt->code = grub_cpu_to_be16_compile_time (GRUB_NET_DHCP6_OPTION_ELAPSED_TIME);
  opt->len = grub_cpu_to_be16_compile_time (sizeof (grub_uint16_t));

  /* the time is expressed in hundredths of a second */
  elapsed = grub_divmod64 (grub_get_time_ms () - se->start_time, 10, 0);

  if (elapsed > 0xffff)
    elapsed = 0xffff;

  grub_set_unaligned16 (opt->data,  grub_cpu_to_be16 ((grub_uint16_t)elapsed));

  err = grub_netbuff_push (nb, sizeof (*v6h));
  if (err)
    {
      grub_netbuff_free (nb);
      return err;
    }

  v6h = (struct grub_net_dhcp6_packet *) nb->data;
  v6h->message_type = GRUB_NET_DHCP6_REQUEST;
  v6h->transaction_id = se->transaction_id;

  err = grub_netbuff_push (nb, sizeof (*udph));
  if (err)
    {
      grub_netbuff_free (nb);
      return err;
    }

  udph = (struct udphdr *) nb->data;
  udph->src = grub_cpu_to_be16_compile_time (DHCP6_CLIENT_PORT);
  udph->dst = grub_cpu_to_be16_compile_time (DHCP6_SERVER_PORT);
  udph->chksum = 0;
  udph->len = grub_cpu_to_be16 (nb->tail - nb->data);

  udph->chksum = grub_net_ip_transport_checksum (nb, GRUB_NET_IP_UDP,
						 &inf->address,
						 &multicast);
  err = grub_net_send_ip_packet (inf, &multicast, &ll_multicast, nb,
				 GRUB_NET_IP_UDP);

  grub_netbuff_free (nb);

  return err;
}

struct grub_net_network_level_interface *
grub_net_configure_by_dhcpv6_reply (const char *name,
	struct grub_net_card *card,
	grub_net_interface_flags_t flags,
	const struct grub_net_dhcp6_packet *v6h,
	grub_size_t size,
	int is_def,
	char **device, char **path)
{
  struct grub_net_network_level_interface *inf;
  grub_dhcp6_options_t dhcp6;
  int mask = -1;

  dhcp6 = grub_dhcp6_options_get (v6h, size);
  if (!dhcp6)
    {
      grub_print_error ();
      return NULL;
    }

  grub_net_configure_by_dhcp6_info (name, card, dhcp6, is_def, flags, &inf);

  if (device && dhcp6->boot_file_proto && dhcp6->boot_file_server_ip)
    {
      *device = grub_xasprintf ("%s,%s", dhcp6->boot_file_proto, dhcp6->boot_file_server_ip);
      grub_print_error ();
    }
  if (path && dhcp6->boot_file_path)
    {
      *path = grub_strdup (dhcp6->boot_file_path);
      grub_print_error ();
      if (*path)
	{
	  char *slash;
	  slash = grub_strrchr (*path, '/');
	  if (slash)
	    *slash = 0;
	  else
	    **path = 0;
	}
    }

  grub_dhcp6_options_free (dhcp6);

  if (inf)
    grub_net_add_ipv6_local (inf, mask);

  return inf;
}

/*
 * This is called directly from net/ip.c:handle_dgram(), because those
 * BOOTP/DHCP packets are a bit special due to their improper
 * sender/receiver IP fields.
 */
void
grub_net_process_dhcp (struct grub_net_buff *nb,
		       struct grub_net_network_level_interface *iface)
{
  char *name;
  struct grub_net_card *card = iface->card;
  const struct grub_net_bootp_packet *bp = (const struct grub_net_bootp_packet *) nb->data;
  grub_size_t size = nb->tail - nb->data;
  const grub_uint8_t *opt;
  grub_uint8_t opt_len, type;
  grub_uint32_t srv_id = 0;

  opt = find_dhcp_option (bp, size, GRUB_NET_DHCP_MESSAGE_TYPE, &opt_len);
  if (opt && opt_len == 1)
    type = *opt;
  else
    type = GRUB_DHCP_MESSAGE_UNKNOWN;

  opt = find_dhcp_option (bp, size, GRUB_NET_DHCP_SERVER_IDENTIFIER, &opt_len);
  if (opt && opt_len == sizeof (srv_id))
    srv_id = grub_get_unaligned32 (opt);

  /*
   * If we received BOOTP reply or DHCPACK, proceed with configuration.
   * Otherwise store offered address and server id for later processing
   * of DHCPACK.
   * xid and srv_id are stored in network order so do not need conversion.
   */
  if ((!iface->srv_id && type == GRUB_DHCP_MESSAGE_UNKNOWN)
      || (iface->srv_id && type == GRUB_DHCP_MESSAGE_ACK
	  && bp->ident == iface->xid
	  && srv_id == iface->srv_id))
    {
      name = grub_xasprintf ("%s:dhcp", card->name);
      if (!name)
	{
	  grub_print_error ();
	  return;
	}
      grub_net_configure_by_dhcp_ack (name, card, 0, bp, size, 0, 0, 0);
      grub_free (name);
      if (grub_errno)
	grub_print_error ();
      else
	grub_net_network_level_interface_unregister (iface);
    }
  else if (!iface->srv_id && type == GRUB_DHCP_MESSAGE_OFFER && srv_id)
    {
      iface->srv_id = srv_id;
      iface->my_ip = bp->your_ip;
      /* Reset retransmission timer */
      iface->dhcp_tmo = iface->dhcp_tmo_left = 1;
    }
  else if (iface->srv_id && type == GRUB_DHCP_MESSAGE_NAK
	   && bp->ident == iface->xid
	   && srv_id == iface->srv_id)
    {
      iface->xid = iface->srv_id = iface->my_ip = 0;
      /* Reset retransmission timer */
      iface->dhcp_tmo = iface->dhcp_tmo_left = 1;
    }
}

grub_err_t
grub_net_process_dhcp6 (struct grub_net_buff *nb,
                       struct grub_net_card *card __attribute__ ((unused)))
{
  const struct grub_net_dhcp6_packet *v6h;
  grub_dhcp6_session_t se;
  grub_size_t size;
  grub_dhcp6_options_t options;

  v6h = (const struct grub_net_dhcp6_packet *) nb->data;
  size = nb->tail - nb->data;

  options = grub_dhcp6_options_get (v6h, size);
  if (!options)
    return grub_errno;

  if (!options->client_duid || !options->server_duid || !options->ia_addr)
    {
      grub_dhcp6_options_free (options);
      return grub_error (GRUB_ERR_BAD_ARGUMENT, "Bad DHCPv6 Packet");
    }

  FOR_DHCP6_SESSIONS (se)
    {
      if (se->transaction_id == v6h->transaction_id &&
         grub_memcmp (options->client_duid, &se->duid, sizeof (se->duid)) == 0 &&
         se->iaid == options->iaid)
       break;
    }

  if (!se)
    {
      grub_dprintf ("bootp", "DHCPv6 session not found\n");
      grub_dhcp6_options_free (options);
      return GRUB_ERR_NONE;
    }

  if (v6h->message_type == GRUB_NET_DHCP6_ADVERTISE)
    {
      if (se->adv)
       {
         grub_dprintf ("bootp", "Skipped DHCPv6 Advertised .. \n");
         grub_dhcp6_options_free (options);
         return GRUB_ERR_NONE;
       }

      se->adv = options;
      return grub_dhcp6_session_send_request (se);
    }
  else if (v6h->message_type == GRUB_NET_DHCP6_REPLY)
    {
      if (!se->adv)
       {
         grub_dprintf ("bootp", "Skipped DHCPv6 Reply .. \n");
         grub_dhcp6_options_free (options);
         return GRUB_ERR_NONE;
       }

      se->reply = options;
      grub_dhcp6_session_configure_network (se);
      grub_dhcp6_session_remove (se);
      return GRUB_ERR_NONE;
    }
  else
    {
      grub_dhcp6_options_free (options);
    }

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_dhcpopt (struct grub_command *cmd __attribute__ ((unused)),
		  int argc, char **args)
{
  struct grub_net_network_level_interface *inter;
  unsigned num;
  const grub_uint8_t *ptr;
  grub_uint8_t taglength;

  if (argc < 4)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       N_("four arguments expected"));

  FOR_NET_NETWORK_LEVEL_INTERFACES (inter)
    if (grub_strcmp (inter->name, args[1]) == 0)
      break;

  if (!inter)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       N_("unrecognised network interface `%s'"), args[1]);

  if (!inter->dhcp_ack)
    return grub_error (GRUB_ERR_IO, N_("no DHCP info found"));

  ptr = inter->dhcp_ack->vendor;

  /* This duplicates check in find_dhcp_option to preserve previous error return */
  if (inter->dhcp_acklen < OFFSET_OF (vendor, inter->dhcp_ack) + sizeof (grub_uint32_t)
      || ptr[0] != GRUB_NET_BOOTP_RFC1048_MAGIC_0
      || ptr[1] != GRUB_NET_BOOTP_RFC1048_MAGIC_1
      || ptr[2] != GRUB_NET_BOOTP_RFC1048_MAGIC_2
      || ptr[3] != GRUB_NET_BOOTP_RFC1048_MAGIC_3)
    return grub_error (GRUB_ERR_IO, N_("no DHCP options found"));

  num = grub_strtoul (args[2], 0, 0);
  if (grub_errno)
    return grub_errno;

  /* Exclude PAD (0) and END (255) option codes */
  if (num == 0 || num > 254)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("invalid DHCP option code"));

  ptr = find_dhcp_option (inter->dhcp_ack, inter->dhcp_acklen, num, &taglength);
  if (!ptr)
    return grub_error (GRUB_ERR_IO, N_("no DHCP option %u found"), num);

  if (grub_strcmp (args[3], "string") == 0)
    {
      grub_err_t err = GRUB_ERR_NONE;
      char *val = grub_malloc (taglength + 1);
      if (!val)
	return grub_errno;
      grub_memcpy (val, ptr, taglength);
      val[taglength] = 0;
      if (args[0][0] == '-' && args[0][1] == 0)
	grub_printf ("%s\n", val);
      else
	err = grub_env_set (args[0], val);
      grub_free (val);
      return err;
    }

  if (grub_strcmp (args[3], "number") == 0)
    {
      grub_uint64_t val = 0;
      int i;
      for (i = 0; i < taglength; i++)
	val = (val << 8) | ptr[i];
      if (args[0][0] == '-' && args[0][1] == 0)
	grub_printf ("%llu\n", (unsigned long long) val);
      else
	{
	  char valn[64];
	  grub_snprintf (valn, sizeof (valn), "%lld\n", (unsigned long long) val);
	  return grub_env_set (args[0], valn);
	}
      return GRUB_ERR_NONE;
    }

  if (grub_strcmp (args[3], "hex") == 0)
    {
      grub_err_t err = GRUB_ERR_NONE;
      char *val = grub_malloc (2 * taglength + 1);
      int i;
      if (!val)
	return grub_errno;
      for (i = 0; i < taglength; i++)
	{
	  val[2 * i] = hexdigit (ptr[i] >> 4);
	  val[2 * i + 1] = hexdigit (ptr[i] & 0xf);
	}
      val[2 * taglength] = 0;
      if (args[0][0] == '-' && args[0][1] == 0)
	grub_printf ("%s\n", val);
      else
	err = grub_env_set (args[0], val);
      grub_free (val);
      return err;
    }

  return grub_error (GRUB_ERR_BAD_ARGUMENT,
		     N_("unrecognised DHCP option format specification `%s'"),
		     args[3]);
}

/* FIXME: allow to specify mac address.  */
static grub_err_t
grub_cmd_bootp (struct grub_command *cmd __attribute__ ((unused)),
		int argc, char **args)
{
  struct grub_net_card *card;
  struct grub_net_network_level_interface *ifaces;
  grub_size_t ncards = 0;
  unsigned j = 0;
  grub_err_t err;
  unsigned i;

  FOR_NET_CARDS (card)
  {
    if (argc > 0 && grub_strcmp (card->name, args[0]) != 0)
      continue;
    ncards++;
  }

  if (ncards == 0)
    return grub_error (GRUB_ERR_NET_NO_CARD, N_("no network card found"));

  ifaces = grub_calloc (ncards, sizeof (ifaces[0]));
  if (!ifaces)
    return grub_errno;

  j = 0;
  FOR_NET_CARDS (card)
  {
    if (argc > 0 && grub_strcmp (card->name, args[0]) != 0)
      continue;
    ifaces[j].card = card;
    ifaces[j].next = &ifaces[j+1];
    if (j)
      ifaces[j].prev = &ifaces[j-1].next;
    ifaces[j].name = grub_xasprintf ("%s:dhcp_tmp", card->name);
    card->num_ifaces++;
    if (!ifaces[j].name)
      {
	for (i = 0; i < j; i++)
	  grub_free (ifaces[i].name);
	grub_free (ifaces);
	return grub_errno;
      }
    ifaces[j].address.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_DHCP_RECV;
    grub_memcpy (&ifaces[j].hwaddress, &card->default_address, 
		 sizeof (ifaces[j].hwaddress));
    ifaces[j].dhcp_tmo = ifaces[j].dhcp_tmo_left = 1;
    j++;
  }
  ifaces[ncards - 1].next = grub_net_network_level_interfaces;
  if (grub_net_network_level_interfaces)
    grub_net_network_level_interfaces->prev = & ifaces[ncards - 1].next;
  grub_net_network_level_interfaces = &ifaces[0];
  ifaces[0].prev = &grub_net_network_level_interfaces;

  /*
   * Running DHCP restransmission timer is kept per interface in dhcp_tmo_left.
   * When it runs off, dhcp_tmo is increased exponentionally and dhcp_tmo_left
   * initialized to it. Max value is 32 which gives approximately 12s total per
   * packet timeout assuming 200ms poll tick. Timeout is reset when DHCP OFFER
   * is received, so total timeout is 25s in the worst case.
   *
   * DHCP NAK also resets timer and transaction starts again.
   *
   * Total wait time is limited to ~25s to prevent endless loop in case of
   * permanent NAK
   */
  for (i = 0; i < GRUB_DHCP_MAX_PACKET_TIMEOUT * 4; i++)
    {
      int need_poll = 0;
      for (j = 0; j < ncards; j++)
	{
	  if (!ifaces[j].prev ||
             ifaces[j].dhcp_tmo > GRUB_DHCP_MAX_PACKET_TIMEOUT)
	    continue;

	  if (--ifaces[j].dhcp_tmo_left)
	    {
	      need_poll = 1;
	      continue;
	    }

	  ifaces[j].dhcp_tmo *= 2;
	  if (ifaces[j].dhcp_tmo > GRUB_DHCP_MAX_PACKET_TIMEOUT)
	    continue;

	  err = send_dhcp_packet (&ifaces[j]);
	  if (err)
	    {
	      grub_print_error ();
	      /* To ignore it during next poll */
	      ifaces[j].dhcp_tmo = GRUB_DHCP_MAX_PACKET_TIMEOUT + 1;
	      continue;
	    }
	  ifaces[j].dhcp_tmo_left = ifaces[j].dhcp_tmo;
	  need_poll = 1;
	}
      if (!need_poll)
	break;
      grub_net_poll_cards (200, 0);
    }

  err = GRUB_ERR_NONE;
  for (j = 0; j < ncards; j++)
    {
      grub_free (ifaces[j].name);
      if (!ifaces[j].prev)
	continue;
      grub_error_push ();
      grub_net_network_level_interface_unregister (&ifaces[j]);
      err = grub_error (GRUB_ERR_FILE_NOT_FOUND,
			N_("couldn't autoconfigure %s"),
			ifaces[j].card->name);
    }

  grub_free (ifaces);
  return err;
}

static grub_err_t
grub_cmd_bootp6 (struct grub_command *cmd __attribute__ ((unused)),
                 int argc, char **args)
{
  struct grub_net_card *card;
  grub_uint32_t iaid = 0;
  int interval;
  grub_err_t err;
  grub_dhcp6_session_t se;

  err = GRUB_ERR_NONE;

  FOR_NET_CARDS (card)
  {
    struct grub_net_network_level_interface *iface;

    if (argc > 0 && grub_strcmp (card->name, args[0]) != 0)
      continue;

    iface = grub_net_ipv6_get_link_local (card, &card->default_address);
    if (!iface)
      {
       grub_dhcp6_session_remove_all ();
       return grub_errno;
      }

    grub_dhcp6_session_add (iface, iaid++);
  }

  for (interval = 200; interval < 10000; interval *= 2)
    {
      int done = 1;

      FOR_DHCP6_SESSIONS (se)
       {
         struct grub_net_buff *nb;
         struct grub_net_dhcp6_option *opt;
         struct grub_net_dhcp6_packet *v6h;
         struct grub_net_dhcp6_option_duid_ll *duid;
         struct grub_net_dhcp6_option_iana *ia_na;
         grub_net_network_level_address_t multicast;
         grub_net_link_level_address_t ll_multicast;
         struct udphdr *udph;

         multicast.type = GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6;
         multicast.ipv6[0] = grub_cpu_to_be64_compile_time (0xff02ULL << 48);
         multicast.ipv6[1] = grub_cpu_to_be64_compile_time (0x10002ULL);

         err = grub_net_link_layer_resolve (se->iface,
                   &multicast, &ll_multicast);
         if (err)
           {
             grub_dhcp6_session_remove_all ();
             return err;
           }

         nb = grub_netbuff_alloc (GRUB_DHCP6_DEFAULT_NETBUFF_ALLOC_SIZE);

         if (!nb)
           {
             grub_dhcp6_session_remove_all ();
             return grub_errno;
           }

         err = grub_netbuff_reserve (nb, GRUB_DHCP6_DEFAULT_NETBUFF_ALLOC_SIZE);
         if (err)
           {
             grub_dhcp6_session_remove_all ();
             grub_netbuff_free (nb);
             return err;
           }

         err = grub_netbuff_push (nb, sizeof (*opt) + sizeof (grub_uint16_t));
         if (err)
           {
             grub_dhcp6_session_remove_all ();
             grub_netbuff_free (nb);
             return err;
           }

         opt = (struct grub_net_dhcp6_option *)nb->data;
         opt->code = grub_cpu_to_be16_compile_time (GRUB_NET_DHCP6_OPTION_ELAPSED_TIME);
         opt->len = grub_cpu_to_be16_compile_time (sizeof (grub_uint16_t));
         grub_set_unaligned16 (opt->data, 0);

         err = grub_netbuff_push (nb, sizeof (*opt) + sizeof (*duid));
         if (err)
           {
             grub_dhcp6_session_remove_all ();
             grub_netbuff_free (nb);
             return err;
           }

         opt = (struct grub_net_dhcp6_option *)nb->data;
         opt->code = grub_cpu_to_be16_compile_time (GRUB_NET_DHCP6_OPTION_CLIENTID);
         opt->len = grub_cpu_to_be16 (sizeof (*duid));

         duid = (struct grub_net_dhcp6_option_duid_ll *) opt->data;
         grub_memcpy (duid, &se->duid, sizeof (*duid));

         err = grub_netbuff_push (nb, sizeof (*opt) + sizeof (*ia_na));
         if (err)
           {
             grub_dhcp6_session_remove_all ();
             grub_netbuff_free (nb);
             return err;
           }

         opt = (struct grub_net_dhcp6_option *)nb->data;
         opt->code = grub_cpu_to_be16_compile_time (GRUB_NET_DHCP6_OPTION_IA_NA);
         opt->len = grub_cpu_to_be16 (sizeof (*ia_na));
         ia_na = (struct grub_net_dhcp6_option_iana *)opt->data;
         ia_na->iaid = grub_cpu_to_be32 (se->iaid);
         ia_na->t1 = 0;
         ia_na->t2 = 0;

         err = grub_netbuff_push (nb, sizeof (*v6h));
         if (err)
           {
             grub_dhcp6_session_remove_all ();
             grub_netbuff_free (nb);
             return err;
           }

         v6h = (struct grub_net_dhcp6_packet *)nb->data;
         v6h->message_type = GRUB_NET_DHCP6_SOLICIT;
         v6h->transaction_id = se->transaction_id;

         grub_netbuff_push (nb, sizeof (*udph));

         udph = (struct udphdr *) nb->data;
         udph->src = grub_cpu_to_be16_compile_time (DHCP6_CLIENT_PORT);
         udph->dst = grub_cpu_to_be16_compile_time (DHCP6_SERVER_PORT);
         udph->chksum = 0;
         udph->len = grub_cpu_to_be16 (nb->tail - nb->data);

         udph->chksum = grub_net_ip_transport_checksum (nb, GRUB_NET_IP_UDP,
                           &se->iface->address, &multicast);

         err = grub_net_send_ip_packet (se->iface, &multicast,
                   &ll_multicast, nb, GRUB_NET_IP_UDP);
         done = 0;
         grub_netbuff_free (nb);

         if (err)
           {
             grub_dhcp6_session_remove_all ();
             return err;
           }
       }
      if (!done)
       grub_net_poll_cards (interval, 0);
    }

  FOR_DHCP6_SESSIONS (se)
    {
      grub_error_push ();
      err = grub_error (GRUB_ERR_FILE_NOT_FOUND,
                       N_("couldn't autoconfigure %s"),
                       se->iface->card->name);
    }

  grub_dhcp6_session_remove_all ();

  return err;
}

static grub_command_t cmd_getdhcp, cmd_bootp, cmd_dhcp, cmd_bootp6;

void
grub_bootp_init (void)
{
  cmd_bootp = grub_register_command ("net_bootp", grub_cmd_bootp,
				     N_("[CARD]"),
				     N_("perform a bootp autoconfiguration"));
  cmd_dhcp = grub_register_command ("net_dhcp", grub_cmd_bootp,
				     N_("[CARD]"),
				     N_("perform a DHCP autoconfiguration"));
  cmd_getdhcp = grub_register_command ("net_get_dhcp_option", grub_cmd_dhcpopt,
				       N_("VAR INTERFACE NUMBER DESCRIPTION"),
				       N_("retrieve DHCP option and save it into VAR. If VAR is - then print the value."));
  cmd_bootp6 = grub_register_command ("net_bootp6", grub_cmd_bootp6,
				     N_("[CARD]"),
				     N_("perform a DHCPv6 autoconfiguration"));
}

void
grub_bootp_fini (void)
{
  grub_unregister_command (cmd_bootp6);
  grub_unregister_command (cmd_getdhcp);
  grub_unregister_command (cmd_bootp);
  grub_unregister_command (cmd_dhcp);
}
