
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/misc.h>
#include <grub/net/efi.h>
#include <grub/charset.h>
#include <grub/env.h>

static void
http_configure (struct grub_efi_net_device *dev, int prefer_ip6)
{
  grub_efi_http_config_data_t http_config;
  grub_efi_httpv4_access_point_t httpv4_node;
  grub_efi_httpv6_access_point_t httpv6_node;
  grub_efi_status_t status;

  grub_efi_http_t *http = dev->http;

  grub_memset (&http_config, 0, sizeof(http_config));
  http_config.http_version = GRUB_EFI_HTTPVERSION11;
  http_config.timeout_millisec = 5000;

  if (prefer_ip6)
    {
      grub_efi_uintn_t sz;
      grub_efi_ip6_config_manual_address_t manual_address;

      http_config.local_address_is_ipv6 = 1;
      sz = sizeof (manual_address);
      status = efi_call_4 (dev->ip6_config->get_data, dev->ip6_config,
			GRUB_EFI_IP6_CONFIG_DATA_TYPE_MANUAL_ADDRESS,
			&sz, &manual_address);

      if (status == GRUB_EFI_NOT_FOUND)
	{
	  grub_printf ("The MANUAL ADDRESS is not found\n");
	}

      /* FIXME: The manual interface would return BUFFER TOO SMALL !!! */
      if (status != GRUB_EFI_SUCCESS)
	{
	  grub_printf ("??? %d\n",(int) status);
	  return;
	}

      grub_memcpy (httpv6_node.local_address, manual_address.address, sizeof (httpv6_node.local_address));
      httpv6_node.local_port = 0;
      http_config.access_point.ipv6_node = &httpv6_node;
    }
  else
    {
      http_config.local_address_is_ipv6 = 0;
      grub_memset (&httpv4_node, 0, sizeof(httpv4_node));
      httpv4_node.use_default_address = 1;

      /* Use random port here */
      /* See TcpBind() in edk2/NetworkPkg/TcpDxe/TcpDispatcher.c */
      httpv4_node.local_port = 0;
      http_config.access_point.ipv4_node = &httpv4_node;
    }

  status = efi_call_2 (http->configure, http, &http_config);

  if (status == GRUB_EFI_ALREADY_STARTED)
    {
      /* XXX: This hangs HTTPS boot */
#if 0
      if (efi_call_2 (http->configure, http, NULL) != GRUB_EFI_SUCCESS)
	{
	  grub_error (GRUB_ERR_IO, N_("couldn't reset http instance"));
	  grub_print_error ();
	  return;
	}
      status = efi_call_2 (http->configure, http, &http_config);
#endif
      return;
    }

  if (status != GRUB_EFI_SUCCESS)
    {
      grub_error (GRUB_ERR_IO, N_("couldn't configure http protocol, reason: %d"), (int)status);
      grub_print_error ();
      return ;
    }
}

static grub_efi_boolean_t request_callback_done;
static grub_efi_boolean_t response_callback_done;

static void
grub_efi_http_request_callback (grub_efi_event_t event __attribute__ ((unused)),
				void *context __attribute__ ((unused)))
{
  request_callback_done = 1;
}

static void
grub_efi_http_response_callback (grub_efi_event_t event __attribute__ ((unused)),
				void *context __attribute__ ((unused)))
{
  response_callback_done = 1;
}

static grub_err_t
efihttp_request (grub_efi_http_t *http, char *server, char *name, int use_https, int headeronly, grub_off_t *file_size)
{
  grub_efi_http_request_data_t request_data;
  grub_efi_http_message_t request_message;
  grub_efi_http_token_t request_token;
  grub_efi_http_response_data_t response_data;
  grub_efi_http_message_t response_message;
  grub_efi_http_token_t response_token;
  grub_efi_http_header_t request_headers[3];

  grub_efi_status_t status;
  grub_efi_boot_services_t *b = grub_efi_system_table->boot_services;
  char *url = NULL;

  request_headers[0].field_name = (grub_efi_char8_t *)"Host";
  request_headers[0].field_value = (grub_efi_char8_t *)server;
  request_headers[1].field_name = (grub_efi_char8_t *)"Accept";
  request_headers[1].field_value = (grub_efi_char8_t *)"*/*";
  request_headers[2].field_name = (grub_efi_char8_t *)"User-Agent";
  request_headers[2].field_value = (grub_efi_char8_t *)"UefiHttpBoot/1.0";

  {
    grub_efi_ipv6_address_t address;
    const char *rest;
    grub_efi_char16_t *ucs2_url;
    grub_size_t url_len, ucs2_url_len;
    const char *protocol = (use_https == 1) ? "https" : "http";

    if (grub_efi_string_to_ip6_address (server, &address, &rest) && *rest == 0)
      url = grub_xasprintf ("%s://[%s]%s", protocol, server, name);
    else
      url = grub_xasprintf ("%s://%s%s", protocol, server, name);

    if (!url)
      {
	return grub_errno;
      }

    url_len = grub_strlen (url);
    ucs2_url_len = url_len * GRUB_MAX_UTF16_PER_UTF8;
    ucs2_url = grub_malloc ((ucs2_url_len + 1) * sizeof (ucs2_url[0]));

    if (!ucs2_url)
      {
	grub_free (url);
	return grub_errno;
      }

    ucs2_url_len = grub_utf8_to_utf16 (ucs2_url, ucs2_url_len, (grub_uint8_t *)url, url_len, NULL); /* convert string format from ascii to usc2 */
    ucs2_url[ucs2_url_len] = 0;
    grub_free (url);
    request_data.url = ucs2_url;
  }

  request_data.method = (headeronly > 0) ? GRUB_EFI_HTTPMETHODHEAD : GRUB_EFI_HTTPMETHODGET;

  request_message.data.request = &request_data;
  request_message.header_count = 3;
  request_message.headers = request_headers;
  request_message.body_length = 0;
  request_message.body = NULL;

  /* request token */
  request_token.event = NULL;
  request_token.status = GRUB_EFI_NOT_READY;
  request_token.message = &request_message;

  request_callback_done = 0;
  status = efi_call_5 (b->create_event,
                       GRUB_EFI_EVT_NOTIFY_SIGNAL,
                       GRUB_EFI_TPL_CALLBACK,
                       grub_efi_http_request_callback,
                       NULL,
                       &request_token.event);

  if (status != GRUB_EFI_SUCCESS)
    {
      grub_free (request_data.url);
      return grub_error (GRUB_ERR_IO, "Fail to create an event");
    }

  status = efi_call_2 (http->request, http, &request_token);

  if (status != GRUB_EFI_SUCCESS)
    {
      efi_call_1 (b->close_event, request_token.event);
      grub_free (request_data.url);
      return grub_error (GRUB_ERR_IO, "Fail to send a request");
    }
  /* TODO: Add Timeout */
  while (!request_callback_done)
    efi_call_1(http->poll, http);

  response_data.status_code = GRUB_EFI_HTTP_STATUS_UNSUPPORTED_STATUS;
  response_message.data.response = &response_data;
  /* herader_count will be updated by the HTTP driver on response */
  response_message.header_count = 0;
  /* headers will be populated by the driver on response */
  response_message.headers = NULL;
  /* use zero BodyLength to only receive the response headers */
  response_message.body_length = 0;
  response_message.body = NULL;
  response_token.event = NULL;

  status = efi_call_5 (b->create_event,
              GRUB_EFI_EVT_NOTIFY_SIGNAL,
              GRUB_EFI_TPL_CALLBACK,
              grub_efi_http_response_callback,
              NULL,
              &response_token.event);

  if (status != GRUB_EFI_SUCCESS)
    {
      efi_call_1 (b->close_event, request_token.event);
      grub_free (request_data.url);
      return grub_error (GRUB_ERR_IO, "Fail to create an event");
    }

  response_token.status = GRUB_EFI_SUCCESS;
  response_token.message = &response_message;

  /* wait for HTTP response */
  response_callback_done = 0;
  status = efi_call_2 (http->response, http, &response_token);

  if (status != GRUB_EFI_SUCCESS)
    {
      efi_call_1 (b->close_event, response_token.event);
      efi_call_1 (b->close_event, request_token.event);
      grub_free (request_data.url);
      return grub_error (GRUB_ERR_IO, "Fail to receive a response! status=%d\n", (int)status);
    }

  /* TODO: Add Timeout */
  while (!response_callback_done)
    efi_call_1 (http->poll, http);

  if (response_message.data.response->status_code != GRUB_EFI_HTTP_STATUS_200_OK)
    {
      grub_efi_http_status_code_t status_code = response_message.data.response->status_code;

      if (response_message.headers)
	efi_call_1 (b->free_pool, response_message.headers);
      efi_call_1 (b->close_event, response_token.event);
      efi_call_1 (b->close_event, request_token.event);
      grub_free (request_data.url);
      if (status_code == GRUB_EFI_HTTP_STATUS_404_NOT_FOUND)
	{
	  return grub_error (GRUB_ERR_FILE_NOT_FOUND, _("file `%s' not found"), name);
	}
      else
	{
	  return grub_error (GRUB_ERR_NET_UNKNOWN_ERROR,
		  _("unsupported uefi http status code 0x%x"), status_code);
	}
    }

  if (file_size)
    {
      int i;
      /* parse the length of the file from the ContentLength header */
      for (*file_size = 0, i = 0; i < (int)response_message.header_count; ++i)
	{
	  if (!grub_strcmp((const char*)response_message.headers[i].field_name, "Content-Length"))
	    {
	      *file_size = grub_strtoul((const char*)response_message.headers[i].field_value, 0, 10);
	      break;
	    }
	}
    }

  if (response_message.headers)
    efi_call_1 (b->free_pool, response_message.headers);
  efi_call_1 (b->close_event, response_token.event);
  efi_call_1 (b->close_event, request_token.event);
  grub_free (request_data.url);

  return GRUB_ERR_NONE;
}

static grub_ssize_t
efihttp_read (struct grub_efi_net_device *dev,
		  char *buf,
		  grub_size_t len)
{
  grub_efi_http_message_t response_message;
  grub_efi_http_token_t response_token;

  grub_efi_status_t status;
  grub_size_t sum = 0;
  grub_efi_boot_services_t *b = grub_efi_system_table->boot_services;
  grub_efi_http_t *http = dev->http;

  if (!len)
    {
      grub_error (GRUB_ERR_BUG, "Invalid arguments to EFI HTTP Read");
      return -1;
    }

  efi_call_5 (b->create_event,
              GRUB_EFI_EVT_NOTIFY_SIGNAL,
              GRUB_EFI_TPL_CALLBACK,
              grub_efi_http_response_callback,
              NULL,
              &response_token.event);

  while (len)
    {
      response_message.data.response = NULL;
      response_message.header_count = 0;
      response_message.headers = NULL;
      response_message.body_length = len;
      response_message.body = buf;

      response_token.message = &response_message;
      response_token.status = GRUB_EFI_NOT_READY;

      response_callback_done = 0;

      status = efi_call_2 (http->response, http, &response_token);
      if (status != GRUB_EFI_SUCCESS)
	{
	  efi_call_1 (b->close_event, response_token.event);
	  grub_error (GRUB_ERR_IO, "Error! status=%d\n", (int)status);
	  return -1;
	}

      while (!response_callback_done)
	efi_call_1(http->poll, http);

      sum += response_message.body_length;
      buf += response_message.body_length;
      len -= response_message.body_length;
    }

  efi_call_1 (b->close_event, response_token.event);

  return sum;
}

static grub_err_t
grub_efihttp_open (struct grub_efi_net_device *dev,
		  int prefer_ip6 __attribute__ ((unused)),
		  grub_file_t file,
		  const char *filename __attribute__ ((unused)),
		  int type)
{
  grub_err_t err;
  grub_off_t size;
  char *buf;
  char *root_url;
  grub_efi_ipv6_address_t address;
  const char *rest;

  if (grub_efi_string_to_ip6_address (file->device->net->server, &address, &rest) && *rest == 0)
    root_url = grub_xasprintf ("%s://[%s]", type ? "https" : "http", file->device->net->server);
  else
    root_url = grub_xasprintf ("%s://%s", type ? "https" : "http", file->device->net->server);
  if (root_url)
    {
      grub_env_unset ("root_url");
      grub_env_set ("root_url", root_url);
      grub_free (root_url);
    }
  else
    {
      return grub_errno;
    }

  err = efihttp_request (dev->http, file->device->net->server, file->device->net->name, type, 1, 0);
  if (err != GRUB_ERR_NONE)
    return err;

  err = efihttp_request (dev->http, file->device->net->server, file->device->net->name, type, 0, &size);
  if (err != GRUB_ERR_NONE)
    return err;

  buf = grub_malloc (size);
  efihttp_read (dev, buf, size);

  file->size = size;
  file->data = buf;
  file->not_easily_seekable = 0;
  file->device->net->offset = 0;

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_efihttp_close (struct grub_efi_net_device *dev __attribute__ ((unused)),
		    int prefer_ip6 __attribute__ ((unused)),
		    grub_file_t file)
{
  if (file->data)
    grub_free (file->data);

  file->data = 0;
  file->offset = 0;
  file->size = 0;
  file->device->net->offset = 0;
  return GRUB_ERR_NONE;
}

static grub_ssize_t
grub_efihttp_read (struct grub_efi_net_device *dev __attribute__((unused)),
		  int prefer_ip6 __attribute__((unused)),
		  grub_file_t file,
		  char *buf,
		  grub_size_t len)
{
  grub_size_t r = len;

  if (!file->data || !buf || !len)
    return 0;

  if ((file->device->net->offset + len) > file->size)
    r = file->size - file->device->net->offset;

  if (r)
    {
      grub_memcpy (buf, (char *)file->data + file->device->net->offset, r);
      file->device->net->offset += r;
    }

  return r;
}

struct grub_efi_net_io io_http =
  {
    .configure = http_configure,
    .open = grub_efihttp_open,
    .read = grub_efihttp_read,
    .close = grub_efihttp_close
  };
