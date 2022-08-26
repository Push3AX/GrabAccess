#include <grub/dl.h>
#include <grub/env.h>
#define EFI_NET_CMD_PREFIX "net_efi"
#include <grub/net/efi.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_command_t cmd_efi_lsroutes;
static grub_command_t cmd_efi_lscards;
static grub_command_t cmd_efi_lsaddrs;
static grub_command_t cmd_efi_addaddr;
static grub_command_t cmd_efi_bootp;
static grub_command_t cmd_efi_bootp6;

static int initialized;

GRUB_MOD_INIT(efi_netfs)
{
  if (grub_net_open)
    return;

  if (grub_efi_net_fs_init ())
    {
      cmd_efi_lsroutes = grub_register_command ("net_efi_ls_routes", grub_efi_net_list_routes,
					    "", N_("list network routes"));
      cmd_efi_lscards = grub_register_command ("net_efi_ls_cards", grub_efi_net_list_cards,
					   "", N_("list network cards"));
      cmd_efi_lsaddrs = grub_register_command ("net_efi_ls_addr", grub_efi_net_list_addrs,
					  "", N_("list network addresses"));
      cmd_efi_addaddr = grub_register_command ("net_efi_add_addr", grub_efi_net_add_addr,
					  N_("SHORTNAME CARD ADDRESS [HWADDRESS]"),
					  N_("Add a network address."));
      cmd_efi_bootp = grub_register_command ("net_efi_bootp", grub_efi_net_bootp,
					 N_("[CARD]"),
					 N_("perform a bootp autoconfiguration"));
      cmd_efi_bootp6 = grub_register_command ("net_efi_bootp6", grub_efi_net_bootp6,
					 N_("[CARD]"),
					 N_("perform a bootp autoconfiguration"));
      initialized = 1;
    }
}

GRUB_MOD_FINI(efi_netfs)
{
  if (initialized)
    {
      grub_unregister_command (cmd_efi_lsroutes);
      grub_unregister_command (cmd_efi_lscards);
      grub_unregister_command (cmd_efi_lsaddrs);
      grub_unregister_command (cmd_efi_addaddr);
      grub_unregister_command (cmd_efi_bootp);
      grub_unregister_command (cmd_efi_bootp6);
      grub_efi_net_fs_fini ();
      initialized = 0;
      return;
    }
}
