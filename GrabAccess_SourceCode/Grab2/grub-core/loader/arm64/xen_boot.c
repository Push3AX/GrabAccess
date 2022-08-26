/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2014  Free Software Foundation, Inc.
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

#include <grub/cache.h>
#include <grub/charset.h>
#include <grub/command.h>
#include <grub/err.h>
#include <grub/file.h>
#include <grub/fdt.h>
#include <grub/list.h>
#include <grub/loader.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/types.h>
#include <grub/cpu/linux.h>
#include <grub/efi/efi.h>
#include <grub/efi/fdtload.h>
#include <grub/efi/memory.h>
#include <grub/efi/pe32.h>	/* required by struct xen_hypervisor_header */
#include <grub/i18n.h>
#include <grub/lib/cmdline.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define XEN_HYPERVISOR_NAME  "xen_hypervisor"
#define MODULE_CUSTOM_COMPATIBLE  "multiboot,module"

/* This maximum size is defined in Power.org ePAPR V1.1
 * https://www.power.org/documentation/epapr-version-1-1/
 * 2.2.1.1 Node Name Requirements
 * node-name@unit-address
 * 31 + 1(@) + 16(64bit address in hex format) + 1(\0) = 49
 */
#define FDT_NODE_NAME_MAX_SIZE  (49)

struct compat_string_struct
{
  grub_size_t size;
  const char *compat_string;
};
typedef struct compat_string_struct compat_string_struct_t;
#define FDT_COMPATIBLE(x) {.size = sizeof(x), .compat_string = (x)}

enum module_type
{
  MODULE_IMAGE,
  MODULE_INITRD,
  MODULE_XSM,
  MODULE_CUSTOM
};
typedef enum module_type module_type_t;

struct xen_hypervisor_header
{
  struct linux_arm64_kernel_header efi_head;

  /* This is always PE\0\0.  */
  grub_uint8_t signature[GRUB_PE32_SIGNATURE_SIZE];
  /* The COFF file header.  */
  struct grub_pe32_coff_header coff_header;
  /* The Optional header.  */
  struct grub_pe64_optional_header optional_header;
};

struct xen_boot_binary
{
  struct xen_boot_binary *next;
  struct xen_boot_binary **prev;
  int is_hypervisor;

  grub_addr_t start;
  grub_size_t size;
  grub_size_t align;

  char *cmdline;
  int cmdline_size;
};

static grub_dl_t my_mod;

static int loaded;

static struct xen_boot_binary *xen_hypervisor;
static struct xen_boot_binary *module_head;

static __inline grub_addr_t
xen_boot_address_align (grub_addr_t start, grub_size_t align)
{
  return (align ? (ALIGN_UP (start, align)) : start);
}

static grub_err_t
prepare_xen_hypervisor_params (void *xen_boot_fdt)
{
  int chosen_node = 0;
  int retval;

  chosen_node = grub_fdt_find_subnode (xen_boot_fdt, 0, "chosen");
  if (chosen_node < 0)
    chosen_node = grub_fdt_add_subnode (xen_boot_fdt, 0, "chosen");
  if (chosen_node < 1)
    return grub_error (GRUB_ERR_IO, "failed to get chosen node in FDT");

  /*
   * The address and size are always written using 64-bits value. Set
   * #address-cells and #size-cells accordingly.
   */
  retval = grub_fdt_set_prop32 (xen_boot_fdt, chosen_node, "#address-cells", 2);
  if (retval)
    return grub_error (GRUB_ERR_IO, "failed to set #address-cells");
  retval = grub_fdt_set_prop32 (xen_boot_fdt, chosen_node, "#size-cells", 2);
  if (retval)
    return grub_error (GRUB_ERR_IO, "failed to set #size-cells");

  grub_dprintf ("xen_loader",
		"Xen Hypervisor cmdline : %s @ %p size:%d\n",
		xen_hypervisor->cmdline, xen_hypervisor->cmdline,
		xen_hypervisor->cmdline_size);

  retval = grub_fdt_set_prop (xen_boot_fdt, chosen_node, "bootargs",
			      xen_hypervisor->cmdline,
			      xen_hypervisor->cmdline_size);
  if (retval)
    return grub_error (GRUB_ERR_IO, "failed to install/update FDT");

  return GRUB_ERR_NONE;
}

static grub_err_t
prepare_xen_module_params (struct xen_boot_binary *module, void *xen_boot_fdt)
{
  int retval, chosen_node = 0, module_node = 0;
  char module_name[FDT_NODE_NAME_MAX_SIZE];

  retval = grub_snprintf (module_name, FDT_NODE_NAME_MAX_SIZE, "module@%lx",
			  xen_boot_address_align (module->start,
						  module->align));
  grub_dprintf ("xen_loader", "Module node name %s \n", module_name);

  if (retval < (int) sizeof ("module@"))
    return grub_error (GRUB_ERR_IO, N_("failed to get FDT"));

  chosen_node = grub_fdt_find_subnode (xen_boot_fdt, 0, "chosen");
  if (chosen_node < 0)
    chosen_node = grub_fdt_add_subnode (xen_boot_fdt, 0, "chosen");
  if (chosen_node < 1)
    return grub_error (GRUB_ERR_IO, "failed to get chosen node in FDT");

  module_node =
    grub_fdt_find_subnode (xen_boot_fdt, chosen_node, module_name);
  if (module_node < 0)
    module_node =
      grub_fdt_add_subnode (xen_boot_fdt, chosen_node, module_name);

  retval = grub_fdt_set_prop (xen_boot_fdt, module_node, "compatible",
			      MODULE_CUSTOM_COMPATIBLE, sizeof(MODULE_CUSTOM_COMPATIBLE));
  if (retval)
    return grub_error (GRUB_ERR_IO, "failed to update FDT");

  grub_dprintf ("xen_loader", "Module\n");

  retval = grub_fdt_set_reg64 (xen_boot_fdt, module_node,
			       xen_boot_address_align (module->start,
						       module->align),
			       module->size);
  if (retval)
    return grub_error (GRUB_ERR_IO, "failed to update FDT");

  if (module->cmdline && module->cmdline_size > 0)
    {
      grub_dprintf ("xen_loader",
		    "Module cmdline : %s @ %p size:%d\n",
		    module->cmdline, module->cmdline, module->cmdline_size);

      retval = grub_fdt_set_prop (xen_boot_fdt, module_node, "bootargs",
				  module->cmdline, module->cmdline_size + 1);
      if (retval)
	return grub_error (GRUB_ERR_IO, "failed to update FDT");
    }
  else
    {
      grub_dprintf ("xen_loader", "Module has no bootargs!\n");
    }

  return GRUB_ERR_NONE;
}

static grub_err_t
finalize_params_xen_boot (void)
{
  struct xen_boot_binary *module;
  void *xen_boot_fdt;
  grub_size_t additional_size = 0x1000;

  /* Hypervisor.  */
  additional_size += FDT_NODE_NAME_MAX_SIZE + xen_hypervisor->cmdline_size;
  FOR_LIST_ELEMENTS (module, module_head)
  {
    additional_size += 6 * FDT_NODE_NAME_MAX_SIZE + sizeof(MODULE_CUSTOM_COMPATIBLE) - 1
      + module->cmdline_size;
  }

  xen_boot_fdt = grub_fdt_load (additional_size);
  if (!xen_boot_fdt)
    return grub_error (GRUB_ERR_IO, "failed to get FDT");

  if (xen_hypervisor)
    {
      if (prepare_xen_hypervisor_params (xen_boot_fdt) != GRUB_ERR_NONE)
	goto fail;
    }
  else
    {
      grub_dprintf ("xen_loader", "Failed to get Xen Hypervisor info!\n");
      goto fail;
    }

  /* Set module params info */
  FOR_LIST_ELEMENTS (module, module_head)
  {
    if (module->start && module->size > 0)
      {
	grub_dprintf ("xen_loader", "Module @ 0x%lx size:0x%lx\n",
		      xen_boot_address_align (module->start, module->align),
		      module->size);
	if (prepare_xen_module_params (module, xen_boot_fdt) != GRUB_ERR_NONE)
	  goto fail;
      }
    else
      {
	grub_dprintf ("xen_loader", "Module info error!\n");
	goto fail;
      }
  }

  if (grub_fdt_install() == GRUB_ERR_NONE)
    return GRUB_ERR_NONE;

fail:
  grub_fdt_unload ();

  return grub_error (GRUB_ERR_IO, "failed to install/update FDT");
}


static grub_err_t
xen_boot (void)
{
  grub_err_t err = finalize_params_xen_boot ();
  if (err)
    return err;

  return grub_arch_efi_linux_boot_image (xen_hypervisor->start,
					  xen_hypervisor->size,
					  xen_hypervisor->cmdline);
}

static void
single_binary_unload (struct xen_boot_binary *binary)
{
  if (!binary)
    return;

  if (binary->start && binary->size > 0)
    {
      grub_efi_free_pages ((grub_efi_physical_address_t) binary->start,
			   GRUB_EFI_BYTES_TO_PAGES (binary->size + binary->align));
    }

  if (binary->cmdline && binary->cmdline_size > 0)
    {
      grub_free (binary->cmdline);
      grub_dprintf ("xen_loader",
		    "Module cmdline memory free @ %p size: %d\n",
		    binary->cmdline, binary->cmdline_size);
    }

  if (!binary->is_hypervisor)
    grub_list_remove (GRUB_AS_LIST (binary));

  grub_dprintf ("xen_loader",
		"Module struct memory free @ %p size: 0x%lx\n",
		binary, sizeof (binary));
  grub_free (binary);

  return;
}

static void
all_binaries_unload (void)
{
  struct xen_boot_binary *binary;

  FOR_LIST_ELEMENTS (binary, module_head)
  {
    single_binary_unload (binary);
  }

  if (xen_hypervisor)
    single_binary_unload (xen_hypervisor);

  return;
}

static grub_err_t
xen_unload (void)
{
  loaded = 0;
  all_binaries_unload ();
  grub_fdt_unload ();
  grub_dl_unref (my_mod);

  return GRUB_ERR_NONE;
}

static void
xen_boot_binary_load (struct xen_boot_binary *binary, grub_file_t file,
		      int argc, char *argv[])
{
  binary->size = grub_file_size (file);
  grub_dprintf ("xen_loader", "Xen_boot file size: 0x%lx\n", binary->size);

  binary->start
    = (grub_addr_t) grub_efi_allocate_any_pages (GRUB_EFI_BYTES_TO_PAGES
						 (binary->size +
						  binary->align));
  if (!binary->start)
    {
      grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
      return;
    }

  grub_dprintf ("xen_loader", "Xen_boot numpages: 0x%lx\n",
	        GRUB_EFI_BYTES_TO_PAGES (binary->size + binary->align));

  if (grub_file_read (file, (void *) xen_boot_address_align (binary->start,
							     binary->align),
		      binary->size) != (grub_ssize_t) binary->size)
    {
      single_binary_unload (binary);
      grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"), argv[0]);
      return;
    }

  if (argc > 1)
    {
      binary->cmdline_size = grub_loader_cmdline_size (argc - 1, argv + 1);
      binary->cmdline = grub_zalloc (binary->cmdline_size);
      if (!binary->cmdline)
	{
	  single_binary_unload (binary);
	  grub_error (GRUB_ERR_OUT_OF_MEMORY, N_("out of memory"));
	  return;
	}
      grub_create_loader_cmdline (argc - 1, argv + 1, binary->cmdline,
				  binary->cmdline_size,
				  GRUB_VERIFY_KERNEL_CMDLINE);
      grub_dprintf ("xen_loader",
		    "Xen_boot cmdline @ %p %s, size: %d\n",
		    binary->cmdline, binary->cmdline, binary->cmdline_size);
    }
  else
    {
      binary->cmdline_size = 0;
      binary->cmdline = NULL;
    }

  grub_errno = GRUB_ERR_NONE;
  return;
}

static grub_err_t
grub_cmd_xen_module (grub_command_t cmd __attribute__((unused)),
		     int argc, char *argv[])
{

  struct xen_boot_binary *module = NULL;
  grub_file_t file = 0;
  int nounzip = 0;

  if (!argc)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));
      goto fail;
    }

  if (grub_strcmp (argv[0], "--nounzip") == 0)
    {
      argv++;
      argc--;
      nounzip = 1;
    }

  if (!argc)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));
      goto fail;
    }

  if (!loaded)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT,
		  N_("you need to load the Xen Hypervisor first"));
      goto fail;
    }

  module =
    (struct xen_boot_binary *) grub_zalloc (sizeof (struct xen_boot_binary));
  if (!module)
    return grub_errno;

  module->is_hypervisor = 0;
  module->align = 4096;

  grub_dprintf ("xen_loader", "Init module and node info\n");

  file = grub_file_open (argv[0], GRUB_FILE_TYPE_XEN_MODULE
			 | (nounzip ? GRUB_FILE_TYPE_NO_DECOMPRESS
			    : GRUB_FILE_TYPE_NONE));
  if (!file)
    goto fail;

  xen_boot_binary_load (module, file, argc, argv);
  if (grub_errno == GRUB_ERR_NONE)
    grub_list_push (GRUB_AS_LIST_P (&module_head), GRUB_AS_LIST (module));

 fail:
  if (file)
    grub_file_close (file);
  if (grub_errno != GRUB_ERR_NONE)
    single_binary_unload (module);

  return grub_errno;
}

static grub_err_t
grub_cmd_xen_hypervisor (grub_command_t cmd __attribute__ ((unused)),
			 int argc, char *argv[])
{
  struct xen_hypervisor_header sh;
  grub_file_t file = NULL;

  grub_dl_ref (my_mod);

  if (!argc)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));
      goto fail;
    }

  file = grub_file_open (argv[0], GRUB_FILE_TYPE_XEN_HYPERVISOR);
  if (!file)
    goto fail;

  if (grub_file_read (file, &sh, sizeof (sh)) != (long) sizeof (sh))
    goto fail;
  if (grub_arch_efi_linux_check_image
      ((struct linux_arch_kernel_header *) &sh) != GRUB_ERR_NONE)
    goto fail;
  grub_file_seek (file, 0);

  /* if another module has called grub_loader_set,
     we need to make sure that another module is unloaded properly */
  grub_loader_unset ();

  xen_hypervisor =
    (struct xen_boot_binary *) grub_zalloc (sizeof (struct xen_boot_binary));
  if (!xen_hypervisor)
    return grub_errno;

  xen_hypervisor->is_hypervisor = 1;
  xen_hypervisor->align = (grub_size_t) sh.optional_header.section_alignment;

  xen_boot_binary_load (xen_hypervisor, file, argc, argv);
  if (grub_errno == GRUB_ERR_NONE)
    {
      grub_loader_set (xen_boot, xen_unload, 0);
      loaded = 1;
    }

fail:
  if (file)
    grub_file_close (file);
  if (grub_errno != GRUB_ERR_NONE)
    {
      loaded = 0;
      all_binaries_unload ();
      grub_dl_unref (my_mod);
    }

  return grub_errno;
}

static grub_command_t cmd_xen_hypervisor;
static grub_command_t cmd_xen_module;

GRUB_MOD_INIT (xen_boot)
{
  cmd_xen_hypervisor =
    grub_register_command ("xen_hypervisor", grub_cmd_xen_hypervisor, 0,
			   N_("Load a xen hypervisor."));
  cmd_xen_module =
    grub_register_command ("xen_module", grub_cmd_xen_module, 0,
			   N_("Load a xen module."));
  my_mod = mod;
}

GRUB_MOD_FINI (xen_boot)
{
  grub_unregister_command (cmd_xen_hypervisor);
  grub_unregister_command (cmd_xen_module);
}
