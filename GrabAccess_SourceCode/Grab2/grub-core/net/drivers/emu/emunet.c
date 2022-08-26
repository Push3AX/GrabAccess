/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010,2011,2012,2013  Free Software Foundation, Inc.
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

#include <grub/dl.h>
#include <grub/net/netbuff.h>
#include <grub/net.h>
#include <grub/term.h>
#include <grub/i18n.h>
#include <grub/emu/net.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_err_t 
send_card_buffer (struct grub_net_card *dev __attribute__ ((unused)),
		  struct grub_net_buff *pack);

static struct grub_net_buff *
get_card_packet (struct grub_net_card *dev __attribute__ ((unused)));

static struct grub_net_card_driver emudriver = 
  {
    .name = "emu",
    .send = send_card_buffer,
    .recv = get_card_packet
  };

static struct grub_net_card emucard = 
  {
    .name = "emu0",
    .driver = &emudriver,
    .mtu = 1500,
    .default_address = {
			 .type = GRUB_NET_LINK_LEVEL_PROTOCOL_ETHERNET,
			 . len = 6,
			 {.mac = {0, 1, 2, 3, 4, 5}}
		       },
    .flags = 0
  };

static grub_err_t 
send_card_buffer (struct grub_net_card *dev __attribute__ ((unused)),
		  struct grub_net_buff *pack)
{
  grub_ssize_t actual;

  actual = grub_emunet_send (pack->data, pack->tail - pack->data);
  if (actual < 0)
    return grub_error (GRUB_ERR_IO, N_("couldn't send network packet"));

  return GRUB_ERR_NONE;
}

static struct grub_net_buff *
get_card_packet (struct grub_net_card *dev __attribute__ ((unused)))
{
  grub_ssize_t actual;
  struct grub_net_buff *nb;

  nb = grub_netbuff_alloc (emucard.mtu + 36 + 2);
  if (!nb)
    return NULL;

  /* Reserve 2 bytes so that 2 + 14/18 bytes of ethernet header is divisible
     by 4. So that IP header is aligned on 4 bytes. */
  grub_netbuff_reserve (nb, 2);
  if (!nb)
    {
      grub_netbuff_free (nb);
      return NULL;
    }

  actual = grub_emunet_receive (nb->data, emucard.mtu + 36);
  if (actual < 0)
    {
      grub_netbuff_free (nb);
      return NULL;
    }
  grub_netbuff_put (nb, actual);

  return nb;
}

static int registered = 0;

GRUB_MOD_INIT(emunet)
{
  if (!grub_emunet_create (&emucard.mtu))
    {
      grub_net_card_register (&emucard);
      registered = 1;
    }
}

GRUB_MOD_FINI(emunet)
{
  if (registered)
    {
      grub_emunet_close ();
      grub_net_card_unregister (&emucard);
      registered = 0;
    }
}
