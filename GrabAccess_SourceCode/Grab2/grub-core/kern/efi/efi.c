/* efi.c - generic EFI support */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2008,2009,2010  Free Software Foundation, Inc.
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
#include <grub/charset.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/console_control.h>
#include <grub/efi/pe32.h>
#include <grub/time.h>
#include <grub/term.h>
#include <grub/kernel.h>
#include <grub/mm.h>
#include <grub/loader.h>

/* The handle of GRUB itself. Filled in by the startup code.  */
grub_efi_handle_t grub_efi_image_handle;

/* The pointer to a system table. Filled in by the startup code.  */
grub_efi_system_table_t *grub_efi_system_table;

grub_efi_uintn_t grub_efi_protocol_data_len;
void *grub_efi_protocol_data_addr;

static grub_efi_guid_t console_control_guid = GRUB_EFI_CONSOLE_CONTROL_GUID;
static grub_efi_guid_t loaded_image_guid = GRUB_EFI_LOADED_IMAGE_GUID;
static grub_efi_guid_t device_path_guid = GRUB_EFI_DEVICE_PATH_GUID;

void *
grub_efi_locate_protocol (grub_efi_guid_t *protocol, void *registration)
{
  void *interface;
  grub_efi_status_t status;

  status = efi_call_3 (grub_efi_system_table->boot_services->locate_protocol,
                       protocol, registration, &interface);
  if (status != GRUB_EFI_SUCCESS)
    return 0;

  return interface;
}

/* Return the array of handles which meet the requirement. If successful,
   the number of handles is stored in NUM_HANDLES. The array is allocated
   from the heap.  */
grub_efi_handle_t *
grub_efi_locate_handle (grub_efi_locate_search_type_t search_type,
			grub_efi_guid_t *protocol,
			void *search_key,
			grub_efi_uintn_t *num_handles)
{
  grub_efi_boot_services_t *b;
  grub_efi_status_t status;
  grub_efi_handle_t *buffer;
  grub_efi_uintn_t buffer_size = 8 * sizeof (grub_efi_handle_t);

  buffer = grub_malloc (buffer_size);
  if (! buffer)
    return 0;

  b = grub_efi_system_table->boot_services;
  status = efi_call_5 (b->locate_handle, search_type, protocol, search_key,
			     &buffer_size, buffer);
  if (status == GRUB_EFI_BUFFER_TOO_SMALL)
    {
      grub_free (buffer);
      buffer = grub_malloc (buffer_size);
      if (! buffer)
	return 0;

      status = efi_call_5 (b->locate_handle, search_type, protocol, search_key,
				 &buffer_size, buffer);
    }

  if (status != GRUB_EFI_SUCCESS)
    {
      grub_free (buffer);
      return 0;
    }

  *num_handles = buffer_size / sizeof (grub_efi_handle_t);
  return buffer;
}

void *
grub_efi_open_protocol (grub_efi_handle_t handle,
			grub_efi_guid_t *protocol,
			grub_efi_uint32_t attributes)
{
  grub_efi_boot_services_t *b;
  grub_efi_status_t status;
  void *interface;

  b = grub_efi_system_table->boot_services;
  status = efi_call_6 (b->open_protocol, handle,
		       protocol,
		       &interface,
		       grub_efi_image_handle,
		       0,
		       attributes);
  if (status != GRUB_EFI_SUCCESS)
    return 0;

  return interface;
}

grub_efi_status_t
grub_efi_close_protocol (grub_efi_handle_t handle, grub_efi_guid_t *protocol)
{
  grub_efi_boot_services_t *b = grub_efi_system_table->boot_services;

  return efi_call_4 (b->close_protocol, handle, protocol, grub_efi_image_handle, NULL);
}

int
grub_efi_set_text_mode (int on)
{
  grub_efi_console_control_protocol_t *c;
  grub_efi_screen_mode_t mode, new_mode;

  c = grub_efi_locate_protocol (&console_control_guid, 0);
  if (! c)
    /* No console control protocol instance available, assume it is
       already in text mode. */
    return 1;

  if (efi_call_4 (c->get_mode, c, &mode, 0, 0) != GRUB_EFI_SUCCESS)
    return 0;

  new_mode = on ? GRUB_EFI_SCREEN_TEXT : GRUB_EFI_SCREEN_GRAPHICS;
  if (mode != new_mode)
    if (efi_call_2 (c->set_mode, c, new_mode) != GRUB_EFI_SUCCESS)
      return 0;

  return 1;
}

void
grub_efi_stall (grub_efi_uintn_t microseconds)
{
  efi_call_1 (grub_efi_system_table->boot_services->stall, microseconds);
}

grub_efi_loaded_image_t *
grub_efi_get_loaded_image (grub_efi_handle_t image_handle)
{
  return grub_efi_open_protocol (image_handle,
				 &loaded_image_guid,
				 GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
}

void
grub_reboot (void)
{
  grub_machine_fini (GRUB_LOADER_FLAG_NORETURN |
		     GRUB_LOADER_FLAG_EFI_KEEP_ALLOCATED_MEMORY);
  efi_call_4 (grub_efi_system_table->runtime_services->reset_system,
              GRUB_EFI_RESET_COLD, GRUB_EFI_SUCCESS, 0, NULL);
  for (;;) ;
}

void
grub_exit (int retval)
{
  int rc = (int) GRUB_EFI_LOAD_ERROR;

  if (retval == 0)
    rc = GRUB_EFI_SUCCESS;

  grub_machine_fini (GRUB_LOADER_FLAG_NORETURN);
  efi_call_4 (grub_efi_system_table->boot_services->exit,
              grub_efi_image_handle, rc, 0, 0);
  for (;;) ;
}

grub_err_t
grub_efi_set_virtual_address_map (grub_efi_uintn_t memory_map_size,
				  grub_efi_uintn_t descriptor_size,
				  grub_efi_uint32_t descriptor_version,
				  grub_efi_memory_descriptor_t *virtual_map)
{
  grub_efi_runtime_services_t *r;
  grub_efi_status_t status;

  r = grub_efi_system_table->runtime_services;
  status = efi_call_4 (r->set_virtual_address_map, memory_map_size,
		       descriptor_size, descriptor_version, virtual_map);

  if (status == GRUB_EFI_SUCCESS)
    return GRUB_ERR_NONE;

  return grub_error (GRUB_ERR_IO, "set_virtual_address_map failed");
}

grub_efi_status_t
grub_efi_set_var_attr(const char *var, const grub_efi_guid_t *guid,
                      void *data, grub_size_t datasize, grub_efi_uint32_t attr)
{
  grub_efi_status_t status;
  grub_efi_runtime_services_t *r;
  grub_efi_char16_t *var16;
  grub_size_t len, len16;

  len = grub_strlen (var);
  len16 = len * GRUB_MAX_UTF16_PER_UTF8;
  var16 = grub_calloc (len16 + 1, sizeof (var16[0]));
  if (!var16)
    return grub_errno;
  len16 = grub_utf8_to_utf16 (var16, len16, (grub_uint8_t *) var, len, NULL);
  var16[len16] = 0;

  r = grub_efi_system_table->runtime_services;

  status = efi_call_5 (r->set_variable, var16, guid, attr, datasize, data);
  grub_free (var16);
  return status;
}

grub_err_t
grub_efi_set_variable(const char *var, const grub_efi_guid_t *guid,
		      void *data, grub_size_t datasize)
{
  grub_efi_status_t status;
  status = grub_efi_set_var_attr(var, guid, data, datasize,
                                 (GRUB_EFI_VARIABLE_NON_VOLATILE
                                 | GRUB_EFI_VARIABLE_BOOTSERVICE_ACCESS
                                 | GRUB_EFI_VARIABLE_RUNTIME_ACCESS));

  if (status == GRUB_EFI_SUCCESS)
    return GRUB_ERR_NONE;

  if (status == GRUB_EFI_NOT_FOUND && datasize == 0)
    return GRUB_ERR_NONE;

  return grub_error (GRUB_ERR_IO, "could not set EFI variable `%s'", var);
}

grub_efi_status_t
grub_efi_get_variable_with_attributes (const char *var,
				       const grub_efi_guid_t *guid,
				       grub_size_t *datasize_out,
				       void **data_out,
				       grub_efi_uint32_t *attributes)
{
  grub_efi_status_t status;
  grub_efi_uintn_t datasize = 0;
  grub_efi_runtime_services_t *r;
  grub_efi_char16_t *var16;
  void *data;
  grub_size_t len, len16;

  *data_out = NULL;
  *datasize_out = 0;

  len = grub_strlen (var);
  len16 = len * GRUB_MAX_UTF16_PER_UTF8;
  var16 = grub_calloc (len16 + 1, sizeof (var16[0]));
  if (!var16)
    return GRUB_EFI_OUT_OF_RESOURCES;
  len16 = grub_utf8_to_utf16 (var16, len16, (grub_uint8_t *) var, len, NULL);
  var16[len16] = 0;

  r = grub_efi_system_table->runtime_services;

  status = efi_call_5 (r->get_variable, var16, guid, NULL, &datasize, NULL);

  if (status != GRUB_EFI_BUFFER_TOO_SMALL || !datasize)
    {
      grub_free (var16);
      return status;
    }

  data = grub_malloc (datasize);
  if (!data)
    {
      grub_free (var16);
      return GRUB_EFI_OUT_OF_RESOURCES;
    }

  status = efi_call_5 (r->get_variable, var16, guid, attributes, &datasize, data);
  grub_free (var16);

  if (status == GRUB_EFI_SUCCESS)
    {
      *data_out = data;
      *datasize_out = datasize;
      return status;
    }

  grub_free (data);
  return status;
}

grub_efi_status_t
grub_efi_get_variable (const char *var, const grub_efi_guid_t *guid,
		       grub_size_t *datasize_out, void **data_out)
{
  return grub_efi_get_variable_with_attributes (var, guid, datasize_out, data_out, NULL);
}

grub_efi_status_t
grub_efi_allocate_pool (grub_efi_memory_type_t pool_type,
                        grub_efi_uintn_t buffer_size, void **buffer)
{
  grub_efi_boot_services_t *b;
  grub_efi_status_t status;

  b = grub_efi_system_table->boot_services;
  status = efi_call_3 (b->allocate_pool, pool_type, buffer_size, buffer);
  return status;
}

grub_efi_status_t
grub_efi_free_pool (void *buffer)
{
  grub_efi_boot_services_t *b;
  grub_efi_status_t status;

  b = grub_efi_system_table->boot_services;
  status = efi_call_1 (b->free_pool, buffer);
  return status;
}

#pragma GCC diagnostic ignored "-Wcast-align"

/* Search the mods section from the PE32/PE32+ image. This code uses
   a PE32 header, but should work with PE32+ as well.  */
grub_addr_t
grub_efi_modules_addr (void)
{
  grub_efi_loaded_image_t *image;
  struct grub_pe32_header *header;
  struct grub_pe32_coff_header *coff_header;
  struct grub_pe32_section_table *sections;
  struct grub_pe32_section_table *section;
  struct grub_module_info *info;
  grub_uint16_t i;

  image = grub_efi_get_loaded_image (grub_efi_image_handle);
  if (! image)
    return 0;

  header = image->image_base;
  coff_header = &(header->coff_header);
  sections
    = (struct grub_pe32_section_table *) ((char *) coff_header
					  + sizeof (*coff_header)
					  + coff_header->optional_header_size);

  for (i = 0, section = sections;
       i < coff_header->num_sections;
       i++, section++)
    {
      if (grub_strcmp (section->name, "mods") == 0)
	break;
    }

  if (i == coff_header->num_sections)
    {
      grub_dprintf("sections", "section %d is last section; invalid.\n", i);
      return 0;
    }

  info = (struct grub_module_info *) ((char *) image->image_base
				      + section->virtual_address);
  if (section->name[0] != '.' && info->magic != GRUB_MODULE_MAGIC)
    {
      grub_dprintf("sections",
		   "section %d has bad magic %08x, should be %08x\n",
		   i, info->magic, GRUB_MODULE_MAGIC);
      return 0;
    }

  grub_dprintf("sections", "returning section info for section %d: \"%s\"\n",
	       i, section->name);
  return (grub_addr_t) info;
}

#pragma GCC diagnostic error "-Wcast-align"

char *
grub_efi_get_filename (grub_efi_device_path_t *dp0)
{
  char *name = 0, *p, *pi;
  grub_size_t filesize = 0;
  grub_efi_device_path_t *dp;

  if (!dp0)
    return NULL;

  dp = dp0;

  while (dp)
    {
      grub_efi_uint8_t type = GRUB_EFI_DEVICE_PATH_TYPE (dp);
      grub_efi_uint8_t subtype = GRUB_EFI_DEVICE_PATH_SUBTYPE (dp);

      if (type == GRUB_EFI_END_DEVICE_PATH_TYPE)
	break;
      if (type == GRUB_EFI_MEDIA_DEVICE_PATH_TYPE
	       && subtype == GRUB_EFI_FILE_PATH_DEVICE_PATH_SUBTYPE)
	{
	  grub_efi_uint16_t len = GRUB_EFI_DEVICE_PATH_LENGTH (dp);

	  if (len < 4)
	    {
	      grub_error (GRUB_ERR_OUT_OF_RANGE,
			  "malformed EFI Device Path node has length=%d", len);
	      return NULL;
	    }
	  len = (len - 4) / sizeof (grub_efi_char16_t);
	  filesize += GRUB_MAX_UTF8_PER_UTF16 * len + 2;
	}

      dp = GRUB_EFI_NEXT_DEVICE_PATH (dp);
    }

  if (!filesize)
    return NULL;

  dp = dp0;

  p = name = grub_malloc (filesize);
  if (!name)
    return NULL;

  while (dp)
    {
      grub_efi_uint8_t type = GRUB_EFI_DEVICE_PATH_TYPE (dp);
      grub_efi_uint8_t subtype = GRUB_EFI_DEVICE_PATH_SUBTYPE (dp);

      if (type == GRUB_EFI_END_DEVICE_PATH_TYPE)
	break;
      else if (type == GRUB_EFI_MEDIA_DEVICE_PATH_TYPE
	       && subtype == GRUB_EFI_FILE_PATH_DEVICE_PATH_SUBTYPE)
	{
	  grub_efi_file_path_device_path_t *fp;
	  grub_efi_uint16_t len;
	  grub_efi_char16_t *dup_name;

	  *p++ = '/';

	  len = GRUB_EFI_DEVICE_PATH_LENGTH (dp);
	  if (len < 4)
	    {
	      grub_error (GRUB_ERR_OUT_OF_RANGE,
			  "malformed EFI Device Path node has length=%d", len);
	      return NULL;
	    }

	  len = (len - 4) / sizeof (grub_efi_char16_t);
	  fp = (grub_efi_file_path_device_path_t *) dp;
	  /* According to EFI spec Path Name is NULL terminated */
	  while (len > 0 && fp->path_name[len - 1] == 0)
	    len--;

	  dup_name = grub_calloc (len, sizeof (*dup_name));
	  if (!dup_name)
	    {
	      grub_free (name);
	      return NULL;
	    }
	  p = (char *) grub_utf16_to_utf8 ((unsigned char *) p,
					    grub_memcpy (dup_name, fp->path_name, len * sizeof (*dup_name)),
					    len);
	  grub_free (dup_name);
	}

      dp = GRUB_EFI_NEXT_DEVICE_PATH (dp);
    }

  *p = '\0';

  for (pi = name, p = name; *pi;)
    {
      /* EFI breaks paths with backslashes.  */
      if (*pi == '\\' || *pi == '/')
	{
	  *p++ = '/';
	  while (*pi == '\\' || *pi == '/')
	    pi++;
	  continue;
	}
      *p++ = *pi++;
    }
  *p = '\0';

  return name;
}

grub_efi_device_path_t *
grub_efi_get_device_path (grub_efi_handle_t handle)
{
  return grub_efi_open_protocol (handle, &device_path_guid,
				 GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
}

/* Return the device path node right before the end node.  */
grub_efi_device_path_t *
grub_efi_find_last_device_path (const grub_efi_device_path_t *dp)
{
  grub_efi_device_path_t *next, *p;

  if (GRUB_EFI_END_ENTIRE_DEVICE_PATH (dp))
    return 0;

  for (p = (grub_efi_device_path_t *) dp, next = GRUB_EFI_NEXT_DEVICE_PATH (p);
       ! GRUB_EFI_END_ENTIRE_DEVICE_PATH (next);
       p = next, next = GRUB_EFI_NEXT_DEVICE_PATH (next))
    ;

  return p;
}

/* Duplicate a device path.  */
grub_efi_device_path_t *
grub_efi_duplicate_device_path (const grub_efi_device_path_t *dp)
{
  grub_efi_device_path_t *p;
  grub_size_t total_size = 0;

  for (p = (grub_efi_device_path_t *) dp;
       ;
       p = GRUB_EFI_NEXT_DEVICE_PATH (p))
    {
      grub_size_t len = GRUB_EFI_DEVICE_PATH_LENGTH (p);

      /*
       * In the event that we find a node that's completely garbage, for
       * example if we get to 0x7f 0x01 0x02 0x00 ... (EndInstance with a size
       * of 2), GRUB_EFI_END_ENTIRE_DEVICE_PATH() will be true and
       * GRUB_EFI_NEXT_DEVICE_PATH() will return NULL, so we won't continue,
       * and neither should our consumers, but there won't be any error raised
       * even though the device path is junk.
       *
       * This keeps us from passing junk down back to our caller.
       */
      if (len < 4)
	{
	  grub_error (GRUB_ERR_OUT_OF_RANGE,
		      "malformed EFI Device Path node has length=%" PRIuGRUB_SIZE, len);
	  return NULL;
	}

      total_size += len;
      if (GRUB_EFI_END_ENTIRE_DEVICE_PATH (p))
	break;
    }

  p = grub_malloc (total_size);
  if (! p)
    return 0;

  grub_memcpy (p, dp, total_size);
  return p;
}

static char *
dump_vendor_path (grub_efi_vendor_device_path_t *vendor)
{
  grub_uint32_t vendor_data_len = vendor->header.length - sizeof (*vendor);
  char data[4];
  char *str = NULL;
  str = grub_malloc (55 + vendor_data_len * 3);
  grub_snprintf (str, 55,
           "Vendor(%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x)[%x: ",
           (unsigned) vendor->vendor_guid.data1,
           (unsigned) vendor->vendor_guid.data2,
           (unsigned) vendor->vendor_guid.data3,
           (unsigned) vendor->vendor_guid.data4[0],
           (unsigned) vendor->vendor_guid.data4[1],
           (unsigned) vendor->vendor_guid.data4[2],
           (unsigned) vendor->vendor_guid.data4[3],
           (unsigned) vendor->vendor_guid.data4[4],
           (unsigned) vendor->vendor_guid.data4[5],
           (unsigned) vendor->vendor_guid.data4[6],
           (unsigned) vendor->vendor_guid.data4[7],
           vendor_data_len);
  if (vendor->header.length > sizeof (*vendor))
  {
    grub_uint32_t i;
    for (i = 0; i < vendor_data_len; i++)
    {
      grub_snprintf (data, 4, "%02x ", vendor->vendor_defined_data[i]);
      grub_strcpy (str + grub_strlen (str), data);
    }
  }
  grub_strcpy (str + grub_strlen (str), "]");
  return str;
}

char *
grub_efi_device_path_to_str (grub_efi_device_path_t *dp)
{
  char *text_dp = NULL;
  while (GRUB_EFI_DEVICE_PATH_VALID (dp))
  {
    char *node = NULL;
    grub_efi_uint8_t type = GRUB_EFI_DEVICE_PATH_TYPE (dp);
    grub_efi_uint8_t subtype = GRUB_EFI_DEVICE_PATH_SUBTYPE (dp);
    grub_efi_uint16_t len = GRUB_EFI_DEVICE_PATH_LENGTH (dp);
    switch (type)
    {
      case GRUB_EFI_END_DEVICE_PATH_TYPE:
        switch (subtype)
        {
          case GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE:
            node = grub_xasprintf ("/EndEntire");
            break;
          case GRUB_EFI_END_THIS_DEVICE_PATH_SUBTYPE:
            node = grub_xasprintf ("/EndThis");
            break;
          default:
            node = grub_xasprintf ("/EndUnknown(%x)", (unsigned) subtype);
            break;
        }
        break;

      case GRUB_EFI_HARDWARE_DEVICE_PATH_TYPE:
        switch (subtype)
        {
          case GRUB_EFI_PCI_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_pci_device_path_t *pci =
                    (grub_efi_pci_device_path_t *) dp;
            node = grub_xasprintf ("/PCI(%x,%x)",
                        (unsigned) pci->function, (unsigned) pci->device);
          }
            break;
          case GRUB_EFI_PCCARD_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_pccard_device_path_t *pccard =
                    (grub_efi_pccard_device_path_t *) dp;
            node = grub_xasprintf ("/PCCARD(%x)", (unsigned) pccard->function);
          }
            break;
          case GRUB_EFI_MEMORY_MAPPED_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_memory_mapped_device_path_t *mmapped =
                    (grub_efi_memory_mapped_device_path_t *) dp;
            node = grub_xasprintf ("/MMap(%x,%llx,%llx)",
                          (unsigned) mmapped->memory_type,
                          (unsigned long long) mmapped->start_address,
                          (unsigned long long) mmapped->end_address);
          }
            break;
          case GRUB_EFI_VENDOR_DEVICE_PATH_SUBTYPE:
          {
            char *tmp_str = NULL;
            tmp_str = dump_vendor_path ((grub_efi_vendor_device_path_t *) dp);
            node = grub_xasprintf ("/Hardware%s", tmp_str);
            if (tmp_str)
              grub_free (tmp_str);
          }
            break;
          case GRUB_EFI_CONTROLLER_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_controller_device_path_t *controller =
                    (grub_efi_controller_device_path_t *) dp;
            node = grub_xasprintf ("/Ctrl(%x)",
                                   (unsigned) controller->controller_number);
          }
            break;
          default:
            node = grub_xasprintf ("/UnknownHW(%x)", (unsigned) subtype);
            break;
        }
        break;

      case GRUB_EFI_ACPI_DEVICE_PATH_TYPE:
        switch (subtype)
        {
          case GRUB_EFI_ACPI_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_acpi_device_path_t *acpi =
                    (grub_efi_acpi_device_path_t *) dp;
            node = grub_xasprintf ("/ACPI(%x,%x)",
                                   (unsigned) acpi->hid, (unsigned) acpi->uid);
          }
            break;
          case GRUB_EFI_EXPANDED_ACPI_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_expanded_acpi_device_path_t *eacpi =
                    (grub_efi_expanded_acpi_device_path_t *) dp;
            char *hidstr = NULL, *uidstr = NULL, *cidstr = NULL;

            if (GRUB_EFI_EXPANDED_ACPI_HIDSTR (dp)[0] == '\0')
              hidstr = grub_xasprintf ("%x,", (unsigned) eacpi->hid);
            else
              hidstr = grub_xasprintf ("%s,", GRUB_EFI_EXPANDED_ACPI_HIDSTR (dp));

            if (GRUB_EFI_EXPANDED_ACPI_UIDSTR (dp)[0] == '\0')
              uidstr = grub_xasprintf ("%x,", (unsigned) eacpi->uid);
            else
              uidstr = grub_xasprintf ("%s,", GRUB_EFI_EXPANDED_ACPI_UIDSTR (dp));

            if (GRUB_EFI_EXPANDED_ACPI_CIDSTR (dp)[0] == '\0')
              cidstr = grub_xasprintf ("%x)", (unsigned) eacpi->cid);
            else
              cidstr = grub_xasprintf ("%s)", GRUB_EFI_EXPANDED_ACPI_CIDSTR (dp));
            node = grub_xasprintf ("/ACPI(%s%s%s", hidstr, uidstr, cidstr);
            if (hidstr) grub_free (hidstr);
            if (uidstr) grub_free (uidstr);
            if (cidstr) grub_free (cidstr);
          }
            break;
          default:
            node = grub_xasprintf ("/UnknownACPI(%x)", (unsigned) subtype);
            break;
        }
        break;

      case GRUB_EFI_MESSAGING_DEVICE_PATH_TYPE:
        switch (subtype)
        {
          case GRUB_EFI_ATAPI_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_atapi_device_path_t *atapi =
                    (grub_efi_atapi_device_path_t *) dp;
            node = grub_xasprintf ("/ATAPI(%x,%x,%x)",
                    (unsigned) atapi->primary_secondary,
                    (unsigned) atapi->slave_master,
                    (unsigned) atapi->lun);
          }
            break;
          case GRUB_EFI_SCSI_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_scsi_device_path_t *scsi =
                    (grub_efi_scsi_device_path_t *) dp;
            node = grub_xasprintf ("/SCSI(%x,%x)",
                  (unsigned) scsi->pun,
                  (unsigned) scsi->lun);
          }
            break;
          case GRUB_EFI_FIBRE_CHANNEL_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_fibre_channel_device_path_t *fc =
                    (grub_efi_fibre_channel_device_path_t *) dp;
            node = grub_xasprintf ("/FibreChannel(%llx,%llx)",
                  (unsigned long long) fc->wwn,
                  (unsigned long long) fc->lun);
          }
            break;
          case GRUB_EFI_1394_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_1394_device_path_t *firewire =
                    (grub_efi_1394_device_path_t *) dp;
            node = grub_xasprintf ("/1394(%llx)", (unsigned long long)firewire->guid);
          }
            break;
          case GRUB_EFI_USB_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_usb_device_path_t *usb =
                    (grub_efi_usb_device_path_t *) dp;
            node = grub_xasprintf ("/USB(%x,%x)",
                  (unsigned) usb->parent_port_number,
                  (unsigned) usb->usb_interface);
          }
            break;
          case GRUB_EFI_USB_CLASS_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_usb_class_device_path_t *usb_class =
                    (grub_efi_usb_class_device_path_t *) dp;
            node = grub_xasprintf ("/USBClass(%x,%x,%x,%x,%x)",
                  (unsigned) usb_class->vendor_id,
                  (unsigned) usb_class->product_id,
                  (unsigned) usb_class->device_class,
                  (unsigned) usb_class->device_subclass,
                  (unsigned) usb_class->device_protocol);
          }
            break;
          case GRUB_EFI_I2O_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_i2o_device_path_t *i2o =
                    (grub_efi_i2o_device_path_t *) dp;
            node = grub_xasprintf ("/I2O(%x)", (unsigned) i2o->tid);
          }
            break;
          case GRUB_EFI_MAC_ADDRESS_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_mac_address_device_path_t *mac =
                    (grub_efi_mac_address_device_path_t *) dp;
            node = grub_xasprintf ("/MacAddr(%02x:%02x:%02x:%02x:%02x:%02x,%x)",
                  (unsigned) mac->mac_address[0],
                  (unsigned) mac->mac_address[1],
                  (unsigned) mac->mac_address[2],
                  (unsigned) mac->mac_address[3],
                  (unsigned) mac->mac_address[4],
                  (unsigned) mac->mac_address[5],
                  (unsigned) mac->if_type);
          }
            break;
          case GRUB_EFI_IPV4_DEVICE_PATH_SUBTYPE:
          {
            char ipv4_dp1[60];
            char ipv4_dp2[35];
            grub_efi_ipv4_device_path_t *ipv4 =
                    (grub_efi_ipv4_device_path_t *) dp;
            grub_snprintf (ipv4_dp1, 60,
                  "(%u.%u.%u.%u,%u.%u.%u.%u,%u,%u,%4x,%2x",
                  (unsigned) ipv4->local_ip_address[0],
                  (unsigned) ipv4->local_ip_address[1],
                  (unsigned) ipv4->local_ip_address[2],
                  (unsigned) ipv4->local_ip_address[3],
                  (unsigned) ipv4->remote_ip_address[0],
                  (unsigned) ipv4->remote_ip_address[1],
                  (unsigned) ipv4->remote_ip_address[2],
                  (unsigned) ipv4->remote_ip_address[3],
                  (unsigned) ipv4->local_port,
                  (unsigned) ipv4->remote_port,
                  (unsigned) ipv4->protocol,
                  (unsigned) ipv4->static_ip_address);
            if (len == sizeof (*ipv4))
            {
              grub_snprintf (ipv4_dp2, 35, ",%u.%u.%u.%u,%u.%u.%u.%u)",
                  (unsigned) ipv4->gateway_ip_address[0],
                  (unsigned) ipv4->gateway_ip_address[1],
                  (unsigned) ipv4->gateway_ip_address[2],
                  (unsigned) ipv4->gateway_ip_address[3],
                  (unsigned) ipv4->subnet_mask[0],
                  (unsigned) ipv4->subnet_mask[1],
                  (unsigned) ipv4->subnet_mask[2],
                  (unsigned) ipv4->subnet_mask[3]);
            }
            else
              grub_strcpy (ipv4_dp2, ")");
            node = grub_xasprintf ("/IPv4%s%s", ipv4_dp1, ipv4_dp2);
          }
            break;
          case GRUB_EFI_IPV6_DEVICE_PATH_SUBTYPE:
          {
            char ipv6_dp1[70];
            char ipv6_dp2[30];
            grub_efi_ipv6_device_path_t *ipv6 =
                    (grub_efi_ipv6_device_path_t *) dp;
            grub_snprintf (ipv6_dp1, 70,
                    "(%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x,%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x,%u,%u,%x,%x",
                  (unsigned) grub_be_to_cpu16 (ipv6->local_ip_address[0]),
                  (unsigned) grub_be_to_cpu16 (ipv6->local_ip_address[1]),
                  (unsigned) grub_be_to_cpu16 (ipv6->local_ip_address[2]),
                  (unsigned) grub_be_to_cpu16 (ipv6->local_ip_address[3]),
                  (unsigned) grub_be_to_cpu16 (ipv6->local_ip_address[4]),
                  (unsigned) grub_be_to_cpu16 (ipv6->local_ip_address[5]),
                  (unsigned) grub_be_to_cpu16 (ipv6->local_ip_address[6]),
                  (unsigned) grub_be_to_cpu16 (ipv6->local_ip_address[7]),
                  (unsigned) grub_be_to_cpu16 (ipv6->remote_ip_address[0]),
                  (unsigned) grub_be_to_cpu16 (ipv6->remote_ip_address[1]),
                  (unsigned) grub_be_to_cpu16 (ipv6->remote_ip_address[2]),
                  (unsigned) grub_be_to_cpu16 (ipv6->remote_ip_address[3]),
                  (unsigned) grub_be_to_cpu16 (ipv6->remote_ip_address[4]),
                  (unsigned) grub_be_to_cpu16 (ipv6->remote_ip_address[5]),
                  (unsigned) grub_be_to_cpu16 (ipv6->remote_ip_address[6]),
                  (unsigned) grub_be_to_cpu16 (ipv6->remote_ip_address[7]),
                  (unsigned) ipv6->local_port,
                  (unsigned) ipv6->remote_port,
                  (unsigned) ipv6->protocol,
                  (unsigned) ipv6->static_ip_address);
            if (len == sizeof (*ipv6))
            {
              grub_snprintf (ipv6_dp2, 30,
                            ",%u,%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x)",
              (unsigned) ipv6->prefix_length,
              (unsigned) grub_be_to_cpu16 (ipv6->gateway_ip_address[0]),
              (unsigned) grub_be_to_cpu16 (ipv6->gateway_ip_address[1]),
              (unsigned) grub_be_to_cpu16 (ipv6->gateway_ip_address[2]),
              (unsigned) grub_be_to_cpu16 (ipv6->gateway_ip_address[3]),
              (unsigned) grub_be_to_cpu16 (ipv6->gateway_ip_address[4]),
              (unsigned) grub_be_to_cpu16 (ipv6->gateway_ip_address[5]),
              (unsigned) grub_be_to_cpu16 (ipv6->gateway_ip_address[6]),
              (unsigned) grub_be_to_cpu16 (ipv6->gateway_ip_address[7]));
            }
            else
              grub_strcpy (ipv6_dp2, ")");
            node = grub_xasprintf ("/IPv6%s%s", ipv6_dp1, ipv6_dp2);
          }
            break;
          case GRUB_EFI_INFINIBAND_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_infiniband_device_path_t *ib =
                    (grub_efi_infiniband_device_path_t *) dp;
            node = grub_xasprintf ("/InfiniBand(%x,%llx,%llx,%llx)",
                  (unsigned) ib->port_gid[0], /* XXX */
                  (unsigned long long) ib->remote_id,
                  (unsigned long long) ib->target_port_id,
                  (unsigned long long) ib->device_id);
          }
            break;
          case GRUB_EFI_UART_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_uart_device_path_t *uart =
                    (grub_efi_uart_device_path_t *) dp;
            node = grub_xasprintf ("/UART(%llu,%u,%x,%x)",
                  (unsigned long long) uart->baud_rate,
                  uart->data_bits,
                  uart->parity,
                  uart->stop_bits);
          }
            break;
          case GRUB_EFI_SATA_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_sata_device_path_t *sata;
            sata = (grub_efi_sata_device_path_t *) dp;
            node = grub_xasprintf ("/Sata(%x,%x,%x)",
                  sata->hba_port,
                  sata->multiplier_port,
                  sata->lun);
          }
            break;

          case GRUB_EFI_VENDOR_MESSAGING_DEVICE_PATH_SUBTYPE:
          {
            char *tmp_str = NULL;
            tmp_str = dump_vendor_path ((grub_efi_vendor_device_path_t *) dp);
            node = grub_xasprintf ("/Messaging%s", tmp_str);
            if (tmp_str)
              grub_free (tmp_str);
          }
            break;
          case GRUB_EFI_URI_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_uri_device_path_t *uri = (grub_efi_uri_device_path_t *) dp;
            node = grub_xasprintf ("/URI(%s)", uri->uri);
          }
            break;
          case GRUB_EFI_DNS_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_dns_device_path_t *dns =
                    (grub_efi_dns_device_path_t *) dp;
            char ip_str[25];
            if (dns->is_ipv6)
            {
              grub_snprintf (ip_str, 25, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
                (grub_uint16_t)(grub_be_to_cpu32(dns->dns_server_ip[0].addr[0]) >> 16),
                (grub_uint16_t)(grub_be_to_cpu32(dns->dns_server_ip[0].addr[0])),
                (grub_uint16_t)(grub_be_to_cpu32(dns->dns_server_ip[0].addr[1]) >> 16),
                (grub_uint16_t)(grub_be_to_cpu32(dns->dns_server_ip[0].addr[1])),
                (grub_uint16_t)(grub_be_to_cpu32(dns->dns_server_ip[0].addr[2]) >> 16),
                (grub_uint16_t)(grub_be_to_cpu32(dns->dns_server_ip[0].addr[2])),
                (grub_uint16_t)(grub_be_to_cpu32(dns->dns_server_ip[0].addr[3]) >> 16),
                (grub_uint16_t)(grub_be_to_cpu32(dns->dns_server_ip[0].addr[3])));
            }
            else
            {
              grub_snprintf (ip_str, 25, "%d.%d.%d.%d",
                dns->dns_server_ip[0].v4.addr[0],
                dns->dns_server_ip[0].v4.addr[1],
                dns->dns_server_ip[0].v4.addr[2],
                dns->dns_server_ip[0].v4.addr[3]);
            }
            node = grub_xasprintf ("/DNS(%s)", ip_str);
          }
            break;
          default:
            node = grub_xasprintf ("/UnknownMessaging(%x)", (unsigned) subtype);
            break;
        }
        break;

      case GRUB_EFI_MEDIA_DEVICE_PATH_TYPE:
        switch (subtype)
        {
          case GRUB_EFI_HARD_DRIVE_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_hard_drive_device_path_t *hd =
                    (grub_efi_hard_drive_device_path_t *) dp;
            node = grub_xasprintf
                  ("/HD(%u,%llx,%llx,%02x%02x%02x%02x%02x%02x%02x%02x,%x,%x)",
                  hd->partition_number,
                  (unsigned long long) hd->partition_start,
                  (unsigned long long) hd->partition_size,
                  (unsigned) hd->partition_signature[0],
                  (unsigned) hd->partition_signature[1],
                  (unsigned) hd->partition_signature[2],
                  (unsigned) hd->partition_signature[3],
                  (unsigned) hd->partition_signature[4],
                  (unsigned) hd->partition_signature[5],
                  (unsigned) hd->partition_signature[6],
                  (unsigned) hd->partition_signature[7],
                  (unsigned) hd->partmap_type,
                  (unsigned) hd->signature_type);
          }
            break;
          case GRUB_EFI_CDROM_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_cdrom_device_path_t *cd =
                    (grub_efi_cdrom_device_path_t *) dp;
            node = grub_xasprintf ("/CD(%u,%llx,%llx)",
                  cd->boot_entry,
                  (unsigned long long) cd->partition_start,
                  (unsigned long long) cd->partition_size);
          }
            break;
          case GRUB_EFI_VENDOR_MEDIA_DEVICE_PATH_SUBTYPE:
          {
            char *tmp_str = NULL;
            tmp_str = dump_vendor_path ((grub_efi_vendor_device_path_t *) dp);
            node = grub_xasprintf ("/Media%s", tmp_str);
            if (tmp_str)
              grub_free (tmp_str);
          }
            break;
          case GRUB_EFI_FILE_PATH_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_file_path_device_path_t *fp;
            grub_uint8_t *buf;
            fp = (grub_efi_file_path_device_path_t *) dp;
            buf = grub_zalloc ((len - 4) * 2 + 1);
            if (buf)
            {
              grub_efi_char16_t *dup_name = grub_zalloc (len - 4);
              if (!dup_name)
              {
                grub_errno = GRUB_ERR_NONE;
                node = grub_xasprintf ("/File((null))");
                grub_free (buf);
                break;
              }
              *grub_utf16_to_utf8 (buf, grub_memcpy (dup_name, fp->path_name, len - 4),
                        (len - 4) / sizeof (grub_efi_char16_t))
                = '\0';
              grub_free (dup_name);
            }
            else
              grub_errno = GRUB_ERR_NONE;
            node = grub_xasprintf ("/File(%s)", buf);
            grub_free (buf);
          }
            break;
          case GRUB_EFI_PROTOCOL_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_protocol_device_path_t *proto =
                    (grub_efi_protocol_device_path_t *) dp;
            node = grub_xasprintf
                  ("/Protocol(%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x)",
                  (unsigned) proto->guid.data1,
                  (unsigned) proto->guid.data2,
                  (unsigned) proto->guid.data3,
                  (unsigned) proto->guid.data4[0],
                  (unsigned) proto->guid.data4[1],
                  (unsigned) proto->guid.data4[2],
                  (unsigned) proto->guid.data4[3],
                  (unsigned) proto->guid.data4[4],
                  (unsigned) proto->guid.data4[5],
                  (unsigned) proto->guid.data4[6],
                  (unsigned) proto->guid.data4[7]);
          }
            break;
          default:
            node = grub_xasprintf ("/UnknownMedia(%x)", (unsigned) subtype);
            break;
          }
        break;

      case GRUB_EFI_BIOS_DEVICE_PATH_TYPE:
        switch (subtype)
        {
          case GRUB_EFI_BIOS_DEVICE_PATH_SUBTYPE:
          {
            grub_efi_bios_device_path_t *bios =
                    (grub_efi_bios_device_path_t *) dp;
            node = grub_xasprintf ("/BIOS(%x,%x,%s)",
                  (unsigned) bios->device_type,
                  (unsigned) bios->status_flags,
                  (char *) (dp + 1));
          }
            break;
          default:
            node = grub_xasprintf ("/UnknownBIOS(%x)", (unsigned) subtype);
            break;
          }
        break;

      default:
      {
        node = grub_xasprintf ("/UnknownType(%x,%x)", (unsigned) type,
                                (unsigned) subtype);
        char *str = text_dp;
        text_dp = grub_zalloc (grub_strlen (text_dp) + grub_strlen(node) + 1);
        if (str)
        {
          grub_strcpy (text_dp, str);
          grub_free (str);
        }
        grub_strcat (text_dp, node);
        if (node)
          grub_free (node);
        return text_dp;
      }
        break;
    }
    char *str = text_dp;
    text_dp = grub_zalloc (grub_strlen (text_dp) + grub_strlen(node) + 1);
    if (str)
    {
      grub_strcpy (text_dp, str);
      grub_free (str);
    }
    grub_strcat (text_dp, node);
    if (node)
      grub_free (node);
    if (GRUB_EFI_END_ENTIRE_DEVICE_PATH (dp))
      break;

    dp = (grub_efi_device_path_t *) ((char *) dp + len);
  }
  return text_dp;
}

/* Print the chain of Device Path nodes. This is mainly for debugging. */
void
grub_efi_print_device_path (grub_efi_device_path_t *dp)
{
  char *text_dp = NULL;
  if (!dp)
    return;
  text_dp = grub_efi_device_path_to_str (dp);
  if (!text_dp)
    return;
  grub_printf ("%s", text_dp);
  grub_free (text_dp);
}

/* GUID */
grub_efi_guid_t *
grub_efi_copy_guid (grub_efi_guid_t *dest, const grub_efi_guid_t *src)
{
  grub_set_unaligned64 ((grub_efi_uint64_t *)dest,
                        grub_get_unaligned64 ((const grub_efi_uint64_t *)src));
  grub_set_unaligned64 ((grub_efi_uint64_t *)dest + 1,
                        grub_get_unaligned64 ((const grub_efi_uint64_t*)src + 1));
  return dest;
}

grub_efi_boolean_t
grub_efi_compare_guid (const grub_efi_guid_t *g1, const grub_efi_guid_t *g2)
{
  grub_efi_uint64_t g1_low, g2_low;
  grub_efi_uint64_t g1_high, g2_high;
  g1_low = grub_get_unaligned64 ((const grub_efi_uint64_t *)g1);
  g2_low = grub_get_unaligned64 ((const grub_efi_uint64_t *)g2);
  g1_high = grub_get_unaligned64 ((const grub_efi_uint64_t *)g1 + 1);
  g2_high = grub_get_unaligned64 ((const grub_efi_uint64_t *)g2 + 1);
  return (grub_efi_boolean_t) (g1_low == g2_low && g1_high == g2_high);
}

/* Compare device paths.  */
int
grub_efi_compare_device_paths (const grub_efi_device_path_t *dp1,
			       const grub_efi_device_path_t *dp2)
{
  if (! dp1 || ! dp2)
    /* Return non-zero.  */
    return 1;

  if (dp1 == dp2)
    return 0;

  while (GRUB_EFI_DEVICE_PATH_VALID (dp1) && GRUB_EFI_DEVICE_PATH_VALID (dp2))
    {
      grub_efi_uint8_t type1, type2;
      grub_efi_uint8_t subtype1, subtype2;
      grub_efi_uint16_t len1, len2;
      int ret;

      type1 = GRUB_EFI_DEVICE_PATH_TYPE (dp1);
      type2 = GRUB_EFI_DEVICE_PATH_TYPE (dp2);

      if (type1 != type2)
	return (int) type2 - (int) type1;

      subtype1 = GRUB_EFI_DEVICE_PATH_SUBTYPE (dp1);
      subtype2 = GRUB_EFI_DEVICE_PATH_SUBTYPE (dp2);

      if (subtype1 != subtype2)
	return (int) subtype1 - (int) subtype2;

      len1 = GRUB_EFI_DEVICE_PATH_LENGTH (dp1);
      len2 = GRUB_EFI_DEVICE_PATH_LENGTH (dp2);

      if (len1 != len2)
	return (int) len1 - (int) len2;

      ret = grub_memcmp (dp1, dp2, len1);
      if (ret != 0)
	return ret;

      if (GRUB_EFI_END_ENTIRE_DEVICE_PATH (dp1))
	break;

      dp1 = (grub_efi_device_path_t *) ((char *) dp1 + len1);
      dp2 = (grub_efi_device_path_t *) ((char *) dp2 + len2);
    }

  /*
   * There's no "right" answer here, but we probably don't want to call a valid
   * dp and an invalid dp equal, so pick one way or the other.
   */
  if (GRUB_EFI_DEVICE_PATH_VALID (dp1) && !GRUB_EFI_DEVICE_PATH_VALID (dp2))
    return 1;
  else if (!GRUB_EFI_DEVICE_PATH_VALID (dp1) && GRUB_EFI_DEVICE_PATH_VALID (dp2))
    return -1;

  return 0;
}

grub_err_t
copy_file_path (grub_efi_file_path_device_path_t *fp,
		const char *str, grub_efi_uint16_t len)
{
  grub_efi_char16_t *p, *path_name;
  grub_efi_uint16_t size;

  fp->header.type = GRUB_EFI_MEDIA_DEVICE_PATH_TYPE;
  fp->header.subtype = GRUB_EFI_FILE_PATH_DEVICE_PATH_SUBTYPE;

  path_name = grub_calloc (len, GRUB_MAX_UTF16_PER_UTF8 * sizeof (*path_name));
  if (!path_name)
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, "failed to allocate path buffer");

  size = grub_utf8_to_utf16 (path_name, len * GRUB_MAX_UTF16_PER_UTF8,
			     (const grub_uint8_t *) str, len, 0);
  for (p = path_name; p < path_name + size; p++)
    if (*p == '/')
      *p = '\\';

  grub_memcpy (fp->path_name, path_name, size * sizeof (*fp->path_name));
  /* File Path is NULL terminated */
  fp->path_name[size++] = '\0';
  fp->header.length = size * sizeof (grub_efi_char16_t) + sizeof (*fp);
  grub_free (path_name);
  return GRUB_ERR_NONE;
}

grub_efi_device_path_t *
grub_efi_file_device_path (grub_efi_device_path_t *dp, const char *filename)
{
  char *dir_start;
  char *dir_end;
  grub_size_t size;
  grub_efi_device_path_t *d;
  grub_efi_device_path_t *file_path;

  dir_start = grub_strchr (filename, ')');
  if (! dir_start)
    dir_start = (char *) filename;
  else
    dir_start++;

  dir_end = grub_strrchr (dir_start, '/');
  if (! dir_end)
    {
      grub_error (GRUB_ERR_BAD_FILENAME, "invalid EFI file path");
      return 0;
    }

  size = 0;
  d = dp;
  while (d)
    {
      grub_size_t len = GRUB_EFI_DEVICE_PATH_LENGTH (d);

      if (len < 4)
	{
	  grub_error (GRUB_ERR_OUT_OF_RANGE,
		      "malformed EFI Device Path node has length=%" PRIuGRUB_SIZE, len);
	  return NULL;
	}

      size += len;
      if ((GRUB_EFI_END_ENTIRE_DEVICE_PATH (d)))
	break;
      d = GRUB_EFI_NEXT_DEVICE_PATH (d);
    }

  /* File Path is NULL terminated. Allocate space for 2 extra characters */
  /* FIXME why we split path in two components? */
  file_path = grub_malloc (size
			   + ((grub_strlen (dir_start) + 2)
			      * GRUB_MAX_UTF16_PER_UTF8
			      * sizeof (grub_efi_char16_t))
			   + sizeof (grub_efi_file_path_device_path_t) * 2);
  if (! file_path)
    return 0;

  grub_memcpy (file_path, dp, size);

  /* Fill the file path for the directory.  */
  d = (grub_efi_device_path_t *) ((char *) file_path
				  + ((char *) d - (char *) dp));

  if (copy_file_path ((grub_efi_file_path_device_path_t *) d,
		      dir_start, dir_end - dir_start) != GRUB_ERR_NONE)
    {
 fail:
      grub_free (file_path);
      return 0;
    }

  /* Fill the file path for the file.  */
  d = GRUB_EFI_NEXT_DEVICE_PATH (d);
  if (copy_file_path ((grub_efi_file_path_device_path_t *) d,
		      dir_end + 1, grub_strlen (dir_end + 1)) != GRUB_ERR_NONE)
    goto fail;

  /* Fill the end of device path nodes.  */
  d = GRUB_EFI_NEXT_DEVICE_PATH (d);
  d->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
  d->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
  d->length = sizeof (*d);

  return file_path;
}

static grub_efi_uintn_t
device_path_node_length (const void *node)
{
  return grub_get_unaligned16 ((grub_efi_uint16_t *)
                              &((grub_efi_device_path_protocol_t *)(node))->length);
}

static void
set_device_path_node_length (void *node, grub_efi_uintn_t len)
{
  grub_set_unaligned16 ((grub_efi_uint16_t *)
                        &((grub_efi_device_path_protocol_t *)(node))->length,
                        (grub_efi_uint16_t)(len));
}

grub_efi_uintn_t
grub_efi_get_dp_size (const grub_efi_device_path_protocol_t *dp)
{
  grub_efi_device_path_t *p;
  grub_efi_uintn_t total_size = 0;
  for (p = (grub_efi_device_path_t *) dp; ; p = GRUB_EFI_NEXT_DEVICE_PATH (p))
  {
    total_size += GRUB_EFI_DEVICE_PATH_LENGTH (p);
    if (GRUB_EFI_END_ENTIRE_DEVICE_PATH (p))
      break;
  }
  return total_size;
}

grub_efi_device_path_protocol_t*
grub_efi_create_device_node (grub_efi_uint8_t node_type, grub_efi_uintn_t node_subtype,
                    grub_efi_uint16_t node_length)
{
  grub_efi_device_path_protocol_t *dp;
  if (node_length < sizeof (grub_efi_device_path_protocol_t))
    return NULL;
  dp = grub_zalloc (node_length);
  if (dp != NULL)
  {
    dp->type = node_type;
    dp->subtype = node_subtype;
    set_device_path_node_length (dp, node_length);
  }
  return dp;
}

grub_efi_device_path_protocol_t*
grub_efi_append_device_path (const grub_efi_device_path_protocol_t *dp1,
                    const grub_efi_device_path_protocol_t *dp2)
{
  grub_efi_uintn_t size;
  grub_efi_uintn_t size1;
  grub_efi_uintn_t size2;
  grub_efi_device_path_protocol_t *new_dp;
  grub_efi_device_path_protocol_t *tmp_dp;
  // If there's only 1 path, just duplicate it.
  if (dp1 == NULL)
  {
    if (dp2 == NULL)
      return grub_efi_create_device_node (GRUB_EFI_END_DEVICE_PATH_TYPE,
                                 GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE,
                                 sizeof (grub_efi_device_path_protocol_t));
    else
      return grub_efi_duplicate_device_path (dp2);
  }
  if (dp2 == NULL)
    grub_efi_duplicate_device_path (dp1);
  // Allocate space for the combined device path. It only has one end node of
  // length EFI_DEVICE_PATH_PROTOCOL.
  size1 = grub_efi_get_dp_size (dp1);
  size2 = grub_efi_get_dp_size (dp2);
  size = size1 + size2 - sizeof (grub_efi_device_path_protocol_t);
  new_dp = grub_malloc (size);

  if (new_dp != NULL)
  {
    new_dp = grub_memcpy (new_dp, dp1, size1);
    // Over write FirstDevicePath EndNode and do the copy
    tmp_dp = (grub_efi_device_path_protocol_t *)
           ((char *) new_dp + (size1 - sizeof (grub_efi_device_path_protocol_t)));
    grub_memcpy (tmp_dp, dp2, size2);
  }
  return new_dp;
}

grub_efi_device_path_protocol_t*
grub_efi_append_device_node (const grub_efi_device_path_protocol_t *device_path,
                    const grub_efi_device_path_protocol_t *device_node)
{
  grub_efi_device_path_protocol_t *tmp_dp;
  grub_efi_device_path_protocol_t *next_node;
  grub_efi_device_path_protocol_t *new_dp;
  grub_efi_uintn_t node_length;
  if (device_node == NULL)
  {
    if (device_path == NULL)
      return grub_efi_create_device_node (GRUB_EFI_END_DEVICE_PATH_TYPE,
                                 GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE,
                                 sizeof (grub_efi_device_path_protocol_t));
    else
      return grub_efi_duplicate_device_path (device_path);
  }
  // Build a Node that has a terminator on it
  node_length = device_path_node_length (device_node);

  tmp_dp = grub_malloc (node_length + sizeof (grub_efi_device_path_protocol_t));
  if (tmp_dp == NULL)
    return NULL;
  tmp_dp = grub_memcpy (tmp_dp, device_node, node_length);
  // Add and end device path node to convert Node to device path
  next_node = GRUB_EFI_NEXT_DEVICE_PATH (tmp_dp);
  next_node->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
  next_node->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
  next_node->length = sizeof (grub_efi_device_path_protocol_t);
  // Append device paths
  new_dp = grub_efi_append_device_path (device_path, tmp_dp);
  grub_free (tmp_dp);
  return new_dp;
}

int
grub_efi_is_child_dp (const grub_efi_device_path_t *child,
                      const grub_efi_device_path_t *parent)
{
  grub_efi_device_path_t *dp, *ldp;
  int ret = 0;

  dp = grub_efi_duplicate_device_path (child);
  if (! dp)
    return 0;

  while (!ret)
  {
    ldp = grub_efi_find_last_device_path (dp);
    if (!ldp)
      break;

    ldp->type = GRUB_EFI_END_DEVICE_PATH_TYPE;
    ldp->subtype = GRUB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
    ldp->length = sizeof (*ldp);

    ret = (grub_efi_compare_device_paths (dp, parent) == 0);
  }

  grub_free (dp);
  return ret;
}
