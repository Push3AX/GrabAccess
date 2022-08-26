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
#include <grub/net/ip.h>
#include <grub/net/tcp.h>
#include <grub/net/netbuff.h>
#include <grub/time.h>
#include <grub/priority_queue.h>

#define TCP_SYN_RETRANSMISSION_TIMEOUT GRUB_NET_INTERVAL
#define TCP_SYN_RETRANSMISSION_COUNT GRUB_NET_TRIES
#define TCP_RETRANSMISSION_TIMEOUT GRUB_NET_INTERVAL
#define TCP_RETRANSMISSION_COUNT GRUB_NET_TRIES

struct unacked
{
  struct unacked *next;
  struct unacked **prev;
  struct grub_net_buff *nb;
  grub_uint64_t last_try;
  int try_count;
};

enum
  {
    TCP_FIN = 0x1,
    TCP_SYN = 0x2,
    TCP_RST = 0x4,
    TCP_PUSH = 0x8,
    TCP_ACK = 0x10,
    TCP_URG = 0x20,
  };

struct grub_net_tcp_socket
{
  struct grub_net_tcp_socket *next;
  struct grub_net_tcp_socket **prev;

  int established;
  int i_closed;
  int they_closed;
  int in_port;
  int out_port;
  int errors;
  int they_reseted;
  int i_reseted;
  int i_stall;
  grub_uint32_t my_start_seq;
  grub_uint32_t my_cur_seq;
  grub_uint32_t their_start_seq;
  grub_uint32_t their_cur_seq;
  grub_uint16_t my_window;
  struct unacked *unack_first;
  struct unacked *unack_last;
  grub_err_t (*recv_hook) (grub_net_tcp_socket_t sock, struct grub_net_buff *nb,
			   void *recv);
  void (*error_hook) (grub_net_tcp_socket_t sock, void *recv);
  void (*fin_hook) (grub_net_tcp_socket_t sock, void *recv);
  void *hook_data;
  grub_net_network_level_address_t out_nla;
  grub_net_link_level_address_t ll_target_addr;
  struct grub_net_network_level_interface *inf;
  grub_net_packets_t packs;
  grub_priority_queue_t pq;
};

struct grub_net_tcp_listen
{
  struct grub_net_tcp_listen *next;
  struct grub_net_tcp_listen **prev;

  grub_uint16_t port;
  const struct grub_net_network_level_interface *inf;

  grub_err_t (*listen_hook) (grub_net_tcp_listen_t listen,
			     grub_net_tcp_socket_t sock,
			     void *data);
  void *hook_data;
};

struct tcphdr
{
  grub_uint16_t src;
  grub_uint16_t dst;
  grub_uint32_t seqnr;
  grub_uint32_t ack;
  grub_uint16_t flags;
  grub_uint16_t window;
  grub_uint16_t checksum;
  grub_uint16_t urgent;
} GRUB_PACKED;

struct tcp_scale_opt {
  grub_uint8_t kind;
  grub_uint8_t length;
  grub_uint8_t scale;
} GRUB_PACKED;

struct tcp_synhdr {
  struct tcphdr tcphdr;
  struct tcp_scale_opt scale_opt;
  grub_uint8_t padding;
};

struct tcp_pseudohdr
{
  grub_uint32_t src;
  grub_uint32_t dst;
  grub_uint8_t zero;
  grub_uint8_t proto;
  grub_uint16_t tcp_length;
} GRUB_PACKED;

struct tcp6_pseudohdr
{
  grub_uint64_t src[2];
  grub_uint64_t dst[2];
  grub_uint32_t tcp_length;
  grub_uint8_t zero[3];
  grub_uint8_t proto;
} GRUB_PACKED;

static struct grub_net_tcp_socket *tcp_sockets;
static struct grub_net_tcp_listen *tcp_listens;

#define FOR_TCP_SOCKETS(var) FOR_LIST_ELEMENTS (var, tcp_sockets)
#define FOR_TCP_LISTENS(var) FOR_LIST_ELEMENTS (var, tcp_listens)

grub_net_tcp_listen_t
grub_net_tcp_listen (grub_uint16_t port,
		     const struct grub_net_network_level_interface *inf,
		     grub_err_t (*listen_hook) (grub_net_tcp_listen_t listen,
						grub_net_tcp_socket_t sock,
						void *data),
		     void *hook_data)
{
  grub_net_tcp_listen_t ret;
  ret = grub_malloc (sizeof (*ret));
  if (!ret)
    return NULL;
  ret->listen_hook = listen_hook;
  ret->hook_data = hook_data;
  ret->port = port;
  ret->inf = inf;
  grub_list_push (GRUB_AS_LIST_P (&tcp_listens), GRUB_AS_LIST (ret));
  return ret;
}

void
grub_net_tcp_stop_listen (grub_net_tcp_listen_t listen)
{
  grub_list_remove (GRUB_AS_LIST (listen));
}

static inline void
tcp_socket_register (grub_net_tcp_socket_t sock)
{
  grub_list_push (GRUB_AS_LIST_P (&tcp_sockets),
		  GRUB_AS_LIST (sock));
}

static void
error (grub_net_tcp_socket_t sock)
{
  struct unacked *unack, *next;

  if (sock->error_hook)
    sock->error_hook (sock, sock->hook_data);

  for (unack = sock->unack_first; unack; unack = next)
    {
      next = unack->next;
      grub_netbuff_free (unack->nb);
      grub_free (unack);
    }

  sock->unack_first = NULL;
  sock->unack_last = NULL;
}

static grub_err_t
tcp_send (struct grub_net_buff *nb, grub_net_tcp_socket_t socket)
{
  grub_err_t err;
  grub_uint8_t *nbd;
  struct unacked *unack;
  struct tcphdr *tcph;
  grub_size_t size;

  tcph = (struct tcphdr *) nb->data;

  tcph->seqnr = grub_cpu_to_be32 (socket->my_cur_seq);
  size = (nb->tail - nb->data - (grub_be_to_cpu16 (tcph->flags) >> 12) * 4);
  if (grub_be_to_cpu16 (tcph->flags) & TCP_FIN)
    size++;
  socket->my_cur_seq += size;
  tcph->src = grub_cpu_to_be16 (socket->in_port);
  tcph->dst = grub_cpu_to_be16 (socket->out_port);
  tcph->checksum = 0;
  tcph->checksum = grub_net_ip_transport_checksum (nb, GRUB_NET_IP_TCP,
						   &socket->inf->address,
						   &socket->out_nla);
  nbd = nb->data;
  if (size)
    {
      unack = grub_malloc (sizeof (*unack));
      if (!unack)
	return grub_errno;

      unack->next = NULL;
      unack->nb = nb;
      unack->try_count = 1;
      unack->last_try = grub_get_time_ms ();
      if (!socket->unack_last)
	socket->unack_first = socket->unack_last = unack;
      else
	socket->unack_last->next = unack;
    }

  err = grub_net_send_ip_packet (socket->inf, &(socket->out_nla),
				 &(socket->ll_target_addr), nb,
				 GRUB_NET_IP_TCP);
  if (err)
    return err;
  nb->data = nbd;
  if (!size)
    grub_netbuff_free (nb);
  return GRUB_ERR_NONE;
}

void
grub_net_tcp_close (grub_net_tcp_socket_t sock,
		    int discard_received)
{
  struct grub_net_buff *nb_fin;
  struct tcphdr *tcph_fin;
  grub_err_t err;

  if (discard_received != GRUB_NET_TCP_CONTINUE_RECEIVING)
    {
      sock->recv_hook = NULL;
      sock->error_hook = NULL;
      sock->fin_hook = NULL;
    }

  if (discard_received == GRUB_NET_TCP_ABORT)
    sock->i_reseted = 1;

  if (sock->i_closed)
    return;

  sock->i_closed = 1;

  nb_fin = grub_netbuff_alloc (sizeof (*tcph_fin)
			       + GRUB_NET_OUR_MAX_IP_HEADER_SIZE
			       + GRUB_NET_MAX_LINK_HEADER_SIZE);
  if (!nb_fin)
    return;
  err = grub_netbuff_reserve (nb_fin, GRUB_NET_OUR_MAX_IP_HEADER_SIZE
			       + GRUB_NET_MAX_LINK_HEADER_SIZE);
  if (err)
    {
      grub_netbuff_free (nb_fin);
      grub_dprintf ("net", "error closing socket\n");
      grub_errno = GRUB_ERR_NONE;
      return;
    }

  err = grub_netbuff_put (nb_fin, sizeof (*tcph_fin));
  if (err)
    {
      grub_netbuff_free (nb_fin);
      grub_dprintf ("net", "error closing socket\n");
      grub_errno = GRUB_ERR_NONE;
      return;
    }
  tcph_fin = (void *) nb_fin->data;
  tcph_fin->ack = grub_cpu_to_be32 (sock->their_cur_seq);
  tcph_fin->flags = grub_cpu_to_be16_compile_time ((5 << 12) | TCP_FIN
						   | TCP_ACK);
  tcph_fin->window = grub_cpu_to_be16_compile_time (0);
  tcph_fin->urgent = 0;
  err = tcp_send (nb_fin, sock);
  if (err)
    {
      grub_netbuff_free (nb_fin);
      grub_dprintf ("net", "error closing socket\n");
      grub_errno = GRUB_ERR_NONE;
    }
  return;
}

static void
ack_real (grub_net_tcp_socket_t sock, int res)
{
  struct grub_net_buff *nb_ack;
  struct tcphdr *tcph_ack;
  grub_err_t err;

  nb_ack = grub_netbuff_alloc (sizeof (*tcph_ack) + 128);
  if (!nb_ack)
    return;
  err = grub_netbuff_reserve (nb_ack, 128);
  if (err)
    {
      grub_netbuff_free (nb_ack);
      grub_dprintf ("net", "error closing socket\n");
      grub_errno = GRUB_ERR_NONE;
      return;
    }

  err = grub_netbuff_put (nb_ack, sizeof (*tcph_ack));
  if (err)
    {
      grub_netbuff_free (nb_ack);
      grub_dprintf ("net", "error closing socket\n");
      grub_errno = GRUB_ERR_NONE;
      return;
    }
  tcph_ack = (void *) nb_ack->data;
  if (res)
    {
      tcph_ack->ack = grub_cpu_to_be32_compile_time (0);
      tcph_ack->flags = grub_cpu_to_be16_compile_time ((5 << 12) | TCP_RST);
      tcph_ack->window = grub_cpu_to_be16_compile_time (0);
    }
  else
    {
      tcph_ack->ack = grub_cpu_to_be32 (sock->their_cur_seq);
      tcph_ack->flags = grub_cpu_to_be16_compile_time ((5 << 12) | TCP_ACK);
      tcph_ack->window = !sock->i_stall ? grub_cpu_to_be16 (sock->my_window)
	: 0;
    }
  tcph_ack->urgent = 0;
  tcph_ack->src = grub_cpu_to_be16 (sock->in_port);
  tcph_ack->dst = grub_cpu_to_be16 (sock->out_port);
  err = tcp_send (nb_ack, sock);
  if (err)
    {
      grub_dprintf ("net", "error acking socket\n");
      grub_errno = GRUB_ERR_NONE;
    }
}

static void
ack (grub_net_tcp_socket_t sock)
{
  ack_real (sock, 0);
}

static void
reset (grub_net_tcp_socket_t sock)
{
  ack_real (sock, 1);
}

void
grub_net_tcp_retransmit (void)
{
  grub_net_tcp_socket_t sock;
  grub_uint64_t ctime = grub_get_time_ms ();
  grub_uint64_t limit_time = ctime - TCP_RETRANSMISSION_TIMEOUT;

  FOR_TCP_SOCKETS (sock)
  {
    struct unacked *unack;
    for (unack = sock->unack_first; unack; unack = unack->next)
      {
	struct tcphdr *tcph;
	grub_uint8_t *nbd;
	grub_err_t err;

	if (unack->last_try > limit_time)
	  continue;
	
	if (unack->try_count > TCP_RETRANSMISSION_COUNT)
	  {
	    error (sock);
	    break;
	  }
	unack->try_count++;
	unack->last_try = ctime;
	nbd = unack->nb->data;
	tcph = (struct tcphdr *) nbd;

	if ((tcph->flags & grub_cpu_to_be16_compile_time (TCP_ACK))
	    && tcph->ack != grub_cpu_to_be32 (sock->their_cur_seq))
	  {
	    tcph->checksum = 0;
	    tcph->checksum = grub_net_ip_transport_checksum (unack->nb,
							     GRUB_NET_IP_TCP,
							     &sock->inf->address,
							     &sock->out_nla);
	  }

	err = grub_net_send_ip_packet (sock->inf, &(sock->out_nla),
				       &(sock->ll_target_addr), unack->nb,
				       GRUB_NET_IP_TCP);
	unack->nb->data = nbd;
	if (err)
	  {
	    grub_dprintf ("net", "TCP retransmit failed: %s\n", grub_errmsg);
	    grub_errno = GRUB_ERR_NONE;
	  }
      }
  }
}

grub_uint16_t
grub_net_ip_transport_checksum (struct grub_net_buff *nb,
				grub_uint16_t proto,
				const grub_net_network_level_address_t *src,
				const grub_net_network_level_address_t *dst)
{
  grub_uint16_t a, b = 0;
  grub_uint32_t c;
  a = ~grub_be_to_cpu16 (grub_net_ip_chksum ((void *) nb->data,
					     nb->tail - nb->data));

  switch (dst->type)
    {
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4:
      {
	struct tcp_pseudohdr ph;
	ph.src = src->ipv4;
	ph.dst = dst->ipv4;
	ph.zero = 0;
	ph.tcp_length = grub_cpu_to_be16 (nb->tail - nb->data);
	ph.proto = proto;
	b = ~grub_be_to_cpu16 (grub_net_ip_chksum ((void *) &ph, sizeof (ph)));
	break;
      }
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV6:
      {
	struct tcp6_pseudohdr ph;
	grub_memcpy (ph.src, src->ipv6, sizeof (ph.src));
	grub_memcpy (ph.dst, dst->ipv6, sizeof (ph.dst));
	grub_memset (ph.zero, 0, sizeof (ph.zero));
	ph.tcp_length = grub_cpu_to_be32 (nb->tail - nb->data);
	ph.proto = proto;
	b = ~grub_be_to_cpu16 (grub_net_ip_chksum ((void *) &ph, sizeof (ph)));
	break;
      }
    case GRUB_NET_NETWORK_LEVEL_PROTOCOL_DHCP_RECV:
      b = 0;
      break;
    }
  c = (grub_uint32_t) a + (grub_uint32_t) b;
  if (c >= 0xffff)
    c -= 0xffff;
  return grub_cpu_to_be16 (~c);
}

/* FIXME: overflow. */
static int
cmp (const void *a__, const void *b__)
{
  struct grub_net_buff *a_ = *(struct grub_net_buff **) a__;
  struct grub_net_buff *b_ = *(struct grub_net_buff **) b__;
  struct tcphdr *a = (struct tcphdr *) a_->data;
  struct tcphdr *b = (struct tcphdr *) b_->data;
  /* We want the first elements to be on top.  */
  if (grub_be_to_cpu32 (a->seqnr) < grub_be_to_cpu32 (b->seqnr))
    return +1;
  if (grub_be_to_cpu32 (a->seqnr) > grub_be_to_cpu32 (b->seqnr))
    return -1;
  return 0;
}

static void
destroy_pq (grub_net_tcp_socket_t sock)
{
  struct grub_net_buff **nb_p;
  while ((nb_p = grub_priority_queue_top (sock->pq)))
    {
      grub_netbuff_free (*nb_p);
      grub_priority_queue_pop (sock->pq);
    }

  grub_priority_queue_destroy (sock->pq);
}

grub_err_t
grub_net_tcp_accept (grub_net_tcp_socket_t sock,
		     grub_err_t (*recv_hook) (grub_net_tcp_socket_t sock,
					      struct grub_net_buff *nb,
					      void *data),
		     void (*error_hook) (grub_net_tcp_socket_t sock,
					 void *data),
		     void (*fin_hook) (grub_net_tcp_socket_t sock,
				       void *data),
		     void *hook_data)
{
  struct grub_net_buff *nb_ack;
  struct tcphdr *tcph;
  grub_err_t err;
  grub_net_network_level_address_t gateway;
  struct grub_net_network_level_interface *inf;

  sock->recv_hook = recv_hook;
  sock->error_hook = error_hook;
  sock->fin_hook = fin_hook;
  sock->hook_data = hook_data;

  err = grub_net_route_address (sock->out_nla, &gateway, &inf);
  if (err)
    return err;

  err = grub_net_link_layer_resolve (sock->inf, &gateway, &(sock->ll_target_addr));
  if (err)
    return err;

  nb_ack = grub_netbuff_alloc (sizeof (*tcph)
			       + GRUB_NET_OUR_MAX_IP_HEADER_SIZE
			       + GRUB_NET_MAX_LINK_HEADER_SIZE);
  if (!nb_ack)
    return grub_errno;
  err = grub_netbuff_reserve (nb_ack, GRUB_NET_OUR_MAX_IP_HEADER_SIZE
			      + GRUB_NET_MAX_LINK_HEADER_SIZE);
  if (err)
    {
      grub_netbuff_free (nb_ack);
      return err;
    }

  err = grub_netbuff_put (nb_ack, sizeof (*tcph));
  if (err)
    {
      grub_netbuff_free (nb_ack);
      return err;
    }
  tcph = (void *) nb_ack->data;
  tcph->ack = grub_cpu_to_be32 (sock->their_cur_seq);
  tcph->flags = grub_cpu_to_be16_compile_time ((5 << 12) | TCP_SYN | TCP_ACK);
  tcph->window = grub_cpu_to_be16 (sock->my_window);
  tcph->urgent = 0;
  sock->established = 1;
  tcp_socket_register (sock);
  err = tcp_send (nb_ack, sock);
  if (err)
    return err;
  sock->my_cur_seq++;
  return GRUB_ERR_NONE;
}

grub_net_tcp_socket_t
grub_net_tcp_open (char *server,
		   grub_uint16_t out_port,
		   grub_err_t (*recv_hook) (grub_net_tcp_socket_t sock,
					    struct grub_net_buff *nb,
					    void *data),
		   void (*error_hook) (grub_net_tcp_socket_t sock,
				       void *data),
		   void (*fin_hook) (grub_net_tcp_socket_t sock,
				     void *data),
		   void *hook_data)
{
  grub_err_t err;
  grub_net_network_level_address_t addr;
  struct grub_net_network_level_interface *inf;
  grub_net_network_level_address_t gateway;
  grub_net_tcp_socket_t socket;
  static grub_uint16_t in_port = 21550;
  struct grub_net_buff *nb;
  struct tcp_synhdr *tcph;
  int i;
  grub_uint8_t *nbd;
  grub_net_link_level_address_t ll_target_addr;

  err = grub_net_resolve_address (server, &addr);
  if (err)
    return NULL;

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
  socket->recv_hook = recv_hook;
  socket->error_hook = error_hook;
  socket->fin_hook = fin_hook;
  socket->hook_data = hook_data;

  nb = grub_netbuff_alloc (sizeof (*tcph) + 128);
  if (!nb)
    {
      grub_free (socket);
      return NULL;
    }

  err = grub_netbuff_reserve (nb, 128);
  if (err)
    {
      grub_free (socket);
      grub_netbuff_free (nb);
      return NULL;
    }

  err = grub_netbuff_put (nb, sizeof (*tcph));
  if (err)
    {
      grub_free (socket);
      grub_netbuff_free (nb);
      return NULL;
    }
  socket->pq = grub_priority_queue_new (sizeof (struct grub_net_buff *), cmp);
  if (!socket->pq)
    {
      grub_free (socket);
      grub_netbuff_free (nb);
      return NULL;
    }

  tcph = (void *) nb->data;
  grub_memset(tcph, 0, sizeof (*tcph));
  socket->my_start_seq = grub_get_time_ms ();
  socket->my_cur_seq = socket->my_start_seq + 1;
  socket->my_window = 32768;
  tcph->tcphdr.seqnr = grub_cpu_to_be32 (socket->my_start_seq);
  tcph->tcphdr.ack = grub_cpu_to_be32_compile_time (0);
  tcph->tcphdr.flags = grub_cpu_to_be16_compile_time ((6 << 12) | TCP_SYN);
  tcph->tcphdr.window = grub_cpu_to_be16 (socket->my_window);
  tcph->tcphdr.urgent = 0;
  tcph->tcphdr.src = grub_cpu_to_be16 (socket->in_port);
  tcph->tcphdr.dst = grub_cpu_to_be16 (socket->out_port);
  tcph->tcphdr.checksum = 0;
  tcph->scale_opt.kind = 3;
  tcph->scale_opt.length = 3;
  tcph->scale_opt.scale = 5;
  tcph->tcphdr.checksum = grub_net_ip_transport_checksum (nb, GRUB_NET_IP_TCP,
							  &socket->inf->address,
							  &socket->out_nla);

  tcp_socket_register (socket);

  nbd = nb->data;
  for (i = 0; i < TCP_SYN_RETRANSMISSION_COUNT; i++)
    {
      int j;
      nb->data = nbd;
      err = grub_net_send_ip_packet (socket->inf, &(socket->out_nla), 
				     &(socket->ll_target_addr), nb,
				     GRUB_NET_IP_TCP);
      if (err)
	{
	  grub_list_remove (GRUB_AS_LIST (socket));
	  grub_free (socket);
	  grub_netbuff_free (nb);
	  return NULL;
	}
      for (j = 0; (j < TCP_SYN_RETRANSMISSION_TIMEOUT / 50 
		   && !socket->established); j++)
	grub_net_poll_cards (50, &socket->established);
      if (socket->established)
	break;
    }
  if (!socket->established)
    {
      grub_list_remove (GRUB_AS_LIST (socket));
      if (socket->they_reseted)
	grub_error (GRUB_ERR_NET_PORT_CLOSED,
		    N_("connection refused"));
      else
	grub_error (GRUB_ERR_NET_NO_ANSWER,
		    N_("connection timeout"));

      grub_netbuff_free (nb);
      destroy_pq (socket);
      grub_free (socket);
      return NULL;
    }

  grub_netbuff_free (nb);
  return socket;
}

grub_err_t
grub_net_send_tcp_packet (const grub_net_tcp_socket_t socket,
			  struct grub_net_buff *nb, int push)
{
  struct tcphdr *tcph;
  grub_err_t err;
  grub_ssize_t fraglen;
  COMPILE_TIME_ASSERT (sizeof (struct tcphdr) == GRUB_NET_TCP_HEADER_SIZE);
  if (socket->out_nla.type == GRUB_NET_NETWORK_LEVEL_PROTOCOL_IPV4)
    fraglen = (socket->inf->card->mtu - GRUB_NET_OUR_IPV4_HEADER_SIZE
	       - sizeof (*tcph));
  else
    fraglen = 1280 - GRUB_NET_OUR_IPV6_HEADER_SIZE;

  while (nb->tail - nb->data > fraglen)
    {
      struct grub_net_buff *nb2;

      nb2 = grub_netbuff_alloc (fraglen + sizeof (*tcph)
				+ GRUB_NET_OUR_MAX_IP_HEADER_SIZE
				+ GRUB_NET_MAX_LINK_HEADER_SIZE);
      if (!nb2)
	return grub_errno;
      err = grub_netbuff_reserve (nb2, GRUB_NET_MAX_LINK_HEADER_SIZE
				  + GRUB_NET_OUR_MAX_IP_HEADER_SIZE);
      if (err)
	return err;
      err = grub_netbuff_put (nb2, sizeof (*tcph));
      if (err)
	return err;

      tcph = (struct tcphdr *) nb2->data;
      tcph->ack = grub_cpu_to_be32 (socket->their_cur_seq);
      tcph->flags = grub_cpu_to_be16_compile_time ((5 << 12) | TCP_ACK);
      tcph->window = !socket->i_stall ? grub_cpu_to_be16 (socket->my_window)
	: 0;
      tcph->urgent = 0;
      err = grub_netbuff_put (nb2, fraglen);
      if (err)
	return err;
      grub_memcpy (tcph + 1, nb->data, fraglen);
      err = grub_netbuff_pull (nb, fraglen);
      if (err)
	return err;

      err = tcp_send (nb2, socket);
      if (err)
	return err;
    }

  err = grub_netbuff_push (nb, sizeof (*tcph));
  if (err)
    return err;

  tcph = (struct tcphdr *) nb->data;
  tcph->ack = grub_cpu_to_be32 (socket->their_cur_seq);
  tcph->flags = (grub_cpu_to_be16_compile_time ((5 << 12) | TCP_ACK)
		 | (push ? grub_cpu_to_be16_compile_time (TCP_PUSH) : 0));
  tcph->window = !socket->i_stall ? grub_cpu_to_be16 (socket->my_window) : 0;
  tcph->urgent = 0;
  return tcp_send (nb, socket);
}

grub_err_t
grub_net_recv_tcp_packet (struct grub_net_buff *nb,
			  struct grub_net_network_level_interface *inf,
			  const grub_net_network_level_address_t *source)
{
  struct tcphdr *tcph;
  grub_net_tcp_socket_t sock;
  grub_err_t err;

  /* Ignore broadcast.  */
  if (!inf)
    {
      grub_netbuff_free (nb);
      return GRUB_ERR_NONE;
    }

  tcph = (struct tcphdr *) nb->data;
  if ((grub_be_to_cpu16 (tcph->flags) >> 12) < 5)
    {
      grub_dprintf ("net", "TCP header too short: %u\n",
		    grub_be_to_cpu16 (tcph->flags) >> 12);
      grub_netbuff_free (nb);
      return GRUB_ERR_NONE;
    }
  if (nb->tail - nb->data < (grub_ssize_t) ((grub_be_to_cpu16 (tcph->flags)
					     >> 12) * sizeof (grub_uint32_t)))
    {
      grub_dprintf ("net", "TCP packet too short: %" PRIuGRUB_SIZE "\n",
		    (grub_size_t) (nb->tail - nb->data));
      grub_netbuff_free (nb);
      return GRUB_ERR_NONE;
    }

  FOR_TCP_SOCKETS (sock)
  {
    if (!(grub_be_to_cpu16 (tcph->dst) == sock->in_port
	  && grub_be_to_cpu16 (tcph->src) == sock->out_port
	  && inf == sock->inf
	  && grub_net_addr_cmp (source, &sock->out_nla) == 0))
      continue;
    if (tcph->checksum)
      {
	grub_uint16_t chk, expected;
	chk = tcph->checksum;
	tcph->checksum = 0;
	expected = grub_net_ip_transport_checksum (nb, GRUB_NET_IP_TCP,
						   &sock->out_nla,
						   &sock->inf->address);
	if (expected != chk)
	  {
	    grub_dprintf ("net", "Invalid TCP checksum. "
			  "Expected %x, got %x\n",
			  grub_be_to_cpu16 (expected),
			  grub_be_to_cpu16 (chk));
	    grub_netbuff_free (nb);
	    return GRUB_ERR_NONE;
	  }
	tcph->checksum = chk;
      }

    if ((grub_be_to_cpu16 (tcph->flags) & TCP_SYN)
	&& (grub_be_to_cpu16 (tcph->flags) & TCP_ACK)
	&& !sock->established)
      {
	sock->their_start_seq = grub_be_to_cpu32 (tcph->seqnr);
	sock->their_cur_seq = sock->their_start_seq + 1;
	sock->established = 1;
      }

    if (grub_be_to_cpu16 (tcph->flags) & TCP_RST)
      {
	sock->they_reseted = 1;

	error (sock);

	grub_netbuff_free (nb);

	return GRUB_ERR_NONE;
      }

    if (grub_be_to_cpu16 (tcph->flags) & TCP_ACK)
      {
	struct unacked *unack, *next;
	grub_uint32_t acked = grub_be_to_cpu32 (tcph->ack);
	for (unack = sock->unack_first; unack; unack = next)
	  {
	    grub_uint32_t seqnr;
	    struct tcphdr *unack_tcph;
	    next = unack->next;
	    seqnr = grub_be_to_cpu32 (((struct tcphdr *) unack->nb->data)
				      ->seqnr);
	    unack_tcph = (struct tcphdr *) unack->nb->data;
	    seqnr += (unack->nb->tail - unack->nb->data
		      - (grub_be_to_cpu16 (unack_tcph->flags) >> 12) * 4);
	    if (grub_be_to_cpu16 (unack_tcph->flags) & TCP_FIN)
	      seqnr++;

	    if (seqnr > acked)
	      break;
	    grub_netbuff_free (unack->nb);
	    grub_free (unack);
	  }
	sock->unack_first = unack;
	if (!sock->unack_first)
	  sock->unack_last = NULL;
      }

    if (grub_be_to_cpu32 (tcph->seqnr) < sock->their_cur_seq)
      {
	ack (sock);
	grub_netbuff_free (nb);
	return GRUB_ERR_NONE;
      }
    if (sock->i_reseted && (nb->tail - nb->data
			    - (grub_be_to_cpu16 (tcph->flags)
			       >> 12) * sizeof (grub_uint32_t)) > 0)
      {
	reset (sock);
      }

    err = grub_priority_queue_push (sock->pq, &nb);
    if (err)
      {
	grub_netbuff_free (nb);
	return err;
      }

    {
      struct grub_net_buff **nb_top_p, *nb_top;
      int do_ack = 0;
      int just_closed = 0;
      while (1)
	{
	  nb_top_p = grub_priority_queue_top (sock->pq);
	  if (!nb_top_p)
	    return GRUB_ERR_NONE;
	  nb_top = *nb_top_p;
	  tcph = (struct tcphdr *) nb_top->data;
	  if (grub_be_to_cpu32 (tcph->seqnr) >= sock->their_cur_seq)
	    break;
	  grub_netbuff_free (nb_top);
	  grub_priority_queue_pop (sock->pq);
	}
      if (grub_be_to_cpu32 (tcph->seqnr) != sock->their_cur_seq)
	{
	  ack (sock);
	  return GRUB_ERR_NONE;
	}
      while (1)
	{
	  nb_top_p = grub_priority_queue_top (sock->pq);
	  if (!nb_top_p)
	    break;
	  nb_top = *nb_top_p;
	  tcph = (struct tcphdr *) nb_top->data;

	  if (grub_be_to_cpu32 (tcph->seqnr) != sock->their_cur_seq)
	    break;
	  grub_priority_queue_pop (sock->pq);

	  err = grub_netbuff_pull (nb_top, (grub_be_to_cpu16 (tcph->flags)
					    >> 12) * sizeof (grub_uint32_t));
	  if (err)
	    {
	      grub_netbuff_free (nb_top);
	      return err;
	    }

	  sock->their_cur_seq += (nb_top->tail - nb_top->data);
	  if (grub_be_to_cpu16 (tcph->flags) & TCP_FIN)
	    {
	      sock->they_closed = 1;
	      just_closed = 1;
	      sock->their_cur_seq++;
	      do_ack = 1;
	    }
	  /* If there is data, puts packet in socket list. */
	  if ((nb_top->tail - nb_top->data) > 0)
	    {
	      grub_net_put_packet (&sock->packs, nb_top);
	      do_ack = 1;
	    }
	  else
	    grub_netbuff_free (nb_top);
	}
      if (do_ack)
	ack (sock);
      while (sock->packs.first)
	{
	  nb = sock->packs.first->nb;
	  if (sock->recv_hook)
	    sock->recv_hook (sock, sock->packs.first->nb, sock->hook_data);
	  else
	    grub_netbuff_free (nb);
	  grub_net_remove_packet (sock->packs.first);
	}

      if (sock->fin_hook && just_closed)
	sock->fin_hook (sock, sock->hook_data);
    }
	
    return GRUB_ERR_NONE;
  }
  if (grub_be_to_cpu16 (tcph->flags) & TCP_SYN)
    {
      grub_net_tcp_listen_t listen;

      FOR_TCP_LISTENS (listen)
      {
	if (!(grub_be_to_cpu16 (tcph->dst) == listen->port
	      && (inf == listen->inf || listen->inf == NULL)))
	  continue;
	sock = grub_zalloc (sizeof (*sock));
	if (sock == NULL)
	  return grub_errno;
	
	sock->out_port = grub_be_to_cpu16 (tcph->src);
	sock->in_port = grub_be_to_cpu16 (tcph->dst);
	sock->inf = inf;
	sock->out_nla = *source;
	sock->their_start_seq = grub_be_to_cpu32 (tcph->seqnr);
	sock->their_cur_seq = sock->their_start_seq + 1;
	sock->my_cur_seq = sock->my_start_seq = grub_get_time_ms ();
	sock->my_window = 8192;

	sock->pq = grub_priority_queue_new (sizeof (struct grub_net_buff *),
					    cmp);
	if (!sock->pq)
	  {
	    grub_free (sock);
	    grub_netbuff_free (nb);
	    return grub_errno;
	  }

	err = listen->listen_hook (listen, sock, listen->hook_data);

	grub_netbuff_free (nb);
	return err;

      }
    }
  grub_netbuff_free (nb);
  return GRUB_ERR_NONE;
}

void
grub_net_tcp_stall (grub_net_tcp_socket_t sock)
{
  if (sock->i_stall)
    return;
  sock->i_stall = 1;
  ack (sock);
}

void
grub_net_tcp_unstall (grub_net_tcp_socket_t sock)
{
  if (!sock->i_stall)
    return;
  sock->i_stall = 0;
  ack (sock);
}
