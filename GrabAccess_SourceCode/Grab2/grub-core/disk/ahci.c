/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2007, 2008, 2009, 2010  Free Software Foundation, Inc.
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
#include <grub/disk.h>
#include <grub/mm.h>
#include <grub/time.h>
#include <grub/pci.h>
#include <grub/ata.h>
#include <grub/scsi.h>
#include <grub/misc.h>
#include <grub/list.h>
#include <grub/loader.h>

GRUB_MOD_LICENSE ("GPLv3+");

struct grub_ahci_cmd_head
{
  grub_uint32_t config;
  grub_uint32_t transferred;
  grub_uint64_t command_table_base;
  grub_uint32_t unused[4];
};

struct grub_ahci_prdt_entry
{
  grub_uint64_t data_base;
  grub_uint32_t unused;
  grub_uint32_t size;
};

struct grub_ahci_cmd_table
{
  grub_uint8_t cfis[0x40];
  grub_uint8_t command[0x10];
  grub_uint8_t reserved[0x30];
  struct grub_ahci_prdt_entry prdt[1];
};

struct grub_ahci_hba_port
{
  grub_uint64_t command_list_base;
  grub_uint64_t fis_base;
  grub_uint32_t intstatus;
  grub_uint32_t inten;
  grub_uint32_t command;
  grub_uint32_t unused1;
  grub_uint32_t task_file_data;
  grub_uint32_t sig;
  grub_uint32_t status;
  grub_uint32_t unused2;
  grub_uint32_t sata_error;
  grub_uint32_t sata_active;
  grub_uint32_t command_issue;
  grub_uint32_t unused3;
  grub_uint32_t fbs;
  grub_uint32_t unused4[15];
};

enum grub_ahci_hba_port_command
  {
    GRUB_AHCI_HBA_PORT_CMD_ST  = 0x01,
    GRUB_AHCI_HBA_PORT_CMD_SPIN_UP = 0x02,
    GRUB_AHCI_HBA_PORT_CMD_POWER_ON = 0x04,
    GRUB_AHCI_HBA_PORT_CMD_FRE = 0x10,
    GRUB_AHCI_HBA_PORT_CMD_CR = 0x8000,
    GRUB_AHCI_HBA_PORT_CMD_FR = 0x4000,
  };

enum grub_ahci_hba_port_int_status
  {
    GRUB_AHCI_HBA_PORT_IS_IFS  = (1UL << 27),
    GRUB_AHCI_HBA_PORT_IS_HBDS = (1UL << 28),
    GRUB_AHCI_HBA_PORT_IS_HBFS = (1UL << 29),
    GRUB_AHCI_HBA_PORT_IS_TFES = (1UL << 30),
  };

#define GRUB_AHCI_HBA_PORT_IS_FATAL_MASK ( \
	GRUB_AHCI_HBA_PORT_IS_IFS | \
	GRUB_AHCI_HBA_PORT_IS_HBDS | \
	GRUB_AHCI_HBA_PORT_IS_HBFS | \
	GRUB_AHCI_HBA_PORT_IS_TFES)

struct grub_ahci_hba
{
  grub_uint32_t cap;
  grub_uint32_t global_control;
  grub_uint32_t intr_status;
  grub_uint32_t ports_implemented;
  grub_uint32_t unused1[6];
  grub_uint32_t bios_handoff;
  grub_uint32_t unused2[53];
  struct grub_ahci_hba_port ports[32];
};

struct grub_ahci_received_fis
{
  char raw[4096];
};

enum
  {
    GRUB_AHCI_HBA_CAP_NPORTS_MASK = 0x1f
  };

enum
  {
    GRUB_AHCI_HBA_GLOBAL_CONTROL_RESET = 0x00000001,
    GRUB_AHCI_HBA_GLOBAL_CONTROL_INTR_EN = 0x00000002,
    GRUB_AHCI_HBA_GLOBAL_CONTROL_AHCI_EN = 0x80000000,
  };

enum
  {
    GRUB_AHCI_BIOS_HANDOFF_BIOS_OWNED = 1,
    GRUB_AHCI_BIOS_HANDOFF_OS_OWNED = 2,
    GRUB_AHCI_BIOS_HANDOFF_OS_OWNERSHIP_CHANGED = 8,
    GRUB_AHCI_BIOS_HANDOFF_RWC = 8
  };


struct grub_ahci_device
{
  struct grub_ahci_device *next;
  struct grub_ahci_device **prev;
  volatile struct grub_ahci_hba *hba;
  int port;
  int num;
  struct grub_pci_dma_chunk *command_list_chunk;
  volatile struct grub_ahci_cmd_head *command_list;
  struct grub_pci_dma_chunk *command_table_chunk;
  volatile struct grub_ahci_cmd_table *command_table;
  struct grub_pci_dma_chunk *rfis;
  int present;
  int atapi;
};

static grub_err_t 
grub_ahci_readwrite_real (struct grub_ahci_device *dev,
			  struct grub_disk_ata_pass_through_parms *parms,
			  int spinup, int reset);


enum
  {
    GRUB_AHCI_CONFIG_READ = 0,
    GRUB_AHCI_CONFIG_CFIS_LENGTH_MASK = 0x1f,
    GRUB_AHCI_CONFIG_ATAPI = 0x20,
    GRUB_AHCI_CONFIG_WRITE = 0x40,
    GRUB_AHCI_CONFIG_PREFETCH = 0x80,
    GRUB_AHCI_CONFIG_RESET = 0x100,
    GRUB_AHCI_CONFIG_BIST = 0x200,
    GRUB_AHCI_CONFIG_CLEAR_R_OK = 0x400,
    GRUB_AHCI_CONFIG_PMP_MASK = 0xf000,
    GRUB_AHCI_CONFIG_PRDT_LENGTH_MASK = 0xffff0000,
  };
#define GRUB_AHCI_CONFIG_CFIS_LENGTH_SHIFT 0
#define GRUB_AHCI_CONFIG_PMP_SHIFT 12
#define GRUB_AHCI_CONFIG_PRDT_LENGTH_SHIFT 16
#define GRUB_AHCI_INTERRUPT_ON_COMPLETE 0x80000000

#define GRUB_AHCI_PRDT_MAX_CHUNK_LENGTH 0x200000

static struct grub_ahci_device *grub_ahci_devices;
static int numdevs;

static int
grub_ahci_pciinit (grub_pci_device_t dev,
		   grub_pci_id_t pciid __attribute__ ((unused)),
		   void *data __attribute__ ((unused)))
{
  grub_pci_address_t addr;
  grub_uint32_t class;
  grub_uint32_t bar;
  unsigned i, nports;
  volatile struct grub_ahci_hba *hba;

  /* Read class.  */
  addr = grub_pci_make_address (dev, GRUB_PCI_REG_CLASS);
  class = grub_pci_read (addr);

  /* Check if this class ID matches that of a PCI IDE Controller.  */
  if (class >> 8 != 0x010601)
    return 0;

  addr = grub_pci_make_address (dev, GRUB_PCI_REG_ADDRESS_REG5);

  bar = grub_pci_read (addr);

  if ((bar & (GRUB_PCI_ADDR_SPACE_MASK | GRUB_PCI_ADDR_MEM_TYPE_MASK
	      | GRUB_PCI_ADDR_MEM_PREFETCH))
      != (GRUB_PCI_ADDR_SPACE_MEMORY | GRUB_PCI_ADDR_MEM_TYPE_32))
    return 0;

  addr = grub_pci_make_address (dev, GRUB_PCI_REG_COMMAND);
  grub_pci_write_word (addr, grub_pci_read_word (addr)
		    | GRUB_PCI_COMMAND_MEM_ENABLED | GRUB_PCI_COMMAND_BUS_MASTER);

  hba = grub_pci_device_map_range (dev, bar & GRUB_PCI_ADDR_MEM_MASK,
				   sizeof (*hba));
  grub_dprintf ("ahci", "dev: %x:%x.%x\n", dev.bus, dev.device, dev.function);

  grub_dprintf ("ahci", "tfd[0]: %x\n",
		hba->ports[0].task_file_data);
  grub_dprintf ("ahci", "cmd[0]: %x\n",
		hba->ports[0].command);
  grub_dprintf ("ahci", "st[0]: %x\n",
		hba->ports[0].status);
  grub_dprintf ("ahci", "err[0]: %x\n",
		hba->ports[0].sata_error);

  grub_dprintf ("ahci", "tfd[1]: %x\n",
		hba->ports[1].task_file_data);
  grub_dprintf ("ahci", "cmd[1]: %x\n",
		hba->ports[1].command);
  grub_dprintf ("ahci", "st[1]: %x\n",
		hba->ports[1].status);
  grub_dprintf ("ahci", "err[1]: %x\n",
		hba->ports[1].sata_error);

  hba->ports[1].sata_error = hba->ports[1].sata_error;

  grub_dprintf ("ahci", "err[1]: %x\n",
		hba->ports[1].sata_error);

  grub_dprintf ("ahci", "BH:%x\n", hba->bios_handoff);

  if (! (hba->bios_handoff & GRUB_AHCI_BIOS_HANDOFF_OS_OWNED))
    {
      grub_uint64_t endtime;

      grub_dprintf ("ahci", "Requesting AHCI ownership\n");
      hba->bios_handoff = (hba->bios_handoff & ~GRUB_AHCI_BIOS_HANDOFF_RWC)
	| GRUB_AHCI_BIOS_HANDOFF_OS_OWNED;
      grub_dprintf ("ahci", "Waiting for BIOS to give up ownership\n");
      endtime = grub_get_time_ms () + 1000;
      while ((hba->bios_handoff & GRUB_AHCI_BIOS_HANDOFF_BIOS_OWNED)
	     && grub_get_time_ms () < endtime);
      if (hba->bios_handoff & GRUB_AHCI_BIOS_HANDOFF_BIOS_OWNED)
	{
	  grub_dprintf ("ahci", "Forcibly taking ownership\n");
	  hba->bios_handoff = GRUB_AHCI_BIOS_HANDOFF_OS_OWNED;
	  hba->bios_handoff |= GRUB_AHCI_BIOS_HANDOFF_OS_OWNERSHIP_CHANGED;
	}
      else
	grub_dprintf ("ahci", "AHCI ownership obtained\n");
    }
  else
    grub_dprintf ("ahci", "AHCI is already in OS mode\n");

  grub_dprintf ("ahci", "GLC:%x\n", hba->global_control);

  grub_dprintf ("ahci", "err[1]: %x\n",
		hba->ports[1].sata_error);

  if (!(hba->global_control & GRUB_AHCI_HBA_GLOBAL_CONTROL_AHCI_EN))
    grub_dprintf ("ahci", "AHCI is in compat mode. Switching\n");
  else
    grub_dprintf ("ahci", "AHCI is in AHCI mode.\n");

  grub_dprintf ("ahci", "err[1]: %x\n",
		hba->ports[1].sata_error);

  grub_dprintf ("ahci", "GLC:%x\n", hba->global_control);

  /*  {
      grub_uint64_t endtime;
      hba->global_control |= 1;
      endtime = grub_get_time_ms () + 1000;
      while (hba->global_control & 1)
	if (grub_get_time_ms () > endtime)
	  {
	    grub_dprintf ("ahci", "couldn't reset AHCI\n");
	    return 0;
	  }
	  }*/

  grub_dprintf ("ahci", "GLC:%x\n", hba->global_control);

  grub_dprintf ("ahci", "err[1]: %x\n",
		hba->ports[1].sata_error);

  for (i = 0; i < 5; i++)
    {
      hba->global_control |= GRUB_AHCI_HBA_GLOBAL_CONTROL_AHCI_EN;
      grub_millisleep (1);
      if (hba->global_control & GRUB_AHCI_HBA_GLOBAL_CONTROL_AHCI_EN)
	break;
    }
  if (i == 5)
    {
      grub_dprintf ("ahci", "Couldn't put AHCI in AHCI mode\n");
      return 0;
    }

  grub_dprintf ("ahci", "GLC:%x\n", hba->global_control);

  grub_dprintf ("ahci", "err[1]: %x\n",
		hba->ports[1].sata_error);

  grub_dprintf ("ahci", "err[1]: %x\n",
		hba->ports[1].sata_error);

  grub_dprintf ("ahci", "GLC:%x\n", hba->global_control);

  for (i = 0; i < 5; i++)
    {
      hba->global_control |= GRUB_AHCI_HBA_GLOBAL_CONTROL_AHCI_EN;
      grub_millisleep (1);
      if (hba->global_control & GRUB_AHCI_HBA_GLOBAL_CONTROL_AHCI_EN)
	break;
    }
  if (i == 5)
    {
      grub_dprintf ("ahci", "Couldn't put AHCI in AHCI mode\n");
      return 0;
    }

  grub_dprintf ("ahci", "err[1]: %x\n",
		hba->ports[1].sata_error);

  grub_dprintf ("ahci", "GLC:%x\n", hba->global_control);

  nports = (GRUB_AHCI_HBA_CAP_NPORTS_MASK) + 1;

  grub_dprintf ("ahci", "%d AHCI ports, PI = 0x%x\n", nports,
		hba->ports_implemented);

  struct grub_ahci_device *adevs[GRUB_AHCI_HBA_CAP_NPORTS_MASK + 1];
  struct grub_ahci_device *failed_adevs[GRUB_AHCI_HBA_CAP_NPORTS_MASK + 1];
  grub_uint32_t fr_running = 0;

  for (i = 0; i < nports; i++)
    failed_adevs[i] = 0;
  for (i = 0; i < nports; i++)
    {
      if (!(hba->ports_implemented & (1 << i)))
	{
	  adevs[i] = 0;
	  continue;
	}

      adevs[i] = grub_zalloc (sizeof (*adevs[i]));
      if (!adevs[i])
	return 1;

      adevs[i]->hba = hba;
      adevs[i]->port = i;
      adevs[i]->present = 1;
      adevs[i]->num = numdevs++;
    }

  for (i = 0; i < nports; i++)
    if (adevs[i])
      {
	adevs[i]->hba->ports[adevs[i]->port].sata_error = adevs[i]->hba->ports[adevs[i]->port].sata_error;
	grub_dprintf ("ahci", "port: %d, err: %x\n", adevs[i]->port,
		      adevs[i]->hba->ports[adevs[i]->port].sata_error);

	adevs[i]->command_list_chunk = grub_memalign_dma32 (1024, sizeof (struct grub_ahci_cmd_head) * 32);
	if (!adevs[i]->command_list_chunk)
	  {
	    adevs[i] = 0;
	    continue;
	  }

	adevs[i]->command_table_chunk = grub_memalign_dma32 (1024,
							    sizeof (struct grub_ahci_cmd_table));
	if (!adevs[i]->command_table_chunk)
	  {
	    grub_dma_free (adevs[i]->command_list_chunk);
	    adevs[i] = 0;
	    continue;
	  }

	adevs[i]->command_list = grub_dma_get_virt (adevs[i]->command_list_chunk);
	adevs[i]->command_table = grub_dma_get_virt (adevs[i]->command_table_chunk);

	grub_memset ((void *) adevs[i]->command_list, 0,
		     sizeof (struct grub_ahci_cmd_table));
	grub_memset ((void *) adevs[i]->command_table, 0,
		     sizeof (struct grub_ahci_cmd_head) * 32);

	adevs[i]->command_list->command_table_base
	  = grub_dma_get_phys (adevs[i]->command_table_chunk);

	grub_dprintf ("ahci", "found device ahci%d (port %d), command_table = %p, command_list = %p\n",
		      adevs[i]->num, adevs[i]->port, grub_dma_get_virt (adevs[i]->command_table_chunk),
		      grub_dma_get_virt (adevs[i]->command_list_chunk));

	adevs[i]->hba->ports[adevs[i]->port].command &= ~GRUB_AHCI_HBA_PORT_CMD_FRE;
      }

  grub_uint64_t endtime;
  endtime = grub_get_time_ms () + 1000;

  while (grub_get_time_ms () < endtime)
    {
      for (i = 0; i < nports; i++)
	if (adevs[i] && (adevs[i]->hba->ports[adevs[i]->port].command & GRUB_AHCI_HBA_PORT_CMD_FR))
	  break;
      if (i == nports)
	break;
    }

  for (i = 0; i < nports; i++)
    if (adevs[i] && (adevs[i]->hba->ports[adevs[i]->port].command & GRUB_AHCI_HBA_PORT_CMD_FR))
      {
	grub_dprintf ("ahci", "couldn't stop FR on port %d\n", i);
	failed_adevs[i] = adevs[i];
	adevs[i] = 0;
      }

  for (i = 0; i < nports; i++)
    if (adevs[i])
      adevs[i]->hba->ports[adevs[i]->port].command &= ~GRUB_AHCI_HBA_PORT_CMD_ST;
  endtime = grub_get_time_ms () + 1000;

  while (grub_get_time_ms () < endtime)
    {
      for (i = 0; i < nports; i++)
	if (adevs[i] && (adevs[i]->hba->ports[adevs[i]->port].command & GRUB_AHCI_HBA_PORT_CMD_CR))
	  break;
      if (i == nports)
	break;
    }

  for (i = 0; i < nports; i++)
    if (adevs[i] && (adevs[i]->hba->ports[adevs[i]->port].command & GRUB_AHCI_HBA_PORT_CMD_CR))
      {
	grub_dprintf ("ahci", "couldn't stop CR on port %d\n", i);
	failed_adevs[i] = adevs[i];
	adevs[i] = 0;
      }
  for (i = 0; i < nports; i++)
    if (adevs[i])
      {
	adevs[i]->hba->ports[adevs[i]->port].inten = 0;
	adevs[i]->hba->ports[adevs[i]->port].intstatus = ~0;
	//  adevs[i]->hba->ports[adevs[i]->port].fbs = 0;

	grub_dprintf ("ahci", "port: %d, err: %x\n", adevs[i]->port,
		      adevs[i]->hba->ports[adevs[i]->port].sata_error);

	adevs[i]->rfis = grub_memalign_dma32 (4096, 
					     sizeof (struct grub_ahci_received_fis));
	grub_memset ((char *) grub_dma_get_virt (adevs[i]->rfis), 0,
		     sizeof (struct grub_ahci_received_fis));
	grub_memset ((char *) grub_dma_get_virt (adevs[i]->command_list_chunk), 0,
		     sizeof (struct grub_ahci_cmd_head));
	grub_memset ((char *) grub_dma_get_virt (adevs[i]->command_table_chunk), 0,
		     sizeof (struct grub_ahci_cmd_table));
	adevs[i]->hba->ports[adevs[i]->port].fis_base = grub_dma_get_phys (adevs[i]->rfis);
	adevs[i]->hba->ports[adevs[i]->port].command_list_base
	  = grub_dma_get_phys (adevs[i]->command_list_chunk);
	adevs[i]->hba->ports[adevs[i]->port].command_issue = 0;
	adevs[i]->hba->ports[adevs[i]->port].command |= GRUB_AHCI_HBA_PORT_CMD_FRE;
      }

  endtime = grub_get_time_ms () + 1000;

  while (grub_get_time_ms () < endtime)
    {
      for (i = 0; i < nports; i++)
	if (adevs[i] && !(adevs[i]->hba->ports[adevs[i]->port].command & GRUB_AHCI_HBA_PORT_CMD_FR))
	  break;
      if (i == nports)
	break;
    }

  for (i = 0; i < nports; i++)
    if (adevs[i] && !(adevs[i]->hba->ports[adevs[i]->port].command & GRUB_AHCI_HBA_PORT_CMD_FR))
      {
	grub_dprintf ("ahci", "couldn't start FR on port %d\n", i);
	failed_adevs[i] = adevs[i];
	adevs[i] = 0;
      }

  for (i = 0; i < nports; i++)
    if (adevs[i])
      {
	grub_dprintf ("ahci", "port: %d, err: %x\n", adevs[i]->port,
		      adevs[i]->hba->ports[adevs[i]->port].sata_error);
	fr_running |= (1 << i);

	adevs[i]->hba->ports[adevs[i]->port].command |= GRUB_AHCI_HBA_PORT_CMD_SPIN_UP;
	adevs[i]->hba->ports[adevs[i]->port].command |= GRUB_AHCI_HBA_PORT_CMD_POWER_ON;
	adevs[i]->hba->ports[adevs[i]->port].command |= 1 << 28;

	grub_dprintf ("ahci", "port: %d, err: %x\n", adevs[i]->port,
		      adevs[i]->hba->ports[adevs[i]->port].sata_error);
      }

  /* 10ms should actually be enough.  */
  endtime = grub_get_time_ms () + 100;

  while (grub_get_time_ms () < endtime)
    {
      for (i = 0; i < nports; i++)
	if (adevs[i] && (adevs[i]->hba->ports[adevs[i]->port].status & 7) != 3)
	  break;
      if (i == nports)
	break;
    }

  for (i = 0; i < nports; i++)
    if (adevs[i] && (adevs[i]->hba->ports[adevs[i]->port].status & 7) != 3)
      {
	grub_dprintf ("ahci", "couldn't detect device on port %d\n", i);
	failed_adevs[i] = adevs[i];
	adevs[i] = 0;
      }

  for (i = 0; i < nports; i++)
    if (adevs[i])
      {
	grub_dprintf ("ahci", "port %d, err: %x\n", adevs[i]->port,
		      adevs[i]->hba->ports[adevs[i]->port].sata_error);

	adevs[i]->hba->ports[adevs[i]->port].command |= GRUB_AHCI_HBA_PORT_CMD_POWER_ON;
	adevs[i]->hba->ports[adevs[i]->port].command |= GRUB_AHCI_HBA_PORT_CMD_SPIN_UP;

	grub_dprintf ("ahci", "port %d, err: %x\n", adevs[i]->port,
		      adevs[i]->hba->ports[adevs[i]->port].sata_error);

	adevs[i]->hba->ports[adevs[i]->port].sata_error = ~0;
	grub_dprintf ("ahci", "port %d, err: %x\n", adevs[i]->port,
		      adevs[i]->hba->ports[adevs[i]->port].sata_error);

	grub_dprintf ("ahci", "port %d, offset: %x, tfd:%x, CMD: %x\n", adevs[i]->port,
		      (int) ((char *) &adevs[i]->hba->ports[adevs[i]->port].task_file_data - 
			     (char *) adevs[i]->hba),
		      adevs[i]->hba->ports[adevs[i]->port].task_file_data,
		      adevs[i]->hba->ports[adevs[i]->port].command);

	grub_dprintf ("ahci", "port %d, err: %x\n", adevs[i]->port,
		      adevs[i]->hba->ports[adevs[i]->port].sata_error);
      }


  for (i = 0; i < nports; i++)
    if (adevs[i])
      {
	grub_dprintf ("ahci", "port %d, offset: %x, tfd:%x, CMD: %x\n", adevs[i]->port,
		      (int) ((char *) &adevs[i]->hba->ports[adevs[i]->port].task_file_data - 
			     (char *) adevs[i]->hba),
		      adevs[i]->hba->ports[adevs[i]->port].task_file_data,
		      adevs[i]->hba->ports[adevs[i]->port].command);

	grub_dprintf ("ahci", "port: %d, err: %x\n", adevs[i]->port,
		      adevs[i]->hba->ports[adevs[i]->port].sata_error);

	adevs[i]->hba->ports[adevs[i]->port].command
	  = (adevs[i]->hba->ports[adevs[i]->port].command & 0x0fffffff) | (1 << 28)
	  | GRUB_AHCI_HBA_PORT_CMD_SPIN_UP
	  | GRUB_AHCI_HBA_PORT_CMD_POWER_ON;

	/*  struct grub_disk_ata_pass_through_parms parms2;
	    grub_memset (&parms2, 0, sizeof (parms2));
	    parms2.taskfile.cmd = 8;
	    grub_ahci_readwrite_real (dev, &parms2, 1, 1);*/
      }

  endtime = grub_get_time_ms () + 32000;

  while (grub_get_time_ms () < endtime)
    {
      for (i = 0; i < nports; i++)
	if (adevs[i] && (adevs[i]->hba->ports[adevs[i]->port].task_file_data & (GRUB_ATA_STATUS_BUSY | GRUB_ATA_STATUS_DRQ)))
	  break;
      if (i == nports)
	break;
    }

  for (i = 0; i < nports; i++)
    if (adevs[i] && (adevs[i]->hba->ports[adevs[i]->port].task_file_data & (GRUB_ATA_STATUS_BUSY | GRUB_ATA_STATUS_DRQ)))
      {
	grub_dprintf ("ahci", "port %d is busy\n", i);
	failed_adevs[i] = adevs[i];
	adevs[i] = 0;
      }

  for (i = 0; i < nports; i++)
    if (adevs[i])
      adevs[i]->hba->ports[adevs[i]->port].command |= GRUB_AHCI_HBA_PORT_CMD_ST;

  endtime = grub_get_time_ms () + 1000;

  while (grub_get_time_ms () < endtime)
    {
      for (i = 0; i < nports; i++)
	if (adevs[i] && !(adevs[i]->hba->ports[adevs[i]->port].command & GRUB_AHCI_HBA_PORT_CMD_CR))
	  break;
      if (i == nports)
	break;
    }

  for (i = 0; i < nports; i++)
    if (adevs[i] && !(adevs[i]->hba->ports[adevs[i]->port].command & GRUB_AHCI_HBA_PORT_CMD_CR))
      {
	grub_dprintf ("ahci", "couldn't start CR on port %d\n", i);
	failed_adevs[i] = adevs[i];
	adevs[i] = 0;
      }

  grub_dprintf ("ahci", "cleaning up failed devs\n");

  for (i = 0; i < nports; i++)
    if (failed_adevs[i] && (fr_running & (1 << i)))
      failed_adevs[i]->hba->ports[failed_adevs[i]->port].command &= ~GRUB_AHCI_HBA_PORT_CMD_FRE;

  endtime = grub_get_time_ms () + 1000;
  while (grub_get_time_ms () < endtime)
    {
      for (i = 0; i < nports; i++)
	if (failed_adevs[i] && (fr_running & (1 << i)) && (failed_adevs[i]->hba->ports[failed_adevs[i]->port].command & GRUB_AHCI_HBA_PORT_CMD_FR))
	  break;
      if (i == nports)
	break;
    }
  for (i = 0; i < nports; i++)
    if (failed_adevs[i])
      {
	grub_dma_free (failed_adevs[i]->command_list_chunk);
	grub_dma_free (failed_adevs[i]->command_table_chunk);
	grub_dma_free (failed_adevs[i]->rfis);
      }

  for (i = 0; i < nports; i++)
    if (adevs[i] && (adevs[i]->hba->ports[adevs[i]->port].sig >> 16) == 0xeb14)
      adevs[i]->atapi = 1;

  addr = grub_pci_make_address (dev, GRUB_PCI_REG_COMMAND);
  grub_pci_write_word (addr, grub_pci_read_word (addr)
		    | GRUB_PCI_COMMAND_BUS_MASTER);

  for (i = 0; i < nports; i++)
    if (adevs[i])
      {
	grub_list_push (GRUB_AS_LIST_P (&grub_ahci_devices),
			GRUB_AS_LIST (adevs[i]));
      }

  return 0;
}

static grub_err_t
grub_ahci_initialize (void)
{
  grub_pci_iterate (grub_ahci_pciinit, NULL);
  return grub_errno;
}

static grub_err_t
grub_ahci_fini_hw (int noreturn __attribute__ ((unused)))
{
  struct grub_ahci_device *dev;

  for (dev = grub_ahci_devices; dev; dev = dev->next)
    {
      grub_uint64_t endtime;

      dev->hba->ports[dev->port].command &= ~GRUB_AHCI_HBA_PORT_CMD_FRE;
      endtime = grub_get_time_ms () + 1000;
      while ((dev->hba->ports[dev->port].command & GRUB_AHCI_HBA_PORT_CMD_FR))
	if (grub_get_time_ms () > endtime)
	  {
	    grub_dprintf ("ahci", "couldn't stop FR\n");
	    break;
	  }

      dev->hba->ports[dev->port].command &= ~GRUB_AHCI_HBA_PORT_CMD_ST;
      endtime = grub_get_time_ms () + 1000;
      while ((dev->hba->ports[dev->port].command & GRUB_AHCI_HBA_PORT_CMD_CR))
	if (grub_get_time_ms () > endtime)
	  {
	    grub_dprintf ("ahci", "couldn't stop CR\n");
	    break;
	  }
      grub_dma_free (dev->command_list_chunk);
      grub_dma_free (dev->command_table_chunk);
      grub_dma_free (dev->rfis);
      dev->command_list_chunk = NULL;
      dev->command_table_chunk = NULL;
      dev->rfis = NULL;
    }
  return GRUB_ERR_NONE;
}

static int 
reinit_port (struct grub_ahci_device *dev)
{
  struct grub_pci_dma_chunk *command_list;
  struct grub_pci_dma_chunk *command_table;
  grub_uint64_t endtime;

  command_list = grub_memalign_dma32 (1024, sizeof (struct grub_ahci_cmd_head));
  if (!command_list)
    return 1;

  command_table = grub_memalign_dma32 (1024,
				       sizeof (struct grub_ahci_cmd_table));
  if (!command_table)
    {
      grub_dma_free (command_list);
      return 1;
    }

  grub_dprintf ("ahci", "found device ahci%d (port %d)\n", dev->num, dev->port);

  dev->hba->ports[dev->port].command &= ~GRUB_AHCI_HBA_PORT_CMD_FRE;
  endtime = grub_get_time_ms () + 1000;
  while ((dev->hba->ports[dev->port].command & GRUB_AHCI_HBA_PORT_CMD_FR))
    if (grub_get_time_ms () > endtime)
      {
	grub_dprintf ("ahci", "couldn't stop FR\n");
	goto out;
      }

  dev->hba->ports[dev->port].command &= ~GRUB_AHCI_HBA_PORT_CMD_ST;
  endtime = grub_get_time_ms () + 1000;
  while ((dev->hba->ports[dev->port].command & GRUB_AHCI_HBA_PORT_CMD_CR))
    if (grub_get_time_ms () > endtime)
      {
	grub_dprintf ("ahci", "couldn't stop CR\n");
	goto out;
      }

  dev->hba->ports[dev->port].fbs = 2;

  dev->rfis = grub_memalign_dma32 (4096, 
				   sizeof (struct grub_ahci_received_fis));
  grub_memset ((char *) grub_dma_get_virt (dev->rfis), 0,
	       sizeof (struct grub_ahci_received_fis));
  dev->hba->ports[dev->port].fis_base = grub_dma_get_phys (dev->rfis);
  dev->hba->ports[dev->port].command_list_base
    = grub_dma_get_phys (command_list);
  dev->hba->ports[dev->port].command |= GRUB_AHCI_HBA_PORT_CMD_FRE;
  while (!(dev->hba->ports[dev->port].command & GRUB_AHCI_HBA_PORT_CMD_FR))
    if (grub_get_time_ms () > endtime)
      {
	grub_dprintf ("ahci", "couldn't start FR\n");
	dev->hba->ports[dev->port].command &= ~GRUB_AHCI_HBA_PORT_CMD_FRE;
	goto out;
      }
  dev->hba->ports[dev->port].command |= GRUB_AHCI_HBA_PORT_CMD_ST;
  while (!(dev->hba->ports[dev->port].command & GRUB_AHCI_HBA_PORT_CMD_CR))
    if (grub_get_time_ms () > endtime)
      {
	grub_dprintf ("ahci", "couldn't start CR\n");
	dev->hba->ports[dev->port].command &= ~GRUB_AHCI_HBA_PORT_CMD_CR;
	goto out_stop_fr;
      }

  dev->hba->ports[dev->port].command
    = (dev->hba->ports[dev->port].command & 0x0fffffff) | (1 << 28) | 2 | 4;

  dev->command_list_chunk = command_list;
  dev->command_list = grub_dma_get_virt (command_list);
  dev->command_table_chunk = command_table;
  dev->command_table = grub_dma_get_virt (command_table);
  dev->command_list->command_table_base
    = grub_dma_get_phys (command_table);

  return 0;
 out_stop_fr:
  dev->hba->ports[dev->port].command &= ~GRUB_AHCI_HBA_PORT_CMD_FRE;
  endtime = grub_get_time_ms () + 1000;
  while ((dev->hba->ports[dev->port].command & GRUB_AHCI_HBA_PORT_CMD_FR))
    if (grub_get_time_ms () > endtime)
      {
	grub_dprintf ("ahci", "couldn't stop FR\n");
	break;
      }
 out:
  grub_dma_free (command_list);
  grub_dma_free (command_table);
  grub_dma_free (dev->rfis);
  return 1;
}

static grub_err_t
grub_ahci_restore_hw (void)
{
  struct grub_ahci_device **pdev;

  for (pdev = &grub_ahci_devices; *pdev; pdev = &((*pdev)->next))
    if (reinit_port (*pdev))
      {
	struct grub_ahci_device *odev;
	odev = *pdev;
	*pdev = (*pdev)->next;
	grub_free (odev);
      }
  return GRUB_ERR_NONE;
}




static int
grub_ahci_iterate (grub_ata_dev_iterate_hook_t hook, void *hook_data,
		   grub_disk_pull_t pull)
{
  struct grub_ahci_device *dev;

  if (pull != GRUB_DISK_PULL_NONE)
    return 0;

  FOR_LIST_ELEMENTS(dev, grub_ahci_devices)
    if (hook (GRUB_SCSI_SUBSYSTEM_AHCI, dev->num, hook_data))
      return 1;

  return 0;
}

#if 0
static int
find_free_cmd_slot (struct grub_ahci_device *dev)
{
  int i;
  for (i = 0; i < 32; i++)
    {
      if (dev->hda->ports[dev->port].command_issue & (1 << i))
	continue;
      if (dev->hda->ports[dev->port].sata_active & (1 << i))
	continue;
      return i;
    }
  return -1;
}
#endif

enum
  {
    GRUB_AHCI_FIS_REG_H2D = 0x27
  };

static const int register_map[11] = { 3 /* Features */,
				      12 /* Sectors */,
				      4 /* LBA low */,
				      5 /* LBA mid */,
				      6 /* LBA high */,
				      7 /* Device */,
				      2 /* CMD register */,
				      13 /* Sectors 48  */,
				      8 /* LBA48 low */,
				      9 /* LBA48 mid */,
				      10 /* LBA48 high */ }; 

static grub_err_t
grub_ahci_reset_port (struct grub_ahci_device *dev, int force)
{
  grub_uint64_t endtime;
  
  dev->hba->ports[dev->port].sata_error = dev->hba->ports[dev->port].sata_error;

  if (force || (dev->hba->ports[dev->port].command_issue & 1)
      || (dev->hba->ports[dev->port].task_file_data & 0x80))
    {
      struct grub_disk_ata_pass_through_parms parms2;
      dev->hba->ports[dev->port].command &= ~GRUB_AHCI_HBA_PORT_CMD_ST;
      dev->hba->ports[dev->port].command_issue = 0;
      dev->command_list[0].config = 0;
      dev->command_table[0].prdt[0].unused = 0;
      dev->command_table[0].prdt[0].size = 0;
      dev->command_table[0].prdt[0].data_base = 0;

      endtime = grub_get_time_ms () + 1000;
      while ((dev->hba->ports[dev->port].command & GRUB_AHCI_HBA_PORT_CMD_CR))
	if (grub_get_time_ms () > endtime)
	  {
	    grub_dprintf ("ahci", "couldn't stop CR");
	    return grub_error (GRUB_ERR_IO, "couldn't stop CR");
	  }
      dev->hba->ports[dev->port].command |= 8;
      while (dev->hba->ports[dev->port].command & 8)
	if (grub_get_time_ms () > endtime)
	  {
	    grub_dprintf ("ahci", "couldn't set CLO\n");
	    dev->hba->ports[dev->port].command &= ~GRUB_AHCI_HBA_PORT_CMD_FRE;
	    return grub_error (GRUB_ERR_IO, "couldn't set CLO");
	  }

      dev->hba->ports[dev->port].command |= GRUB_AHCI_HBA_PORT_CMD_ST;
      while (!(dev->hba->ports[dev->port].command & GRUB_AHCI_HBA_PORT_CMD_CR))
	if (grub_get_time_ms () > endtime)
	  {
	    grub_dprintf ("ahci", "couldn't stop CR");
	    dev->hba->ports[dev->port].command &= ~GRUB_AHCI_HBA_PORT_CMD_ST;
	    return grub_error (GRUB_ERR_IO, "couldn't stop CR");
	  }
      dev->hba->ports[dev->port].sata_error = dev->hba->ports[dev->port].sata_error;
      grub_memset (&parms2, 0, sizeof (parms2));
      parms2.taskfile.cmd = 8;
      return grub_ahci_readwrite_real (dev, &parms2, 1, 1);
    }
  return GRUB_ERR_NONE;
}

static grub_err_t 
grub_ahci_readwrite_real (struct grub_ahci_device *dev,
			  struct grub_disk_ata_pass_through_parms *parms,
			  int spinup, int reset)
{
  struct grub_pci_dma_chunk *bufc;
  grub_uint64_t endtime;
  unsigned i;
  grub_err_t err = GRUB_ERR_NONE;

  grub_dprintf ("ahci", "AHCI tfd = %x\n",
		dev->hba->ports[dev->port].task_file_data);

  if (!reset)
    grub_ahci_reset_port (dev, 0);
 
  grub_dprintf ("ahci", "AHCI tfd = %x\n",
		dev->hba->ports[dev->port].task_file_data);
  dev->hba->ports[dev->port].task_file_data = 0;
  dev->hba->ports[dev->port].command_issue = 0;
  grub_dprintf ("ahci", "AHCI tfd = %x\n",
		dev->hba->ports[dev->port].task_file_data);

  dev->hba->ports[dev->port].sata_error = dev->hba->ports[dev->port].sata_error;

  grub_dprintf("ahci", "grub_ahci_read (size=%llu, cmdsize = %llu)\n",
	       (unsigned long long) parms->size,
	       (unsigned long long) parms->cmdsize);

  if (parms->cmdsize != 0 && parms->cmdsize != 12 && parms->cmdsize != 16)
    return grub_error (GRUB_ERR_BUG, "incorrect ATAPI command size");

  if (parms->size > GRUB_AHCI_PRDT_MAX_CHUNK_LENGTH)
    return grub_error (GRUB_ERR_BUG, "too big data buffer");

  if (parms->size)
    bufc = grub_memalign_dma32 (1024, parms->size + (parms->size & 1));
  else
    bufc = grub_memalign_dma32 (1024, 512);

  grub_dprintf ("ahci", "AHCI tfd = %x, CL=%p\n",
		dev->hba->ports[dev->port].task_file_data,
		dev->command_list);
  /* FIXME: support port multipliers.  */
  dev->command_list[0].config
    = (5 << GRUB_AHCI_CONFIG_CFIS_LENGTH_SHIFT)
    //    | GRUB_AHCI_CONFIG_CLEAR_R_OK
    | (0 << GRUB_AHCI_CONFIG_PMP_SHIFT)
    | ((parms->size ? 1 : 0) << GRUB_AHCI_CONFIG_PRDT_LENGTH_SHIFT)
    | (parms->cmdsize ? GRUB_AHCI_CONFIG_ATAPI : 0)
    | (parms->write ? GRUB_AHCI_CONFIG_WRITE : GRUB_AHCI_CONFIG_READ)
    | (parms->taskfile.cmd == 8 ? (1 << 8) : 0);
  grub_dprintf ("ahci", "AHCI tfd = %x\n",
		dev->hba->ports[dev->port].task_file_data);

  dev->command_list[0].transferred = 0;
  dev->command_list[0].command_table_base
    = grub_dma_get_phys (dev->command_table_chunk);

  grub_memset ((char *) dev->command_list[0].unused, 0,
	       sizeof (dev->command_list[0].unused));

  grub_memset ((char *) &dev->command_table[0], 0,
	       sizeof (dev->command_table[0]));
  grub_dprintf ("ahci", "AHCI tfd = %x\n",
		dev->hba->ports[dev->port].task_file_data);

  if (parms->cmdsize)
    grub_memcpy ((char *) dev->command_table[0].command, parms->cmd,
		 parms->cmdsize);

  grub_dprintf ("ahci", "AHCI tfd = %x\n",
		dev->hba->ports[dev->port].task_file_data);

  dev->command_table[0].cfis[0] = GRUB_AHCI_FIS_REG_H2D;
  dev->command_table[0].cfis[1] = 0x80;
  for (i = 0; i < sizeof (parms->taskfile.raw); i++)
    dev->command_table[0].cfis[register_map[i]] = parms->taskfile.raw[i]; 

  grub_dprintf ("ahci", "cfis: %02x %02x %02x %02x %02x %02x %02x %02x\n",
		dev->command_table[0].cfis[0], dev->command_table[0].cfis[1],
		dev->command_table[0].cfis[2], dev->command_table[0].cfis[3],
		dev->command_table[0].cfis[4], dev->command_table[0].cfis[5],
		dev->command_table[0].cfis[6], dev->command_table[0].cfis[7]);
  grub_dprintf ("ahci", "cfis: %02x %02x %02x %02x %02x %02x %02x %02x\n",
		dev->command_table[0].cfis[8], dev->command_table[0].cfis[9],
		dev->command_table[0].cfis[10], dev->command_table[0].cfis[11],
		dev->command_table[0].cfis[12], dev->command_table[0].cfis[13],
		dev->command_table[0].cfis[14], dev->command_table[0].cfis[15]);

  dev->command_table[0].prdt[0].data_base = grub_dma_get_phys (bufc);
  dev->command_table[0].prdt[0].unused = 0;
  dev->command_table[0].prdt[0].size = (parms->size - 1);

  grub_dprintf ("ahci", "PRDT = %" PRIxGRUB_UINT64_T ", %x, %x (%"
		PRIuGRUB_SIZE ")\n",
		dev->command_table[0].prdt[0].data_base,
		dev->command_table[0].prdt[0].unused,
		dev->command_table[0].prdt[0].size,
		(grub_size_t) ((char *) &dev->command_table[0].prdt[0]
			       - (char *) &dev->command_table[0]));

  if (parms->write)
    grub_memcpy ((char *) grub_dma_get_virt (bufc), parms->buffer, parms->size);

  grub_dprintf ("ahci", "AHCI command scheduled\n");
  grub_dprintf ("ahci", "AHCI tfd = %x\n",
		dev->hba->ports[dev->port].task_file_data);
  grub_dprintf ("ahci", "AHCI inten = %x\n",
		dev->hba->ports[dev->port].inten);
  grub_dprintf ("ahci", "AHCI intstatus = %x\n",
		dev->hba->ports[dev->port].intstatus);

  dev->hba->ports[dev->port].inten = 0xffffffff;//(1 << 2) | (1 << 5);
  dev->hba->ports[dev->port].intstatus = 0xffffffff;//(1 << 2) | (1 << 5);
  grub_dprintf ("ahci", "AHCI inten = %x\n",
		dev->hba->ports[dev->port].inten);
  grub_dprintf ("ahci", "AHCI tfd = %x\n",
		dev->hba->ports[dev->port].task_file_data);
  dev->hba->ports[dev->port].sata_active = 1;
  dev->hba->ports[dev->port].command_issue = 1;
  grub_dprintf ("ahci", "AHCI sig = %x\n", dev->hba->ports[dev->port].sig);
  grub_dprintf ("ahci", "AHCI tfd = %x\n",
		dev->hba->ports[dev->port].task_file_data);

  endtime = grub_get_time_ms () + (spinup ? 20000 : 20000);
  while ((dev->hba->ports[dev->port].command_issue & 1))
    if (grub_get_time_ms () > endtime ||
	(dev->hba->ports[dev->port].intstatus & GRUB_AHCI_HBA_PORT_IS_FATAL_MASK))
      {
	grub_dprintf ("ahci", "AHCI status <%x %x %x %x>\n",
		      dev->hba->ports[dev->port].command_issue,
		      dev->hba->ports[dev->port].sata_active,
		      dev->hba->ports[dev->port].intstatus,
		      dev->hba->ports[dev->port].task_file_data);
	dev->hba->ports[dev->port].command_issue = 0;
	if (dev->hba->ports[dev->port].intstatus & GRUB_AHCI_HBA_PORT_IS_FATAL_MASK)
	  err = grub_error (GRUB_ERR_IO, "AHCI transfer error");
	else
	  err = grub_error (GRUB_ERR_IO, "AHCI transfer timed out");
	if (!reset)
	  grub_ahci_reset_port (dev, 1);
	break;
      }

  grub_dprintf ("ahci", "AHCI command completed <%x %x %x %x %x, %x %x>\n",
		dev->hba->ports[dev->port].command_issue,
		dev->hba->ports[dev->port].intstatus,
		dev->hba->ports[dev->port].task_file_data,
		dev->command_list[0].transferred,
		dev->hba->ports[dev->port].sata_error,
		((grub_uint32_t *) grub_dma_get_virt (dev->rfis))[0x00],
		((grub_uint32_t *) grub_dma_get_virt (dev->rfis))[0x18]);
  grub_dprintf ("ahci",
		"last PIO FIS %08x %08x %08x %08x %08x %08x %08x %08x\n",
		((grub_uint32_t *) grub_dma_get_virt (dev->rfis))[0x08],
		((grub_uint32_t *) grub_dma_get_virt (dev->rfis))[0x09],
		((grub_uint32_t *) grub_dma_get_virt (dev->rfis))[0x0a],
		((grub_uint32_t *) grub_dma_get_virt (dev->rfis))[0x0b],
		((grub_uint32_t *) grub_dma_get_virt (dev->rfis))[0x0c],
		((grub_uint32_t *) grub_dma_get_virt (dev->rfis))[0x0d],
		((grub_uint32_t *) grub_dma_get_virt (dev->rfis))[0x0e],
		((grub_uint32_t *) grub_dma_get_virt (dev->rfis))[0x0f]);
  grub_dprintf ("ahci",
		"last REG FIS %08x %08x %08x %08x %08x %08x %08x %08x\n",
		((grub_uint32_t *) grub_dma_get_virt (dev->rfis))[0x10],
		((grub_uint32_t *) grub_dma_get_virt (dev->rfis))[0x11],
		((grub_uint32_t *) grub_dma_get_virt (dev->rfis))[0x12],
		((grub_uint32_t *) grub_dma_get_virt (dev->rfis))[0x13],
		((grub_uint32_t *) grub_dma_get_virt (dev->rfis))[0x14],
		((grub_uint32_t *) grub_dma_get_virt (dev->rfis))[0x15],
		((grub_uint32_t *) grub_dma_get_virt (dev->rfis))[0x16],
		((grub_uint32_t *) grub_dma_get_virt (dev->rfis))[0x17]);

  if (!parms->write)
    grub_memcpy (parms->buffer, (char *) grub_dma_get_virt (bufc), parms->size);
  grub_dma_free (bufc);

  return err;
}

static grub_err_t 
grub_ahci_readwrite (grub_ata_t disk,
		     struct grub_disk_ata_pass_through_parms *parms,
		     int spinup)
{
  return grub_ahci_readwrite_real (disk->data, parms, spinup, 0);
}

static grub_err_t
grub_ahci_open (int id, int devnum, struct grub_ata *ata)
{
  struct grub_ahci_device *dev;

  if (id != GRUB_SCSI_SUBSYSTEM_AHCI)
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "not an AHCI device");

  FOR_LIST_ELEMENTS(dev, grub_ahci_devices)
    if (dev->num == devnum)
      break;

  if (! dev)
    return grub_error (GRUB_ERR_UNKNOWN_DEVICE, "no such AHCI device");

  grub_dprintf ("ahci", "opening AHCI dev `ahci%d'\n", dev->num);

  ata->data = dev;
  ata->dma = 1;
  ata->atapi = dev->atapi;
  ata->maxbuffer = GRUB_AHCI_PRDT_MAX_CHUNK_LENGTH;
  ata->present = &dev->present;

  return GRUB_ERR_NONE;
}

static struct grub_ata_dev grub_ahci_dev =
  {
    .iterate = grub_ahci_iterate,
    .open = grub_ahci_open,
    .readwrite = grub_ahci_readwrite,
  };



static struct grub_preboot *fini_hnd;

GRUB_MOD_INIT(ahci)
{
  grub_stop_disk_firmware ();

  /* AHCI initialization.  */
  grub_ahci_initialize ();

  /* AHCI devices are handled by scsi.mod.  */
  grub_ata_dev_register (&grub_ahci_dev);

  fini_hnd = grub_loader_register_preboot_hook (grub_ahci_fini_hw,
						grub_ahci_restore_hw,
						GRUB_LOADER_PREBOOT_HOOK_PRIO_DISK);
}

GRUB_MOD_FINI(ahci)
{
  grub_ahci_fini_hw (0);
  grub_loader_unregister_preboot_hook (fini_hnd);

  grub_ata_dev_unregister (&grub_ahci_dev);
}
