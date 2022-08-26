/* lspci.c - List PCI devices.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008, 2009  Free Software Foundation, Inc.
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

#include <grub/pci.h>
#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/extcmd.h>
#include <grub/env.h>
#include <grub/mm.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

struct pci_register
{
  const char *name;
  grub_uint16_t addr;
  unsigned size;
};

static struct pci_register pci_registers[] =
  {
    {"VENDOR_ID",       GRUB_PCI_REG_VENDOR      , 2},
    {"DEVICE_ID",       GRUB_PCI_REG_DEVICE      , 2},
    {"COMMAND",         GRUB_PCI_REG_COMMAND     , 2},
    {"STATUS",          GRUB_PCI_REG_STATUS      , 2},
    {"REVISION",        GRUB_PCI_REG_REVISION    , 1},
    {"CLASS_PROG",      GRUB_PCI_REG_CLASS + 1   , 1},
    {"CLASS_DEVICE",    GRUB_PCI_REG_CLASS + 2   , 2},
    {"CACHE_LINE_SIZE", GRUB_PCI_REG_CACHELINE   , 1},
    {"LATENCY_TIMER",   GRUB_PCI_REG_LAT_TIMER   , 1},
    {"HEADER_TYPE",     GRUB_PCI_REG_HEADER_TYPE , 1},
    {"BIST",            GRUB_PCI_REG_BIST        , 1},
    {"BASE_ADDRESS_0",  GRUB_PCI_REG_ADDRESS_REG0, 4},
    {"BASE_ADDRESS_1",  GRUB_PCI_REG_ADDRESS_REG1, 4},
    {"BASE_ADDRESS_2",  GRUB_PCI_REG_ADDRESS_REG2, 4},
    {"BASE_ADDRESS_3",  GRUB_PCI_REG_ADDRESS_REG3, 4},
    {"BASE_ADDRESS_4",  GRUB_PCI_REG_ADDRESS_REG4, 4},
    {"BASE_ADDRESS_5",  GRUB_PCI_REG_ADDRESS_REG5, 4},
    {"CARDBUS_CIS",     GRUB_PCI_REG_CIS_POINTER , 4},
    {"SUBVENDOR_ID",    GRUB_PCI_REG_SUBVENDOR   , 2},
    {"SUBSYSTEM_ID",    GRUB_PCI_REG_SUBSYSTEM   , 2},
    {"ROM_ADDRESS",     GRUB_PCI_REG_ROM_ADDRESS , 4},
    {"CAP_POINTER",     GRUB_PCI_REG_CAP_POINTER , 1},
    {"INTERRUPT_LINE",  GRUB_PCI_REG_IRQ_LINE    , 1},
    {"INTERRUPT_PIN",   GRUB_PCI_REG_IRQ_PIN     , 1},
    {"MIN_GNT",         GRUB_PCI_REG_MIN_GNT     , 1},
    {"MAX_LAT",         GRUB_PCI_REG_MIN_GNT     , 1},
  };

static const struct grub_arg_option options[] =
  {
    {0, 'd', 0, N_("Select device by vendor and device IDs."),
     N_("[vendor]:[device]"), ARG_TYPE_STRING},
    {0, 's', 0, N_("Select device by its position on the bus."),
     N_("[bus]:[slot][.func]"), ARG_TYPE_STRING},
    {0, 'v', 0, N_("Save read value into variable VARNAME."),
     N_("VARNAME"), ARG_TYPE_STRING},
    {0, 0, 0, 0, 0, 0}
  };

static grub_uint32_t pciid_check_mask, pciid_check_value;
static int bus, device, function;
static int check_bus, check_device, check_function;
static grub_uint32_t write_mask, regwrite;
static int regsize;
static grub_uint16_t regaddr;
static const char *varname;

static int
grub_setpci_iter (grub_pci_device_t dev, grub_pci_id_t pciid,
		  void *data __attribute__ ((unused)))
{
  grub_uint32_t regval = 0;
  grub_pci_address_t addr;

  if ((pciid & pciid_check_mask) != pciid_check_value)
    return 0;

  if (check_bus && grub_pci_get_bus (dev) != bus)
    return 0;

  if (check_device && grub_pci_get_device (dev) != device)
    return 0;

  if (check_function && grub_pci_get_function (dev) != function)
    return 0;

  addr = grub_pci_make_address (dev, regaddr);

  switch (regsize)
    {
    case 1:
      regval = grub_pci_read_byte (addr);
      break;

    case 2:
      regval = grub_pci_read_word (addr);
      break;

    case 4:
      regval = grub_pci_read (addr);
      break;
    }

  if (varname)
    {
      char buf[sizeof ("XXXXXXXX")];
      grub_snprintf (buf, sizeof (buf), "%x", regval);
      grub_env_set (varname, buf);
      return 1;
    }

  if (!write_mask)
    {
      grub_printf (_("Register %x of %x:%02x.%x is %x\n"), regaddr,
		   grub_pci_get_bus (dev),
		   grub_pci_get_device (dev),
		   grub_pci_get_function (dev),
		   regval);
      return 0;
    }

  regval = (regval & ~write_mask) | regwrite;

  switch (regsize)
    {
    case 1:
      grub_pci_write_byte (addr, regval);
      break;

    case 2:
      grub_pci_write_word (addr, regval);
      break;

    case 4:
      grub_pci_write (addr, regval);
      break;
    }

  return 0;
}

static grub_err_t
grub_cmd_setpci (grub_extcmd_context_t ctxt, int argc, char **argv)
{
  const char *ptr;
  unsigned i;

  pciid_check_value = 0;
  pciid_check_mask = 0;

  if (ctxt->state[0].set)
    {
      ptr = ctxt->state[0].arg;
      pciid_check_value |= (grub_strtoul (ptr, &ptr, 16) & 0xffff);
      if (grub_errno == GRUB_ERR_BAD_NUMBER)
	{
	  grub_errno = GRUB_ERR_NONE;
	  ptr = ctxt->state[0].arg;
	}
      else
	pciid_check_mask |= 0xffff;
      if (grub_errno)
	return grub_errno;
      if (*ptr != ':')
	return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("missing `%c' symbol"), ':');
      ptr++;
      pciid_check_value |= (grub_strtoul (ptr, &ptr, 16) & 0xffff) << 16;
      if (grub_errno == GRUB_ERR_BAD_NUMBER)
	grub_errno = GRUB_ERR_NONE;
      else
	pciid_check_mask |= 0xffff0000;
    }

  pciid_check_value &= pciid_check_mask;

  check_bus = check_device = check_function = 0;

  if (ctxt->state[1].set)
    {
      const char *optr;

      ptr = ctxt->state[1].arg;
      optr = ptr;
      bus = grub_strtoul (ptr, &ptr, 16);
      if (grub_errno == GRUB_ERR_BAD_NUMBER)
	{
	  grub_errno = GRUB_ERR_NONE;
	  ptr = optr;
	}
      else
	check_bus = 1;
      if (grub_errno)
	return grub_errno;
      if (*ptr != ':')
	return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("missing `%c' symbol"), ':');
      ptr++;
      optr = ptr;
      device = grub_strtoul (ptr, &ptr, 16);
      if (grub_errno == GRUB_ERR_BAD_NUMBER)
	{
	  grub_errno = GRUB_ERR_NONE;
	  ptr = optr;
	}
      else
	check_device = 1;
      if (*ptr == '.')
	{
	  ptr++;
	  function = grub_strtoul (ptr, &ptr, 16);
	  if (grub_errno)
	    return grub_errno;
	  check_function = 1;
	}
    }

  if (ctxt->state[2].set)
    varname = ctxt->state[2].arg;
  else
    varname = NULL;

  write_mask = 0;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("one argument expected"));

  ptr = argv[0];

  for (i = 0; i < ARRAY_SIZE (pci_registers); i++)
    {
      if (grub_strncmp (ptr, pci_registers[i].name,
			grub_strlen (pci_registers[i].name)) == 0)
	break;
    }
  if (i == ARRAY_SIZE (pci_registers))
    {
      regsize = 0;
      regaddr = grub_strtoul (ptr, &ptr, 16);
      if (grub_errno)
	return grub_error (GRUB_ERR_BAD_ARGUMENT, "unknown register");
    }
  else
    {
      regaddr = pci_registers[i].addr;
      regsize = pci_registers[i].size;
      ptr += grub_strlen (pci_registers[i].name);
    }

  if (grub_errno)
    return grub_errno;

  if (*ptr == '+')
    {
      ptr++;
      regaddr += grub_strtoul (ptr, &ptr, 16);
      if (grub_errno)
	return grub_errno;
    }

  if (grub_memcmp (ptr, ".L", sizeof (".L") - 1) == 0
      || grub_memcmp (ptr, ".l", sizeof (".l") - 1) == 0)
    {
      regsize = 4;
      ptr += sizeof (".l") - 1;
    }
  else if (grub_memcmp (ptr, ".W", sizeof (".W") - 1) == 0
      || grub_memcmp (ptr, ".w", sizeof (".w") - 1) == 0)
    {
      regsize = 2;
      ptr += sizeof (".w") - 1;
    }
  else if (grub_memcmp (ptr, ".B", sizeof (".B") - 1) == 0
      || grub_memcmp (ptr, ".b", sizeof (".b") - 1) == 0)
    {
      regsize = 1;
      ptr += sizeof (".b") - 1;
    }

  if (!regsize)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       "unknown register size");

  write_mask = 0;
  if (*ptr == '=')
    {
      ptr++;
      regwrite = grub_strtoul (ptr, &ptr, 16);
      if (grub_errno)
	return grub_errno;
      write_mask = 0xffffffff;
      if (*ptr == ':')
	{
	  ptr++;
	  write_mask = grub_strtoul (ptr, &ptr, 16);
	  if (grub_errno)
	    return grub_errno;
	}
      regwrite &= write_mask;
    }

  if (write_mask && varname)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       "option -v isn't valid for writes");

  grub_pci_iterate (grub_setpci_iter, NULL);
  return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(setpci)
{
  cmd = grub_register_extcmd ("setpci", grub_cmd_setpci, 0,
			      N_("[-s POSITION] [-d DEVICE] [-v VAR] "
				 "REGISTER[=VALUE[:MASK]]"),
			      N_("Manipulate PCI devices."), options);
}

GRUB_MOD_FINI(setpci)
{
  grub_unregister_extcmd (cmd);
}
