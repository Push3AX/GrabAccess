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
#include <grub/net/ip.h>
#include <grub/net/netbuff.h>
#include <grub/time.h>

struct grub_net_udp_socket
{
  struct grub_net_udp_socket *next;
  struct grub_net_udp_socket **prev;

  enum { GRUB_NET_SOCKET_START,
	 GRUB_NET_SOCKET_ESTABLISHED,
	 GRUB_NET_SOCKET_CLOSED } status;
  int in_port;
  int out_port;
  grub_err_t (*recv_hook) (grub_net_udp_socket_t sock, struct grub_net_buff *nb,
			   void *recv);
  void *recv_hook_data;
  grub_net_network_level_address_t out_nla;
  grub_net_link_level_address_t ll_target_addr;
  struct grub_net_network_level_interface *inf;
};

static struct grub_net_udp_socket *udp_sockets;

#define FOR_UDP_SOCKETS(var) for (var = udp_sockets; var; var = var->next)

static inline void
udp_socket_register (grub_net_udp_socket_t sock)
{
  grub_list_push (GRUB_AS_LIST_P (&udp_sockets),
		  GRUB_AS_LIST (sock));
}

void
grub_net_udp_close (grub_net_udp_socket_t sock)
{
  grub_list_remove (GRUB_AS_LIST (sock));
  grub_free (sock);
}

grub_net_udp_socket_t
grub_net_udp_open (grub_net_network_level_address_t addr,
		   grub_uint16_t out_port,
		   grub_err_t (*recv_hook) (grub_net_udp_socket_t sock,
					    struct grub_net_buff *nb,
					    void *data),
		   void *recv_hook_data)
{
  grub_err_t err;
  struct grub_net_network_level_interface *inf;
  grub_net_network_level_address_t gateway;
  grub_net_udp_socket_t socket;
  static int in_port = 25300;
  grub_net_link_level_address_t ll_target_addr;

  if (addr.type != GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4
      && addr.type != GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6)
    {
      grub_error (GRUB_ERR_BUG, "not an IP address");
      return NULL;
    }
 
  err = grub_net_route_address (addr, &gateway, &inf);
  if (err)
    return NULL;

  err = grub_net_link_layer_resolve (inf, &gateway, &ll_target_addr);
  if (err)
    return NULL;

  socket = grub_zalloc (sizeof (*socket));
  if (socket == NULL)
    return NULL; 

  socket->out_port = out_port;
  socket->inf = inf;
  socket->out_nla = addr;
  socket->ll_target_addr = ll_target_addr;
  socket->in_port = in_port++;
  socket->status = GRUB_NET_SOCKET_START;
  socket->recv_hook = recv_hook;
  socket->recv_hook_data = recv_hook_data;

  udp_socket_register (socket);

  return socket;
}

grub_err_t
grub_net_send_udp_packet (const grub_net_udp_socket_t socket,
			  struct grub_net_buff *nb)
{
  struct udphdr *udph;
  grub_err_t err;

  COMPILE_TIME_ASSERT (GRUB_NET_UDP_HEADER_SIZE == sizeof (*udph));

  err = grub_netbuff_push (nb, sizeof (*udph));
  if (err)
    return err;

  udph = (struct udphdr *) nb->data;
  udph->src = grub_cpu_to_be16 (socket->in_port);
  udph->dst = grub_cpu_to_be16 (socket->out_port);

  udph->chksum = 0;
  udph->len = grub_cpu_to_be16 (nb->tail - nb->data);

  udph->chksum = grub_net_ip_transport_checksum (nb, GRUB_NET_IP_UDP,
						 &socket->inf->address,
						 &socket->out_nla);

  return grub_net_send_ip_packet (socket->inf, &(socket->out_nla),
				  &(socket->ll_target_addr), nb,
				  GRUB_NET_IP_UDP);
}

grub_err_t
grub_net_recv_udp_packet (struct grub_net_buff *nb,
			  struct grub_net_network_level_interface *inf,
			  const grub_net_network_level_address_t *source)
{
  struct udphdr *udph;
  grub_net_udp_socket_t sock;
  grub_err_t err;

  /* Ignore broadcast.  */
  if (!inf)
    {
      grub_netbuff_free (nb);
      return GRUB_ERR_NONE;
    }

  udph = (struct udphdr *) nb->data;
  if (nb->tail - nb->data < (grub_ssize_t) sizeof (*udph))
    {
      grub_dprintf ("net", "UDP packet too short: %" PRIuGRUB_SIZE "\n",
		    (grub_size_t) (nb->tail - nb->data));
      grub_netbuff_free (nb);
      return GRUB_ERR_NONE;
    }

  FOR_UDP_SOCKETS (sock)
  {
    if (grub_be_to_cpu16 (udph->dst) == sock->in_port
	&& inf == sock->inf
	&& grub_net_addr_cmp (source, &sock->out_nla) == 0
	&& (sock->status == GRUB_NET_SOCKET_START
	    || grub_be_to_cpu16 (udph->src) == sock->out_port))
      {
	if (udph->chksum)
	  {
	    grub_uint16_t chk, expected;
	    chk = udph->chksum;
	    udph->chksum = 0;
	    expected = grub_net_ip_transport_checksum (nb, GRUB_NET_IP_UDP,
						       &sock->out_nla,
						       &sock->inf->address);
	    if (expected != chk)
	      {
		grub_dprintf ("net", "Invalid UDP checksum. "
			      "Expected %x, got %x\n",
			      grub_be_to_cpu16 (expected),
			      grub_be_to_cpu16 (chk));
		grub_netbuff_free (nb);
		return GRUB_ERR_NONE;
	      }
	    udph->chksum = chk;
	  }

	if (sock->status == GRUB_NET_SOCKET_START)
	  {
	    sock->out_port = grub_be_to_cpu16 (udph->src);
	    sock->status = GRUB_NET_SOCKET_ESTABLISHED;
	  }

	err = grub_netbuff_pull (nb, sizeof (*udph));
	if (err)
	  return err;

	/* App protocol remove its own reader.  */
	if (sock->recv_hook)
	  sock->recv_hook (sock, nb, sock->recv_hook_data);
	else
	  grub_netbuff_free (nb);
	return GRUB_ERR_NONE;
      }
  }
  grub_netbuff_free (nb);
  return GRUB_ERR_NONE;
}
