/* uhci.c - UHCI Support.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008  Free Software Foundation, Inc.
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
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/usb.h>
#include <grub/usbtrans.h>
#include <grub/pci.h>
#include <grub/cpu/io.h>
#include <grub/time.h>
#include <grub/cpu/pci.h>
#include <grub/disk.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define GRUB_UHCI_IOMASK	(0x7FF << 5)

#define N_QH  256
#define N_TD  640

typedef enum
  {
    GRUB_UHCI_REG_USBCMD = 0x00,
    GRUB_UHCI_REG_USBINTR = 0x04,
    GRUB_UHCI_REG_FLBASEADD = 0x08,
    GRUB_UHCI_REG_PORTSC1 = 0x10,
    GRUB_UHCI_REG_PORTSC2 = 0x12,
    GRUB_UHCI_REG_USBLEGSUP = 0xc0
  } grub_uhci_reg_t;

enum
  {
    GRUB_UHCI_DETECT_CHANGED = (1 << 1),
    GRUB_UHCI_DETECT_HAVE_DEVICE = 1,
    GRUB_UHCI_DETECT_LOW_SPEED = (1 << 8)
  };

/* R/WC legacy support bits */
enum
  {
    GRUB_UHCI_LEGSUP_END_A20GATE = (1 << 15),
    GRUB_UHCI_TRAP_BY_64H_WSTAT = (1 << 11),
    GRUB_UHCI_TRAP_BY_64H_RSTAT = (1 << 10),
    GRUB_UHCI_TRAP_BY_60H_WSTAT = (1 <<  9),
    GRUB_UHCI_TRAP_BY_60H_RSTAT = (1 <<  8)
  };

/* Reset all legacy support - clear all R/WC bits and all R/W bits */
#define GRUB_UHCI_RESET_LEGSUP_SMI ( GRUB_UHCI_LEGSUP_END_A20GATE \
                                     | GRUB_UHCI_TRAP_BY_64H_WSTAT \
                                     | GRUB_UHCI_TRAP_BY_64H_RSTAT \
                                     | GRUB_UHCI_TRAP_BY_60H_WSTAT \
                                     | GRUB_UHCI_TRAP_BY_60H_RSTAT )

/* Some UHCI commands */
#define GRUB_UHCI_CMD_RUN_STOP (1 << 0)
#define GRUB_UHCI_CMD_HCRESET  (1 << 1)
#define GRUB_UHCI_CMD_MAXP     (1 << 7)

/* Important bits in structures */
#define GRUB_UHCI_LINK_TERMINATE	1
#define GRUB_UHCI_LINK_QUEUE_HEAD	2

enum
  {
    GRUB_UHCI_REG_PORTSC_CONNECT_CHANGED = 0x0002,
    GRUB_UHCI_REG_PORTSC_PORT_ENABLED    = 0x0004,
    GRUB_UHCI_REG_PORTSC_RESUME          = 0x0040,
    GRUB_UHCI_REG_PORTSC_RESET           = 0x0200,
    GRUB_UHCI_REG_PORTSC_SUSPEND         = 0x1000,
    GRUB_UHCI_REG_PORTSC_RW = GRUB_UHCI_REG_PORTSC_PORT_ENABLED
    | GRUB_UHCI_REG_PORTSC_RESUME | GRUB_UHCI_REG_PORTSC_RESET
    | GRUB_UHCI_REG_PORTSC_SUSPEND,
    /* These bits should not be written as 1 unless we really need it */
    GRUB_UHCI_PORTSC_RWC = ((1 << 1) | (1 << 3) | (1 << 11) | (3 << 13))
  };

/* UHCI Queue Head.  */
struct grub_uhci_qh
{
  /* Queue head link pointer which points to the next queue head.  */
  grub_uint32_t linkptr;

  /* Queue element link pointer which points to the first data object
     within the queue.  */
  grub_uint32_t elinkptr;

  /* Queue heads are aligned on 16 bytes, pad so a queue head is 16
     bytes so we can store many in a 4K page.  */
  grub_uint8_t pad[8];
} GRUB_PACKED;

/* UHCI Transfer Descriptor.  */
struct grub_uhci_td
{
  /* Pointer to the next TD in the list.  */
  grub_uint32_t linkptr;

  /* Control and status bits.  */
  grub_uint32_t ctrl_status;

  /* All information required to transfer the Token packet.  */
  grub_uint32_t token;

  /* A pointer to the data buffer, UHCI requires this pointer to be 32
     bits.  */
  grub_uint32_t buffer;

  /* Another linkptr that is not overwritten by the Host Controller.
     This is GRUB specific.  */
  grub_uint32_t linkptr2;

  /* 3 additional 32 bits words reserved for the Host Controller Driver.  */
  grub_uint32_t data[3];
} GRUB_PACKED;

typedef volatile struct grub_uhci_td *grub_uhci_td_t;
typedef volatile struct grub_uhci_qh *grub_uhci_qh_t;

struct grub_uhci
{
  grub_port_t iobase;
  volatile grub_uint32_t *framelist_virt;
  grub_uint32_t framelist_phys;
  struct grub_pci_dma_chunk *framelist_chunk;

  /* N_QH Queue Heads.  */
  struct grub_pci_dma_chunk *qh_chunk;
  volatile grub_uhci_qh_t qh_virt;
  grub_uint32_t qh_phys;

  /* N_TD Transfer Descriptors.  */
  struct grub_pci_dma_chunk *td_chunk;
  volatile grub_uhci_td_t td_virt;
  grub_uint32_t td_phys;

  /* Free Transfer Descriptors.  */
  grub_uhci_td_t tdfree;

  int qh_busy[N_QH];

  struct grub_uhci *next;
};

static struct grub_uhci *uhci;

static grub_uint16_t
grub_uhci_readreg16 (struct grub_uhci *u, grub_uhci_reg_t reg)
{
  return grub_inw (u->iobase + reg);
}

#if 0
static grub_uint32_t
grub_uhci_readreg32 (struct grub_uhci *u, grub_uhci_reg_t reg)
{
  return grub_inl (u->iobase + reg);
}
#endif

static void
grub_uhci_writereg16 (struct grub_uhci *u,
		      grub_uhci_reg_t reg, grub_uint16_t val)
{
  grub_outw (val, u->iobase + reg);
}

static void
grub_uhci_writereg32 (struct grub_uhci *u,
		    grub_uhci_reg_t reg, grub_uint32_t val)
{
  grub_outl (val, u->iobase + reg);
}

/* Iterate over all PCI devices.  Determine if a device is an UHCI
   controller.  If this is the case, initialize it.  */
static int
grub_uhci_pci_iter (grub_pci_device_t dev,
		    grub_pci_id_t pciid __attribute__((unused)),
		    void *data __attribute__ ((unused)))
{
  grub_uint32_t class_code;
  grub_uint32_t class;
  grub_uint32_t subclass;
  grub_uint32_t interf;
  grub_uint32_t base;
  grub_uint32_t fp;
  grub_pci_address_t addr;
  struct grub_uhci *u;
  int i;

  addr = grub_pci_make_address (dev, GRUB_PCI_REG_CLASS);
  class_code = grub_pci_read (addr) >> 8;

  interf = class_code & 0xFF;
  subclass = (class_code >> 8) & 0xFF;
  class = class_code >> 16;

  /* If this is not an UHCI controller, just return.  */
  if (class != 0x0c || subclass != 0x03 || interf != 0x00)
    return 0;

  /* Determine IO base address.  */
  addr = grub_pci_make_address (dev, GRUB_PCI_REG_ADDRESS_REG4);
  base = grub_pci_read (addr);
  /* Stop if there is no IO space base address defined.  */
  if ((base & GRUB_PCI_ADDR_SPACE_MASK) != GRUB_PCI_ADDR_SPACE_IO)
    return 0;

  if ((base & GRUB_UHCI_IOMASK) == 0)
    return 0;

  /* Set bus master - needed for coreboot or broken BIOSes */
  addr = grub_pci_make_address (dev, GRUB_PCI_REG_COMMAND);
  grub_pci_write_word(addr, GRUB_PCI_COMMAND_IO_ENABLED
		      | GRUB_PCI_COMMAND_BUS_MASTER
		      | GRUB_PCI_COMMAND_MEM_ENABLED
		      | grub_pci_read_word (addr));

  grub_dprintf ("uhci", "base = %x\n", base);

  /* Allocate memory for the controller and register it.  */
  u = grub_zalloc (sizeof (*u));
  if (! u)
    return 1;

  u->iobase = (base & GRUB_UHCI_IOMASK) + GRUB_MACHINE_PCI_IO_BASE;

  /* Reset PIRQ and SMI */
  addr = grub_pci_make_address (dev, GRUB_UHCI_REG_USBLEGSUP);       
  grub_pci_write_word(addr, GRUB_UHCI_RESET_LEGSUP_SMI);
  /* Reset the HC */
  grub_uhci_writereg16(u, GRUB_UHCI_REG_USBCMD, GRUB_UHCI_CMD_HCRESET); 
  grub_millisleep(5);
  /* Disable interrupts and commands (just to be safe) */
  grub_uhci_writereg16(u, GRUB_UHCI_REG_USBINTR, 0);
  /* Finish HC reset, HC remains disabled */
  grub_uhci_writereg16(u, GRUB_UHCI_REG_USBCMD, 0);
  /* Read back to be sure PCI write is done */
  grub_uhci_readreg16(u, GRUB_UHCI_REG_USBCMD);

  /* Reserve a page for the frame list.  */
  u->framelist_chunk = grub_memalign_dma32 (4096, 4096);
  if (! u->framelist_chunk)
    goto fail;
  u->framelist_virt = grub_dma_get_virt (u->framelist_chunk);
  u->framelist_phys = grub_dma_get_phys (u->framelist_chunk);

  grub_dprintf ("uhci",
		"class=0x%02x 0x%02x interface 0x%02x base=0x%x framelist=%p\n",
		class, subclass, interf, u->iobase, u->framelist_virt);

  /* The QH pointer of UHCI is only 32 bits, make sure this
     code works on on 64 bits architectures.  */
  u->qh_chunk = grub_memalign_dma32 (4096, sizeof(struct grub_uhci_qh) * N_QH);
  if (! u->qh_chunk)
    goto fail;
  u->qh_virt = grub_dma_get_virt (u->qh_chunk);
  u->qh_phys = grub_dma_get_phys (u->qh_chunk);

  /* The TD pointer of UHCI is only 32 bits, make sure this
     code works on on 64 bits architectures.  */
  u->td_chunk = grub_memalign_dma32 (4096, sizeof(struct grub_uhci_td) * N_TD);
  if (! u->td_chunk)
    goto fail;
  u->td_virt = grub_dma_get_virt (u->td_chunk);
  u->td_phys = grub_dma_get_phys (u->td_chunk);

  grub_dprintf ("uhci", "QH=%p, TD=%p\n",
		u->qh_virt, u->td_virt);

  /* Link all Transfer Descriptors in a list of available Transfer
     Descriptors.  */
  for (i = 0; i < N_TD; i++)
    u->td_virt[i].linkptr = u->td_phys + (i + 1) * sizeof(struct grub_uhci_td);
  u->td_virt[N_TD - 2].linkptr = 0;
  u->tdfree = u->td_virt;

  /* Setup the frame list pointers.  Since no isochronous transfers
     are and will be supported, they all point to the (same!) queue
     head.  */
  fp = u->qh_phys & (~15);
  /* Mark this as a queue head.  */
  fp |= 2;
  for (i = 0; i < 1024; i++)
    u->framelist_virt[i] = fp;
  /* Program the framelist address into the UHCI controller.  */
  grub_uhci_writereg32 (u, GRUB_UHCI_REG_FLBASEADD, u->framelist_phys);

  /* Make the Queue Heads point to each other.  */
  for (i = 0; i < N_QH; i++)
    {
      /* Point to the next QH.  */
      u->qh_virt[i].linkptr = ((u->qh_phys
				+ (i + 1) * sizeof(struct grub_uhci_qh))
			  & (~15));

      /* This is a QH.  */
      u->qh_virt[i].linkptr |= GRUB_UHCI_LINK_QUEUE_HEAD;

      /* For the moment, do not point to a Transfer Descriptor.  These
	 are set at transfer time, so just terminate it.  */
      u->qh_virt[i].elinkptr = 1;
    }

  /* The last Queue Head should terminate.  */
  u->qh_virt[N_QH - 1].linkptr = 1;

  /* Enable UHCI again.  */
  grub_uhci_writereg16 (u, GRUB_UHCI_REG_USBCMD,
                        GRUB_UHCI_CMD_RUN_STOP | GRUB_UHCI_CMD_MAXP);

  /* UHCI is initialized and ready for transfers.  */
  grub_dprintf ("uhci", "UHCI initialized\n");


#if 0
  {
    int i;
    for (i = 0; i < 10; i++)
      {
	grub_uint16_t frnum;

	frnum = grub_uhci_readreg16 (u, 6);
	grub_dprintf ("uhci", "Framenum=%d\n", frnum);
	grub_millisleep (100);
      }
  }
#endif

  /* Link to uhci now that initialisation is successful.  */
  u->next = uhci;
  uhci = u;

  return 0;

 fail:
  if (u)
    {
      grub_dma_free (u->qh_chunk);
      grub_dma_free (u->framelist_chunk);
    }
  grub_free (u);

  return 1;
}

static void
grub_uhci_inithw (void)
{
  grub_pci_iterate (grub_uhci_pci_iter, NULL);
}

static grub_uhci_td_t
grub_alloc_td (struct grub_uhci *u)
{
  grub_uhci_td_t ret;

  /* Check if there is a Transfer Descriptor available.  */
  if (! u->tdfree)
    return NULL;

  ret = u->tdfree;
  u->tdfree = grub_dma_phys2virt (u->tdfree->linkptr, u->td_chunk);

  return ret;
}

static void
grub_free_td (struct grub_uhci *u, grub_uhci_td_t td)
{
  td->linkptr = grub_dma_virt2phys (u->tdfree, u->td_chunk);
  u->tdfree = td;
}

static void
grub_free_queue (struct grub_uhci *u, grub_uhci_qh_t qh, grub_uhci_td_t td,
                 grub_usb_transfer_t transfer, grub_size_t *actual)
{
  int i; /* Index of TD in transfer */

  u->qh_busy[qh - u->qh_virt] = 0;

  *actual = 0;
  
  /* Free the TDs in this queue and set last_trans.  */
  for (i=0; td; i++)
    {
      grub_uhci_td_t tdprev;

      grub_dprintf ("uhci", "Freeing %p\n", td);
      /* Check state of TD and possibly set last_trans */
      if (transfer && (td->linkptr & 1))
        transfer->last_trans = i;

      *actual += (td->ctrl_status + 1) & 0x7ff;
      
      /* Unlink the queue.  */
      tdprev = td;
      if (!td->linkptr2)
	td = 0;
      else
	td = grub_dma_phys2virt (td->linkptr2, u->td_chunk);

      /* Free the TD.  */
      grub_free_td (u, tdprev);
    }
}

static grub_uhci_qh_t
grub_alloc_qh (struct grub_uhci *u,
	       grub_transaction_type_t tr __attribute__((unused)))
{
  int i;
  grub_uhci_qh_t qh;

  /* Look for a Queue Head for this transfer.  Skip the first QH if
     this is a Interrupt Transfer.  */
#if 0
  if (tr == GRUB_USB_TRANSACTION_TYPE_INTERRUPT)
    i = 0;
  else
#endif
    i = 1;

  for (; i < N_QH; i++)
    {
      if (!u->qh_busy[i])
	break;
    }
  qh = &u->qh_virt[i];
  if (i == N_QH)
    {
      grub_error (GRUB_ERR_OUT_OF_MEMORY,
		  "no free queue heads available");
      return NULL;
    }

  u->qh_busy[qh - u->qh_virt] = 1;

  return qh;
}

static grub_uhci_td_t
grub_uhci_transaction (struct grub_uhci *u, unsigned int endp,
		       grub_transfer_type_t type, unsigned int addr,
		       unsigned int toggle, grub_size_t size,
		       grub_uint32_t data, grub_usb_speed_t speed)
{
  grub_uhci_td_t td;
  static const unsigned int tf[] = { 0x69, 0xE1, 0x2D };

  /* XXX: Check if data is <4GB.  If it isn't, just copy stuff around.
     This is only relevant for 64 bits architectures.  */

  /* Grab a free Transfer Descriptor and initialize it.  */
  td = grub_alloc_td (u);
  if (! td)
    {
      grub_error (GRUB_ERR_OUT_OF_MEMORY,
		  "no transfer descriptors available for UHCI transfer");
      return 0;
    }

  grub_dprintf ("uhci",
		"transaction: endp=%d, type=%d, addr=%d, toggle=%d, size=%lu data=0x%x td=%p\n",
		endp, type, addr, toggle, (unsigned long) size, data, td);

  /* Don't point to any TD, just terminate.  */
  td->linkptr = 1;

  /* Active!  Only retry a transfer 3 times.  */
  td->ctrl_status = (1 << 23) | (3 << 27) |
                    ((speed == GRUB_USB_SPEED_LOW) ? (1 << 26) : 0);

  /* If zero bytes are transmitted, size is 0x7FF.  Otherwise size is
     size-1.  */
  if (size == 0)
    size = 0x7FF;
  else
    size = size - 1;

  /* Setup whatever is required for the token packet.  */
  td->token = ((size << 21) | (toggle << 19) | (endp << 15)
	       | (addr << 8) | tf[type]);

  td->buffer = data;

  return td;
}

struct grub_uhci_transfer_controller_data
{
  grub_uhci_qh_t qh;
  grub_uhci_td_t td_first;
};

static grub_usb_err_t
grub_uhci_setup_transfer (grub_usb_controller_t dev,
			  grub_usb_transfer_t transfer)
{
  struct grub_uhci *u = (struct grub_uhci *) dev->data;
  grub_uhci_td_t td;
  grub_uhci_td_t td_prev = NULL;
  int i;
  struct grub_uhci_transfer_controller_data *cdata;

  cdata = grub_malloc (sizeof (*cdata));
  if (!cdata)
    return GRUB_USB_ERR_INTERNAL;

  cdata->td_first = NULL;

  /* Allocate a queue head for the transfer queue.  */
  cdata->qh = grub_alloc_qh (u, GRUB_USB_TRANSACTION_TYPE_CONTROL);
  if (! cdata->qh)
    {
      grub_free (cdata);
      return GRUB_USB_ERR_INTERNAL;
    }

  grub_dprintf ("uhci", "transfer, iobase:%08x\n", u->iobase);
  
  for (i = 0; i < transfer->transcnt; i++)
    {
      grub_usb_transaction_t tr = &transfer->transactions[i];

      td = grub_uhci_transaction (u, transfer->endpoint & 15, tr->pid,
				  transfer->devaddr, tr->toggle,
				  tr->size, tr->data,
				  transfer->dev->speed);
      if (! td)
	{
	  grub_size_t actual = 0;
	  /* Terminate and free.  */
	  if (td_prev)
	    {
	      td_prev->linkptr2 = 0;
	      td_prev->linkptr = 1;
	    }

	  if (cdata->td_first)
	    grub_free_queue (u, cdata->qh, cdata->td_first, NULL, &actual);

	  grub_free (cdata);
	  return GRUB_USB_ERR_INTERNAL;
	}

      if (! cdata->td_first)
	cdata->td_first = td;
      else
	{
	  td_prev->linkptr2 = grub_dma_virt2phys (td, u->td_chunk);
	  td_prev->linkptr = grub_dma_virt2phys (td, u->td_chunk);
	  td_prev->linkptr |= 4;
	}
      td_prev = td;
    }
  td_prev->linkptr2 = 0;
  td_prev->linkptr = 1;

  grub_dprintf ("uhci", "setup transaction %d\n", transfer->type);

  /* Link it into the queue and terminate.  Now the transaction can
     take place.  */
  cdata->qh->elinkptr = grub_dma_virt2phys (cdata->td_first, u->td_chunk);

  grub_dprintf ("uhci", "initiate transaction\n");

  transfer->controller_data = cdata;

  return GRUB_USB_ERR_NONE;
}

static grub_usb_err_t
grub_uhci_check_transfer (grub_usb_controller_t dev,
			  grub_usb_transfer_t transfer,
			  grub_size_t *actual)
{
  struct grub_uhci *u = (struct grub_uhci *) dev->data;
  grub_uhci_td_t errtd;
  struct grub_uhci_transfer_controller_data *cdata = transfer->controller_data;

  *actual = 0;

  if (cdata->qh->elinkptr & ~0x0f)
    errtd = grub_dma_phys2virt (cdata->qh->elinkptr & ~0x0f, u->qh_chunk);
  else
    errtd = 0;
  
  if (errtd)
    {
      grub_dprintf ("uhci", ">t status=0x%02x data=0x%02x td=%p, %x\n",
		    errtd->ctrl_status, errtd->buffer & (~15), errtd,
		    cdata->qh->elinkptr);
    }

  /* Check if the transaction completed.  */
  if (cdata->qh->elinkptr & 1)
    {
      grub_dprintf ("uhci", "transaction complete\n");

      /* Place the QH back in the free list and deallocate the associated
	 TDs.  */
      cdata->qh->elinkptr = 1;
      grub_free_queue (u, cdata->qh, cdata->td_first, transfer, actual);
      grub_free (cdata);
      return GRUB_USB_ERR_NONE;
    }

  if (errtd && !(errtd->ctrl_status & (1 << 23)))
    {
      grub_usb_err_t err = GRUB_USB_ERR_NONE;

      /* Check if the endpoint is stalled.  */
      if (errtd->ctrl_status & (1 << 22))
	err = GRUB_USB_ERR_STALL;
      
      /* Check if an error related to the data buffer occurred.  */
      else if (errtd->ctrl_status & (1 << 21))
	err = GRUB_USB_ERR_DATA;
      
      /* Check if a babble error occurred.  */
      else if (errtd->ctrl_status & (1 << 20))
	err = GRUB_USB_ERR_BABBLE;
      
      /* Check if a NAK occurred.  */
      else if (errtd->ctrl_status & (1 << 19))
	err = GRUB_USB_ERR_NAK;
      
      /* Check if a timeout occurred.  */
      else if (errtd->ctrl_status & (1 << 18))
	err = GRUB_USB_ERR_TIMEOUT;
      
      /* Check if a bitstuff error occurred.  */
      else if (errtd->ctrl_status & (1 << 17))
	err = GRUB_USB_ERR_BITSTUFF;
      
      if (err)
	{
	  grub_dprintf ("uhci", "transaction failed\n");

	  /* Place the QH back in the free list and deallocate the associated
	     TDs.  */
	  cdata->qh->elinkptr = 1;
	  grub_free_queue (u, cdata->qh, cdata->td_first, transfer, actual);
	  grub_free (cdata);

	  return err;
	}
    }

  /* Fall through, no errors occurred, so the QH might be
     updated.  */
  grub_dprintf ("uhci", "transaction fallthrough\n");

  return GRUB_USB_ERR_WAIT;
}

static grub_usb_err_t
grub_uhci_cancel_transfer (grub_usb_controller_t dev,
			   grub_usb_transfer_t transfer)
{
  struct grub_uhci *u = (struct grub_uhci *) dev->data;
  grub_size_t actual;
  struct grub_uhci_transfer_controller_data *cdata = transfer->controller_data;

  grub_dprintf ("uhci", "transaction cancel\n");

  /* Place the QH back in the free list and deallocate the associated
     TDs.  */
  cdata->qh->elinkptr = 1;
  grub_free_queue (u, cdata->qh, cdata->td_first, transfer, &actual);
  grub_free (cdata);

  return GRUB_USB_ERR_NONE;
}

static int
grub_uhci_iterate (grub_usb_controller_iterate_hook_t hook, void *hook_data)
{
  struct grub_uhci *u;
  struct grub_usb_controller dev;

  for (u = uhci; u; u = u->next)
    {
      dev.data = u;
      if (hook (&dev, hook_data))
	return 1;
    }

  return 0;
}

static grub_usb_err_t
grub_uhci_portstatus (grub_usb_controller_t dev,
		      unsigned int port, unsigned int enable)
{
  struct grub_uhci *u = (struct grub_uhci *) dev->data;
  int reg;
  unsigned int status;
  grub_uint64_t endtime;

  grub_dprintf ("uhci", "portstatus, iobase:%08x\n", u->iobase);
  
  grub_dprintf ("uhci", "enable=%d port=%d\n", enable, port);

  if (port == 0)
    reg = GRUB_UHCI_REG_PORTSC1;
  else if (port == 1)
    reg = GRUB_UHCI_REG_PORTSC2;
  else
    return GRUB_USB_ERR_INTERNAL;

  status = grub_uhci_readreg16 (u, reg);
  grub_dprintf ("uhci", "detect=0x%02x\n", status);

  if (!enable) /* We don't need reset port */
    {
      /* Disable the port.  */
      grub_uhci_writereg16 (u, reg, 0 << 2);
      grub_dprintf ("uhci", "waiting for the port to be disabled\n");
      endtime = grub_get_time_ms () + 1000;
      while ((grub_uhci_readreg16 (u, reg) & (1 << 2)))
        if (grub_get_time_ms () > endtime)
          return GRUB_USB_ERR_TIMEOUT;

      status = grub_uhci_readreg16 (u, reg);
      grub_dprintf ("uhci", ">3detect=0x%02x\n", status);
      return GRUB_USB_ERR_NONE;
    }
    
  /* Reset the port.  */
  status = grub_uhci_readreg16 (u, reg) & ~GRUB_UHCI_PORTSC_RWC;
  grub_uhci_writereg16 (u, reg, status | (1 << 9));
  grub_uhci_readreg16 (u, reg); /* Ensure it is writen... */

  /* Wait for the reset to complete.  XXX: How long exactly?  */
  grub_millisleep (50); /* For root hub should be nominaly 50ms */
  status = grub_uhci_readreg16 (u, reg) & ~GRUB_UHCI_PORTSC_RWC;
  grub_uhci_writereg16 (u, reg, status & ~(1 << 9));
  grub_uhci_readreg16 (u, reg); /* Ensure it is writen... */

  /* Note: some debug prints were removed because they affected reset/enable timing. */

  grub_millisleep (1); /* Probably not needed at all or only few microsecs. */

  /* Reset bits Connect & Enable Status Change */
  status = grub_uhci_readreg16 (u, reg) & ~GRUB_UHCI_PORTSC_RWC;
  grub_uhci_writereg16 (u, reg, status | (1 << 3) | GRUB_UHCI_REG_PORTSC_CONNECT_CHANGED);
  grub_uhci_readreg16 (u, reg); /* Ensure it is writen... */

  /* Enable the port.  */
  status = grub_uhci_readreg16 (u, reg) & ~GRUB_UHCI_PORTSC_RWC;
  grub_uhci_writereg16 (u, reg, status | (1 << 2));
  grub_uhci_readreg16 (u, reg); /* Ensure it is writen... */

  endtime = grub_get_time_ms () + 1000;
  while (! ((status = grub_uhci_readreg16 (u, reg)) & (1 << 2)))
    if (grub_get_time_ms () > endtime)
      return GRUB_USB_ERR_TIMEOUT;

  /* Reset recovery time */
  grub_millisleep (10);

  /* Read final port status */
  status = grub_uhci_readreg16 (u, reg);
  grub_dprintf ("uhci", ">3detect=0x%02x\n", status);


  return GRUB_USB_ERR_NONE;
}

static grub_usb_speed_t
grub_uhci_detect_dev (grub_usb_controller_t dev, int port, int *changed)
{
  struct grub_uhci *u = (struct grub_uhci *) dev->data;
  int reg;
  unsigned int status;

  grub_dprintf ("uhci", "detect_dev, iobase:%08x\n", u->iobase);
  
  if (port == 0)
    reg = GRUB_UHCI_REG_PORTSC1;
  else if (port == 1)
    reg = GRUB_UHCI_REG_PORTSC2;
  else
    return GRUB_USB_SPEED_NONE;

  status = grub_uhci_readreg16 (u, reg);

  grub_dprintf ("uhci", "detect=0x%02x port=%d\n", status, port);

  /* Connect Status Change bit - it detects change of connection */
  if (status & GRUB_UHCI_DETECT_CHANGED)
    {
      *changed = 1;
      /* Reset bit Connect Status Change */
      grub_uhci_writereg16 (u, reg, (status & GRUB_UHCI_REG_PORTSC_RW)
			    | GRUB_UHCI_REG_PORTSC_CONNECT_CHANGED);
    }
  else
    *changed = 0;
    
  if (! (status & GRUB_UHCI_DETECT_HAVE_DEVICE))
    return GRUB_USB_SPEED_NONE;
  else if (status & GRUB_UHCI_DETECT_LOW_SPEED)
    return GRUB_USB_SPEED_LOW;
  else
    return GRUB_USB_SPEED_FULL;
}

static int
grub_uhci_hubports (grub_usb_controller_t dev __attribute__((unused)))
{
  /* The root hub has exactly two ports.  */
  return 2;
}


static struct grub_usb_controller_dev usb_controller =
{
  .name = "uhci",
  .iterate = grub_uhci_iterate,
  .setup_transfer = grub_uhci_setup_transfer,
  .check_transfer = grub_uhci_check_transfer,
  .cancel_transfer = grub_uhci_cancel_transfer,
  .hubports = grub_uhci_hubports,
  .portstatus = grub_uhci_portstatus,
  .detect_dev = grub_uhci_detect_dev,
  .attach_dev = NULL,
  .detach_dev = NULL,
  /* estimated max. count of TDs for one bulk transfer */
  .max_bulk_tds = N_TD * 3 / 4
};

GRUB_MOD_INIT(uhci)
{
  grub_stop_disk_firmware ();

  grub_uhci_inithw ();
  grub_usb_controller_dev_register (&usb_controller);
  grub_dprintf ("uhci", "registered\n");
}

GRUB_MOD_FINI(uhci)
{
  struct grub_uhci *u;

  /* Disable all UHCI controllers.  */
  for (u = uhci; u; u = u->next)
    grub_uhci_writereg16 (u, GRUB_UHCI_REG_USBCMD, 0);

  /* Unregister the controller.  */
  grub_usb_controller_dev_unregister (&usb_controller);
}
