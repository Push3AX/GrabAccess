#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/misc.h>
#include <grub/net/efi.h>
#include <grub/charset.h>
#include <grub/safemath.h>

char *
grub_efi_ip6_address_to_string (grub_efi_pxe_ipv6_address_t *address)
{
  char *str = grub_malloc (sizeof ("XXXX:XXXX:XXXX:XXXX:XXXX:XXXX:XXXX:XXXX"));
  char *p;
  int i;
  int squash;

  if (!str)
    return NULL;

  p = str;
  squash = 0;
  for (i = 0; i < 8; ++i)
    {
      grub_uint16_t addr;

      if (i == 7)
	squash = 2;

      addr = grub_get_unaligned16 (address->addr + i * 2);

      if (grub_be_to_cpu16 (addr))
	{
	  char buf[sizeof ("XXXX")];
	  if (i > 0)
	    *p++ = ':';
	  grub_snprintf (buf, sizeof (buf), "%x", grub_be_to_cpu16 (addr));
	  grub_strcpy (p, buf);
	  p += grub_strlen (buf);

	  if (squash == 1)
	    squash = 2;
	}
      else
	{
	  if (squash == 0)
	    {
	      *p++ = ':';
	      squash = 1;
	    }
	  else if (squash == 2)
	    {
	      *p++ = ':';
	      *p++ = '0';
	    }
	}
    }
  *p = '\0';
  return str;
}

int
grub_efi_string_to_ip6_address (const char *val, grub_efi_ipv6_address_t *address, const char **rest)
{
  grub_uint16_t newip[8];
  const char *ptr = val;
  int word, quaddot = -1;
  int bracketed = 0;

  if (ptr[0] == '[') {
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
  grub_memcpy (address, newip, 16);
  if (bracketed && *ptr == ']') {
    ptr++;
  }
  if (rest)
    *rest = ptr;
  return 1;
}

static grub_efi_ip6_config_interface_info_t *
efi_ip6_config_interface_info (grub_efi_ip6_config_protocol_t *ip6_config)
{
  grub_efi_uintn_t sz;
  grub_efi_status_t status;
  grub_efi_ip6_config_interface_info_t *interface_info;

  sz = sizeof (*interface_info) + sizeof (*interface_info->route_table);
  interface_info = grub_malloc (sz);

  status = efi_call_4 (ip6_config->get_data, ip6_config,
		GRUB_EFI_IP6_CONFIG_DATA_TYPE_INTERFACEINFO,
		&sz, interface_info);

  if (status == GRUB_EFI_BUFFER_TOO_SMALL)
    {
      grub_free (interface_info);
      interface_info = grub_malloc (sz);
      status = efi_call_4 (ip6_config->get_data, ip6_config,
		    GRUB_EFI_IP6_CONFIG_DATA_TYPE_INTERFACEINFO,
		    &sz, interface_info);
    }

  if (status != GRUB_EFI_SUCCESS)
    {
      grub_free (interface_info);
      return NULL;
    }

  return interface_info;
}

static grub_efi_ip6_config_manual_address_t *
efi_ip6_config_manual_address (grub_efi_ip6_config_protocol_t *ip6_config)
{
  grub_efi_uintn_t sz;
  grub_efi_status_t status;
  grub_efi_ip6_config_manual_address_t *manual_address;

  sz = sizeof (*manual_address);
  manual_address = grub_malloc (sz);
  if (!manual_address)
    return NULL;

  status = efi_call_4 (ip6_config->get_data, ip6_config,
		    GRUB_EFI_IP6_CONFIG_DATA_TYPE_MANUAL_ADDRESS,
		    &sz, manual_address);

  if (status != GRUB_EFI_SUCCESS)
    {
      grub_free (manual_address);
      return NULL;
    }

  return manual_address;
}

char *
grub_efi_ip6_interface_name (struct grub_efi_net_device *dev)
{
  grub_efi_ip6_config_interface_info_t *interface_info;
  char *name;

  interface_info = efi_ip6_config_interface_info (dev->ip6_config);

  if (!interface_info)
    return NULL;

  name = grub_malloc (GRUB_EFI_IP4_CONFIG2_INTERFACE_INFO_NAME_SIZE
		      * GRUB_MAX_UTF8_PER_UTF16 + 1);
  *grub_utf16_to_utf8 ((grub_uint8_t *)name, interface_info->name,
		      GRUB_EFI_IP4_CONFIG2_INTERFACE_INFO_NAME_SIZE) = 0;
  grub_free (interface_info);
  return name;
}

static char *
grub_efi_ip6_interface_hw_address (struct grub_efi_net_device *dev)
{
  grub_efi_ip6_config_interface_info_t *interface_info;
  char *hw_addr;

  interface_info = efi_ip6_config_interface_info (dev->ip6_config);

  if (!interface_info)
    return NULL;

  hw_addr = grub_efi_hw_address_to_string (interface_info->hw_address_size, interface_info->hw_address);
  grub_free (interface_info);

  return hw_addr;
}

static char *
grub_efi_ip6_interface_address (struct grub_efi_net_device *dev)
{
  grub_efi_ip6_config_manual_address_t *manual_address;
  char *addr;

  manual_address = efi_ip6_config_manual_address (dev->ip6_config);

  if (!manual_address)
    return NULL;

  addr = grub_efi_ip6_address_to_string ((grub_efi_pxe_ipv6_address_t *)&manual_address->address);
  grub_free (manual_address);
  return addr;
}

static char **
grub_efi_ip6_interface_route_table (struct grub_efi_net_device *dev)
{
  grub_efi_ip6_config_interface_info_t *interface_info;
  char **ret;
  int id;
  grub_size_t i, nmemb;

  interface_info = efi_ip6_config_interface_info (dev->ip6_config);
  if (!interface_info)
    return NULL;

  if (grub_add (interface_info->route_count, 1, &nmemb))
    {
      grub_errno = GRUB_ERR_OUT_OF_RANGE;
      return NULL;
    }

  ret = grub_calloc (nmemb, sizeof (*ret));
  if (!ret)
    {
      grub_free (interface_info);
      return NULL;
    }

  id = 0;
  for (i = 0; i < interface_info->route_count ; i++)
    {
      char *gateway, *destination;
      grub_uint64_t u64_gateway[2];
      grub_uint64_t u64_destination[2];
      grub_efi_ip6_route_table_t *route_table = interface_info->route_table + i;
      grub_efi_net_interface_t *inf;
      char *interface_name = NULL;

      gateway = grub_efi_ip6_address_to_string (&route_table->gateway);
      destination = grub_efi_ip6_address_to_string (&route_table->destination);

      u64_gateway[0] = grub_get_unaligned64 (route_table->gateway.addr);
      u64_gateway[1] = grub_get_unaligned64 (route_table->gateway.addr + 8);
      u64_destination[0] = grub_get_unaligned64 (route_table->destination.addr);
      u64_destination[1] = grub_get_unaligned64 (route_table->destination.addr + 8);

      for (inf = dev->net_interfaces; inf; inf = inf->next)
	if (inf->prefer_ip6)
	  interface_name = inf->name;

      if ((!u64_gateway[0] && !u64_gateway[1])
	  && (u64_destination[0] || u64_destination[1]))
	{
	  if (interface_name)
	    {
	      if ((grub_be_to_cpu64 (u64_destination[0]) == 0xfe80000000000000ULL)
	      && (!u64_destination[1])
	      && (route_table->prefix_length == 64))
		ret[id++] = grub_xasprintf ("%s:link %s/%d %s", dev->card_name, destination, route_table->prefix_length, interface_name);
	      else
		ret[id++] = grub_xasprintf ("%s:local %s/%d %s", dev->card_name, destination, route_table->prefix_length, interface_name);
	    }
	}
      else if ((u64_gateway[0] || u64_gateway[1])
	  && (u64_destination[0] || u64_destination[1]))
	ret[id++] = grub_xasprintf ("%s:gw %s/%d gw %s", dev->card_name, destination, route_table->prefix_length, gateway);
      else if ((u64_gateway[0] || u64_gateway[1])
	  && (!u64_destination[0] && !u64_destination[1]))
	ret[id++] = grub_xasprintf ("%s:default %s/%d gw %s", dev->card_name, destination, route_table->prefix_length, gateway);

      grub_free (gateway);
      grub_free (destination);
    }

  ret[id] = NULL;
  grub_free (interface_info);
  return ret;
}

static grub_efi_net_interface_t *
grub_efi_ip6_interface_match (struct grub_efi_net_device *dev, grub_efi_net_ip_address_t *ip_address)
{
  grub_efi_ip6_config_interface_info_t *interface_info;
  grub_efi_net_interface_t *inf;
  int i;
  grub_efi_ipv6_address_t *address = &ip_address->ip6;

  interface_info = efi_ip6_config_interface_info (dev->ip6_config);
  if (!interface_info)
    return NULL;

  for (i = 0; i < (int)interface_info->route_count ; i++)
    {
      grub_uint64_t u64_addr[2];
      grub_uint64_t u64_subnet[2];
      grub_uint64_t u64_mask[2];

      grub_efi_ip6_route_table_t *route_table = interface_info->route_table + i;

      /* SKIP Default GATEWAY */
      if (route_table->prefix_length == 0)
	continue;

      u64_addr[0] = grub_get_unaligned64 (address);
      u64_addr[1] = grub_get_unaligned64 (address + 4);
      u64_subnet[0] = grub_get_unaligned64 (route_table->destination.addr);
      u64_subnet[1] = grub_get_unaligned64 (route_table->destination.addr + 8);
      u64_mask[0] = (route_table->prefix_length <= 64) ?
	    0xffffffffffffffffULL << (64 - route_table->prefix_length) :
	    0xffffffffffffffffULL;
      u64_mask[1] = (route_table->prefix_length <= 64) ?
	    0 :
	    0xffffffffffffffffULL << (128 - route_table->prefix_length);

      if (((u64_addr[0] & u64_mask[0]) == u64_subnet[0])
	  && ((u64_addr[1] & u64_mask[1]) == u64_subnet[1]))
	{
	  for (inf = dev->net_interfaces; inf; inf = inf->next)
	    if (inf->prefer_ip6)
	      {
		grub_free (interface_info);
		return inf;
	      }
	}
    }

  grub_free (interface_info);
  return NULL;
}

static int
grub_efi_ip6_interface_set_manual_address (struct grub_efi_net_device *dev,
	    grub_efi_net_ip_manual_address_t *net_ip,
	    int with_subnet)
{
  grub_efi_status_t status;
  grub_efi_ip6_config_manual_address_t *address = &net_ip->ip6;

  if (!with_subnet)
    {
      grub_efi_ip6_config_manual_address_t *manual_address =
      efi_ip6_config_manual_address (dev->ip6_config);

      if (manual_address)
	{
	  address->prefix_length = manual_address->prefix_length;
	  grub_free (manual_address);
	}
      else
	{
	  /* XXX: */
	  address->prefix_length = 64;
	}
    }

  status = efi_call_4 (dev->ip6_config->set_data, dev->ip6_config,
		    GRUB_EFI_IP6_CONFIG_DATA_TYPE_MANUAL_ADDRESS,
		    sizeof(*address), address);

  if (status != GRUB_EFI_SUCCESS)
    return 0;

  return 1;
}

static int
grub_efi_ip6_interface_set_gateway (struct grub_efi_net_device *dev,
	      grub_efi_net_ip_address_t *address)
{
  grub_efi_status_t status;

  status = efi_call_4 (dev->ip6_config->set_data, dev->ip6_config,
		GRUB_EFI_IP6_CONFIG_DATA_TYPE_GATEWAY,
		sizeof (address->ip6), &address->ip6);

  if (status != GRUB_EFI_SUCCESS)
    return 0;
  return 1;
}

static int
grub_efi_ip6_interface_set_dns (struct grub_efi_net_device *dev,
	      grub_efi_net_ip_address_t *address)
{

  grub_efi_status_t status;

  status = efi_call_4 (dev->ip6_config->set_data, dev->ip6_config,
		GRUB_EFI_IP6_CONFIG_DATA_TYPE_DNSSERVER,
		sizeof (address->ip6), &address->ip6);

  if (status != GRUB_EFI_SUCCESS)
    return 0;
  return 1;
}

grub_efi_net_ip_config_t *efi_net_ip6_config = &(grub_efi_net_ip_config_t)
  {
    .get_hw_address = grub_efi_ip6_interface_hw_address,
    .get_address = grub_efi_ip6_interface_address,
    .get_route_table = grub_efi_ip6_interface_route_table,
    .best_interface = grub_efi_ip6_interface_match,
    .set_address = grub_efi_ip6_interface_set_manual_address,
    .set_gateway = grub_efi_ip6_interface_set_gateway,
    .set_dns = grub_efi_ip6_interface_set_dns
  };
