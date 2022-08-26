 /*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019,2020  Free Software Foundation, Inc.
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
 *
 */

#include <grub/dl.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/disk.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/charset.h>
#include <grub/err.h>
#include <grub/extcmd.h>
#include <grub/file.h>
#include <grub/i18n.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/net.h>
#include <grub/types.h>
#include <grub/term.h>
#include <grub/lua.h>
#include <grub/usbdesc.h>
#include <grub/procfs.h>

GRUB_MOD_LICENSE ("GPLv3+");

typedef enum
{
  GRUB_EFI_USB_DATA_IN,
  GRUB_EFI_USB_DATA_OUT,
  GRUB_EFI_USB_NO_DATA,
} grub_efi_usb_data_direction;

typedef grub_efi_status_t (EFIAPI *grub_efi_async_usb_transfer_callback)
    (void *data, grub_efi_uintn_t len, void *context, grub_efi_uint32_t status);

struct grub_efi_usb_io
{
  // IO transfer
  grub_efi_status_t (*control_transfer) (struct grub_efi_usb_io *this,
                                         struct grub_usb_dev_request *request,
                                         grub_efi_usb_data_direction direction,
                                         grub_efi_uint32_t timeout,
                                         void *data,
                                         grub_efi_uintn_t len,
                                         grub_efi_uint32_t *status);

  grub_efi_status_t (*bulk_transfer) (struct grub_efi_usb_io *this,
                                      grub_efi_uint8_t dev_endpoint,
                                      void *data,
                                      grub_efi_uintn_t len,
                                      grub_efi_uint32_t timeout,
                                      grub_efi_uint32_t *status);

  grub_efi_status_t (*async_interrupt_transfer) (struct grub_efi_usb_io *this,
                        grub_efi_uint8_t dev_endpoint,
                        grub_efi_boolean_t is_new_transfer,
                        grub_efi_uintn_t polling_interval,
                        grub_efi_uintn_t len,
                        grub_efi_async_usb_transfer_callback interrupt_call_back,
                        void *context);

  grub_efi_status_t (*sync_interrupt_transfer) (struct grub_efi_usb_io *this,
                                                grub_efi_uint8_t dev_endpoint,
                                                void *data,
                                                grub_efi_uintn_t *len,
                                                grub_efi_uintn_t timeout,
                                                grub_efi_uint32_t *status);

  grub_efi_status_t (*isochronous_transfer) (struct grub_efi_usb_io *this,
                                             grub_efi_uint8_t dev_endpoint,
                                             void *data,
                                             grub_efi_uintn_t len,
                                             grub_efi_uint32_t *status);

  grub_efi_status_t (*async_isochronous_transfer) (struct grub_efi_usb_io *this,
                        grub_efi_uint8_t dev_endpoint,
                        void *data,
                        grub_efi_uintn_t len,
                        grub_efi_async_usb_transfer_callback isochronous_call_back,
                        void *context);

  // Common device request
  grub_efi_status_t (*get_device_desc) (struct grub_efi_usb_io *this,
                                        struct grub_usb_desc_device *device_desc);

  grub_efi_status_t (*get_config_desc) (struct grub_efi_usb_io *this,
                                        struct grub_usb_desc_config *config_desc);

  grub_efi_status_t (*get_if_desc) (struct grub_efi_usb_io *this,
                                    struct grub_usb_desc_if *if_desc);

  grub_efi_status_t (*get_endp_desc) (struct grub_efi_usb_io *this,
                                      grub_efi_uint8_t endpoint_index,
                                      struct grub_usb_desc_endp *endp_desc);

  grub_efi_status_t (*get_str_desc) (struct grub_efi_usb_io *this,
                                     grub_efi_uint16_t lang_id,
                                     grub_efi_uint8_t string_id,
                                     grub_efi_char16_t **string);

  grub_efi_status_t (*get_supported_lang) (struct grub_efi_usb_io *this,
                                           grub_efi_uint16_t **lang_id_table,
                                           grub_efi_uint16_t *table_size);

  // Reset controller's parent port
  grub_efi_status_t (*port_reset) (struct grub_efi_usb_io *this);
};
typedef struct grub_efi_usb_io grub_efi_usb_io_t;

#define LANG_ID_ENGLISH 0x0409

grub_efi_handle_t
grub_efi_bootpart (grub_efi_device_path_t *dp, const char *filename)
{
  char *text_dp = NULL;
  grub_efi_status_t status;
  grub_efi_handle_t image_handle = 0;
  grub_efi_device_path_t *boot_file = NULL;
  grub_efi_boot_services_t *b = grub_efi_system_table->boot_services;
  if (!dp)
  {
    grub_printf ("Invalid device path\n");
    return NULL;
  }
  boot_file = grub_efi_file_device_path (dp, filename);
  if (!boot_file)
  {
    grub_printf ("Invalid device path\n");
    return NULL;
  }
  text_dp = grub_efi_device_path_to_str (boot_file);
  grub_printf ("Load image %s\n", text_dp);
  status = efi_call_6 (b->load_image, TRUE, grub_efi_image_handle,
                       boot_file, NULL, 0, (void **)&image_handle);
  if (status != GRUB_EFI_SUCCESS)
  {
    grub_printf ("Failed to load image %s\n", text_dp);
    image_handle = NULL;
  }
  else
    grub_printf ("Will boot %s\n", text_dp);
  grub_free (text_dp);
  grub_free (boot_file);
  return image_handle;
}

grub_efi_handle_t
grub_efi_bootdisk (grub_efi_device_path_t *dp, const char *filename)
{
  grub_efi_status_t status;
  grub_efi_uintn_t count = 0;
  grub_efi_uintn_t i = 0;
  grub_efi_handle_t *buf = NULL;
  grub_efi_device_path_t *tmp_dp;
  grub_efi_handle_t image_handle = NULL;
  grub_efi_guid_t sfs_guid = GRUB_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

  grub_efi_boot_services_t *b = grub_efi_system_table->boot_services;

  status = efi_call_5 (b->locate_handle_buffer, GRUB_EFI_BY_PROTOCOL,
                       &sfs_guid, NULL, &count, &buf);
  if(status != GRUB_EFI_SUCCESS)
  {
    grub_printf ("SimpleFileSystemProtocol not found.\n");
    return NULL;
  }
  for (i = 0; i < count; i++)
  {
    tmp_dp = grub_efi_get_device_path (buf[i]);
    if (!grub_efi_is_child_dp (tmp_dp, dp))
      continue;
    image_handle = grub_efi_bootpart (tmp_dp, filename);
    if (image_handle)
      break;
  }
  return image_handle;
}

static grub_err_t
grub_cmd_dp (grub_extcmd_context_t ctxt __attribute__ ((unused)),
             int argc, char **args)
{
  if (argc != 1)
  {
    grub_efi_status_t status;
    grub_efi_guid_t guid = GRUB_EFI_LOADED_IMAGE_GUID;
    grub_efi_guid_t dp_guid = GRUB_EFI_DEVICE_PATH_GUID;
    grub_efi_boot_services_t *b = grub_efi_system_table->boot_services;
    grub_efi_loaded_image_t *img = NULL;
    grub_efi_device_path_protocol_t *dev = NULL;
    status = efi_call_3 (b->handle_protocol, grub_efi_image_handle,
                         &guid, (void **)&img);
    if (status != GRUB_EFI_SUCCESS)
      return grub_error (GRUB_ERR_BAD_OS, "loaded image protocol not found");
    status = efi_call_3 (b->handle_protocol, img->device_handle,
                         &dp_guid, (void **)&dev);
    if (status != GRUB_EFI_SUCCESS)
      return grub_error (GRUB_ERR_BAD_OS, "device path protocol not found");
    grub_printf ("DevicePath: ");
    grub_efi_print_device_path (dev);
    grub_printf ("\n");
    grub_efi_print_device_path (img->file_path);
    grub_printf ("\n");
    return GRUB_ERR_NONE;
  }
  char *text_dp = NULL;
  char *filename = NULL;
  char *devname = NULL;
  grub_device_t dev = 0;
  grub_efi_device_path_t *dp = NULL;
  grub_efi_device_path_t *file_dp = NULL;
  grub_efi_handle_t dev_handle = 0;
  int namelen = grub_strlen (args[0]);
  if (args[0][0] == '(' && args[0][namelen - 1] == ')')
  {
    args[0][namelen - 1] = 0;
    dev = grub_device_open (&args[0][1]);
  }
  else if (args[0][0] != '(' && args[0][0] != '/')
    dev = grub_device_open (args[0]);
  else
  {
    filename = args[0];
    devname = grub_file_get_device_name (args[0]);
    dev = grub_device_open (devname);
    if (devname)
      grub_free (devname);
  }

  if (dev->disk)
    dev_handle = grub_efidisk_get_device_handle (dev->disk);
  else if (dev->net && dev->net->server)
  {
    grub_net_network_level_address_t addr;
    struct grub_net_network_level_interface *inf;
    grub_net_network_level_address_t gateway;
    grub_err_t err;
    err = grub_net_resolve_address (dev->net->server, &addr);
    if (err)
      goto out;
    err = grub_net_route_address (addr, &gateway, &inf);
    if (err)
      goto out;
    dev_handle = grub_efinet_get_device_handle (inf->card);
  }
  if (dev_handle)
    dp = grub_efi_get_device_path (dev_handle);
  if (filename)
    file_dp = grub_efi_file_device_path (dp, filename);
  else
    file_dp = grub_efi_duplicate_device_path (dp);
out:
  grub_printf ("DevicePath: ");
  if (!file_dp)
    grub_printf ("NULL\n");
  else
  {
    text_dp = grub_efi_device_path_to_str (file_dp);
    grub_printf ("%s\n", text_dp);
    if (text_dp)
      grub_free (text_dp);
    grub_free (file_dp);
  }

  if (dev)
    grub_device_close (dev);
  return 0;
}

static grub_size_t
wcslen (const grub_efi_char16_t *str)
{
  grub_size_t len = 0;
  while (*(str++))
    len++;
  return len;
}

static char *
wcstostr (const grub_efi_char16_t *str)
{
  char *ret = NULL;
  grub_size_t len = wcslen (str), i;
  ret = grub_zalloc (len + 1);
  if (!ret)
    return NULL;
  for (i = 0; i < len; i++)
    ret[i] = str[i];
  return ret;
}

static grub_err_t
grub_cmd_usb (grub_extcmd_context_t ctxt __attribute__ ((unused)),
              int argc, char **args)
{
  if (argc != 1)
    return GRUB_ERR_BAD_ARGUMENT;
  char *devname = NULL;
  grub_device_t dev = 0;
  grub_efi_handle_t dev_handle = 0;
  grub_efi_status_t status;
  int namelen = grub_strlen (args[0]);
  grub_efi_guid_t usb_guid = GRUB_EFI_USB_IO_PROTOCOL_GUID;
  grub_efi_boot_services_t *b = grub_efi_system_table->boot_services;
  grub_efi_usb_io_t *usb_io;
  struct grub_usb_desc_device dev_desc;
  grub_efi_char16_t *str16;
  char *str;
  if (args[0][0] == '(' && args[0][namelen - 1] == ')')
  {
    args[0][namelen - 1] = 0;
    devname = &args[0][1];
  }
  else
    devname = args[0];

  dev = grub_device_open (devname);
  if (dev->disk)
    dev_handle = grub_efidisk_get_device_handle (dev->disk);
  if (dev)
    grub_device_close (dev);

  if (!dev_handle)
    return grub_error (GRUB_ERR_BAD_OS, "device handle not found");

  status = efi_call_3 (b->handle_protocol,
                       dev_handle, &usb_guid, (void **)&usb_io);
  if (status != GRUB_EFI_SUCCESS)
    return grub_error (GRUB_ERR_BAD_OS, "usb i/o protocol not found");
  grub_printf ("found usb i/o protocol: %p\n", usb_io);

  status = efi_call_2 (usb_io->get_device_desc, usb_io, &dev_desc);
  if (status != GRUB_EFI_SUCCESS)
    return grub_error (GRUB_ERR_BAD_OS, "failed to get device descriptor");

  grub_printf ("Vendor ID = %04X\nProduct ID = %04X\n",
               dev_desc.vendorid, dev_desc.prodid);

  status = efi_call_4 (usb_io->get_str_desc, usb_io,
                       LANG_ID_ENGLISH, dev_desc.strvendor, &str16);
  if (status != GRUB_EFI_SUCCESS)
    grub_printf ("Manufacturer : (null)\n");
  else
  {
    str = wcstostr (str16);
    grub_printf ("Manufacturer : %s\n", str);
    grub_free (str);
    efi_call_1 (b->free_pool, str16);
  }

  status = efi_call_4 (usb_io->get_str_desc, usb_io,
                       LANG_ID_ENGLISH, dev_desc.strprod, &str16);
  if (status != GRUB_EFI_SUCCESS)
    grub_printf ("Product : (null)\n");
  else
  {
    str = wcstostr (str16);
    grub_printf ("Product : %s\n", str);
    grub_free (str);
    efi_call_1 (b->free_pool, str16);
  }

  status = efi_call_4 (usb_io->get_str_desc, usb_io,
                       LANG_ID_ENGLISH, dev_desc.strserial, &str16);
  if (status != GRUB_EFI_SUCCESS)
    grub_printf ("Serial Number : (null)\n");
  else
  {
    str = wcstostr (str16);
    grub_printf ("Serial Number : %s\n", str);
    grub_free (str);
    efi_call_1 (b->free_pool, str16);
  }

  return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd_dp, cmd_usb;

static int
lua_efi_vendor (lua_State *state)
{
  char *vendor;
  grub_uint16_t *vendor16, *fv = grub_efi_system_table->firmware_vendor;

  for (vendor16 = fv; *vendor16; vendor16++)
    ;
  vendor = grub_malloc (4 * (vendor16 - fv + 1));
  if (!vendor)
    return 0;
  *grub_utf16_to_utf8 ((grub_uint8_t *) vendor, fv, vendor16 - fv) = 0;
  lua_pushstring (state, vendor);
  grub_free (vendor);
  return 1;
}

static int
lua_efi_version (lua_State *state)
{
  char uefi_ver[11];
  grub_efi_uint16_t uefi_major_rev =
            grub_efi_system_table->hdr.revision >> 16;
  grub_efi_uint16_t uefi_minor_rev =
            grub_efi_system_table->hdr.revision & 0xffff;
  grub_efi_uint8_t uefi_minor_1 = uefi_minor_rev / 10;
  grub_efi_uint8_t uefi_minor_2 = uefi_minor_rev % 10;
  grub_snprintf (uefi_ver, 10, "%d.%d", uefi_major_rev, uefi_minor_1);
  if (uefi_minor_2)
    grub_snprintf (uefi_ver, 10, "%s.%d", uefi_ver, uefi_minor_2);
  lua_pushstring (state, uefi_ver);
  return 1;
}

static int
lua_efi_getdp (lua_State *state)
{
  grub_disk_t disk;
  grub_efi_handle_t handle = 0;
  grub_efi_device_path_t *dp = NULL;
  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  disk = lua_touserdata (state, 1);
  handle = grub_efidisk_get_device_handle (disk);
  if (!handle)
    return 0;
  dp = grub_efi_get_device_path (handle);
  if (!dp)
    return 0;
  lua_pushlightuserdata (state, dp);
  return 1;
}

static int
lua_efi_dptostr (lua_State *state)
{
  char *str = NULL;
  grub_efi_device_path_t *dp;
  luaL_checktype (state, 1, LUA_TLIGHTUSERDATA);
  dp = lua_touserdata (state, 1);
  str = grub_efi_device_path_to_str (dp);
  if (!str)
    return 0;
  lua_pushstring (state, str);
  grub_free (str);
  return 1;
}

static luaL_Reg efilib[] =
{
  {"vendor", lua_efi_vendor},
  {"version", lua_efi_version},
  {"getdp", lua_efi_getdp},
  {"dptostr", lua_efi_dptostr},
  {0, 0}
};

struct systab_info
{
  char magic[8];
  char arch[8];
  grub_uint64_t systab;
  grub_uint64_t handle;
};

static char *
get_systab (grub_size_t *sz)
{
  struct systab_info *ret = NULL;
  *sz = sizeof (struct systab_info);
  ret = grub_zalloc (*sz);
  if (!ret)
    return NULL;
  grub_strncpy (ret->magic, "GRUB EFI", 8);
  grub_strncpy (ret->arch, GRUB_TARGET_CPU, 8);
  ret->systab = (grub_addr_t) grub_efi_system_table;
  ret->handle = (grub_addr_t) grub_efi_image_handle;
  return (char *) ret;
}

struct grub_procfs_entry proc_systab =
{
  .name = "systab",
  .get_contents = get_systab,
};

GRUB_MOD_INIT(dp)
{
  cmd_dp = grub_register_extcmd ("dp", grub_cmd_dp, 0, N_("DEVICE"),
                  N_("DevicePath."), 0);
  cmd_usb = grub_register_extcmd ("efiusb", grub_cmd_usb, 0, N_("DEVICE"),
                                  N_("USB info."), 0);

  if (grub_lua_global_state)
  {
    lua_gc (grub_lua_global_state, LUA_GCSTOP, 0);
    luaL_register (grub_lua_global_state, "efi", efilib);
    lua_gc (grub_lua_global_state, LUA_GCRESTART, 0);
  }
  grub_procfs_register ("systab", &proc_systab);
}

GRUB_MOD_FINI(dp)
{
  grub_unregister_extcmd (cmd_dp);
  grub_unregister_extcmd (cmd_usb);
  grub_procfs_unregister (&proc_systab);
}
