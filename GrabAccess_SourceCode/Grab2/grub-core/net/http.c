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
#include <grub/net/tcp.h>
#include <grub/net/ip.h>
#include <grub/net/ethernet.h>
#include <grub/net/netbuff.h>
#include <grub/net.h>
#include <grub/mm.h>
#include <grub/dl.h>
#include <grub/file.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

enum
  {
    HTTP_PORT = 80,
    HTTP_MAX_CHUNK_SIZE = 0x80000000
  };


typedef struct http_data
{
  char *current_line;
  grub_size_t current_line_len;
  int headers_recv;
  int first_line_recv;
  int size_recv;
  grub_net_tcp_socket_t sock;
  char *filename;
  grub_err_t err;
  char *errmsg;
  int chunked;
  grub_size_t chunk_rem;
  int in_chunk_len;
} *http_data_t;

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
parse_line (grub_file_t file, http_data_t data, char *ptr, grub_size_t len)
{
  char *end = ptr + len;
  while (end > ptr && *(end - 1) == '\r')
    end--;
  *end = 0;
  /* Trailing CRLF.  */
  if (data->in_chunk_len == 1)
    {
      data->in_chunk_len = 2;
      return GRUB_ERR_NONE;
    }
  if (data->in_chunk_len == 2)
    {
      data->chunk_rem = grub_strtoul (ptr, 0, 16);
      if (data->chunk_rem > HTTP_MAX_CHUNK_SIZE)
	  return GRUB_ERR_NET_PACKET_TOO_BIG;
      grub_errno = GRUB_ERR_NONE;
      if (data->chunk_rem == 0)
	{
	  file->device->net->eof = 1;
	  file->device->net->stall = 1;
	  if (file->size == GRUB_FILE_SIZE_UNKNOWN)
	    file->size = have_ahead (file);
	}
      data->in_chunk_len = 0;
      return GRUB_ERR_NONE;
    }
  if (ptr == end)
    {
      data->headers_recv = 1;
      if (data->chunked)
	data->in_chunk_len = 2;
      return GRUB_ERR_NONE;
    }

  if (!data->first_line_recv)
    {
      int code;
      if (grub_memcmp (ptr, "HTTP/1.1 ", sizeof ("HTTP/1.1 ") - 1) != 0)
	{
	  data->errmsg = grub_strdup (_("unsupported HTTP response"));
	  data->first_line_recv = 1;
	  return GRUB_ERR_NONE;
	}
      ptr += sizeof ("HTTP/1.1 ") - 1;
      code = grub_strtoul (ptr, (const char **)&ptr, 10);
      if (grub_errno)
	return grub_errno;
      switch (code)
	{
	case 200:
	case 206:
	  break;
	case 404:
	  data->err = GRUB_ERR_FILE_NOT_FOUND;
	  data->errmsg = grub_xasprintf (_("file `%s' not found"), data->filename);
	  return GRUB_ERR_NONE;
	default:
	  data->err = GRUB_ERR_NET_UNKNOWN_ERROR;
	  /* TRANSLATORS: GRUB HTTP code is pretty young. So even perfectly
	     valid answers like 403 will trigger this very generic message.  */
	  data->errmsg = grub_xasprintf (_("unsupported HTTP error %d: %s"),
					 code, ptr);
	  return GRUB_ERR_NONE;
	}
      data->first_line_recv = 1;
      return GRUB_ERR_NONE;
    }
  if (grub_memcmp (ptr, "Content-Length: ", sizeof ("Content-Length: ") - 1)
      == 0 && !data->size_recv)
    {
      ptr += sizeof ("Content-Length: ") - 1;
      file->size = grub_strtoull (ptr, (const char **)&ptr, 10);
      data->size_recv = 1;
      return GRUB_ERR_NONE;
    }
  if (grub_memcmp (ptr, "Transfer-Encoding: chunked",
		   sizeof ("Transfer-Encoding: chunked") - 1) == 0)
    {
      data->chunked = 1;
      return GRUB_ERR_NONE;
    }

  return GRUB_ERR_NONE;  
}

static void
http_err (grub_net_tcp_socket_t sock __attribute__ ((unused)),
	  void *f)
{
  grub_file_t file = f;
  http_data_t data = file->data;

  if (data->sock)
    grub_net_tcp_close (data->sock, GRUB_NET_TCP_ABORT);
  data->sock = 0;
  if (data->current_line)
    grub_free (data->current_line);
  data->current_line = 0;
  file->device->net->eof = 1;
  file->device->net->stall = 1;
  if (file->size == GRUB_FILE_SIZE_UNKNOWN)
    file->size = have_ahead (file);
}

static grub_err_t
http_receive (grub_net_tcp_socket_t sock __attribute__ ((unused)),
	      struct grub_net_buff *nb,
	      void *f)
{
  grub_file_t file = f;
  http_data_t data = file->data;
  grub_err_t err;

  if (!data->sock)
    {
      grub_netbuff_free (nb);
      return GRUB_ERR_NONE;
    }

  while (1)
    {
      char *ptr = (char *) nb->data;
      if ((!data->headers_recv || data->in_chunk_len) && data->current_line)
	{
	  int have_line = 1;
	  char *t;
	  ptr = grub_memchr (nb->data, '\n', nb->tail - nb->data);
	  if (ptr)
	    ptr++;
	  else
	    {
	      have_line = 0;
	      ptr = (char *) nb->tail;
	    }
	  t = grub_realloc (data->current_line,
			    data->current_line_len + (ptr - (char *) nb->data));
	  if (!t)
	    {
	      grub_netbuff_free (nb);
	      grub_net_tcp_close (data->sock, GRUB_NET_TCP_ABORT);
	      return grub_errno;
	    }
	      
	  data->current_line = t;
	  grub_memcpy (data->current_line + data->current_line_len,
		       nb->data, ptr - (char *) nb->data);
	  data->current_line_len += ptr - (char *) nb->data;
	  if (!have_line)
	    {
	      grub_netbuff_free (nb);
	      return GRUB_ERR_NONE;
	    }
	  err = parse_line (file, data, data->current_line,
			    data->current_line_len);
	  grub_free (data->current_line);
	  data->current_line = 0;
	  data->current_line_len = 0;
	  if (err)
	    {
	      grub_net_tcp_close (data->sock, GRUB_NET_TCP_ABORT);
	      grub_netbuff_free (nb);
	      return err;
	    }
	}

      while (ptr < (char *) nb->tail && (!data->headers_recv
					 || data->in_chunk_len))
	{
	  char *ptr2;
	  ptr2 = grub_memchr (ptr, '\n', (char *) nb->tail - ptr);
	  if (!ptr2)
	    {
	      data->current_line = grub_malloc ((char *) nb->tail - ptr);
	      if (!data->current_line)
		{
		  grub_netbuff_free (nb);
		  grub_net_tcp_close (data->sock, GRUB_NET_TCP_ABORT);
		  return grub_errno;
		}
	      data->current_line_len = (char *) nb->tail - ptr;
	      grub_memcpy (data->current_line, ptr, data->current_line_len);
	      grub_netbuff_free (nb);
	      return GRUB_ERR_NONE;
	    }
	  err = parse_line (file, data, ptr, ptr2 - ptr);
	  if (err)
	    {
	      grub_net_tcp_close (data->sock, GRUB_NET_TCP_ABORT);
	      grub_netbuff_free (nb);
	      return err;
	    }
	  ptr = ptr2 + 1;
	}

      if (((char *) nb->tail - ptr) <= 0)
	{
	  grub_netbuff_free (nb);
	  return GRUB_ERR_NONE;
	} 
      err = grub_netbuff_pull (nb, ptr - (char *) nb->data);
      if (err)
	{
	  grub_net_tcp_close (data->sock, GRUB_NET_TCP_ABORT);
	  grub_netbuff_free (nb);
	  return err;
	}
      if (!(data->chunked && (grub_ssize_t) data->chunk_rem
	    < nb->tail - nb->data))
	{
	  grub_net_put_packet (&file->device->net->packs, nb);
	  if (file->device->net->packs.count >= 20)
	    file->device->net->stall = 1;

	  if (file->device->net->packs.count >= 100)
	    grub_net_tcp_stall (data->sock);

	  if (data->chunked)
	    data->chunk_rem -= nb->tail - nb->data;
	  return GRUB_ERR_NONE;
	}
      if (data->chunk_rem)
	{
	  struct grub_net_buff *nb2;
	  nb2 = grub_netbuff_alloc (data->chunk_rem);
	  if (!nb2)
	    return grub_errno;
	  err = grub_netbuff_put (nb2, data->chunk_rem);
	  if (err)
	    return grub_errno;
	  grub_memcpy (nb2->data, nb->data, data->chunk_rem);
	  if (file->device->net->packs.count >= 20)
	    {
	      file->device->net->stall = 1;
	      grub_net_tcp_stall (data->sock);
	    }

	  grub_net_put_packet (&file->device->net->packs, nb2);
	  grub_netbuff_pull (nb, data->chunk_rem);
	}
      data->in_chunk_len = 1;
    }
}

static grub_err_t
http_establish (struct grub_file *file, grub_off_t offset, int initial)
{
  http_data_t data = file->data;
  grub_uint8_t *ptr;
  int i;
  struct grub_net_buff *nb;
  grub_err_t err;
  char* server = file->device->net->server;
  int port = file->device->net->port;

  nb = grub_netbuff_alloc (GRUB_NET_TCP_RESERVE_SIZE
			   + sizeof ("GET ") - 1
			   + grub_strlen (data->filename)
			   + sizeof (" HTTP/1.1\r\nHost: ") - 1
			   + grub_strlen (server) + sizeof (":XXXXXXXXXX")
			   + sizeof ("\r\nUser-Agent: " PACKAGE_STRING
				     "\r\n") - 1
			   + sizeof ("Range: bytes=XXXXXXXXXXXXXXXXXXXX"
				     "-\r\n\r\n"));
  if (!nb)
    return grub_errno;

  grub_netbuff_reserve (nb, GRUB_NET_TCP_RESERVE_SIZE);
  ptr = nb->tail;
  err = grub_netbuff_put (nb, sizeof ("GET ") - 1);
  if (err)
    {
      grub_netbuff_free (nb);
      return err;
    }
  grub_memcpy (ptr, "GET ", sizeof ("GET ") - 1);

  ptr = nb->tail;

  err = grub_netbuff_put (nb, grub_strlen (data->filename));
  if (err)
    {
      grub_netbuff_free (nb);
      return err;
    }
  grub_memcpy (ptr, data->filename, grub_strlen (data->filename));

  ptr = nb->tail;
  err = grub_netbuff_put (nb, sizeof (" HTTP/1.1\r\nHost: ") - 1);
  if (err)
    {
      grub_netbuff_free (nb);
      return err;
    }
  grub_memcpy (ptr, " HTTP/1.1\r\nHost: ",
	       sizeof (" HTTP/1.1\r\nHost: ") - 1);

  ptr = nb->tail;
  err = grub_netbuff_put (nb, grub_strlen (server));
  if (err)
    {
      grub_netbuff_free (nb);
      return err;
    }
  grub_memcpy (ptr, file->device->net->server,
	       grub_strlen (file->device->net->server));

  if (port)
    {
      ptr = nb->tail;
      grub_snprintf ((char *) ptr,
	  sizeof (":XXXXXXXXXX"),
	  ":%d",
	  port);
    }

  ptr = nb->tail;
  err = grub_netbuff_put (nb, 
			  sizeof ("\r\nUser-Agent: " PACKAGE_STRING "\r\n")
			  - 1);
  if (err)
    {
      grub_netbuff_free (nb);
      return err;
    }
  grub_memcpy (ptr, "\r\nUser-Agent: " PACKAGE_STRING "\r\n",
	       sizeof ("\r\nUser-Agent: " PACKAGE_STRING "\r\n") - 1);
  if (!initial)
    {
      ptr = nb->tail;
      grub_snprintf ((char *) ptr,
		     sizeof ("Range: bytes=XXXXXXXXXXXXXXXXXXXX-"
			     "\r\n"),
		     "Range: bytes=%" PRIuGRUB_UINT64_T "-\r\n",
		     offset);
      grub_netbuff_put (nb, grub_strlen ((char *) ptr));
    }
  ptr = nb->tail;
  grub_netbuff_put (nb, 2);
  grub_memcpy (ptr, "\r\n", 2);

  grub_dprintf ("http", "opening path %s on host %s TCP port %d\n",
		data->filename, server, port ? port : HTTP_PORT);
  data->sock = grub_net_tcp_open (server,
				  port ? port : HTTP_PORT, http_receive,
				  http_err, NULL,
				  file);
  if (!data->sock)
    {
      grub_netbuff_free (nb);
      return grub_errno;
    }

  //  grub_net_poll_cards (5000);

  err = grub_net_send_tcp_packet (data->sock, nb, 1);
  if (err)
    {
      grub_net_tcp_close (data->sock, GRUB_NET_TCP_ABORT);
      return err;
    }

  for (i = 0; !data->headers_recv && i < 100; i++)
    {
      grub_net_tcp_retransmit ();
      grub_net_poll_cards (300, &data->headers_recv);
    }

  if (!data->headers_recv)
    {
      grub_net_tcp_close (data->sock, GRUB_NET_TCP_ABORT);
      if (data->err)
	{
	  char *str = data->errmsg;
	  err = grub_error (data->err, "%s", str);
	  grub_free (str);
	  data->errmsg = 0;
	  return data->err;
	}
      return grub_error (GRUB_ERR_TIMEOUT, N_("time out opening `%s'"), data->filename);
    }
  return GRUB_ERR_NONE;
}

static grub_err_t
http_seek (struct grub_file *file, grub_off_t off)
{
  struct http_data *old_data, *data;
  grub_err_t err;
  old_data = file->data;
  /* FIXME: Reuse socket?  */
  if (old_data->sock)
    grub_net_tcp_close (old_data->sock, GRUB_NET_TCP_ABORT);
  old_data->sock = 0;

  while (file->device->net->packs.first)
    {
      grub_netbuff_free (file->device->net->packs.first->nb);
      grub_net_remove_packet (file->device->net->packs.first);
    }

  file->device->net->stall = 0;
  file->device->net->eof = 0;
  file->device->net->offset = off;

  data = grub_zalloc (sizeof (*data));
  if (!data)
    return grub_errno;

  data->size_recv = 1;
  data->filename = old_data->filename;
  if (!data->filename)
    {
      grub_free (data);
      file->data = 0;
      return grub_errno;
    }
  grub_free (old_data);

  file->data = data;
  err = http_establish (file, off, 0);
  if (err)
    {
      grub_free (data->filename);
      grub_free (data);
      file->data = 0;
      return err;
    }
  return GRUB_ERR_NONE;
}

static grub_err_t
http_open (struct grub_file *file, const char *filename)
{
  grub_err_t err;
  struct http_data *data;

  data = grub_zalloc (sizeof (*data));
  if (!data)
    return grub_errno;
  file->size = GRUB_FILE_SIZE_UNKNOWN;

  data->filename = grub_strdup (filename);
  if (!data->filename)
    {
      grub_free (data);
      return grub_errno;
    }

  file->not_easily_seekable = 0;
  file->data = data;

  err = http_establish (file, 0, 1);
  if (err)
    {
      grub_free (data->filename);
      grub_free (data);
      return err;
    }

  return GRUB_ERR_NONE;
}

static grub_err_t
http_close (struct grub_file *file)
{
  http_data_t data = file->data;

  if (!data)
    return GRUB_ERR_NONE;

  if (data->sock)
    grub_net_tcp_close (data->sock, GRUB_NET_TCP_ABORT);
  if (data->current_line)
    grub_free (data->current_line);
  grub_free (data->filename);
  grub_free (data);
  return GRUB_ERR_NONE;
}

static grub_err_t
http_packets_pulled (struct grub_file *file)
{
  http_data_t data = file->data;

  if (file->device->net->packs.count >= 20)
    return 0;

  if (!file->device->net->eof)
    file->device->net->stall = 0;
  if (data && data->sock)
    grub_net_tcp_unstall (data->sock);
  return 0;
}

static struct grub_net_app_protocol grub_http_protocol = 
  {
    .name = "http",
    .open = http_open,
    .close = http_close,
    .seek = http_seek,
    .packets_pulled = http_packets_pulled
  };

GRUB_MOD_INIT (http)
{
  grub_net_app_level_register (&grub_http_protocol);
}

GRUB_MOD_FINI (http)
{
  grub_net_app_level_unregister (&grub_http_protocol);
}
