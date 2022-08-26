
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/misc.h>
#include <grub/net/efi.h>
#include <grub/charset.h>

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

static grub_efi_ip4_config2_manual_address_t *
efi_ip4_config_manual_address (grub_efi_ip4_config2_protocol_t *ip4_config)
{
  grub_efi_uintn_t sz;
  grub_efi_status_t status;
  grub_efi_ip4_config2_manual_address_t *manual_address;

  sz = sizeof (*manual_address);
  manual_address = grub_malloc (sz);
  if (!manual_address)
    return NULL;

  status = efi_call_4 (ip4_config->get_data, ip4_config,
		    GRUB_EFI_IP4_CONFIG2_DATA_TYPE_MANUAL_ADDRESS,
		    &sz, manual_address);

  if (status != GRUB_EFI_SUCCESS)
    {
      grub_free (manual_address);
      return NULL;
    }

  return manual_address;
}

static void
pxe_configure (struct grub_efi_net_device *dev, int prefer_ip6)
{
  grub_efi_pxe_t *pxe = (prefer_ip6) ? dev->ip6_pxe : dev->ip4_pxe;

  grub_efi_pxe_mode_t *mode = pxe->mode;

  if (!mode->started)
    {
      grub_efi_status_t status;
      status = efi_call_2 (pxe->start, pxe, prefer_ip6);

      if (status != GRUB_EFI_SUCCESS)
	  grub_printf ("Couldn't start PXE\n");
    }

#if 0
  grub_printf ("PXE STARTED: %u\n", mode->started);
  grub_printf ("PXE USING IPV6: %u\n", mode->using_ipv6);
#endif

  if (mode->using_ipv6)
    {
      grub_efi_ip6_config_manual_address_t *manual_address;
      manual_address = efi_ip6_config_manual_address (dev->ip6_config);

      if (manual_address &&
	  grub_memcmp (manual_address->address, mode->station_ip.v6, sizeof (manual_address->address)) != 0)
	{
	  grub_efi_status_t status;
	  grub_efi_pxe_ip_address_t station_ip;

	  grub_memcpy (station_ip.v6.addr, manual_address->address, sizeof (station_ip.v6.addr));
	  status = efi_call_3 (pxe->set_station_ip, pxe, &station_ip, NULL);

	  if (status != GRUB_EFI_SUCCESS)
	      grub_printf ("Couldn't set station ip\n");

	  grub_free (manual_address);
	}
    }
  else
    {
      grub_efi_ip4_config2_manual_address_t *manual_address;
      manual_address = efi_ip4_config_manual_address (dev->ip4_config);

      if (manual_address &&
	  grub_memcmp (manual_address->address, mode->station_ip.v4, sizeof (manual_address->address)) != 0)
	{
	  grub_efi_status_t status;
	  grub_efi_pxe_ip_address_t station_ip;
	  grub_efi_pxe_ip_address_t subnet_mask;

	  grub_memcpy (station_ip.v4.addr, manual_address->address, sizeof (station_ip.v4.addr));
	  grub_memcpy (subnet_mask.v4.addr, manual_address->subnet_mask, sizeof (subnet_mask.v4.addr));

	  status = efi_call_3 (pxe->set_station_ip, pxe, &station_ip, &subnet_mask);

	  if (status != GRUB_EFI_SUCCESS)
	      grub_printf ("Couldn't set station ip\n");

	  grub_free (manual_address);
	}
    }

#if 0
  if (mode->using_ipv6)
    {
      grub_printf ("PXE STATION IP: %02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x\n",
	mode->station_ip.v6.addr[0],
	mode->station_ip.v6.addr[1],
	mode->station_ip.v6.addr[2],
	mode->station_ip.v6.addr[3],
	mode->station_ip.v6.addr[4],
	mode->station_ip.v6.addr[5],
	mode->station_ip.v6.addr[6],
	mode->station_ip.v6.addr[7],
	mode->station_ip.v6.addr[8],
	mode->station_ip.v6.addr[9],
	mode->station_ip.v6.addr[10],
	mode->station_ip.v6.addr[11],
	mode->station_ip.v6.addr[12],
	mode->station_ip.v6.addr[13],
	mode->station_ip.v6.addr[14],
	mode->station_ip.v6.addr[15]);
    }
  else
    {
      grub_printf ("PXE STATION IP: %d.%d.%d.%d\n",
	mode->station_ip.v4.addr[0],
	mode->station_ip.v4.addr[1],
	mode->station_ip.v4.addr[2],
	mode->station_ip.v4.addr[3]);
      grub_printf ("PXE SUBNET MASK: %d.%d.%d.%d\n",
	mode->subnet_mask.v4.addr[0],
	mode->subnet_mask.v4.addr[1],
	mode->subnet_mask.v4.addr[2],
	mode->subnet_mask.v4.addr[3]);
    }
#endif

  /* TODO: Set The Station IP to the IP2 Config */
}

static int
parse_ip6 (const char *val, grub_uint64_t *ip, const char **rest)
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
  grub_memcpy (ip, newip, 16);
  if (bracketed && *ptr == ']') {
    ptr++;
  }
  if (rest)
    *rest = ptr;
  return 1;
}

static grub_err_t
pxe_open (struct grub_efi_net_device *dev,
	  int prefer_ip6,
	  grub_file_t file,
	  const char *filename,
	  int type __attribute__((unused)))
{
  int i;
  char *p;
  grub_efi_status_t status;
  grub_efi_pxe_ip_address_t server_ip;
  grub_efi_uint64_t file_size = 0;
  grub_efi_pxe_t *pxe = (prefer_ip6) ? dev->ip6_pxe : dev->ip4_pxe;

  if (pxe->mode->using_ipv6)
    {
      const char *rest;
      grub_uint64_t ip6[2];
      if (parse_ip6 (file->device->net->server, ip6, &rest) && *rest == 0)
	grub_memcpy (server_ip.v6.addr, ip6, sizeof (server_ip.v6.addr));
      /* TODO: ERROR Handling Here */
#if 0
      grub_printf ("PXE SERVER IP: %02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x\n",
	server_ip.v6.addr[0],
	server_ip.v6.addr[1],
	server_ip.v6.addr[2],
	server_ip.v6.addr[3],
	server_ip.v6.addr[4],
	server_ip.v6.addr[5],
	server_ip.v6.addr[6],
	server_ip.v6.addr[7],
	server_ip.v6.addr[8],
	server_ip.v6.addr[9],
	server_ip.v6.addr[10],
	server_ip.v6.addr[11],
	server_ip.v6.addr[12],
	server_ip.v6.addr[13],
	server_ip.v6.addr[14],
	server_ip.v6.addr[15]);
#endif
    }
  else
    {
      for (i = 0, p = file->device->net->server; i < 4; ++i, ++p)
	server_ip.v4.addr[i] = grub_strtoul (p, (const char **)&p, 10);
    }

  status = efi_call_10 (pxe->mtftp,
	    pxe,
	    GRUB_EFI_PXE_BASE_CODE_TFTP_GET_FILE_SIZE,
	    NULL,
	    0,
	    &file_size,
	    NULL,
	    &server_ip,
	    (grub_efi_char8_t *)filename,
	    NULL,
	    0);

  if (status != GRUB_EFI_SUCCESS)
    return grub_error (GRUB_ERR_IO, "Couldn't get file size");

  file->size = (grub_off_t)file_size;
  file->not_easily_seekable = 0;
  file->data = 0;
  file->device->net->offset = 0;

  return GRUB_ERR_NONE;
}

static grub_err_t
pxe_close (struct grub_efi_net_device *dev __attribute__((unused)),
	  int prefer_ip6 __attribute__((unused)),
	  grub_file_t file __attribute__((unused)))
{
  file->offset = 0;
  file->size = 0;
  file->device->net->offset = 0;

  if (file->data)
    {
      grub_free (file->data);
      file->data = NULL;
    }

  return GRUB_ERR_NONE;
}

static grub_ssize_t
pxe_read (struct grub_efi_net_device *dev,
	  int prefer_ip6,
	  grub_file_t file,
	  char *buf,
	  grub_size_t len)
{
  int i;
  char *p;
  grub_efi_status_t status;
  grub_efi_pxe_t *pxe = (prefer_ip6) ? dev->ip6_pxe : dev->ip4_pxe;
  grub_efi_uint64_t bufsz = len;
  grub_efi_pxe_ip_address_t server_ip;
  char *buf2 = NULL;

  if (file->data)
    {
      /* TODO: RANGE Check for offset and file size */
      grub_memcpy (buf, (char*)file->data + file->device->net->offset, len);
      file->device->net->offset += len;
      return len;
    }

  if (file->device->net->offset)
    {
      grub_error (GRUB_ERR_BUG, "No Offet Read Possible");
      grub_print_error ();
      return 0;
    }

  if (pxe->mode->using_ipv6)
    {
      const char *rest;
      grub_uint64_t ip6[2];
      if (parse_ip6 (file->device->net->server, ip6, &rest) && *rest == 0)
	grub_memcpy (server_ip.v6.addr, ip6, sizeof (server_ip.v6.addr));
      /* TODO: ERROR Handling Here */
    }
  else
    {
      for (i = 0, p = file->device->net->server; i < 4; ++i, ++p)
	server_ip.v4.addr[i] = grub_strtoul (p, (const char **)&p, 10);
    }

  status = efi_call_10 (pxe->mtftp,
	    pxe,
	    GRUB_EFI_PXE_BASE_CODE_TFTP_READ_FILE,
	    buf,
	    0,
	    &bufsz,
	    NULL,
	    &server_ip,
	    (grub_efi_char8_t *)file->device->net->name,
	    NULL,
	    0);

  if (bufsz != file->size)
    {
      grub_error (GRUB_ERR_BUG, "Short read should not happen here");
      grub_print_error ();
      return 0;
    }

  if (status == GRUB_EFI_BUFFER_TOO_SMALL)
    {

      buf2 = grub_malloc (bufsz);

      if (!buf2)
	{
	  grub_error (GRUB_ERR_OUT_OF_MEMORY, "ERROR OUT OF MEMORY");
	  grub_print_error ();
	  return 0;
	}

      status = efi_call_10 (pxe->mtftp,
		pxe,
		GRUB_EFI_PXE_BASE_CODE_TFTP_READ_FILE,
		buf2,
		0,
		&bufsz,
		NULL,
		&server_ip,
		(grub_efi_char8_t *)file->device->net->name,
		NULL,
		0);
    }

  if (status != GRUB_EFI_SUCCESS)
    {
      if (buf2)
	grub_free (buf2);

      grub_error (GRUB_ERR_IO, "Failed to Read File");
      grub_print_error ();
      return 0;
    }

  if (buf2)
    grub_memcpy (buf, buf2, len);

  file->device->net->offset = len;

  if (buf2)
    file->data = buf2;

  return len;
}

struct grub_efi_net_io io_pxe =
  {
    .configure = pxe_configure,
    .open = pxe_open,
    .read = pxe_read,
    .close = pxe_close
  };

