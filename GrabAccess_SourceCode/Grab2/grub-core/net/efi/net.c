#include <grub/net.h>
#include <grub/env.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/dl.h>
#include <grub/command.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/i18n.h>
#include <grub/bufio.h>
#include <grub/efi/http.h>
#include <grub/efi/dhcp.h>
#include <grub/net/efi.h>
#include <grub/charset.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define GRUB_EFI_IP6_PREFIX_LENGTH 64

static grub_efi_guid_t ip4_config_guid = GRUB_EFI_IP4_CONFIG2_PROTOCOL_GUID;
static grub_efi_guid_t ip6_config_guid = GRUB_EFI_IP6_CONFIG_PROTOCOL_GUID;
static grub_efi_guid_t http_service_binding_guid = GRUB_EFI_HTTP_SERVICE_BINDING_PROTOCOL_GUID;
static grub_efi_guid_t http_guid = GRUB_EFI_HTTP_PROTOCOL_GUID;
static grub_efi_guid_t pxe_io_guid = GRUB_EFI_PXE_GUID;
static grub_efi_guid_t dhcp4_service_binding_guid = GRUB_EFI_DHCP4_SERVICE_BINDING_PROTOCOL_GUID;
static grub_efi_guid_t dhcp4_guid = GRUB_EFI_DHCP4_PROTOCOL_GUID;
static grub_efi_guid_t dhcp6_service_binding_guid = GRUB_EFI_DHCP6_SERVICE_BINDING_PROTOCOL_GUID;
static grub_efi_guid_t dhcp6_guid = GRUB_EFI_DHCP6_PROTOCOL_GUID;

struct grub_efi_net_device *net_devices;

static char *default_server;
static grub_efi_net_interface_t *net_interface;
static grub_efi_net_interface_t *net_default_interface;

#define efi_net_interface_configure(inf) inf->io->configure (inf->dev, inf->prefer_ip6)
#define efi_net_interface_open(inf, file, name) inf->io->open (inf->dev, inf->prefer_ip6, file, name, inf->io_type)
#define efi_net_interface_read(inf, file, buf, sz) inf->io->read (inf->dev, inf->prefer_ip6, file, buf, sz)
#define efi_net_interface_close(inf, file) inf->io->close (inf->dev, inf->prefer_ip6, file)
#define efi_net_interface(m,...) efi_net_interface_ ## m (net_interface, ## __VA_ARGS__)

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

static int
url_parse_fields (const char *url, char **proto, char **host, char **path)
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

static void
url_get_boot_location (const char *url, char **device, char **path, int is_default)
{
  char *protocol, *server, *file;
  char *slash;

  if (!url_parse_fields (url, &protocol, &server, &file))
    return;

  if ((slash = grub_strrchr (file, '/')))
    *slash = 0;
  else
    *file = 0;

  *device = grub_xasprintf ("%s,%s", protocol, server);
  *path = grub_strdup(file);

  if (is_default)
    default_server = server;
  else
    grub_free (server);

  grub_free (protocol);
  grub_free (file);
}

static void
pxe_get_boot_location (const struct grub_net_bootp_packet *bp,
		  char **device,
		  char **path,
		  int is_default)
{
  char *server = grub_xasprintf ("%d.%d.%d.%d",
	     ((grub_uint8_t *) &bp->server_ip)[0],
	     ((grub_uint8_t *) &bp->server_ip)[1],
	     ((grub_uint8_t *) &bp->server_ip)[2],
	     ((grub_uint8_t *) &bp->server_ip)[3]);

  *device = grub_xasprintf ("tftp,%s", server);

  *path = grub_strndup (bp->boot_file, sizeof (bp->boot_file));

  if (*path)
    {
      char *slash;
      slash = grub_strrchr (*path, '/');
      if (slash)
	*slash = 0;
      else
	**path = 0;
    }

  if (is_default)
    default_server = server;
  else
    grub_free (server);
}

static void
pxe_get_boot_location_v6 (const struct grub_net_dhcp6_packet *dp,
		  grub_size_t dhcp_size,
		  char **device,
		  char **path)
{

  struct grub_net_dhcp6_option *dhcp_opt;
  grub_size_t dhcp_remain_size;
  *device = *path = 0;

  if (dhcp_size < sizeof (*dp))
    {
      grub_error (GRUB_ERR_OUT_OF_RANGE, N_("DHCPv6 packet size too small"));
      return;
    }

  dhcp_remain_size = dhcp_size - sizeof (*dp);
  dhcp_opt = (struct grub_net_dhcp6_option *)dp->dhcp_options;

  while (dhcp_remain_size)
    {
      grub_uint16_t code = grub_be_to_cpu16 (dhcp_opt->code);
      grub_uint16_t len = grub_be_to_cpu16 (dhcp_opt->len);
      grub_uint16_t option_size = sizeof (*dhcp_opt) + len;

      if (dhcp_remain_size < option_size || code == 0)
	break;

      if (code == GRUB_NET_DHCP6_OPTION_BOOTFILE_URL)
	{
	  char *url = grub_malloc (len + 1);

	  grub_memcpy (url, dhcp_opt->data, len);
	  url[len] = 0;

	  url_get_boot_location ((const char *)url, device, path, 1);
	  grub_free (url);
	  break;
	}

      dhcp_remain_size -= option_size;
      dhcp_opt = (struct grub_net_dhcp6_option *)((grub_uint8_t *)dhcp_opt + option_size);
    }
}

static grub_efi_net_interface_t *
grub_efi_net_config_from_device_path (grub_efi_device_path_t *dp,
		  struct grub_efi_net_device *netdev,
		  char **device,
		  char **path)
{
  grub_efi_net_interface_t *inf = NULL;

  while (1)
    {
      grub_efi_uint8_t type = GRUB_EFI_DEVICE_PATH_TYPE (dp);
      grub_efi_uint8_t subtype = GRUB_EFI_DEVICE_PATH_SUBTYPE (dp);
      grub_efi_uint16_t len = GRUB_EFI_DEVICE_PATH_LENGTH (dp);

      if (type == GRUB_EFI_MESSAGING_DEVICE_PATH_TYPE)
	{
	  if (subtype == GRUB_EFI_URI_DEVICE_PATH_SUBTYPE)
	    {
	      grub_efi_uri_device_path_t *uri_dp;
	      uri_dp = (grub_efi_uri_device_path_t *) dp;
	      /* Beware that uri_dp->uri may not be null terminated */
	      url_get_boot_location ((const char *)uri_dp->uri, device, path, 1);
	    }
	  else if (subtype == GRUB_EFI_IPV4_DEVICE_PATH_SUBTYPE)
	    {
	      grub_efi_net_ip_manual_address_t net_ip;
	      grub_efi_ipv4_device_path_t *ipv4 = (grub_efi_ipv4_device_path_t *) dp;

	      if (inf)
		continue;
	      grub_memcpy (net_ip.ip4.address, ipv4->local_ip_address, sizeof (net_ip.ip4.address));
	      grub_memcpy (net_ip.ip4.subnet_mask, ipv4->subnet_mask, sizeof (net_ip.ip4.subnet_mask));
	      net_ip.is_ip6 = 0;
	      inf = grub_efi_net_create_interface (netdev,
			    netdev->card_name,
			    &net_ip,
			    1);
	    }
	  else if (subtype == GRUB_EFI_IPV6_DEVICE_PATH_SUBTYPE)
	    {
	      grub_efi_net_ip_manual_address_t net_ip;
	      grub_efi_ipv6_device_path_t *ipv6 = (grub_efi_ipv6_device_path_t *) dp;

	      if (inf)
		continue;
	      grub_memcpy (net_ip.ip6.address, ipv6->local_ip_address, sizeof (net_ip.ip6.address));
	      net_ip.ip6.prefix_length = GRUB_EFI_IP6_PREFIX_LENGTH;
	      net_ip.ip6.is_anycast = 0;
	      net_ip.is_ip6 = 1;
	      inf = grub_efi_net_create_interface (netdev,
			    netdev->card_name,
			    &net_ip,
			    1);
	    }
	}

      if (GRUB_EFI_END_ENTIRE_DEVICE_PATH (dp))
        break;
      dp = (grub_efi_device_path_t *) ((char *) dp + len);
    }

  return inf;
}

static grub_efi_net_interface_t *
grub_efi_net_config_from_handle (grub_efi_handle_t *hnd,
		  struct grub_efi_net_device *netdev,
		  char **device,
		  char **path)
{
  grub_efi_pxe_t *pxe = NULL;

  if (hnd == netdev->ip4_pxe_handle)
    pxe = netdev->ip4_pxe;
  else if (hnd == netdev->ip6_pxe_handle)
    pxe = netdev->ip6_pxe;

  if (!pxe)
    return (grub_efi_net_config_from_device_path (
		grub_efi_get_device_path (hnd),
		netdev,
		device,
		path));

  if (pxe->mode->using_ipv6)
    {
      grub_efi_net_ip_manual_address_t net_ip;

      pxe_get_boot_location_v6 (
	    (const struct grub_net_dhcp6_packet *) &pxe->mode->dhcp_ack,
	    sizeof (pxe->mode->dhcp_ack),
	    device,
	    path);

      grub_memcpy (net_ip.ip6.address, pxe->mode->station_ip.v6, sizeof(net_ip.ip6.address));
      net_ip.ip6.prefix_length = GRUB_EFI_IP6_PREFIX_LENGTH;
      net_ip.ip6.is_anycast = 0;
      net_ip.is_ip6 = 1;
      return (grub_efi_net_create_interface (netdev,
		    netdev->card_name,
		    &net_ip,
		    1));
    }
  else
    {
      grub_efi_net_ip_manual_address_t net_ip;

      pxe_get_boot_location (
		(const struct grub_net_bootp_packet *) &pxe->mode->dhcp_ack,
		device,
		path,
		1);

      grub_memcpy (net_ip.ip4.address, pxe->mode->station_ip.v4, sizeof (net_ip.ip4.address));
      grub_memcpy (net_ip.ip4.subnet_mask, pxe->mode->subnet_mask.v4, sizeof (net_ip.ip4.subnet_mask));
      net_ip.is_ip6 = 0;
      return (grub_efi_net_create_interface (netdev,
		    netdev->card_name,
		    &net_ip,
		    1));
    }
}

static const char *
grub_efi_net_var_get_address (struct grub_env_var *var,
                   const char *val __attribute__ ((unused)))
{
  struct grub_efi_net_device *dev;

  for (dev = net_devices; dev; dev = dev->next)
    {
      grub_efi_net_interface_t *inf;

      for (inf = dev->net_interfaces; inf; inf = inf->next)
	{
	  char *var_name;

	  var_name = grub_xasprintf ("net_%s_ip", inf->name);
	  if (grub_strcmp (var_name, var->name) == 0)
	    return efi_net_interface_get_address (inf);
	  grub_free (var_name);
	  var_name = grub_xasprintf ("net_%s_mac", inf->name);
	  if (grub_strcmp (var_name, var->name) == 0)
	    return efi_net_interface_get_hw_address (inf);
	  grub_free (var_name);
	}
    }

  return NULL;
}

static char *
grub_efi_net_var_set_interface (struct grub_env_var *var __attribute__ ((unused)),
		   const char *val)
{
  struct grub_efi_net_device *dev;
  grub_efi_net_interface_t *inf;

  for (dev = net_devices; dev; dev = dev->next)
    for (inf = dev->net_interfaces; inf; inf = inf->next)
      if (grub_strcmp (inf->name, val) == 0)
	{
	  net_default_interface = inf;
	  return grub_strdup (val);
	}

  return NULL;
}

static char *
grub_efi_net_var_set_server (struct grub_env_var *var __attribute__ ((unused)),
		   const char *val)
{
  grub_free (default_server);
  default_server = grub_strdup (val);
  return grub_strdup (val);
}

static const char *
grub_efi_net_var_get_server (struct grub_env_var *var __attribute__ ((unused)),
		   const char *val __attribute__ ((unused)))
{
  return default_server ? : "";
}

static const char *
grub_efi_net_var_get_ip (struct grub_env_var *var __attribute__ ((unused)),
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

static const char *
grub_efi_net_var_get_mac (struct grub_env_var *var __attribute__ ((unused)),
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

static void
grub_efi_net_export_interface_vars (void)
{
  struct grub_efi_net_device *dev;

  for (dev = net_devices; dev; dev = dev->next)
    {
      grub_efi_net_interface_t *inf;

      for (inf = dev->net_interfaces; inf; inf = inf->next)
	{
	  char *var;

	  var = grub_xasprintf ("net_%s_ip", inf->name);
	  grub_register_variable_hook (var, grub_efi_net_var_get_address, 0);
	  grub_env_export (var);
	  grub_free (var);
	  var = grub_xasprintf ("net_%s_mac", inf->name);
	  grub_register_variable_hook (var, grub_efi_net_var_get_address, 0);
	  grub_env_export (var);
	  grub_free (var);
	}
    }
}

static void
grub_efi_net_unset_interface_vars (void)
{
  struct grub_efi_net_device *dev;

  for (dev = net_devices; dev; dev = dev->next)
    {
      grub_efi_net_interface_t *inf;

      for (inf = dev->net_interfaces; inf; inf = inf->next)
	{
	  char *var;

	  var = grub_xasprintf ("net_%s_ip", inf->name);
	  grub_register_variable_hook (var, 0, 0);
	  grub_env_unset (var);
	  grub_free (var);
	  var = grub_xasprintf ("net_%s_mac", inf->name);
	  grub_register_variable_hook (var, 0, 0);
	  grub_env_unset (var);
	  grub_free (var);
	}
    }
}

grub_efi_net_interface_t *
grub_efi_net_create_interface (struct grub_efi_net_device *dev,
		const char *interface_name,
		grub_efi_net_ip_manual_address_t *net_ip,
		int has_subnet)
{
  grub_efi_net_interface_t *inf;

  for (inf = dev->net_interfaces; inf; inf = inf->next)
    {
      if (inf->prefer_ip6 == net_ip->is_ip6)
	break;
    }

  if (!inf)
    {
      inf = grub_malloc (sizeof(*inf));
      inf->name = grub_strdup (interface_name);
      inf->prefer_ip6 = net_ip->is_ip6;
      inf->dev = dev;
      inf->next = dev->net_interfaces;
      inf->ip_config = (net_ip->is_ip6) ? efi_net_ip6_config : efi_net_ip4_config ;
      dev->net_interfaces = inf;
    }
  else
    {
      grub_free (inf->name);
      inf->name = grub_strdup (interface_name);
    }

  if (!efi_net_interface_set_address (inf, net_ip, has_subnet))
    {
      grub_error (GRUB_ERR_BUG, N_("Set Address Failed"));
      return NULL;
    }

  return inf;
}

static void
grub_efi_net_config_real (grub_efi_handle_t hnd, char **device,
			  char **path)
{
  grub_efi_handle_t config_hnd;

  struct grub_efi_net_device *netdev;
  grub_efi_net_interface_t *inf;

  config_hnd = grub_efi_locate_device_path (&ip4_config_guid, grub_efi_get_device_path (hnd), NULL);

  if (!config_hnd)
    return;

  for (netdev = net_devices; netdev; netdev = netdev->next)
    if (netdev->handle == config_hnd)
      break;

  if (!netdev)
    return;

  if (!(inf = grub_efi_net_config_from_handle (hnd, netdev, device, path)))
    return;

  grub_env_set ("net_default_interface", inf->name);
  grub_efi_net_export_interface_vars ();
}

static grub_err_t
grub_efi_netfs_dir (grub_device_t device, const char *path __attribute__ ((unused)),
		 grub_fs_dir_hook_t hook __attribute__ ((unused)),
		 void *hook_data __attribute__ ((unused)))
{
  if (!device->net)
    return grub_error (GRUB_ERR_BUG, "invalid net device");
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_efi_netfs_open (struct grub_file *file_out __attribute__ ((unused)),
		  const char *name __attribute__ ((unused)))
{
  struct grub_file *file, *bufio;

  file = grub_malloc (sizeof (*file));
  if (!file)
    return grub_errno;

  grub_memcpy (file, file_out, sizeof (struct grub_file));
  file->device->net->name = grub_strdup (name);

  if (!file->device->net->name)
    {
      grub_free (file);
      return grub_errno;
    }

  efi_net_interface(open, file, name);
  grub_print_error ();

  bufio = grub_bufio_open (file, 32768);
  if (!bufio)
    {
      grub_free (file->device->net->name);
      grub_free (file);
      return grub_errno;
    }
  grub_memcpy (file_out, bufio, sizeof (struct grub_file));
  grub_free (bufio);

  return GRUB_ERR_NONE;
}

static grub_ssize_t
grub_efihttp_chunk_read (grub_file_t file, char *buf,
			grub_size_t len, grub_size_t chunk_size)
{
  char *chunk = grub_malloc (chunk_size);
  grub_size_t sum = 0;

  while (len)
    {
      grub_ssize_t rd;
      grub_size_t sz = (len > chunk_size) ? chunk_size : len;

      rd = efi_net_interface (read, file, chunk, sz);

      if (rd <= 0) {
	grub_free (chunk);
	return rd;
      }

      if (buf)
	{
	  grub_memcpy (buf, chunk, rd);
	  buf += rd;
	}
      sum += rd;
      len -= rd;
    }

  grub_free (chunk);
  return sum;
}

static grub_ssize_t
grub_efi_netfs_read (grub_file_t file __attribute__ ((unused)),
		  char *buf __attribute__ ((unused)), grub_size_t len __attribute__ ((unused)))
{
  if (file->offset > file->device->net->offset)
    {
      grub_efihttp_chunk_read (file, NULL, file->offset - file->device->net->offset, 10240);
    }
  else if (file->offset < file->device->net->offset)
    {
      efi_net_interface (close, file);
      efi_net_interface (open, file, file->device->net->name);
      if (file->offset)
	grub_efihttp_chunk_read (file, NULL, file->offset, 10240);
    }

  return efi_net_interface (read, file, buf, len);
}

static grub_err_t
grub_efi_netfs_close (grub_file_t file)
{
  efi_net_interface (close, file);
  return GRUB_ERR_NONE;
}

static grub_efi_handle_t
grub_efi_service_binding (grub_efi_handle_t dev, grub_efi_guid_t *service_binding_guid)
{
  grub_efi_service_binding_t *service;
  grub_efi_status_t status;
  grub_efi_handle_t child_dev = NULL;

  service = grub_efi_open_protocol (dev, service_binding_guid, GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
  if (!service)
    {
      grub_error (GRUB_ERR_IO, N_("couldn't open efi service binding protocol"));
      return NULL;
    }

  status = efi_call_2 (service->create_child, service, &child_dev);
  if (status != GRUB_EFI_SUCCESS)
    {
      grub_error (GRUB_ERR_IO, N_("Failed to create child device of http service"));
      return NULL;
    }

  return child_dev;
}

static grub_err_t
grub_efi_net_parse_address (const char *address,
    grub_efi_ip4_config2_manual_address_t *ip4,
    grub_efi_ip6_config_manual_address_t *ip6,
    int *is_ip6,
    int *has_cidr)
{
  const char *rest;

  if (grub_efi_string_to_ip4_address (address, &ip4->address, &rest))
    {
      *is_ip6 = 0;
      if (*rest == '/')
	{
	  grub_uint32_t subnet_mask_size;

	  subnet_mask_size = grub_strtoul (rest + 1, &rest, 0);

	  if (!grub_errno && subnet_mask_size <= 32 && *rest == 0)
	    {
	      grub_uint32_t subnet_mask;

	      subnet_mask = grub_cpu_to_be32 ((0xffffffffU << (32 - subnet_mask_size)));
	      grub_memcpy (ip4->subnet_mask, &subnet_mask, sizeof (ip4->subnet_mask));
	      if (has_cidr)
		*has_cidr = 1;
	      return GRUB_ERR_NONE;
	    }
	}
      else if (*rest == 0)
	{
	  grub_uint32_t subnet_mask = 0xffffffffU;
	  grub_memcpy (ip4->subnet_mask, &subnet_mask, sizeof (ip4->subnet_mask));
	  if (has_cidr)
	    *has_cidr = 0;
	  return GRUB_ERR_NONE;
	}
    }
  else if (grub_efi_string_to_ip6_address (address, &ip6->address, &rest))
    {
      *is_ip6 = 1;
      if (*rest == '/')
	{
	  grub_efi_uint8_t prefix_length;

	  prefix_length = grub_strtoul (rest + 1, &rest, 0);
	  if (!grub_errno && prefix_length <= 128 && *rest == 0)
	    {
	      ip6->prefix_length = prefix_length;
	      ip6->is_anycast = 0;
	      if (has_cidr)
		*has_cidr = 1;
	      return GRUB_ERR_NONE;
	    }
	}
      else if (*rest == 0)
	{
	  ip6->prefix_length = 128;
	  ip6->is_anycast = 0;
	  if (has_cidr)
	    *has_cidr = 0;
	  return GRUB_ERR_NONE;
	}
    }

  return grub_error (GRUB_ERR_NET_BAD_ADDRESS,
		   N_("unrecognised network address `%s'"),
		   address);
}

static grub_efi_net_interface_t *
match_route (const char *server)
{
  grub_err_t err;
  grub_efi_ip4_config2_manual_address_t ip4;
  grub_efi_ip6_config_manual_address_t ip6;
  grub_efi_net_interface_t *inf;
  int is_ip6 = 0;

  err = grub_efi_net_parse_address (server, &ip4, &ip6, &is_ip6, 0);

  if (err)
    {
      grub_print_error ();
      return NULL;
    }

  if (is_ip6)
    {
      struct grub_efi_net_device *dev;
      grub_efi_net_ip_address_t addr;

      grub_memcpy (addr.ip6, ip6.address, sizeof(ip6.address));

      for (dev = net_devices; dev; dev = dev->next)
	  if ((inf = efi_net_ip6_config->best_interface (dev, &addr)))
	    return inf;
    }
  else
    {
      struct grub_efi_net_device *dev;
      grub_efi_net_ip_address_t addr;

      grub_memcpy (addr.ip4, ip4.address, sizeof(ip4.address));

      for (dev = net_devices; dev; dev = dev->next)
	  if ((inf = efi_net_ip4_config->best_interface (dev, &addr)))
	    return inf;
    }

  return 0;
}

static void
grub_efi_net_add_pxebc_to_cards (void)
{
  grub_efi_uintn_t num_handles;
  grub_efi_handle_t *handles;
  grub_efi_handle_t *handle;

  handles = grub_efi_locate_handle (GRUB_EFI_BY_PROTOCOL, &pxe_io_guid,
				    0, &num_handles);
  if (!handles)
    return;

  for (handle = handles; num_handles--; handle++)
    {
      grub_efi_device_path_t *dp, *ddp, *ldp;
      grub_efi_pxe_t *pxe;
      struct grub_efi_net_device *d;
      int is_ip6 = 0;

      dp = grub_efi_get_device_path (*handle);
      if (!dp)
	continue;

      ddp = grub_efi_duplicate_device_path (dp);
      ldp = grub_efi_find_last_device_path (ddp);

      if (ldp->type == GRUB_EFI_MESSAGING_DEVICE_PATH_TYPE
	  && ldp->subtype == GRUB_EFI_IPV4_DEVICE_PATH_SUBTYPE)
	{
	  ldp->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
	  ldp->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
	  ldp->length = sizeof (*ldp);
	}
      else if (ldp->type == GRUB_EFI_MESSAGING_DEVICE_PATH_TYPE
	  && ldp->subtype == GRUB_EFI_IPV6_DEVICE_PATH_SUBTYPE)
	{
	  is_ip6 = 1;
	  ldp->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
	  ldp->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
	  ldp->length = sizeof (*ldp);
	}

      for (d = net_devices; d; d = d->next)
	if (grub_efi_compare_device_paths (ddp, grub_efi_get_device_path (d->handle)) == 0)
	  break;

      if (!d)
	{
	  grub_free (ddp);
	  continue;
	}

      pxe = grub_efi_open_protocol (*handle, &pxe_io_guid,
				GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);

      if (!pxe)
	{
	  grub_free (ddp);
	  continue;
	}

      if (is_ip6)
	{
	  d->ip6_pxe_handle = *handle;
	  d->ip6_pxe = pxe;
	}
      else
	{
	  d->ip4_pxe_handle = *handle;
	  d->ip4_pxe = pxe;
	}

      grub_free (ddp);
    }

  grub_free (handles);
}

static void
set_ip_policy_to_static (void)
{
  struct grub_efi_net_device *dev;

  for (dev = net_devices; dev; dev = dev->next)
    {
      grub_efi_ip4_config2_policy_t ip4_policy = GRUB_EFI_IP4_CONFIG2_POLICY_STATIC;

      if (efi_call_4 (dev->ip4_config->set_data, dev->ip4_config,
		    GRUB_EFI_IP4_CONFIG2_DATA_TYPE_POLICY,
		    sizeof (ip4_policy), &ip4_policy) != GRUB_EFI_SUCCESS)
	grub_dprintf ("efinetfs", "could not set GRUB_EFI_IP4_CONFIG2_POLICY_STATIC on dev `%s'", dev->card_name);

      if (dev->ip6_config)
	{
	  grub_efi_ip6_config_policy_t ip6_policy = GRUB_EFI_IP6_CONFIG_POLICY_MANUAL;

	  if (efi_call_4 (dev->ip6_config->set_data, dev->ip6_config,
		    GRUB_EFI_IP6_CONFIG_DATA_TYPE_POLICY,
		    sizeof (ip6_policy), &ip6_policy) != GRUB_EFI_SUCCESS)
	    grub_dprintf ("efinetfs", "could not set GRUB_EFI_IP6_CONFIG_POLICY_MANUAL on dev `%s'", dev->card_name);
	}
    }
}

/* FIXME: Do not fail if the card did not support any of the protocol (Eg http) */
static void
grub_efi_net_find_cards (void)
{
  grub_efi_uintn_t num_handles;
  grub_efi_handle_t *handles;
  grub_efi_handle_t *handle;
  int id;

  handles = grub_efi_locate_handle (GRUB_EFI_BY_PROTOCOL, &ip4_config_guid,
				    0, &num_handles);
  if (!handles)
    return;

  for (id = 0, handle = handles; num_handles--; handle++, id++)
    {
      grub_efi_device_path_t *dp;
      grub_efi_ip4_config2_protocol_t *ip4_config;
      grub_efi_ip6_config_protocol_t *ip6_config;
      grub_efi_handle_t http_handle;
      grub_efi_http_t *http;
      grub_efi_handle_t dhcp4_handle;
      grub_efi_dhcp4_protocol_t *dhcp4;
      grub_efi_handle_t dhcp6_handle;
      grub_efi_dhcp6_protocol_t *dhcp6;

      struct grub_efi_net_device *d;

      dp = grub_efi_get_device_path (*handle);
      if (!dp)
	continue;

      ip4_config = grub_efi_open_protocol (*handle, &ip4_config_guid,
				    GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
      if (!ip4_config)
	continue;

      ip6_config = grub_efi_open_protocol (*handle, &ip6_config_guid,
				    GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);

      http_handle = grub_efi_service_binding (*handle, &http_service_binding_guid);
      grub_errno = GRUB_ERR_NONE;
      http = (http_handle)
	? grub_efi_open_protocol (http_handle, &http_guid, GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL)
	: NULL;

      dhcp4_handle = grub_efi_service_binding (*handle, &dhcp4_service_binding_guid);
      grub_errno = GRUB_ERR_NONE;
      dhcp4 = (dhcp4_handle)
	? grub_efi_open_protocol (dhcp4_handle, &dhcp4_guid, GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL)
	: NULL;


      dhcp6_handle = grub_efi_service_binding (*handle, &dhcp6_service_binding_guid);
      grub_errno = GRUB_ERR_NONE;
      dhcp6 = (dhcp6_handle)
	? grub_efi_open_protocol (dhcp6_handle, &dhcp6_guid, GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL)
	: NULL;

      d = grub_malloc (sizeof (*d));
      if (!d)
	{
	  grub_free (handles);
	  while (net_devices)
	    {
	      d = net_devices->next;
	      grub_free (net_devices);
	      net_devices = d;
	    }
	  return;
	}
      d->handle = *handle;
      d->ip4_config = ip4_config;
      d->ip6_config = ip6_config;
      d->http_handle = http_handle;
      d->http = http;
      d->dhcp4_handle = dhcp4_handle;
      d->dhcp4 = dhcp4;
      d->dhcp6_handle = dhcp6_handle;
      d->dhcp6 = dhcp6;
      d->next = net_devices;
      d->card_name = grub_xasprintf ("efinet%d", id);
      d->net_interfaces = NULL;
      net_devices = d;
    }

  grub_efi_net_add_pxebc_to_cards ();
  grub_free (handles);
  set_ip_policy_to_static ();
}

static void
listroutes_ip4 (struct grub_efi_net_device *netdev)
{
  char **routes;

  routes = NULL;

  if ((routes = efi_net_ip4_config->get_route_table (netdev)))
    {
      char **r;

      for (r = routes; *r; ++r)
	grub_printf ("%s\n", *r);
    }

  if (routes)
    {
      char **r;

      for (r = routes; *r; ++r)
	grub_free (*r);
      grub_free (routes);
    }
}

static void
listroutes_ip6 (struct grub_efi_net_device *netdev)
{
  char **routes;

  routes = NULL;

  if ((routes = efi_net_ip6_config->get_route_table (netdev)))
    {
      char **r;

      for (r = routes; *r; ++r)
	grub_printf ("%s\n", *r);
    }

  if (routes)
    {
      char **r;

      for (r = routes; *r; ++r)
	grub_free (*r);
      grub_free (routes);
    }
}

static grub_err_t
grub_cmd_efi_listroutes (struct grub_command *cmd __attribute__ ((unused)),
		     int argc __attribute__ ((unused)),
		     char **args __attribute__ ((unused)))
{
  struct grub_efi_net_device *netdev;

  for (netdev = net_devices; netdev; netdev = netdev->next)
    {
      listroutes_ip4 (netdev);
      listroutes_ip6 (netdev);
    }

  return GRUB_ERR_NONE;
}
static grub_err_t
grub_cmd_efi_listcards (struct grub_command *cmd __attribute__ ((unused)),
		    int argc __attribute__ ((unused)),
		    char **args __attribute__ ((unused)))
{
  struct grub_efi_net_device *dev;

  for (dev = net_devices; dev; dev = dev->next)
    {
      char *hw_addr;

      hw_addr = efi_net_ip4_config->get_hw_address (dev);

      if (hw_addr)
	{
	  grub_printf ("%s %s\n", dev->card_name, hw_addr);
	  grub_free (hw_addr);
	}
    }

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_efi_listaddrs (struct grub_command *cmd __attribute__ ((unused)),
		    int argc __attribute__ ((unused)),
		    char **args __attribute__ ((unused)))
{
  struct grub_efi_net_device *dev;
  grub_efi_net_interface_t *inf;

  for (dev = net_devices; dev; dev = dev->next)
    for (inf = dev->net_interfaces; inf; inf = inf->next)
      {
	char *hw_addr = NULL;
	char *addr = NULL;

	if ((hw_addr = efi_net_interface_get_hw_address (inf))
	    && (addr = efi_net_interface_get_address (inf)))
	  grub_printf ("%s %s %s\n", inf->name, hw_addr, addr);

	if (hw_addr)
	  grub_free (hw_addr);
	if (addr)
	  grub_free (addr);
      }

  return GRUB_ERR_NONE;
}

/* FIXME: support MAC specifying.  */
static grub_err_t
grub_cmd_efi_addaddr (struct grub_command *cmd __attribute__ ((unused)),
                  int argc, char **args)
{
  struct grub_efi_net_device *dev;
  grub_err_t err;
  grub_efi_ip4_config2_manual_address_t ip4;
  grub_efi_ip6_config_manual_address_t ip6;
  grub_efi_net_ip_manual_address_t net_ip;
  int is_ip6 = 0;
  int cidr = 0;

  if (argc != 3)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("three arguments expected"));

  for (dev = net_devices; dev; dev = dev->next)
    {
      if (grub_strcmp (dev->card_name, args[1]) == 0)
	break;
    }

  if (!dev)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("card not found"));

  err = grub_efi_net_parse_address (args[2], &ip4, &ip6, &is_ip6, &cidr);

  if (err)
    return err;

  net_ip.is_ip6 = is_ip6;
  if (is_ip6)
    grub_memcpy (&net_ip.ip6, &ip6, sizeof(net_ip.ip6));
  else
    grub_memcpy (&net_ip.ip4, &ip4, sizeof(net_ip.ip4));

  if (!grub_efi_net_create_interface (dev,
		args[0],
		&net_ip,
		cidr))
    return grub_errno;

  return GRUB_ERR_NONE;
}

static struct grub_fs grub_efi_netfs;

static grub_net_t
grub_net_open_real (const char *name __attribute__ ((unused)))
{
  grub_size_t protnamelen;
  const char *protname, *server;
  grub_net_t ret;

  net_interface = NULL;

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
      server = default_server;
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
	  server = default_server;
	  protname = name;
	}
    }

  if (!server)
    {
      grub_error (GRUB_ERR_NET_BAD_ADDRESS,
		  N_("no server is specified"));
      return NULL;
    }

  /*FIXME: Use DNS translate name to address */
  net_interface = match_route (server);

  /*XXX: should we check device with default gateway ? */
  if (!net_interface && !(net_interface = net_default_interface))
    {
      grub_error (GRUB_ERR_UNKNOWN_DEVICE, N_("disk `%s' no route found"),
		  name);
      return NULL;
    }

  if ((protnamelen == (sizeof ("https") - 1)
	&& grub_memcmp ("https", protname, protnamelen) == 0))
    {
      net_interface->io = &io_http;
      net_interface->io_type = 1;
    }
  else if ((protnamelen == (sizeof ("http") - 1)
	&& grub_memcmp ("http", protname, protnamelen) == 0))
    {
      net_interface->io = &io_http;
      net_interface->io_type = 0;
    }
  else if (protnamelen == (sizeof ("tftp") - 1)
	&& grub_memcmp ("tftp", protname, protnamelen) == 0)
    {
      net_interface->io = &io_pxe;
      net_interface->io_type = 0;
    }
  else
    {
      grub_error (GRUB_ERR_UNKNOWN_DEVICE, N_("disk `%s' not found"),
		  name);
      return NULL;
    }

  /*XXX: Should we try to avoid doing excess "reconfigure" here ??? */
  efi_net_interface (configure);

  ret = grub_zalloc (sizeof (*ret));
  if (!ret)
    return NULL;

  ret->server = grub_strdup (server);
  if (!ret->server)
    {
      grub_free (ret);
      return NULL;
    }

  ret->fs = &grub_efi_netfs;
  return ret;
}
#if 0
static grub_command_t cmd_efi_lsaddr;
static grub_command_t cmd_efi_lscards;
static grub_command_t cmd_efi_lsroutes;
static grub_command_t cmd_efi_addaddr;
#endif

static struct grub_fs grub_efi_netfs =
  {
    .name = "efi netfs",
    .fs_dir = grub_efi_netfs_dir,
    .fs_open = grub_efi_netfs_open,
    .fs_read = grub_efi_netfs_read,
    .fs_close = grub_efi_netfs_close,
    .fs_label = NULL,
    .fs_uuid = NULL,
    .fs_mtime = NULL,
  };

int
grub_efi_net_boot_from_https (void)
{
  grub_efi_loaded_image_t *image = NULL;
  grub_efi_device_path_t *dp;

  image = grub_efi_get_loaded_image (grub_efi_image_handle);
  if (!image)
    return 0;

  dp = grub_efi_get_device_path (image->device_handle);

  while (1)
    {
      grub_efi_uint8_t type = GRUB_EFI_DEVICE_PATH_TYPE (dp);
      grub_efi_uint8_t subtype = GRUB_EFI_DEVICE_PATH_SUBTYPE (dp);
      grub_efi_uint16_t len = GRUB_EFI_DEVICE_PATH_LENGTH (dp);

      if ((type == GRUB_EFI_MESSAGING_DEVICE_PATH_TYPE)
	  && (subtype == GRUB_EFI_URI_DEVICE_PATH_SUBTYPE))
	{
	  grub_efi_uri_device_path_t *uri_dp = (grub_efi_uri_device_path_t *) dp;
	  grub_dprintf ("efinet", "url:%s\n", (const char *)uri_dp->uri);
	  return (grub_strncmp ((const char *)uri_dp->uri, "https://", sizeof ("https://") - 1) == 0 ||
	          grub_strncmp ((const char *)uri_dp->uri, "http://", sizeof ("http://") - 1) == 0);
	}

      if (GRUB_EFI_END_ENTIRE_DEVICE_PATH (dp))
        break;
      dp = (grub_efi_device_path_t *) ((char *) dp + len);
    }

  return 0;
}

int
grub_efi_net_boot_from_opa (void)
{
  grub_efi_loaded_image_t *image = NULL;
  grub_efi_device_path_t *dp;

  image = grub_efi_get_loaded_image (grub_efi_image_handle);
  if (!image)
    return 0;

  dp = grub_efi_get_device_path (image->device_handle);

  while (1)
    {
      grub_efi_uint8_t type = GRUB_EFI_DEVICE_PATH_TYPE (dp);
      grub_efi_uint8_t subtype = GRUB_EFI_DEVICE_PATH_SUBTYPE (dp);
      grub_efi_uint16_t len = GRUB_EFI_DEVICE_PATH_LENGTH (dp);

      if ((type == GRUB_EFI_MESSAGING_DEVICE_PATH_TYPE)
	  && (subtype == GRUB_EFI_MAC_ADDRESS_DEVICE_PATH_SUBTYPE))
	{
	  grub_efi_mac_address_device_path_t *mac_dp  = (grub_efi_mac_address_device_path_t *)dp;
	  return (mac_dp->if_type == 0xC7) ? 1 : 0;
	}

      if (GRUB_EFI_END_ENTIRE_DEVICE_PATH (dp))
        break;
      dp = (grub_efi_device_path_t *) ((char *) dp + len);
    }

  return 0;
}

static char *
grub_env_write_readonly (struct grub_env_var *var __attribute__ ((unused)),
			 const char *val __attribute__ ((unused)))
{
  return NULL;
}

grub_command_func_t grub_efi_net_list_routes = grub_cmd_efi_listroutes;
grub_command_func_t grub_efi_net_list_cards = grub_cmd_efi_listcards;
grub_command_func_t grub_efi_net_list_addrs = grub_cmd_efi_listaddrs;
grub_command_func_t grub_efi_net_add_addr = grub_cmd_efi_addaddr;

int
grub_efi_net_fs_init ()
{
  grub_efi_net_find_cards ();
  grub_efi_net_config = grub_efi_net_config_real;
  grub_net_open = grub_net_open_real;
  grub_register_variable_hook ("net_default_server", grub_efi_net_var_get_server,
			       grub_efi_net_var_set_server);
  grub_env_export ("net_default_server");
  grub_register_variable_hook ("pxe_default_server", grub_efi_net_var_get_server,
			       grub_efi_net_var_set_server);
  grub_env_export ("pxe_default_server");
  grub_register_variable_hook ("net_default_interface", 0,
			       grub_efi_net_var_set_interface);
  grub_env_export ("net_default_interface");
  grub_register_variable_hook ("net_default_ip", grub_efi_net_var_get_ip,
			       0);
  grub_env_export ("net_default_ip");
  grub_register_variable_hook ("net_default_mac", grub_efi_net_var_get_mac,
			       0);
  grub_env_export ("net_default_mac");

  grub_env_set ("grub_netfs_type", "efi");
  grub_register_variable_hook ("grub_netfs_type", 0, grub_env_write_readonly);
  grub_env_export ("grub_netfs_type");

  return 1;
}

void
grub_efi_net_fs_fini (void)
{
  grub_env_unset ("grub_netfs_type");
  grub_efi_net_unset_interface_vars ();
  grub_register_variable_hook ("net_default_server", 0, 0);
  grub_env_unset ("net_default_server");
  grub_register_variable_hook ("net_default_interface", 0, 0);
  grub_env_unset ("net_default_interface");
  grub_register_variable_hook ("pxe_default_server", 0, 0);
  grub_env_unset ("pxe_default_server");
  grub_register_variable_hook ("net_default_ip", 0, 0);
  grub_env_unset ("net_default_ip");
  grub_register_variable_hook ("net_default_mac", 0, 0);
  grub_env_unset ("net_default_mac");
  grub_efi_net_config = NULL;
  grub_net_open = NULL;
  grub_fs_unregister (&grub_efi_netfs);
}
