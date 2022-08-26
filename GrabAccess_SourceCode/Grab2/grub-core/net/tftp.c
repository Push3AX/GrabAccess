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
#include <grub/net/udp.h>
#include <grub/net/ip.h>
#include <grub/net/ethernet.h>
#include <grub/net/netbuff.h>
#include <grub/net.h>
#include <grub/mm.h>
#include <grub/dl.h>
#include <grub/file.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

/* IP port for the MTFTP server used for Intel's PXE */
enum
  {
    MTFTP_SERVER_PORT = 75,
    MTFTP_CLIENT_PORT = 76,
    /* IP port for the TFTP server */
    TFTP_SERVER_PORT = 69
  };

enum
  {
    TFTP_DEFAULTSIZE_PACKET = 512,
  };

enum
  {
    TFTP_CODE_EOF = 1,
    TFTP_CODE_MORE = 2,
    TFTP_CODE_ERROR = 3,
    TFTP_CODE_BOOT = 4,
    TFTP_CODE_CFG = 5
  };

enum
  {
    TFTP_RRQ = 1,
    TFTP_WRQ = 2,
    TFTP_DATA = 3,
    TFTP_ACK = 4,
    TFTP_ERROR = 5,
    TFTP_OACK = 6
  };

enum
  {
    TFTP_EUNDEF = 0,                   /* not defined */
    TFTP_ENOTFOUND = 1,                /* file not found */
    TFTP_EACCESS = 2,                  /* access violation */
    TFTP_ENOSPACE = 3,                 /* disk full or allocation exceeded */
    TFTP_EBADOP = 4,                   /* illegal TFTP operation */
    TFTP_EBADID = 5,                   /* unknown transfer ID */
    TFTP_EEXISTS = 6,                  /* file already exists */
    TFTP_ENOUSER = 7                  /* no such user */
  };

struct tftphdr {
  grub_uint16_t opcode;
  union {
    grub_int8_t rrq[TFTP_DEFAULTSIZE_PACKET];
    struct {
      grub_uint16_t block;
      grub_int8_t download[0];
    } data;
    struct {
      grub_uint16_t block;
    } ack;
    struct {
      grub_uint16_t errcode;
      grub_int8_t errmsg[TFTP_DEFAULTSIZE_PACKET];
    } err;
    struct {
      grub_int8_t data[TFTP_DEFAULTSIZE_PACKET+2];
    } oack;
  } u;
} GRUB_PACKED ;


typedef struct tftp_data
{
  grub_uint64_t file_size;
  grub_uint64_t block;
  grub_uint32_t block_size;
  grub_uint64_t ack_sent;
  int have_oack;
  struct grub_error_saved save_err;
  grub_net_udp_socket_t sock;
} *tftp_data_t;

static grub_err_t
ack (tftp_data_t data, grub_uint64_t block)
{
  struct tftphdr *tftph_ack;
  grub_uint8_t nbdata[512];
  struct grub_net_buff nb_ack;
  grub_err_t err;

  nb_ack.head = nbdata;
  nb_ack.end = nbdata + sizeof (nbdata);
  grub_netbuff_clear (&nb_ack);
  grub_netbuff_reserve (&nb_ack, 512);
  err = grub_netbuff_push (&nb_ack, sizeof (tftph_ack->opcode)
			   + sizeof (tftph_ack->u.ack.block));
  if (err)
    return err;

  tftph_ack = (struct tftphdr *) nb_ack.data;
  tftph_ack->opcode = grub_cpu_to_be16_compile_time (TFTP_ACK);
  tftph_ack->u.ack.block = grub_cpu_to_be16 (block);

  err = grub_net_send_udp_packet (data->sock, &nb_ack);
  if (err)
    return err;
  data->ack_sent = block;
  return GRUB_ERR_NONE;
}

static grub_err_t
tftp_receive (grub_net_udp_socket_t sock __attribute__ ((unused)),
	      struct grub_net_buff *nb,
	      void *f)
{
  grub_file_t file = f;
  struct tftphdr *tftph = (void *) nb->data;
  tftp_data_t data = file->data;
  grub_err_t err;
  grub_uint8_t *ptr;

  if (nb->tail - nb->data < (grub_ssize_t) sizeof (tftph->opcode))
    {
      grub_dprintf ("tftp", "TFTP packet too small\n");
      return GRUB_ERR_NONE;
    }

  tftph = (struct tftphdr *) nb->data;
  switch (grub_be_to_cpu16 (tftph->opcode))
    {
    case TFTP_OACK:
      data->block_size = TFTP_DEFAULTSIZE_PACKET;
      data->have_oack = 1; 
      for (ptr = nb->data + sizeof (tftph->opcode); ptr < nb->tail;)
	{
	  if (grub_memcmp (ptr, "tsize\0", sizeof ("tsize\0") - 1) == 0)
	    data->file_size = grub_strtoul ((char *) ptr + sizeof ("tsize\0")
					    - 1, 0, 0);
	  if (grub_memcmp (ptr, "blksize\0", sizeof ("blksize\0") - 1) == 0)
	    data->block_size = grub_strtoul ((char *) ptr + sizeof ("blksize\0")
					     - 1, 0, 0);
	  while (ptr < nb->tail && *ptr)
	    ptr++;
	  ptr++;
	}
      data->block = 0;
      grub_netbuff_free (nb);
      err = ack (data, 0);
      grub_error_save (&data->save_err);
      return GRUB_ERR_NONE;
    case TFTP_DATA:
      if (nb->tail - nb->data < (grub_ssize_t) (sizeof (tftph->opcode)
						+ sizeof (tftph->u.data.block)))
	{
	  grub_dprintf ("tftp", "TFTP packet too small\n");
	  return GRUB_ERR_NONE;
	}

      /*
       * Ack old/retransmitted block.
       *
       * The block number is a 16-bit counter, thus the maximum file size that
       * could be transfered is 65535 * block size. Most TFTP hosts support to
       * roll-over the block counter to allow unlimited transfer file size.
       *
       * This behavior is not defined in the RFC 1350 [0] but is implemented by
       * most TFTP clients and hosts.
       *
       * [0]: https://tools.ietf.org/html/rfc1350
       */
      if (grub_be_to_cpu16 (tftph->u.data.block) < ((grub_uint16_t) (data->block + 1)))
	ack (data, grub_be_to_cpu16 (tftph->u.data.block));
      /* Ignore unexpected block. */
      else if (grub_be_to_cpu16 (tftph->u.data.block) > ((grub_uint16_t) (data->block + 1)))
	grub_dprintf ("tftp", "TFTP unexpected block # %d\n", tftph->u.data.block);
      else
	{
	  unsigned size;

	  if (file->device->net->packs.count < 50)
	    {
	      err = ack (data, data->block + 1);
	      if (err)
		return err;
	    }
	  else
	    file->device->net->stall = 1;

	  err = grub_netbuff_pull (nb, sizeof (tftph->opcode) +
				   sizeof (tftph->u.data.block));
	  if (err)
	    return err;
	  size = nb->tail - nb->data;

	  data->block++;
	  if (size < data->block_size)
	    {
	      if (data->ack_sent < data->block)
		ack (data, data->block);
	      file->device->net->eof = 1;
	      file->device->net->stall = 1;
	      grub_net_udp_close (data->sock);
	      data->sock = NULL;
	    }
	  /*
	   * Prevent garbage in broken cards. Is it still necessary
	   * given that IP implementation has been fixed?
	   */
	  if (size > data->block_size)
	    {
	      err = grub_netbuff_unput (nb, size - data->block_size);
	      if (err)
		return err;
	    }
	  /* If there is data, puts packet in socket list. */
	  if ((nb->tail - nb->data) > 0)
	    {
	      grub_net_put_packet (&file->device->net->packs, nb);
	      /* Do not free nb. */
	      return GRUB_ERR_NONE;
	    }
	}
      grub_netbuff_free (nb);
      return GRUB_ERR_NONE;
    case TFTP_ERROR:
      data->have_oack = 1;
      grub_netbuff_free (nb);
      grub_error (GRUB_ERR_IO, "%s", tftph->u.err.errmsg);
      grub_error_save (&data->save_err);
      return GRUB_ERR_NONE;
    default:
      grub_netbuff_free (nb);
      return GRUB_ERR_NONE;
    }
}

/* Create a normalized copy of the filename.
   Compress any string of consecutive forward slashes to a single forward
   slash. */
static void
grub_normalize_filename (char *normalized, const char *filename)
{
  char *dest = normalized;
  const char *src = filename;

  while (*src != '\0')
    {
      if (src[0] == '/' && src[1] == '/')
	src++;
      else
	*dest++ = *src++;
    }
  *dest = '\0';
}

static grub_err_t
tftp_open (struct grub_file *file, const char *filename)
{
  struct tftphdr *tftph;
  char *rrq;
  int i;
  int rrqlen;
  int hdrlen;
  grub_uint8_t open_data[1500];
  struct grub_net_buff nb;
  tftp_data_t data;
  grub_err_t err;
  grub_uint8_t *nbd;
  grub_net_network_level_address_t addr;
  int port = file->device->net->port;

  data = grub_zalloc (sizeof (*data));
  if (!data)
    return grub_errno;

  nb.head = open_data;
  nb.end = open_data + sizeof (open_data);
  grub_netbuff_clear (&nb);

  grub_netbuff_reserve (&nb, 1500);
  err = grub_netbuff_push (&nb, sizeof (*tftph));
  if (err)
    {
      grub_free (data);
      return err;
    }

  tftph = (struct tftphdr *) nb.data;

  rrq = (char *) tftph->u.rrq;
  rrqlen = 0;

  tftph->opcode = grub_cpu_to_be16_compile_time (TFTP_RRQ);

  /* Copy and normalize the filename to work-around issues on some tftp
     servers when file names are being matched for remapping. */
  grub_normalize_filename (rrq, filename);
  rrqlen += grub_strlen (rrq) + 1;
  rrq += grub_strlen (rrq) + 1;

  grub_strcpy (rrq, "octet");
  rrqlen += grub_strlen ("octet") + 1;
  rrq += grub_strlen ("octet") + 1;

  grub_strcpy (rrq, "blksize");
  rrqlen += grub_strlen ("blksize") + 1;
  rrq += grub_strlen ("blksize") + 1;

  grub_strcpy (rrq, "1024");
  rrqlen += grub_strlen ("1024") + 1;
  rrq += grub_strlen ("1024") + 1;

  grub_strcpy (rrq, "tsize");
  rrqlen += grub_strlen ("tsize") + 1;
  rrq += grub_strlen ("tsize") + 1;

  grub_strcpy (rrq, "0");
  rrqlen += grub_strlen ("0") + 1;
  rrq += grub_strlen ("0") + 1;
  hdrlen = sizeof (tftph->opcode) + rrqlen;

  err = grub_netbuff_unput (&nb, nb.tail - (nb.data + hdrlen));
  if (err)
    {
      grub_free (data);
      return err;
    }

  file->not_easily_seekable = 1;
  file->data = data;

  grub_dprintf("tftp", "resolving address for %s\n", file->device->net->server);
  err = grub_net_resolve_address (file->device->net->server, &addr);
  if (err)
    {
      grub_dprintf ("tftp", "Address resolution failed: %d\n", err);
      grub_dprintf ("tftp", "file_size is %llu, block_size is %llu\n",
		    (unsigned long long)data->file_size,
		    (unsigned long long)data->block_size);
      grub_free (data);
      return err;
    }

  grub_dprintf("tftp", "opening connection\n");
  data->sock = grub_net_udp_open (addr,
				  port ? port : TFTP_SERVER_PORT, tftp_receive,
				  file);
  if (!data->sock)
    {
      grub_dprintf("tftp", "connection failed\n");
      grub_free (data);
      return grub_errno;
    }

  /* Receive OACK packet.  */
  nbd = nb.data;
  for (i = 0; i < GRUB_NET_TRIES; i++)
    {
      nb.data = nbd;
      err = grub_net_send_udp_packet (data->sock, &nb);
      if (err)
	{
	  grub_net_udp_close (data->sock);
	  grub_free (data);
	  return err;
	}
      grub_net_poll_cards (GRUB_NET_INTERVAL + (i * GRUB_NET_INTERVAL_ADDITION),
                           &data->have_oack);
      if (data->have_oack)
	break;
    }

  if (!data->have_oack)
    grub_error (GRUB_ERR_TIMEOUT, N_("time out opening `%s'"), filename);
  else
    grub_error_load (&data->save_err);
  if (grub_errno)
    {
      grub_net_udp_close (data->sock);
      grub_free (data);
      return grub_errno;
    }

  file->size = data->file_size;

  return GRUB_ERR_NONE;
}

static grub_err_t
tftp_close (struct grub_file *file)
{
  tftp_data_t data = file->data;

  if (data->sock)
    {
      grub_uint8_t nbdata[512];
      grub_err_t err;
      struct grub_net_buff nb_err;
      struct tftphdr *tftph;

      nb_err.head = nbdata;
      nb_err.end = nbdata + sizeof (nbdata);

      grub_netbuff_clear (&nb_err);
      grub_netbuff_reserve (&nb_err, 512);
      err = grub_netbuff_push (&nb_err, sizeof (tftph->opcode)
			       + sizeof (tftph->u.err.errcode)
			       + sizeof ("closed"));
      if (!err)
	{
	  tftph = (struct tftphdr *) nb_err.data;
	  tftph->opcode = grub_cpu_to_be16_compile_time (TFTP_ERROR);
	  tftph->u.err.errcode = grub_cpu_to_be16_compile_time (TFTP_EUNDEF);
	  grub_memcpy (tftph->u.err.errmsg, "closed", sizeof ("closed"));

	  err = grub_net_send_udp_packet (data->sock, &nb_err);
	}
      if (err)
	grub_print_error ();
      grub_net_udp_close (data->sock);
    }
  grub_free (data);
  file->data = NULL;
  return GRUB_ERR_NONE;
}

static grub_err_t
tftp_packets_pulled (struct grub_file *file)
{
  tftp_data_t data = file->data;
  if (file->device->net->packs.count >= 50)
    return 0;

  if (!file->device->net->eof)
    file->device->net->stall = 0;
  if (data->ack_sent >= data->block)
    return 0;
  return ack (data, data->block);
}

static struct grub_net_app_protocol grub_tftp_protocol = 
  {
    .name = "tftp",
    .open = tftp_open,
    .close = tftp_close,
    .packets_pulled = tftp_packets_pulled
  };

GRUB_MOD_INIT (tftp)
{
  grub_net_app_level_register (&grub_tftp_protocol);
}

GRUB_MOD_FINI (tftp)
{
  grub_net_app_level_unregister (&grub_tftp_protocol);
}
