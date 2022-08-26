#ifndef GRUB_NET_EFI_HEADER
#define GRUB_NET_EFI_HEADER	1

#include <grub/efi/api.h>
#include <grub/efi/http.h>
#include <grub/efi/dhcp.h>
#include <grub/command.h>

typedef struct grub_efi_net_interface grub_efi_net_interface_t;
typedef struct grub_efi_net_ip_config grub_efi_net_ip_config_t;
typedef union grub_efi_net_ip_address grub_efi_net_ip_address_t;
typedef struct grub_efi_net_ip_manual_address grub_efi_net_ip_manual_address_t;

struct grub_efi_net_interface
{
  char *name;
  int prefer_ip6;
  struct grub_efi_net_device *dev;
  struct grub_efi_net_io *io;
  grub_efi_net_ip_config_t *ip_config;
  int io_type;
  struct grub_efi_net_interface *next;
};

#define efi_net_interface_get_hw_address(inf) inf->ip_config->get_hw_address (inf->dev)
#define efi_net_interface_get_address(inf) inf->ip_config->get_address (inf->dev)
#define efi_net_interface_get_route_table(inf) inf->ip_config->get_route_table (inf->dev)
#define efi_net_interface_set_address(inf, addr, with_subnet) inf->ip_config->set_address (inf->dev, addr, with_subnet)
#define efi_net_interface_set_gateway(inf, addr) inf->ip_config->set_gateway (inf->dev, addr)
#define efi_net_interface_set_dns(inf, addr) inf->ip_config->set_dns (inf->dev, addr)

struct grub_efi_net_ip_config
{
  char * (*get_hw_address) (struct grub_efi_net_device *dev);
  char * (*get_address) (struct grub_efi_net_device *dev);
  char ** (*get_route_table) (struct grub_efi_net_device *dev);
  grub_efi_net_interface_t * (*best_interface) (struct grub_efi_net_device *dev, grub_efi_net_ip_address_t *address);
  int (*set_address) (struct grub_efi_net_device *dev, grub_efi_net_ip_manual_address_t *net_ip, int with_subnet);
  int (*set_gateway) (struct grub_efi_net_device *dev, grub_efi_net_ip_address_t *address);
  int (*set_dns) (struct grub_efi_net_device *dev, grub_efi_net_ip_address_t *dns);
};

union grub_efi_net_ip_address
{
  grub_efi_ipv4_address_t ip4;
  grub_efi_ipv6_address_t ip6;
};

struct grub_efi_net_ip_manual_address
{
  int is_ip6;
  union
  {
    grub_efi_ip4_config2_manual_address_t ip4;
    grub_efi_ip6_config_manual_address_t ip6;
  };
};

struct grub_efi_net_device
{
  grub_efi_handle_t handle;
  grub_efi_ip4_config2_protocol_t *ip4_config;
  grub_efi_ip6_config_protocol_t *ip6_config;
  grub_efi_handle_t http_handle;
  grub_efi_http_t *http;
  grub_efi_handle_t ip4_pxe_handle;
  grub_efi_pxe_t *ip4_pxe;
  grub_efi_handle_t ip6_pxe_handle;
  grub_efi_pxe_t *ip6_pxe;
  grub_efi_handle_t dhcp4_handle;
  grub_efi_dhcp4_protocol_t *dhcp4;
  grub_efi_handle_t dhcp6_handle;
  grub_efi_dhcp6_protocol_t *dhcp6;
  char *card_name;
  grub_efi_net_interface_t *net_interfaces;
  struct grub_efi_net_device *next;
};

struct grub_efi_net_io
{
  void (*configure) (struct grub_efi_net_device *dev, int prefer_ip6);
  grub_err_t (*open) (struct grub_efi_net_device *dev,
		    int prefer_ip6,
		    grub_file_t file,
		    const char *filename,
		    int type);
  grub_ssize_t (*read) (struct grub_efi_net_device *dev,
			int prefer_ip6,
			grub_file_t file,
			char *buf,
			grub_size_t len);
  grub_err_t (*close) (struct grub_efi_net_device *dev,
		      int prefer_ip6,
		      grub_file_t file);
};

extern struct grub_efi_net_device *net_devices;

extern struct grub_efi_net_io io_http;
extern struct grub_efi_net_io io_pxe;

extern grub_efi_net_ip_config_t *efi_net_ip4_config;
extern grub_efi_net_ip_config_t *efi_net_ip6_config;

char *
grub_efi_ip4_address_to_string (grub_efi_ipv4_address_t *address);

char *
grub_efi_ip6_address_to_string (grub_efi_pxe_ipv6_address_t *address);

char *
grub_efi_hw_address_to_string (grub_efi_uint32_t hw_address_size, grub_efi_mac_address_t hw_address);

int
grub_efi_string_to_ip4_address (const char *val, grub_efi_ipv4_address_t *address, const char **rest);

int
grub_efi_string_to_ip6_address (const char *val, grub_efi_ipv6_address_t *address, const char **rest);

char *
grub_efi_ip6_interface_name (struct grub_efi_net_device *dev);

char *
grub_efi_ip4_interface_name (struct grub_efi_net_device *dev);

grub_efi_net_interface_t *
grub_efi_net_create_interface (struct grub_efi_net_device *dev,
		const char *interface_name,
		grub_efi_net_ip_manual_address_t *net_ip,
		int has_subnet);

int grub_efi_net_fs_init (void);
void grub_efi_net_fs_fini (void);
int grub_efi_net_boot_from_https (void);
int grub_efi_net_boot_from_opa (void);

extern grub_command_func_t grub_efi_net_list_routes;
extern grub_command_func_t grub_efi_net_list_cards;
extern grub_command_func_t grub_efi_net_list_addrs;
extern grub_command_func_t grub_efi_net_add_addr;
extern grub_command_func_t grub_efi_net_bootp;
extern grub_command_func_t grub_efi_net_bootp6;

#endif /* ! GRUB_NET_EFI_HEADER */
