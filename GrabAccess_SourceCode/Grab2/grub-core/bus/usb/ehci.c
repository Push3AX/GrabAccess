/* ehci.c - EHCI Support.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2011  Free Software Foundation, Inc.
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
#include <grub/usb.h>
#include <grub/usbtrans.h>
#include <grub/misc.h>
#include <grub/time.h>
#include <grub/loader.h>
#include <grub/disk.h>
#include <grub/dma.h>
#include <grub/cache.h>

GRUB_MOD_LICENSE ("GPLv3+");

/* This simple GRUB implementation of EHCI driver:
 *      - assumes no IRQ
 *      - is not supporting isochronous transfers (iTD, siTD)
 *      - is not supporting interrupt transfers
 */

/* Capability registers offsets */
enum
{
  GRUB_EHCI_EHCC_CAPLEN = 0x00,	/* byte */
  GRUB_EHCI_EHCC_VERSION = 0x02,	/* word */
  GRUB_EHCI_EHCC_SPARAMS = 0x04,	/* dword */
  GRUB_EHCI_EHCC_CPARAMS = 0x08,	/* dword */
  GRUB_EHCI_EHCC_PROUTE = 0x0c,	/* 60 bits */
};

#define GRUB_EHCI_EECP_MASK     (0xff << 8)
#define GRUB_EHCI_EECP_SHIFT    8

#define GRUB_EHCI_POINTER_MASK	(~0x1f)

/* Capability register SPARAMS bits */
enum
{
  GRUB_EHCI_SPARAMS_N_PORTS = (0xf << 0),
  GRUB_EHCI_SPARAMS_PPC = (1 << 4),	/* Power port control */
  GRUB_EHCI_SPARAMS_PRR = (1 << 7),	/* Port routing rules */
  GRUB_EHCI_SPARAMS_N_PCC = (0xf << 8),	/* No of ports per comp. */
  GRUB_EHCI_SPARAMS_NCC = (0xf << 12),	/* No of com. controllers */
  GRUB_EHCI_SPARAMS_P_IND = (1 << 16),	/* Port indicators present */
  GRUB_EHCI_SPARAMS_DEBUG_P = (0xf << 20)	/* Debug port */
};

#define GRUB_EHCI_MAX_N_PORTS     15	/* Max. number of ports */

/* Capability register CPARAMS bits */
enum
{
  GRUB_EHCI_CPARAMS_64BIT = (1 << 0),
  GRUB_EHCI_CPARAMS_PROG_FRAMELIST = (1 << 1),
  GRUB_EHCI_CPARAMS_PARK_CAP = (1 << 2)
};

#define GRUB_EHCI_N_FRAMELIST   1024
#define GRUB_EHCI_N_QH  256
#define GRUB_EHCI_N_TD  640

#define GRUB_EHCI_QH_EMPTY 1

/* Operational registers offsets */
enum
{
  GRUB_EHCI_COMMAND = 0x00,
  GRUB_EHCI_STATUS = 0x04,
  GRUB_EHCI_INTERRUPT = 0x08,
  GRUB_EHCI_FRAME_INDEX = 0x0c,
  GRUB_EHCI_64BIT_SEL = 0x10,
  GRUB_EHCI_FL_BASE = 0x14,
  GRUB_EHCI_CUR_AL_ADDR = 0x18,
  GRUB_EHCI_CONFIG_FLAG = 0x40,
  GRUB_EHCI_PORT_STAT_CMD = 0x44
};

/* Operational register COMMAND bits */
enum
{
  GRUB_EHCI_CMD_RUNSTOP = (1 << 0),
  GRUB_EHCI_CMD_HC_RESET = (1 << 1),
  GRUB_EHCI_CMD_FL_SIZE = (3 << 2),
  GRUB_EHCI_CMD_PS_ENABL = (1 << 4),
  GRUB_EHCI_CMD_AS_ENABL = (1 << 5),
  GRUB_EHCI_CMD_AS_ADV_D = (1 << 6),
  GRUB_EHCI_CMD_L_HC_RES = (1 << 7),
  GRUB_EHCI_CMD_AS_PARKM = (3 << 8),
  GRUB_EHCI_CMD_AS_PARKE = (1 << 11),
  GRUB_EHCI_CMD_INT_THRS = (0xff << 16)
};

/* Operational register STATUS bits */
enum
{
  GRUB_EHCI_ST_INTERRUPT = (1 << 0),
  GRUB_EHCI_ST_ERROR_INT = (1 << 1),
  GRUB_EHCI_ST_PORT_CHG = (1 << 2),
  GRUB_EHCI_ST_FL_ROLLOVR = (1 << 3),
  GRUB_EHCI_ST_HS_ERROR = (1 << 4),
  GRUB_EHCI_ST_AS_ADVANCE = (1 << 5),
  GRUB_EHCI_ST_HC_HALTED = (1 << 12),
  GRUB_EHCI_ST_RECLAM = (1 << 13),
  GRUB_EHCI_ST_PS_STATUS = (1 << 14),
  GRUB_EHCI_ST_AS_STATUS = (1 << 15)
};

/* Operational register PORT_STAT_CMD bits */
enum
{
  GRUB_EHCI_PORT_CONNECT = (1 << 0),
  GRUB_EHCI_PORT_CONNECT_CH = (1 << 1),
  GRUB_EHCI_PORT_ENABLED = (1 << 2),
  GRUB_EHCI_PORT_ENABLED_CH = (1 << 3),
  GRUB_EHCI_PORT_OVERCUR = (1 << 4),
  GRUB_EHCI_PORT_OVERCUR_CH = (1 << 5),
  GRUB_EHCI_PORT_RESUME = (1 << 6),
  GRUB_EHCI_PORT_SUSPEND = (1 << 7),
  GRUB_EHCI_PORT_RESET = (1 << 8),
  GRUB_EHCI_PORT_LINE_STAT = (3 << 10),
  GRUB_EHCI_PORT_POWER = (1 << 12),
  GRUB_EHCI_PORT_OWNER = (1 << 13),
  GRUB_EHCI_PORT_INDICATOR = (3 << 14),
  GRUB_EHCI_PORT_TEST = (0xf << 16),
  GRUB_EHCI_PORT_WON_CONN_E = (1 << 20),
  GRUB_EHCI_PORT_WON_DISC_E = (1 << 21),
  GRUB_EHCI_PORT_WON_OVER_E = (1 << 22),

  GRUB_EHCI_PORT_LINE_SE0 = (0 << 10),
  GRUB_EHCI_PORT_LINE_K = (1 << 10),
  GRUB_EHCI_PORT_LINE_J = (2 << 10),
  GRUB_EHCI_PORT_LINE_UNDEF = (3 << 10),
  GRUB_EHCI_PORT_LINE_LOWSP = GRUB_EHCI_PORT_LINE_K,	/* K state means low speed */
  GRUB_EHCI_PORT_WMASK = ~(GRUB_EHCI_PORT_CONNECT_CH
			   | GRUB_EHCI_PORT_ENABLED_CH
			   | GRUB_EHCI_PORT_OVERCUR_CH)
};

/* Operational register CONFIGFLAGS bits */
enum
{
  GRUB_EHCI_CF_EHCI_OWNER = (1 << 0)
};

/* Queue Head & Transfer Descriptor constants */
#define GRUB_EHCI_HPTR_OFF       5	/* Horiz. pointer bit offset */
enum
{
  GRUB_EHCI_HPTR_TYPE_MASK = (3 << 1),
  GRUB_EHCI_HPTR_TYPE_ITD = (0 << 1),
  GRUB_EHCI_HPTR_TYPE_QH = (1 << 1),
  GRUB_EHCI_HPTR_TYPE_SITD = (2 << 1),
  GRUB_EHCI_HPTR_TYPE_FSTN = (3 << 1)
};

enum
{
  GRUB_EHCI_C = (1 << 27),
  GRUB_EHCI_MAXPLEN_MASK = (0x7ff << 16),
  GRUB_EHCI_H = (1 << 15),
  GRUB_EHCI_DTC = (1 << 14),
  GRUB_EHCI_SPEED_MASK = (3 << 12),
  GRUB_EHCI_SPEED_FULL = (0 << 12),
  GRUB_EHCI_SPEED_LOW = (1 << 12),
  GRUB_EHCI_SPEED_HIGH = (2 << 12),
  GRUB_EHCI_SPEED_RESERVED = (3 << 12),
  GRUB_EHCI_EP_NUM_MASK = (0xf << 8),
  GRUB_EHCI_DEVADDR_MASK = 0x7f,
  GRUB_EHCI_TARGET_MASK = (GRUB_EHCI_EP_NUM_MASK | GRUB_EHCI_DEVADDR_MASK)
};

enum
{
  GRUB_EHCI_MAXPLEN_OFF = 16,
  GRUB_EHCI_SPEED_OFF = 12,
  GRUB_EHCI_EP_NUM_OFF = 8
};

enum
{
  GRUB_EHCI_MULT_MASK = (3 << 30),
  GRUB_EHCI_MULT_RESERVED = (0 << 30),
  GRUB_EHCI_MULT_ONE = (1 << 30),
  GRUB_EHCI_MULT_TWO = (2 << 30),
  GRUB_EHCI_MULT_THREE = (3 << 30),
  GRUB_EHCI_DEVPORT_MASK = (0x7f << 23),
  GRUB_EHCI_HUBADDR_MASK = (0x7f << 16),
  GRUB_EHCI_CMASK_MASK = (0xff << 8),
  GRUB_EHCI_SMASK_MASK = (0xff << 0),
};

enum
{
  GRUB_EHCI_MULT_OFF = 30,
  GRUB_EHCI_DEVPORT_OFF = 23,
  GRUB_EHCI_HUBADDR_OFF = 16,
  GRUB_EHCI_CMASK_OFF = 8,
  GRUB_EHCI_SMASK_OFF = 0,
};

#define GRUB_EHCI_TERMINATE      (1<<0)

#define GRUB_EHCI_TOGGLE         (1<<31)

enum
{
  GRUB_EHCI_TOTAL_MASK = (0x7fff << 16),
  GRUB_EHCI_CERR_MASK = (3 << 10),
  GRUB_EHCI_CERR_0 = (0 << 10),
  GRUB_EHCI_CERR_1 = (1 << 10),
  GRUB_EHCI_CERR_2 = (2 << 10),
  GRUB_EHCI_CERR_3 = (3 << 10),
  GRUB_EHCI_PIDCODE_OUT = (0 << 8),
  GRUB_EHCI_PIDCODE_IN = (1 << 8),
  GRUB_EHCI_PIDCODE_SETUP = (2 << 8),
  GRUB_EHCI_STATUS_MASK = 0xff,
  GRUB_EHCI_STATUS_ACTIVE = (1 << 7),
  GRUB_EHCI_STATUS_HALTED = (1 << 6),
  GRUB_EHCI_STATUS_BUFERR = (1 << 5),
  GRUB_EHCI_STATUS_BABBLE = (1 << 4),
  GRUB_EHCI_STATUS_TRANERR = (1 << 3),
  GRUB_EHCI_STATUS_MISSDMF = (1 << 2),
  GRUB_EHCI_STATUS_SPLITST = (1 << 1),
  GRUB_EHCI_STATUS_PINGERR = (1 << 0)
};

enum
{
  GRUB_EHCI_TOTAL_OFF = 16,
  GRUB_EHCI_CERR_OFF = 10
};

#define GRUB_EHCI_BUFPTR_MASK    (0xfffff<<12)
#define GRUB_EHCI_QHTDPTR_MASK   0xffffffe0

#define GRUB_EHCI_TD_BUF_PAGES   5

#define GRUB_EHCI_BUFPAGELEN     0x1000
#define GRUB_EHCI_MAXBUFLEN      0x5000

struct grub_ehci_td;
struct grub_ehci_qh;
typedef volatile struct grub_ehci_td *grub_ehci_td_t;
typedef volatile struct grub_ehci_qh *grub_ehci_qh_t;

/* EHCI Isochronous Transfer Descriptor */
/* Currently not supported */

/* EHCI Split Transaction Isochronous Transfer Descriptor */
/* Currently not supported */

/* EHCI Queue Element Transfer Descriptor (qTD) */
/* Align to 32-byte boundaries */
struct grub_ehci_td
{
  /* EHCI HW part */
  grub_uint32_t next_td;	/* Pointer to next qTD */
  grub_uint32_t alt_next_td;	/* Pointer to alternate next qTD */
  grub_uint32_t token;		/* Toggle, Len, Interrupt, Page, Error, PID, Status */
  grub_uint32_t buffer_page[GRUB_EHCI_TD_BUF_PAGES];	/* Buffer pointer (+ cur. offset in page 0 */
  /* 64-bits part */
  grub_uint32_t buffer_page_high[GRUB_EHCI_TD_BUF_PAGES];
  /* EHCI driver part */
  grub_uint32_t link_td;	/* pointer to next free/chained TD */
  grub_uint32_t size;
  grub_uint32_t pad[1];		/* padding to some multiple of 32 bytes */
};

/* EHCI Queue Head */
/* Align to 32-byte boundaries */
/* QH allocation is made in the similar/same way as in OHCI driver,
 * because unlninking QH from the Asynchronous list is not so
 * trivial as on UHCI (at least it is time consuming) */
struct grub_ehci_qh
{
  /* EHCI HW part */
  grub_uint32_t qh_hptr;	/* Horiz. pointer & Terminate */
  grub_uint32_t ep_char;	/* EP characteristics */
  grub_uint32_t ep_cap;		/* EP capabilities */
  grub_uint32_t td_current;	/* current TD link pointer  */
  struct grub_ehci_td td_overlay;	/* TD overlay area = 64 bytes */
  /* EHCI driver part */
  grub_uint32_t pad[4];		/* padding to some multiple of 32 bytes */
};

/* EHCI Periodic Frame Span Traversal Node */
/* Currently not supported */

struct grub_ehci
{
  volatile grub_uint32_t *iobase_ehcc;	/* Capability registers */
  volatile grub_uint32_t *iobase;	/* Operational registers */
  struct grub_pci_dma_chunk *framelist_chunk;	/* Currently not used */
  volatile grub_uint32_t *framelist_virt;
  grub_uint32_t framelist_phys;
  struct grub_pci_dma_chunk *qh_chunk;	/* GRUB_EHCI_N_QH Queue Heads */
  grub_ehci_qh_t qh_virt;
  grub_uint32_t qh_phys;
  struct grub_pci_dma_chunk *td_chunk;	/* GRUB_EHCI_N_TD Transfer Descriptors */
  grub_ehci_td_t td_virt;
  grub_uint32_t td_phys;
  grub_ehci_td_t tdfree_virt;	/* Free Transfer Descriptors */
  int flag64;
  grub_uint32_t reset;		/* bits 1-15 are flags if port was reset from connected time or not */
  struct grub_ehci *next;
};

static struct grub_ehci *ehci;

static void
sync_all_caches (struct grub_ehci *e)
{
  if (!e)
    return;
  if (e->td_virt)
    grub_arch_sync_dma_caches (e->td_virt, sizeof (struct grub_ehci_td) *
			       GRUB_EHCI_N_TD);
  if (e->qh_virt)
    grub_arch_sync_dma_caches (e->qh_virt, sizeof (struct grub_ehci_qh) *
			       GRUB_EHCI_N_QH);
  if (e->framelist_virt)
    grub_arch_sync_dma_caches (e->framelist_virt, 4096);
}

/* EHCC registers access functions */
static inline grub_uint32_t
grub_ehci_ehcc_read32 (struct grub_ehci *e, grub_uint32_t addr)
{
  return
    grub_le_to_cpu32 (*((volatile grub_uint32_t *) e->iobase_ehcc +
		       (addr / sizeof (grub_uint32_t))));
}

static inline grub_uint16_t
grub_ehci_ehcc_read16 (struct grub_ehci *e, grub_uint32_t addr)
{
  return
    grub_le_to_cpu16 (*((volatile grub_uint16_t *) e->iobase_ehcc +
		       (addr / sizeof (grub_uint16_t))));
}

static inline grub_uint8_t
grub_ehci_ehcc_read8 (struct grub_ehci *e, grub_uint32_t addr)
{
  return *((volatile grub_uint8_t *) e->iobase_ehcc + addr);
}

/* Operational registers access functions */
static inline grub_uint32_t
grub_ehci_oper_read32 (struct grub_ehci *e, grub_uint32_t addr)
{
  return
    grub_le_to_cpu32 (*
		      ((volatile grub_uint32_t *) e->iobase +
		       (addr / sizeof (grub_uint32_t))));
}

static inline void
grub_ehci_oper_write32 (struct grub_ehci *e, grub_uint32_t addr,
			grub_uint32_t value)
{
  *((volatile grub_uint32_t *) e->iobase + (addr / sizeof (grub_uint32_t))) =
    grub_cpu_to_le32 (value);
}

static inline grub_uint32_t
grub_ehci_port_read (struct grub_ehci *e, grub_uint32_t port)
{
  return grub_ehci_oper_read32 (e, GRUB_EHCI_PORT_STAT_CMD + port * 4);
}

static inline void
grub_ehci_port_resbits (struct grub_ehci *e, grub_uint32_t port,
			grub_uint32_t bits)
{
  grub_ehci_oper_write32 (e, GRUB_EHCI_PORT_STAT_CMD + port * 4,
			  grub_ehci_port_read (e,
					       port) & GRUB_EHCI_PORT_WMASK &
			  ~(bits));
  grub_ehci_port_read (e, port);
}

static inline void
grub_ehci_port_setbits (struct grub_ehci *e, grub_uint32_t port,
			grub_uint32_t bits)
{
  grub_ehci_oper_write32 (e, GRUB_EHCI_PORT_STAT_CMD + port * 4,
			  (grub_ehci_port_read (e, port) &
			   GRUB_EHCI_PORT_WMASK) | bits);
  grub_ehci_port_read (e, port);
}

/* Halt if EHCI HC not halted */
static grub_usb_err_t
grub_ehci_halt (struct grub_ehci *e)
{
  grub_uint64_t maxtime;

  if ((grub_ehci_oper_read32 (e, GRUB_EHCI_STATUS) & GRUB_EHCI_ST_HC_HALTED) == 0)	/* EHCI is not halted */
    {
      /* Halt EHCI */
      grub_ehci_oper_write32 (e, GRUB_EHCI_COMMAND,
			      ~GRUB_EHCI_CMD_RUNSTOP
			      & grub_ehci_oper_read32 (e, GRUB_EHCI_COMMAND));
      /* Ensure command is written */
      grub_ehci_oper_read32 (e, GRUB_EHCI_COMMAND);
      maxtime = grub_get_time_ms () + 1000;	/* Fix: Should be 2ms ! */
      while (((grub_ehci_oper_read32 (e, GRUB_EHCI_STATUS)
	       & GRUB_EHCI_ST_HC_HALTED) == 0)
	     && (grub_get_time_ms () < maxtime));
      if ((grub_ehci_oper_read32 (e, GRUB_EHCI_STATUS)
	   & GRUB_EHCI_ST_HC_HALTED) == 0)
	return GRUB_USB_ERR_TIMEOUT;
    }

  return GRUB_USB_ERR_NONE;
}

/* EHCI HC reset */
static grub_usb_err_t
grub_ehci_reset (struct grub_ehci *e)
{
  grub_uint64_t maxtime;

  sync_all_caches (e);

  grub_dprintf ("ehci", "reset\n");

  grub_ehci_oper_write32 (e, GRUB_EHCI_COMMAND,
			  GRUB_EHCI_CMD_HC_RESET);
  /* Ensure command is written */
  grub_ehci_oper_read32 (e, GRUB_EHCI_COMMAND);
  /* XXX: How long time could take reset of HC ? */
  maxtime = grub_get_time_ms () + 1000;
  while (((grub_ehci_oper_read32 (e, GRUB_EHCI_COMMAND)
	   & GRUB_EHCI_CMD_HC_RESET) != 0)
	 && (grub_get_time_ms () < maxtime));
  if ((grub_ehci_oper_read32 (e, GRUB_EHCI_COMMAND)
       & GRUB_EHCI_CMD_HC_RESET) != 0)
    return GRUB_USB_ERR_TIMEOUT;

  return GRUB_USB_ERR_NONE;
}

/* PCI iteration function... */
void
grub_ehci_init_device (volatile void *regs)
{
  struct grub_ehci *e;
  grub_uint32_t fp;
  int i;
  grub_uint32_t n_ports;
  grub_uint8_t caplen;

  /* Allocate memory for the controller and fill basic values. */
  e = grub_zalloc (sizeof (*e));
  if (!e)
    return;
  e->framelist_chunk = NULL;
  e->td_chunk = NULL;
  e->qh_chunk = NULL;
  e->iobase_ehcc = regs;

  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: CAPLEN: %02x\n",
		grub_ehci_ehcc_read8 (e, GRUB_EHCI_EHCC_CAPLEN));
  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: VERSION: %04x\n",
		grub_ehci_ehcc_read16 (e, GRUB_EHCI_EHCC_VERSION));
  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: SPARAMS: %08x\n",
		grub_ehci_ehcc_read32 (e, GRUB_EHCI_EHCC_SPARAMS));
  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: CPARAMS: %08x\n",
		grub_ehci_ehcc_read32 (e, GRUB_EHCI_EHCC_CPARAMS));

  /* Determine base address of EHCI operational registers */
  caplen = grub_ehci_ehcc_read8 (e, GRUB_EHCI_EHCC_CAPLEN);
#ifndef GRUB_HAVE_UNALIGNED_ACCESS
  if (caplen & (sizeof (grub_uint32_t) - 1))
    {
      grub_dprintf ("ehci", "Unaligned caplen\n");
      return;
    }
  e->iobase = ((volatile grub_uint32_t *) e->iobase_ehcc
	       + (caplen / sizeof (grub_uint32_t)));
#else  
  e->iobase = (volatile grub_uint32_t *) 
    ((grub_uint8_t *) e->iobase_ehcc + caplen);
#endif

  grub_dprintf ("ehci",
		"EHCI grub_ehci_pci_iter: iobase of oper. regs: %08llxx\n",
		(unsigned long long) (grub_addr_t) e->iobase_ehcc + caplen);
  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: COMMAND: %08x\n",
		grub_ehci_oper_read32 (e, GRUB_EHCI_COMMAND));
  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: STATUS: %08x\n",
		grub_ehci_oper_read32 (e, GRUB_EHCI_STATUS));
  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: INTERRUPT: %08x\n",
		grub_ehci_oper_read32 (e, GRUB_EHCI_INTERRUPT));
  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: FRAME_INDEX: %08x\n",
		grub_ehci_oper_read32 (e, GRUB_EHCI_FRAME_INDEX));
  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: FL_BASE: %08x\n",
		grub_ehci_oper_read32 (e, GRUB_EHCI_FL_BASE));
  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: CUR_AL_ADDR: %08x\n",
		grub_ehci_oper_read32 (e, GRUB_EHCI_CUR_AL_ADDR));
  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: CONFIG_FLAG: %08x\n",
		grub_ehci_oper_read32 (e, GRUB_EHCI_CONFIG_FLAG));

  /* Check format of data structures requested by EHCI */
  /* XXX: In fact it is not used at any place, it is prepared for future
   * This implementation uses 32-bits pointers only */
  e->flag64 = ((grub_ehci_ehcc_read32 (e, GRUB_EHCI_EHCC_CPARAMS)
		& GRUB_EHCI_CPARAMS_64BIT) != 0);

  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: flag64=%d\n", e->flag64);

  /* Reserve a page for the frame list - it is accurate for max.
   * possible size of framelist. But currently it is not used. */
  e->framelist_chunk = grub_memalign_dma32 (4096, 4096);
  if (!e->framelist_chunk)
    goto fail;
  e->framelist_virt = grub_dma_get_virt (e->framelist_chunk);
  e->framelist_phys = grub_dma_get_phys (e->framelist_chunk);
  grub_memset ((void *) e->framelist_virt, 0, 4096);

  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: framelist mem=%p. OK\n",
		e->framelist_virt);

  /* Allocate memory for the QHs and register it in "e".  */
  e->qh_chunk = grub_memalign_dma32 (4096,
				     sizeof (struct grub_ehci_qh) *
				     GRUB_EHCI_N_QH);
  if (!e->qh_chunk)
    goto fail;
  e->qh_virt = (grub_ehci_qh_t) grub_dma_get_virt (e->qh_chunk);
  e->qh_phys = grub_dma_get_phys (e->qh_chunk);
  grub_memset ((void *) e->qh_virt, 0,
	       sizeof (struct grub_ehci_qh) * GRUB_EHCI_N_QH);

  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: QH mem=%p. OK\n",
		e->qh_virt);

  /* Allocate memory for the TDs and register it in "e".  */
  e->td_chunk = grub_memalign_dma32 (4096,
				     sizeof (struct grub_ehci_td) *
				     GRUB_EHCI_N_TD);
  if (!e->td_chunk)
    goto fail;
  e->td_virt = (grub_ehci_td_t) grub_dma_get_virt (e->td_chunk);
  e->td_phys = grub_dma_get_phys (e->td_chunk);
  grub_memset ((void *) e->td_virt, 0,
	       sizeof (struct grub_ehci_td) * GRUB_EHCI_N_TD);

  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: TD mem=%p. OK\n",
		e->td_virt);

  /* Setup all frame list pointers. Since no isochronous transfers
     are supported, they all point to the (same!) queue
     head with index 0. */
  fp = grub_cpu_to_le32 ((e->qh_phys & GRUB_EHCI_POINTER_MASK)
			 | GRUB_EHCI_HPTR_TYPE_QH);
  for (i = 0; i < GRUB_EHCI_N_FRAMELIST; i++)
    e->framelist_virt[i] = fp;
  /* Prepare chain of all TDs and set Terminate in all TDs */
  for (i = 0; i < (GRUB_EHCI_N_TD - 1); i++)
    {
      e->td_virt[i].link_td = e->td_phys + (i + 1) * sizeof (struct grub_ehci_td);
      e->td_virt[i].next_td = grub_cpu_to_le32_compile_time (GRUB_EHCI_TERMINATE);
      e->td_virt[i].alt_next_td = grub_cpu_to_le32_compile_time (GRUB_EHCI_TERMINATE);
    }
  e->td_virt[GRUB_EHCI_N_TD - 1].next_td =
    grub_cpu_to_le32_compile_time (GRUB_EHCI_TERMINATE);
  e->td_virt[GRUB_EHCI_N_TD - 1].alt_next_td =
    grub_cpu_to_le32_compile_time (GRUB_EHCI_TERMINATE);
  e->tdfree_virt = e->td_virt;
  /* Set Terminate in first QH, which is used in framelist */
  e->qh_virt[0].qh_hptr = grub_cpu_to_le32_compile_time (GRUB_EHCI_TERMINATE | GRUB_EHCI_HPTR_TYPE_QH);
  e->qh_virt[0].td_overlay.next_td = grub_cpu_to_le32_compile_time (GRUB_EHCI_TERMINATE);
  e->qh_virt[0].td_overlay.alt_next_td =
    grub_cpu_to_le32_compile_time (GRUB_EHCI_TERMINATE);
  /* Also set Halted bit in token */
  e->qh_virt[0].td_overlay.token = grub_cpu_to_le32_compile_time (GRUB_EHCI_STATUS_HALTED);
  /* Set the H bit in first QH used for AL */
  e->qh_virt[1].ep_char = grub_cpu_to_le32_compile_time (GRUB_EHCI_H);
  /* Set Terminate into TD in rest of QHs and set horizontal link
   * pointer to itself - these QHs will be used for asynchronous
   * schedule and they should have valid value in horiz. link */
  for (i = 1; i < GRUB_EHCI_N_QH; i++)
    {
      e->qh_virt[i].qh_hptr =
	grub_cpu_to_le32 ((grub_dma_virt2phys (&e->qh_virt[i],
						e->qh_chunk) &
			   GRUB_EHCI_POINTER_MASK) | GRUB_EHCI_HPTR_TYPE_QH);
      e->qh_virt[i].td_overlay.next_td =
	grub_cpu_to_le32_compile_time (GRUB_EHCI_TERMINATE);
      e->qh_virt[i].td_overlay.alt_next_td =
	grub_cpu_to_le32_compile_time (GRUB_EHCI_TERMINATE);
      /* Also set Halted bit in token */
      e->qh_virt[i].td_overlay.token =
	grub_cpu_to_le32_compile_time (GRUB_EHCI_STATUS_HALTED);
    }

  /* Note: QH 0 and QH 1 are reserved and must not be used anywhere.
   * QH 0 is used as empty QH for framelist
   * QH 1 is used as starting empty QH for asynchronous schedule
   * QH 1 must exist at any time because at least one QH linked to
   * itself must exist in asynchronous schedule
   * QH 1 has the H flag set to one */

  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: QH/TD init. OK\n");

  /* Now we can setup EHCI (maybe...) */

  /* Check if EHCI is halted and halt it if not */
  if (grub_ehci_halt (e) != GRUB_USB_ERR_NONE)
    {
      grub_error (GRUB_ERR_TIMEOUT,
		  "EHCI grub_ehci_pci_iter: EHCI halt timeout");
      goto fail;
    }

  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: halted OK\n");

  /* Reset EHCI */
  if (grub_ehci_reset (e) != GRUB_USB_ERR_NONE)
    {
      grub_error (GRUB_ERR_TIMEOUT,
		  "EHCI grub_ehci_pci_iter: EHCI reset timeout");
      goto fail;
    }

  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: reset OK\n");

  /* Setup list address registers */
  grub_ehci_oper_write32 (e, GRUB_EHCI_FL_BASE, e->framelist_phys);
  grub_ehci_oper_write32 (e, GRUB_EHCI_CUR_AL_ADDR,
			  grub_dma_virt2phys (&e->qh_virt[1],
					       e->qh_chunk));

  /* Set ownership of root hub ports to EHCI */
  grub_ehci_oper_write32 (e, GRUB_EHCI_CONFIG_FLAG, GRUB_EHCI_CF_EHCI_OWNER);

  /* Enable both lists */
  grub_ehci_oper_write32 (e, GRUB_EHCI_COMMAND,
			  GRUB_EHCI_CMD_AS_ENABL
			  | GRUB_EHCI_CMD_PS_ENABL
			  | grub_ehci_oper_read32 (e, GRUB_EHCI_COMMAND));

  /* Now should be possible to power-up and enumerate ports etc. */
  if ((grub_ehci_ehcc_read32 (e, GRUB_EHCI_EHCC_SPARAMS)
       & GRUB_EHCI_SPARAMS_PPC) != 0)
    {				/* EHCI has port powering control */
      /* Power on all ports */
      n_ports = grub_ehci_ehcc_read32 (e, GRUB_EHCI_EHCC_SPARAMS)
	& GRUB_EHCI_SPARAMS_N_PORTS;
      for (i = 0; i < (int) n_ports; i++)
	grub_ehci_oper_write32 (e, GRUB_EHCI_PORT_STAT_CMD + i * 4,
				GRUB_EHCI_PORT_POWER
				| grub_ehci_oper_read32 (e,
							 GRUB_EHCI_PORT_STAT_CMD
							 + i * 4));
    }

  /* Ensure all commands are written */
  grub_ehci_oper_read32 (e, GRUB_EHCI_COMMAND);

  /* Enable EHCI */
  grub_ehci_oper_write32 (e, GRUB_EHCI_COMMAND,
			  GRUB_EHCI_CMD_RUNSTOP
			  | grub_ehci_oper_read32 (e, GRUB_EHCI_COMMAND));

  /* Ensure command is written */
  grub_ehci_oper_read32 (e, GRUB_EHCI_COMMAND);

  /* Link to ehci now that initialisation is successful.  */
  e->next = ehci;
  ehci = e;

  sync_all_caches (e);

  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: OK at all\n");

  grub_dprintf ("ehci",
		"EHCI grub_ehci_pci_iter: iobase of oper. regs: %08llx\n",
		(unsigned long long) (grub_addr_t) regs);
  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: COMMAND: %08x\n",
		grub_ehci_oper_read32 (e, GRUB_EHCI_COMMAND));
  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: STATUS: %08x\n",
		grub_ehci_oper_read32 (e, GRUB_EHCI_STATUS));
  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: INTERRUPT: %08x\n",
		grub_ehci_oper_read32 (e, GRUB_EHCI_INTERRUPT));
  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: FRAME_INDEX: %08x\n",
		grub_ehci_oper_read32 (e, GRUB_EHCI_FRAME_INDEX));
  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: FL_BASE: %08x\n",
		grub_ehci_oper_read32 (e, GRUB_EHCI_FL_BASE));
  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: CUR_AL_ADDR: %08x\n",
		grub_ehci_oper_read32 (e, GRUB_EHCI_CUR_AL_ADDR));
  grub_dprintf ("ehci", "EHCI grub_ehci_pci_iter: CONFIG_FLAG: %08x\n",
		grub_ehci_oper_read32 (e, GRUB_EHCI_CONFIG_FLAG));

  return;

fail:
  if (e)
    {
      if (e->td_chunk)
	grub_dma_free ((void *) e->td_chunk);
      if (e->qh_chunk)
	grub_dma_free ((void *) e->qh_chunk);
      if (e->framelist_chunk)
	grub_dma_free (e->framelist_chunk);
    }
  grub_free (e);

  return;
}

static int
grub_ehci_iterate (grub_usb_controller_iterate_hook_t hook, void *hook_data)
{
  struct grub_ehci *e;
  struct grub_usb_controller dev;

  for (e = ehci; e; e = e->next)
    {
      dev.data = e;
      if (hook (&dev, hook_data))
	return 1;
    }

  return 0;
}

static void
grub_ehci_setup_qh (grub_ehci_qh_t qh, grub_usb_transfer_t transfer)
{
  grub_uint32_t ep_char = 0;
  grub_uint32_t ep_cap = 0;

  /* Note: Another part of code is responsible to this QH is
   * Halted ! But it can be linked in AL, so we cannot erase or
   * change qh_hptr ! */
  /* We will not change any TD field because they should/must be
   * in safe state from previous use. */

  /* EP characteristic setup */
  /* Currently not used NAK counter (RL=0),
   * C bit set if EP is not HIGH speed and is control,
   * Max Packet Length is taken from transfer structure,
   * H bit = 0 (because QH[1] has this bit set),
   * DTC bit set to 1 because we are using our own toggle bit control,
   * SPEED is selected according to value from transfer structure,
   * EP number is taken from transfer structure
   * "I" bit must not be set,
   * Device Address is taken from transfer structure
   * */
  if ((transfer->dev->speed != GRUB_USB_SPEED_HIGH)
      && (transfer->type == GRUB_USB_TRANSACTION_TYPE_CONTROL))
    ep_char |= GRUB_EHCI_C;
  ep_char |= (transfer->max << GRUB_EHCI_MAXPLEN_OFF)
    & GRUB_EHCI_MAXPLEN_MASK;
  ep_char |= GRUB_EHCI_DTC;
  switch (transfer->dev->speed)
    {
    case GRUB_USB_SPEED_LOW:
      ep_char |= GRUB_EHCI_SPEED_LOW;
      break;
    case GRUB_USB_SPEED_FULL:
      ep_char |= GRUB_EHCI_SPEED_FULL;
      break;
    case GRUB_USB_SPEED_HIGH:
    default:
      ep_char |= GRUB_EHCI_SPEED_HIGH;
      /* XXX: How we will handle unknown value of speed? */
    }
  ep_char |= (transfer->endpoint << GRUB_EHCI_EP_NUM_OFF)
    & GRUB_EHCI_EP_NUM_MASK;
  ep_char |= transfer->devaddr & GRUB_EHCI_DEVADDR_MASK;
  qh->ep_char = grub_cpu_to_le32 (ep_char);
  /* EP capabilities setup */
  /* MULT field - we try to use max. number
   * PortNumber - included now in device structure referenced
   *              inside transfer structure
   * HubAddress - included now in device structure referenced
   *              inside transfer structure
   * SplitCompletionMask - AFAIK it is ignored in asynchronous list,
   * InterruptScheduleMask - AFAIK it should be zero in async. list */
  ep_cap |= GRUB_EHCI_MULT_THREE;
  ep_cap |= (transfer->dev->split_hubport << GRUB_EHCI_DEVPORT_OFF)
    & GRUB_EHCI_DEVPORT_MASK;
  ep_cap |= (transfer->dev->split_hubaddr << GRUB_EHCI_HUBADDR_OFF)
    & GRUB_EHCI_HUBADDR_MASK;
  if (transfer->dev->speed == GRUB_USB_SPEED_LOW
      && transfer->type != GRUB_USB_TRANSACTION_TYPE_CONTROL)
  {
    ep_cap |= (1<<0) << GRUB_EHCI_SMASK_OFF;
    ep_cap |= (7<<2) << GRUB_EHCI_CMASK_OFF;
  }
  qh->ep_cap = grub_cpu_to_le32 (ep_cap);

  grub_dprintf ("ehci", "setup_qh: qh=%p, not changed: qh_hptr=%08x\n",
		qh, grub_le_to_cpu32 (qh->qh_hptr));
  grub_dprintf ("ehci", "setup_qh: ep_char=%08x, ep_cap=%08x\n",
		ep_char, ep_cap);
  grub_dprintf ("ehci", "setup_qh: end\n");
  grub_dprintf ("ehci", "setup_qh: not changed: td_current=%08x\n",
		grub_le_to_cpu32 (qh->td_current));
  grub_dprintf ("ehci", "setup_qh: not changed: next_td=%08x\n",
		grub_le_to_cpu32 (qh->td_overlay.next_td));
  grub_dprintf ("ehci", "setup_qh: not changed: alt_next_td=%08x\n",
		grub_le_to_cpu32 (qh->td_overlay.alt_next_td));
  grub_dprintf ("ehci", "setup_qh: not changed: token=%08x\n",
		grub_le_to_cpu32 (qh->td_overlay.token));
}

static grub_ehci_qh_t
grub_ehci_find_qh (struct grub_ehci *e, grub_usb_transfer_t transfer)
{
  grub_uint32_t target, mask;
  int i;
  grub_ehci_qh_t qh = e->qh_virt;
  grub_ehci_qh_t head;
  grub_uint32_t qh_phys;
  grub_uint32_t qh_terminate =
    GRUB_EHCI_TERMINATE | GRUB_EHCI_HPTR_TYPE_QH;
  grub_ehci_qh_t qh_iter;

  /* Prepare part of EP Characteristic to find existing QH */
  target = ((transfer->endpoint << GRUB_EHCI_EP_NUM_OFF) |
	    transfer->devaddr) & GRUB_EHCI_TARGET_MASK;
  target = grub_cpu_to_le32 (target);
  mask = grub_cpu_to_le32_compile_time (GRUB_EHCI_TARGET_MASK);

  /* low speed interrupt transfers are linked to the periodic */
  /* schedule, everything else to the asynchronous schedule */
  if (transfer->dev->speed == GRUB_USB_SPEED_LOW
      && transfer->type != GRUB_USB_TRANSACTION_TYPE_CONTROL)
    head = &qh[0];
  else
    head = &qh[1];

  /* First try to find existing QH with proper target in proper list */
  qh_phys = grub_le_to_cpu32( head->qh_hptr );
  if (qh_phys != qh_terminate)
    qh_iter = grub_dma_phys2virt ( qh_phys & GRUB_EHCI_QHTDPTR_MASK,
      e->qh_chunk );
  else
    qh_iter = NULL;

  for (
    i = 0;
    (qh_phys != qh_terminate) && (qh_iter != NULL) &&
    (qh_iter != head) && (i < GRUB_EHCI_N_QH);
    i++ )
    {
      if (target == (qh_iter->ep_char & mask))
	{		
	  /* Found proper existing (and linked) QH, do setup of QH */
	  grub_dprintf ("ehci", "find_qh: found, QH=%p\n", qh_iter);
	  grub_ehci_setup_qh (qh_iter, transfer);
	  sync_all_caches (e);
	  return qh_iter;
	}

      qh_phys = grub_le_to_cpu32( qh_iter->qh_hptr );
      if (qh_phys != qh_terminate)
        qh_iter = grub_dma_phys2virt ( qh_phys & GRUB_EHCI_QHTDPTR_MASK,
	  e->qh_chunk );
      else
        qh_iter = NULL;
    }

  /* variable "i" should be never equal to GRUB_EHCI_N_QH here */
  if (i >= GRUB_EHCI_N_QH)
    { /* Something very bad happened in QH list(s) ! */
      grub_dprintf ("ehci", "find_qh: Mismatch in QH list! head=%p\n",
        head);
    }

  /* QH with target_addr does not exist, we have to find and add it */
  for (i = 2; i < GRUB_EHCI_N_QH; i++) /* We ignore zero and first QH */
    {
      if (!qh[i].ep_char)
	break;	             /* Found first not-allocated QH, finish */
    }

  /* Have we any free QH in array ? */
  if (i >= GRUB_EHCI_N_QH)	/* No. */
    {
      grub_dprintf ("ehci", "find_qh: end - no free QH\n");
      return NULL;
    }
  grub_dprintf ("ehci", "find_qh: new, i=%d, QH=%p\n",
		i, &qh[i]);
  /* Currently we simply take next (current) QH in array, no allocation
   * function is used. It should be no problem until we will need to
   * de-allocate QHs of unplugged devices. */
  /* We should preset new QH and link it into AL */
  grub_ehci_setup_qh (&qh[i], transfer);

  /* Linking - this new (last) QH will copy the QH from the head QH */
  qh[i].qh_hptr = head->qh_hptr;
  /* Linking - the head QH will point to this new QH */
  head->qh_hptr = grub_cpu_to_le32 (GRUB_EHCI_HPTR_TYPE_QH
                                    | grub_dma_virt2phys (&qh[i],
                                                          e->qh_chunk));

  return &qh[i];
}

static grub_ehci_td_t
grub_ehci_alloc_td (struct grub_ehci *e)
{
  grub_ehci_td_t ret;

  /* Check if there is a Transfer Descriptor available.  */
  if (!e->tdfree_virt)
    {
      grub_dprintf ("ehci", "alloc_td: end - no free TD\n");
      return NULL;
    }

  ret = e->tdfree_virt;		/* Take current free TD */
  /* Advance to next free TD in chain */
  if (ret->link_td)
    e->tdfree_virt = grub_dma_phys2virt (ret->link_td, e->td_chunk);
  else
    e->tdfree_virt = NULL;
  ret->link_td = 0;		/* Reset link_td in allocated TD */
  return ret;
}

static void
grub_ehci_free_td (struct grub_ehci *e, grub_ehci_td_t td)
{
  /* Chain new free TD & rest */
  if (e->tdfree_virt)
    td->link_td = grub_dma_virt2phys (e->tdfree_virt, e->td_chunk);
  else
    td->link_td = 0;
  e->tdfree_virt = td;		/* Change address of first free TD */
}

static void
grub_ehci_free_tds (struct grub_ehci *e, grub_ehci_td_t td,
		    grub_usb_transfer_t transfer, grub_size_t * actual)
{
  int i;			/* Index of TD in transfer */
  grub_uint32_t token, to_transfer;

  /* Note: Another part of code is responsible to this QH is
   * INACTIVE ! */
  *actual = 0;

  /* Free the TDs in this queue and set last_trans.  */
  for (i = 0; td; i++)
    {
      grub_ehci_td_t tdprev;

      token = grub_le_to_cpu32 (td->token);
      to_transfer = (token & GRUB_EHCI_TOTAL_MASK) >> GRUB_EHCI_TOTAL_OFF;

      /* Check state of TD - if it did not transfer
       * whole data then set last_trans - it should be last executed TD
       * in case when something went wrong. */
      if (transfer && (td->size != to_transfer))
	transfer->last_trans = i;

      *actual += td->size - to_transfer;

      /* Unlink the TD */
      tdprev = td;
      if (td->link_td)
	td = grub_dma_phys2virt (td->link_td, e->td_chunk);
      else
	td = NULL;

      /* Free the TD.  */
      grub_ehci_free_td (e, tdprev);
    }

  /* Check if last_trans was set. If not and something was
   * transferred (it should be all data in this case), set it
   * to index of last TD, i.e. i-1 */
  if (transfer && (transfer->last_trans < 0) && (*actual != 0))
    transfer->last_trans = i - 1;

  /* XXX: Fix it: last_trans may be set to bad index.
   * Probably we should test more error flags to distinguish
   * if TD was at least partialy executed or not at all.
   * Generaly, we still could have problem with toggling because
   * EHCI can probably split transactions into smaller parts then
   * we defined in transaction even if we did not exceed MaxFrame
   * length - it probably could happen at the end of microframe (?)
   * and if the buffer is crossing page boundary (?). */
}

static grub_ehci_td_t
grub_ehci_transaction (struct grub_ehci *e,
		       grub_transfer_type_t type,
		       unsigned int toggle, grub_size_t size,
		       grub_uint32_t data, grub_ehci_td_t td_alt)
{
  grub_ehci_td_t td;
  grub_uint32_t token;
  grub_uint32_t bufadr;
  int i;

  /* Test of transfer size, it can be:
   * <= GRUB_EHCI_MAXBUFLEN if data aligned to page boundary
   * <= GRUB_EHCI_MAXBUFLEN - GRUB_EHCI_BUFPAGELEN if not aligned
   *    (worst case)
   */
  if ((((data % GRUB_EHCI_BUFPAGELEN) == 0)
       && (size > GRUB_EHCI_MAXBUFLEN))
      ||
      (((data % GRUB_EHCI_BUFPAGELEN) != 0)
       && (size > (GRUB_EHCI_MAXBUFLEN - GRUB_EHCI_BUFPAGELEN))))
    {
      grub_error (GRUB_ERR_OUT_OF_MEMORY,
		  "too long data buffer for EHCI transaction");
      return 0;
    }

  /* Grab a free Transfer Descriptor and initialize it.  */
  td = grub_ehci_alloc_td (e);
  if (!td)
    {
      grub_error (GRUB_ERR_OUT_OF_MEMORY,
		  "no transfer descriptors available for EHCI transfer");
      return 0;
    }

  grub_dprintf ("ehci",
		"transaction: type=%d, toggle=%d, size=%lu data=0x%x td=%p\n",
		type, toggle, (unsigned long) size, data, td);

  /* Fill whole TD by zeros */
  grub_memset ((void *) td, 0, sizeof (struct grub_ehci_td));

  /* Don't point to any TD yet, just terminate.  */
  td->next_td = grub_cpu_to_le32_compile_time (GRUB_EHCI_TERMINATE);
  /* Set alternate pointer. When short packet occurs, alternate TD
   * will not be really fetched because it is not active. But don't
   * forget, EHCI will try to fetch alternate TD every scan of AL
   * until QH is halted. */
  td->alt_next_td = grub_cpu_to_le32 (grub_dma_virt2phys (td_alt,
							   e->td_chunk));
  /* token:
   * TOGGLE - according to toggle
   * TOTAL SIZE = size
   * Interrupt On Complete = FALSE, we don't need IRQ
   * Current Page = 0
   * Error Counter = max. value = 3
   * PID Code - according to type
   * STATUS:
   *  ACTIVE bit should be set to one
   *  SPLIT TRANS. STATE bit should be zero. It is ignored
   *   in HIGH speed transaction, and should be zero for LOW/FULL
   *   speed to indicate state Do Split Transaction */
  token = toggle ? GRUB_EHCI_TOGGLE : 0;
  token |= (size << GRUB_EHCI_TOTAL_OFF) & GRUB_EHCI_TOTAL_MASK;
  token |= GRUB_EHCI_CERR_3;
  switch (type)
    {
    case GRUB_USB_TRANSFER_TYPE_IN:
      token |= GRUB_EHCI_PIDCODE_IN;
      break;
    case GRUB_USB_TRANSFER_TYPE_OUT:
      token |= GRUB_EHCI_PIDCODE_OUT;
      break;
    case GRUB_USB_TRANSFER_TYPE_SETUP:
      token |= GRUB_EHCI_PIDCODE_SETUP;
      break;
    default:			/* XXX: Should not happen, but what to do if it does ? */
      break;
    }
  token |= GRUB_EHCI_STATUS_ACTIVE;
  td->token = grub_cpu_to_le32 (token);

  /* Fill buffer pointers according to size */
  bufadr = data;
  td->buffer_page[0] = grub_cpu_to_le32 (bufadr);
  bufadr = ((bufadr / GRUB_EHCI_BUFPAGELEN) + 1) * GRUB_EHCI_BUFPAGELEN;
  for (i = 1; ((bufadr - data) < size) && (i < GRUB_EHCI_TD_BUF_PAGES); i++)
    {
      td->buffer_page[i] = grub_cpu_to_le32 (bufadr & GRUB_EHCI_BUFPTR_MASK);
      bufadr = ((bufadr / GRUB_EHCI_BUFPAGELEN) + 1) * GRUB_EHCI_BUFPAGELEN;
    }

  /* Remember data size for future use... */
  td->size = (grub_uint32_t) size;

  grub_dprintf ("ehci", "td=%p\n", td);
  grub_dprintf ("ehci", "HW: next_td=%08x, alt_next_td=%08x\n",
		grub_le_to_cpu32 (td->next_td),
		grub_le_to_cpu32 (td->alt_next_td));
  grub_dprintf ("ehci", "HW: token=%08x, buffer[0]=%08x\n",
		grub_le_to_cpu32 (td->token),
		grub_le_to_cpu32 (td->buffer_page[0]));
  grub_dprintf ("ehci", "HW: buffer[1]=%08x, buffer[2]=%08x\n",
		grub_le_to_cpu32 (td->buffer_page[1]),
		grub_le_to_cpu32 (td->buffer_page[2]));
  grub_dprintf ("ehci", "HW: buffer[3]=%08x, buffer[4]=%08x\n",
		grub_le_to_cpu32 (td->buffer_page[3]),
		grub_le_to_cpu32 (td->buffer_page[4]));
  grub_dprintf ("ehci", "link_td=%08x, size=%08x\n",
		td->link_td, td->size);

  return td;
}

struct grub_ehci_transfer_controller_data
{
  grub_ehci_qh_t qh_virt;
  grub_ehci_td_t td_first_virt;
  grub_ehci_td_t td_alt_virt;
  grub_ehci_td_t td_last_virt;
  grub_uint32_t td_last_phys;
};

static grub_usb_err_t
grub_ehci_setup_transfer (grub_usb_controller_t dev,
			  grub_usb_transfer_t transfer)
{
  struct grub_ehci *e = (struct grub_ehci *) dev->data;
  grub_ehci_td_t td = NULL;
  grub_ehci_td_t td_prev = NULL;
  int i;
  struct grub_ehci_transfer_controller_data *cdata;
  grub_uint32_t status;

  sync_all_caches (e);

  /* Check if EHCI is running and AL is enabled */
  status = grub_ehci_oper_read32 (e, GRUB_EHCI_STATUS);
  if ((status & GRUB_EHCI_ST_HC_HALTED) != 0)
    /* XXX: Fix it: Currently we don't do anything to restart EHCI */
    {
      grub_dprintf ("ehci", "setup_transfer: halted, status = 0x%x\n",
		    status);
      return GRUB_USB_ERR_INTERNAL;
    }
  status = grub_ehci_oper_read32 (e, GRUB_EHCI_STATUS);
  if ((status
       & (GRUB_EHCI_ST_AS_STATUS | GRUB_EHCI_ST_PS_STATUS)) == 0)
    /* XXX: Fix it: Currently we don't do anything to restart EHCI */
    {
      grub_dprintf ("ehci", "setup_transfer: no AS/PS, status = 0x%x\n",
		    status);
      return GRUB_USB_ERR_INTERNAL;
    }

  /* Allocate memory for controller transfer data.  */
  cdata = grub_malloc (sizeof (*cdata));
  if (!cdata)
    return GRUB_USB_ERR_INTERNAL;
  cdata->td_first_virt = NULL;

  /* Allocate a queue head for the transfer queue.  */
  cdata->qh_virt = grub_ehci_find_qh (e, transfer);
  if (!cdata->qh_virt)
    {
      grub_dprintf ("ehci", "setup_transfer: no QH\n");
      grub_free (cdata);
      return GRUB_USB_ERR_INTERNAL;
    }

  /* To detect short packet we need some additional "alternate" TD,
   * allocate it first. */
  cdata->td_alt_virt = grub_ehci_alloc_td (e);
  if (!cdata->td_alt_virt)
    {
      grub_dprintf ("ehci", "setup_transfer: no TDs\n");
      grub_free (cdata);
      return GRUB_USB_ERR_INTERNAL;
    }
  /* Fill whole alternate TD by zeros (= inactive) and set
   * Terminate bits and Halt bit */
  grub_memset ((void *) cdata->td_alt_virt, 0, sizeof (struct grub_ehci_td));
  cdata->td_alt_virt->next_td = grub_cpu_to_le32_compile_time (GRUB_EHCI_TERMINATE);
  cdata->td_alt_virt->alt_next_td = grub_cpu_to_le32_compile_time (GRUB_EHCI_TERMINATE);
  cdata->td_alt_virt->token = grub_cpu_to_le32_compile_time (GRUB_EHCI_STATUS_HALTED);

  /* Allocate appropriate number of TDs and set */
  for (i = 0; i < transfer->transcnt; i++)
    {
      grub_usb_transaction_t tr = &transfer->transactions[i];

      td = grub_ehci_transaction (e, tr->pid, tr->toggle, tr->size,
				  tr->data, cdata->td_alt_virt);

      if (!td)			/* de-allocate and free all */
	{
	  grub_size_t actual = 0;

	  if (cdata->td_first_virt)
	    grub_ehci_free_tds (e, cdata->td_first_virt, NULL, &actual);

	  grub_free (cdata);
	  grub_dprintf ("ehci", "setup_transfer: no TD\n");
	  return GRUB_USB_ERR_INTERNAL;
	}

      /* Register new TD in cdata or previous TD */
      if (!cdata->td_first_virt)
	cdata->td_first_virt = td;
      else
	{
	  td_prev->link_td = grub_dma_virt2phys (td, e->td_chunk);
	  td_prev->next_td =
	    grub_cpu_to_le32 (grub_dma_virt2phys (td, e->td_chunk));
	}
      td_prev = td;
    }

  /* Remember last TD */
  cdata->td_last_virt = td;
  cdata->td_last_phys = grub_dma_virt2phys (td, e->td_chunk);
  /* Last TD should not have set alternate TD */
  cdata->td_last_virt->alt_next_td = grub_cpu_to_le32_compile_time (GRUB_EHCI_TERMINATE);

  grub_dprintf ("ehci", "setup_transfer: cdata=%p, qh=%p\n",
		cdata,cdata->qh_virt);
  grub_dprintf ("ehci", "setup_transfer: td_first=%p, td_alt=%p\n",
		cdata->td_first_virt,
		cdata->td_alt_virt);
  grub_dprintf ("ehci", "setup_transfer: td_last=%p\n",
		cdata->td_last_virt);

  /* Start transfer: */
  /* Unlink possible alternate pointer in QH */
  cdata->qh_virt->td_overlay.alt_next_td =
    grub_cpu_to_le32_compile_time (GRUB_EHCI_TERMINATE);
  /* Link new TDs with QH via next_td */
  cdata->qh_virt->td_overlay.next_td =
    grub_cpu_to_le32 (grub_dma_virt2phys
		      (cdata->td_first_virt, e->td_chunk));
  /* Reset Active and Halted bits in QH to activate Advance Queue,
   * i.e. reset token */
  cdata->qh_virt->td_overlay.token = grub_cpu_to_le32_compile_time (0);

  sync_all_caches (e);

  /* Finito */
  transfer->controller_data = cdata;

  return GRUB_USB_ERR_NONE;
}

/* This function expects QH is not active.
 * Function set Halt bit in QH TD overlay and possibly prints
 * necessary debug information. */
static void
grub_ehci_pre_finish_transfer (grub_usb_transfer_t transfer)
{
  struct grub_ehci_transfer_controller_data *cdata =
    transfer->controller_data;

  /* Collect debug data here if necessary */

  /* Set Halt bit in not active QH. AL will not attempt to do
   * Advance Queue on QH with Halt bit set, i.e., we can then
   * safely manipulate with QH TD part. */
  cdata->qh_virt->td_overlay.token = (cdata->qh_virt->td_overlay.token
				      |
				      grub_cpu_to_le32_compile_time
				      (GRUB_EHCI_STATUS_HALTED)) &
    grub_cpu_to_le32_compile_time (~GRUB_EHCI_STATUS_ACTIVE);

  /* Print debug data here if necessary */

}

static grub_usb_err_t
grub_ehci_parse_notrun (grub_usb_controller_t dev,
			grub_usb_transfer_t transfer, grub_size_t * actual)
{
  struct grub_ehci *e = dev->data;
  struct grub_ehci_transfer_controller_data *cdata =
    transfer->controller_data;

  grub_dprintf ("ehci", "parse_notrun: info\n");

  /* QH can be in any state in this case. */
  /* But EHCI or AL is not running, so QH is surely not active
   * even if it has Active bit set... */
  grub_ehci_pre_finish_transfer (transfer);
  grub_ehci_free_tds (e, cdata->td_first_virt, transfer, actual);
  grub_ehci_free_td (e, cdata->td_alt_virt);
  grub_free (cdata);

  sync_all_caches (e);

  /* Additionally, do something with EHCI to make it running (what?) */
  /* Try enable EHCI and AL */
  grub_ehci_oper_write32 (e, GRUB_EHCI_COMMAND,
			  GRUB_EHCI_CMD_RUNSTOP | GRUB_EHCI_CMD_AS_ENABL
			  | GRUB_EHCI_CMD_PS_ENABL
			  | grub_ehci_oper_read32 (e, GRUB_EHCI_COMMAND));
  /* Ensure command is written */
  grub_ehci_oper_read32 (e, GRUB_EHCI_COMMAND);

  return GRUB_USB_ERR_UNRECOVERABLE;
}

static grub_usb_err_t
grub_ehci_parse_halt (grub_usb_controller_t dev,
		      grub_usb_transfer_t transfer, grub_size_t * actual)
{
  struct grub_ehci *e = dev->data;
  struct grub_ehci_transfer_controller_data *cdata =
    transfer->controller_data;
  grub_uint32_t token;
  grub_usb_err_t err = GRUB_USB_ERR_NAK;

  /* QH should be halted and not active in this case. */

  grub_dprintf ("ehci", "parse_halt: info\n");

  /* Remember token before call pre-finish function */
  token = grub_le_to_cpu32 (cdata->qh_virt->td_overlay.token);

  /* Do things like in normal finish */
  grub_ehci_pre_finish_transfer (transfer);
  grub_ehci_free_tds (e, cdata->td_first_virt, transfer, actual);
  grub_ehci_free_td (e, cdata->td_alt_virt);
  grub_free (cdata);

  sync_all_caches (e);

  /* Evaluation of error code - currently we don't have GRUB USB error
   * codes for some EHCI states, GRUB_USB_ERR_DATA is used for them.
   * Order of evaluation is critical, specially bubble/stall. */
  if ((token & GRUB_EHCI_STATUS_BABBLE) != 0)
    err = GRUB_USB_ERR_BABBLE;
  else if ((token & GRUB_EHCI_CERR_MASK) != 0)
    err = GRUB_USB_ERR_STALL;
  else if ((token & GRUB_EHCI_STATUS_TRANERR) != 0)
    err = GRUB_USB_ERR_DATA;
  else if ((token & GRUB_EHCI_STATUS_BUFERR) != 0)
    err = GRUB_USB_ERR_DATA;
  else if ((token & GRUB_EHCI_STATUS_MISSDMF) != 0)
    err = GRUB_USB_ERR_DATA;

  return err;
}

static grub_usb_err_t
grub_ehci_parse_success (grub_usb_controller_t dev,
			 grub_usb_transfer_t transfer, grub_size_t * actual)
{
  struct grub_ehci *e = dev->data;
  struct grub_ehci_transfer_controller_data *cdata =
    transfer->controller_data;

  grub_dprintf ("ehci", "parse_success: info\n");

  /* QH should be not active in this case, but it is not halted. */
  grub_ehci_pre_finish_transfer (transfer);
  grub_ehci_free_tds (e, cdata->td_first_virt, transfer, actual);
  grub_ehci_free_td (e, cdata->td_alt_virt);
  grub_free (cdata);

  sync_all_caches (e);

  return GRUB_USB_ERR_NONE;
}


static grub_usb_err_t
grub_ehci_check_transfer (grub_usb_controller_t dev,
			  grub_usb_transfer_t transfer, grub_size_t * actual)
{
  struct grub_ehci *e = dev->data;
  struct grub_ehci_transfer_controller_data *cdata =
    transfer->controller_data;
  grub_uint32_t token, token_ftd;

  sync_all_caches (e);

  grub_dprintf ("ehci",
		"check_transfer: EHCI STATUS=%08x, cdata=%p, qh=%p\n",
		grub_ehci_oper_read32 (e, GRUB_EHCI_STATUS),
		cdata, cdata->qh_virt);
  grub_dprintf ("ehci", "check_transfer: qh_hptr=%08x, ep_char=%08x\n",
		grub_le_to_cpu32 (cdata->qh_virt->qh_hptr),
		grub_le_to_cpu32 (cdata->qh_virt->ep_char));
  grub_dprintf ("ehci", "check_transfer: ep_cap=%08x, td_current=%08x\n",
		grub_le_to_cpu32 (cdata->qh_virt->ep_cap),
		grub_le_to_cpu32 (cdata->qh_virt->td_current));
  grub_dprintf ("ehci", "check_transfer: next_td=%08x, alt_next_td=%08x\n",
		grub_le_to_cpu32 (cdata->qh_virt->td_overlay.next_td),
		grub_le_to_cpu32 (cdata->qh_virt->td_overlay.alt_next_td));
  grub_dprintf ("ehci", "check_transfer: token=%08x, buffer[0]=%08x\n",
		grub_le_to_cpu32 (cdata->qh_virt->td_overlay.token),
		grub_le_to_cpu32 (cdata->qh_virt->td_overlay.buffer_page[0]));

  /* Check if EHCI is running and AL is enabled */
  if ((grub_ehci_oper_read32 (e, GRUB_EHCI_STATUS)
       & GRUB_EHCI_ST_HC_HALTED) != 0)
    return grub_ehci_parse_notrun (dev, transfer, actual);
  if ((grub_ehci_oper_read32 (e, GRUB_EHCI_STATUS)
       & (GRUB_EHCI_ST_AS_STATUS | GRUB_EHCI_ST_PS_STATUS)) == 0)
    return grub_ehci_parse_notrun (dev, transfer, actual);

  token = grub_le_to_cpu32 (cdata->qh_virt->td_overlay.token);
  /* If the transfer consist from only one TD, we should check */
  /* if the TD was really executed and deactivated - to prevent */
  /* false detection of transfer finish. */
  token_ftd = grub_le_to_cpu32 (cdata->td_first_virt->token);

  /* Detect QH halted */
  if ((token & GRUB_EHCI_STATUS_HALTED) != 0)
    return grub_ehci_parse_halt (dev, transfer, actual);

  /* Detect QH not active - QH is not active and no next TD */
  if (token && ((token & GRUB_EHCI_STATUS_ACTIVE) == 0)
	&& ((token_ftd & GRUB_EHCI_STATUS_ACTIVE) == 0))
    {
      /* It could be finish at all or short packet condition */
      if ((grub_le_to_cpu32 (cdata->qh_virt->td_overlay.next_td)
	   & GRUB_EHCI_TERMINATE) &&
	  ((grub_le_to_cpu32 (cdata->qh_virt->td_current)
	    & GRUB_EHCI_QHTDPTR_MASK) == cdata->td_last_phys))
	/* Normal finish */
	return grub_ehci_parse_success (dev, transfer, actual);
      else if ((token & GRUB_EHCI_TOTAL_MASK) != 0)
	/* Short packet condition */
	/* But currently we don't handle it - higher level will do it */
	return grub_ehci_parse_success (dev, transfer, actual);
    }

  return GRUB_USB_ERR_WAIT;
}

static grub_usb_err_t
grub_ehci_cancel_transfer (grub_usb_controller_t dev,
			   grub_usb_transfer_t transfer)
{
  struct grub_ehci *e = dev->data;
  struct grub_ehci_transfer_controller_data *cdata =
    transfer->controller_data;
  grub_size_t actual;
  int i;
  grub_uint64_t maxtime;
  grub_uint32_t qh_phys;

  sync_all_caches (e);

  grub_uint32_t interrupt =
    cdata->qh_virt->ep_cap & GRUB_EHCI_SMASK_MASK;

  /* QH can be active and should be de-activated and halted */

  grub_dprintf ("ehci", "cancel_transfer: begin\n");

  /* First check if EHCI is running - if not, there is no problem */
  /* to cancel any transfer. Or, if transfer is asynchronous, check */
  /* if AL is enabled - if not, transfer can be canceled also. */
  if (((grub_ehci_oper_read32 (e, GRUB_EHCI_STATUS) &
      GRUB_EHCI_ST_HC_HALTED) != 0) ||
    (!interrupt && ((grub_ehci_oper_read32 (e, GRUB_EHCI_STATUS) &
      (GRUB_EHCI_ST_AS_STATUS | GRUB_EHCI_ST_PS_STATUS)) == 0)))
    {
      grub_ehci_pre_finish_transfer (transfer);
      grub_ehci_free_tds (e, cdata->td_first_virt, transfer, &actual);
      grub_ehci_free_td (e, cdata->td_alt_virt);
      grub_free (cdata);
      sync_all_caches (e);
      grub_dprintf ("ehci", "cancel_transfer: end - EHCI not running\n");
      return GRUB_USB_ERR_NONE;
    }

  /* EHCI and (AL or SL) are running. What to do? */
  /* Try to Halt QH via de-scheduling QH. */
  /* Find index of previous QH */
  qh_phys = grub_dma_virt2phys(cdata->qh_virt, e->qh_chunk);
  for (i = 0; i < GRUB_EHCI_N_QH; i++)
    {
      if ((grub_le_to_cpu32(e->qh_virt[i].qh_hptr)
        & GRUB_EHCI_QHTDPTR_MASK) == qh_phys)
        break;
    }
  if (i == GRUB_EHCI_N_QH)
    {
      grub_printf ("%s: prev not found, queues are corrupt\n", __func__);
      return GRUB_USB_ERR_UNRECOVERABLE;
    }
  /* Unlink QH from AL */
  e->qh_virt[i].qh_hptr = cdata->qh_virt->qh_hptr;

  sync_all_caches (e);

  /* If this is an interrupt transfer, we just wait for the periodic
   * schedule to advance a few times and then assume that the EHCI
   * controller has read the updated QH. */
  if (cdata->qh_virt->ep_cap & GRUB_EHCI_SMASK_MASK)
    {
      grub_millisleep(20);
    }
  else
    {
      /* For the asynchronous schedule we use the advance doorbell to find
       * out when the EHCI controller has read the updated QH. */

      /* Ring the doorbell */
      grub_ehci_oper_write32 (e, GRUB_EHCI_COMMAND,
                              GRUB_EHCI_CMD_AS_ADV_D
                              | grub_ehci_oper_read32 (e, GRUB_EHCI_COMMAND));
      /* Ensure command is written */
      grub_ehci_oper_read32 (e, GRUB_EHCI_COMMAND);
      /* Wait answer with timeout */
      maxtime = grub_get_time_ms () + 2;
      while (((grub_ehci_oper_read32 (e, GRUB_EHCI_STATUS)
               & GRUB_EHCI_ST_AS_ADVANCE) == 0)
             && (grub_get_time_ms () < maxtime));

      /* We do not detect the timeout because if timeout occurs, it most
       * probably means something wrong with EHCI - maybe stopped etc. */

      /* Shut up the doorbell */
      grub_ehci_oper_write32 (e, GRUB_EHCI_COMMAND,
                              ~GRUB_EHCI_CMD_AS_ADV_D
                              & grub_ehci_oper_read32 (e, GRUB_EHCI_COMMAND));
      grub_ehci_oper_write32 (e, GRUB_EHCI_STATUS,
                              GRUB_EHCI_ST_AS_ADVANCE
                              | grub_ehci_oper_read32 (e, GRUB_EHCI_STATUS));
      /* Ensure command is written */
      grub_ehci_oper_read32 (e, GRUB_EHCI_STATUS);
    }

  /* Now is QH out of AL and we can do anything with it... */
  grub_ehci_pre_finish_transfer (transfer);
  grub_ehci_free_tds (e, cdata->td_first_virt, transfer, &actual);
  grub_ehci_free_td (e, cdata->td_alt_virt);

  /* "Free" the QH - link it to itself */
  cdata->qh_virt->ep_char = 0;
  cdata->qh_virt->qh_hptr =
    grub_cpu_to_le32 ((grub_dma_virt2phys (cdata->qh_virt,
                                           e->qh_chunk)
                       & GRUB_EHCI_POINTER_MASK) | GRUB_EHCI_HPTR_TYPE_QH);

  grub_free (cdata);

  grub_dprintf ("ehci", "cancel_transfer: end\n");

  sync_all_caches (e);

  return GRUB_USB_ERR_NONE;
}

static int
grub_ehci_hubports (grub_usb_controller_t dev)
{
  struct grub_ehci *e = (struct grub_ehci *) dev->data;
  grub_uint32_t portinfo;

  portinfo = grub_ehci_ehcc_read32 (e, GRUB_EHCI_EHCC_SPARAMS)
    & GRUB_EHCI_SPARAMS_N_PORTS;
  grub_dprintf ("ehci", "root hub ports=%d\n", portinfo);
  return portinfo;
}

static grub_usb_err_t
grub_ehci_portstatus (grub_usb_controller_t dev,
		      unsigned int port, unsigned int enable)
{
  struct grub_ehci *e = (struct grub_ehci *) dev->data;
  grub_uint64_t endtime;

  grub_dprintf ("ehci", "portstatus: EHCI STATUS: %08x\n",
		grub_ehci_oper_read32 (e, GRUB_EHCI_STATUS));
  grub_dprintf ("ehci",
		"portstatus: begin, iobase=%p, port=%d, status=0x%02x\n",
		e->iobase, port, grub_ehci_port_read (e, port));

  /* In any case we need to disable port:
   * - if enable==false - we should disable port
   * - if enable==true we will do the reset and the specification says
   *   PortEnable should be FALSE in such case */
  /* Disable the port and wait for it. */
  grub_ehci_port_resbits (e, port, GRUB_EHCI_PORT_ENABLED);
  endtime = grub_get_time_ms () + 1000;
  while (grub_ehci_port_read (e, port) & GRUB_EHCI_PORT_ENABLED)
    if (grub_get_time_ms () > endtime)
      return GRUB_USB_ERR_TIMEOUT;

  if (!enable)			/* We don't need reset port */
    {
      grub_dprintf ("ehci", "portstatus: Disabled.\n");
      grub_dprintf ("ehci", "portstatus: end, status=0x%02x\n",
		    grub_ehci_port_read (e, port));
      return GRUB_USB_ERR_NONE;
    }

  grub_dprintf ("ehci", "portstatus: enable\n");

  grub_boot_time ("Resetting port %d", port);

  /* Now we will do reset - if HIGH speed device connected, it will
   * result in Enabled state, otherwise port remains disabled. */
  /* Set RESET bit for 50ms */
  grub_ehci_port_setbits (e, port, GRUB_EHCI_PORT_RESET);
  grub_millisleep (50);

  /* Reset RESET bit and wait for the end of reset */
  grub_ehci_port_resbits (e, port, GRUB_EHCI_PORT_RESET);
  endtime = grub_get_time_ms () + 1000;
  while (grub_ehci_port_read (e, port) & GRUB_EHCI_PORT_RESET)
    if (grub_get_time_ms () > endtime)
      return GRUB_USB_ERR_TIMEOUT;
  grub_boot_time ("Port %d reset", port);
  /* Remember "we did the reset" - needed by detect_dev */
  e->reset |= (1 << port);
  /* Test if port enabled, i.e. HIGH speed device connected */
  if ((grub_ehci_port_read (e, port) & GRUB_EHCI_PORT_ENABLED) != 0)	/* yes! */
    {
      grub_dprintf ("ehci", "portstatus: Enabled!\n");
      /* "Reset recovery time" (USB spec.) */
      grub_millisleep (10);
    }
  else				/* no... */
    {
      /* FULL speed device connected - change port ownership.
       * It results in disconnected state of this EHCI port. */
      grub_ehci_port_setbits (e, port, GRUB_EHCI_PORT_OWNER);
      return GRUB_USB_ERR_BADDEVICE;
    }

  /* XXX: Fix it! There is possible problem - we can say to calling
   * function that we lost device if it is FULL speed onlu via
   * return value <> GRUB_ERR_NONE. It (maybe) displays also error
   * message on screen - but this situation is not error, it is normal
   * state! */

  grub_dprintf ("ehci", "portstatus: end, status=0x%02x\n",
		grub_ehci_port_read (e, port));

  return GRUB_USB_ERR_NONE;
}

static grub_usb_speed_t
grub_ehci_detect_dev (grub_usb_controller_t dev, int port, int *changed)
{
  struct grub_ehci *e = (struct grub_ehci *) dev->data;
  grub_uint32_t status, line_state;

  status = grub_ehci_port_read (e, port);

  /* Connect Status Change bit - it detects change of connection */
  if (status & GRUB_EHCI_PORT_CONNECT_CH)
    {
      *changed = 1;
      /* Reset bit Connect Status Change */
      grub_ehci_port_setbits (e, port, GRUB_EHCI_PORT_CONNECT_CH);
    }
  else
    *changed = 0;

  if (!(status & GRUB_EHCI_PORT_CONNECT))
    {				/* We should reset related "reset" flag in not connected state */
      e->reset &= ~(1 << port);
      return GRUB_USB_SPEED_NONE;
    }
  /* Detected connected state, so we should return speed.
   * But we can detect only LOW speed device and only at connection
   * time when PortEnabled=FALSE. FULL / HIGH speed detection is made
   * later by EHCI-specific reset procedure.
   * Another thing - if detected speed is LOW at connection time,
   * we should change port ownership to companion controller.
   * So:
   * 1. If we detect connected and enabled and EHCI-owned port,
   * we can say it is HIGH speed.
   * 2. If we detect connected and not EHCI-owned port, we can say
   * NONE speed, because such devices are not handled by EHCI.
   * 3. If we detect connected, not enabled but reset port, we can say
   * NONE speed, because it means FULL device connected to port and
   * such devices are not handled by EHCI.
   * 4. If we detect connected, not enabled and not reset port, which
   * has line state != "K", we will say HIGH - it could be FULL or HIGH
   * device, we will see it later after end of EHCI-specific reset
   * procedure.
   * 5. If we detect connected, not enabled and not reset port, which
   * has line state == "K", we can say NONE speed, because LOW speed
   * device is connected and we should change port ownership. */
  if ((status & GRUB_EHCI_PORT_ENABLED) != 0)	/* Port already enabled, return high speed. */
    return GRUB_USB_SPEED_HIGH;
  if ((status & GRUB_EHCI_PORT_OWNER) != 0)	/* EHCI is not port owner */
    return GRUB_USB_SPEED_NONE;	/* EHCI driver is ignoring this port. */
  if ((e->reset & (1 << port)) != 0)	/* Port reset was done = FULL speed */
    return GRUB_USB_SPEED_NONE;	/* EHCI driver is ignoring this port. */
  else				/* Port connected but not enabled - test port speed. */
    {
      line_state = status & GRUB_EHCI_PORT_LINE_STAT;
      if (line_state != GRUB_EHCI_PORT_LINE_LOWSP)
	return GRUB_USB_SPEED_HIGH;
      /* Detected LOW speed device, we should change
       * port ownership.
       * XXX: Fix it!: There should be test if related companion
       * controler is available ! And what to do if it does not exist ? */
      grub_ehci_port_setbits (e, port, GRUB_EHCI_PORT_OWNER);
      return GRUB_USB_SPEED_NONE;	/* Ignore this port */
      /* Note: Reset of PORT_OWNER bit is done by EHCI HW when
       * device is really disconnected from port.
       * Don't do PORT_OWNER bit reset by SW when not connected signal
       * is detected in port register ! */
    }
}

static grub_err_t
grub_ehci_restore_hw (void)
{
  struct grub_ehci *e;
  grub_uint32_t n_ports;
  int i;

  /* We should re-enable all EHCI HW similarly as on inithw */
  for (e = ehci; e; e = e->next)
    {
      /* Check if EHCI is halted and halt it if not */
      if (grub_ehci_halt (e) != GRUB_USB_ERR_NONE)
	grub_error (GRUB_ERR_TIMEOUT, "restore_hw: EHCI halt timeout");

      /* Reset EHCI */
      if (grub_ehci_reset (e) != GRUB_USB_ERR_NONE)
	grub_error (GRUB_ERR_TIMEOUT, "restore_hw: EHCI reset timeout");

      /* Setup some EHCI registers and enable EHCI */
      grub_ehci_oper_write32 (e, GRUB_EHCI_FL_BASE, e->framelist_phys);
      grub_ehci_oper_write32 (e, GRUB_EHCI_CUR_AL_ADDR,
			      grub_dma_virt2phys (&e->qh_virt[1],
						   e->qh_chunk));
      grub_ehci_oper_write32 (e, GRUB_EHCI_COMMAND,
			      GRUB_EHCI_CMD_RUNSTOP |
			      grub_ehci_oper_read32 (e, GRUB_EHCI_COMMAND));

      /* Set ownership of root hub ports to EHCI */
      grub_ehci_oper_write32 (e, GRUB_EHCI_CONFIG_FLAG,
			      GRUB_EHCI_CF_EHCI_OWNER);

      /* Enable asynchronous list */
      grub_ehci_oper_write32 (e, GRUB_EHCI_COMMAND,
			      GRUB_EHCI_CMD_AS_ENABL
			      | GRUB_EHCI_CMD_PS_ENABL
			      | grub_ehci_oper_read32 (e, GRUB_EHCI_COMMAND));

      /* Now should be possible to power-up and enumerate ports etc. */
      if ((grub_ehci_ehcc_read32 (e, GRUB_EHCI_EHCC_SPARAMS)
	   & GRUB_EHCI_SPARAMS_PPC) != 0)
	{			/* EHCI has port powering control */
	  /* Power on all ports */
	  n_ports = grub_ehci_ehcc_read32 (e, GRUB_EHCI_EHCC_SPARAMS)
	    & GRUB_EHCI_SPARAMS_N_PORTS;
	  for (i = 0; i < (int) n_ports; i++)
	    grub_ehci_oper_write32 (e, GRUB_EHCI_PORT_STAT_CMD + i * 4,
				    GRUB_EHCI_PORT_POWER
				    | grub_ehci_oper_read32 (e,
							     GRUB_EHCI_PORT_STAT_CMD
							     + i * 4));
	}
    }

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_ehci_fini_hw (int noreturn __attribute__ ((unused)))
{
  struct grub_ehci *e;

  /* We should disable all EHCI HW to prevent any DMA access etc. */
  for (e = ehci; e; e = e->next)
    {
      /* Disable both lists */
      grub_ehci_oper_write32 (e, GRUB_EHCI_COMMAND,
        ~(GRUB_EHCI_CMD_AS_ENABL | GRUB_EHCI_CMD_PS_ENABL)
        & grub_ehci_oper_read32 (e, GRUB_EHCI_COMMAND));

      /* Check if EHCI is halted and halt it if not */
      grub_ehci_halt (e);

      /* Reset EHCI */
      grub_ehci_reset (e);
    }

  return GRUB_ERR_NONE;
}

static struct grub_usb_controller_dev usb_controller = {
  .name = "ehci",
  .iterate = grub_ehci_iterate,
  .setup_transfer = grub_ehci_setup_transfer,
  .check_transfer = grub_ehci_check_transfer,
  .cancel_transfer = grub_ehci_cancel_transfer,
  .hubports = grub_ehci_hubports,
  .portstatus = grub_ehci_portstatus,
  .detect_dev = grub_ehci_detect_dev,
  .attach_dev = NULL,
  .detach_dev = NULL,
  /* estimated max. count of TDs for one bulk transfer */
  .max_bulk_tds = GRUB_EHCI_N_TD * 3 / 4 
};

GRUB_MOD_INIT (ehci)
{
  COMPILE_TIME_ASSERT (sizeof (struct grub_ehci_td) == 64);
  COMPILE_TIME_ASSERT (sizeof (struct grub_ehci_qh) == 96);

  grub_stop_disk_firmware ();

  grub_boot_time ("Initing EHCI hardware");
  grub_ehci_pci_scan ();
  grub_boot_time ("Registering EHCI driver");
  grub_usb_controller_dev_register (&usb_controller);
  grub_boot_time ("EHCI driver registered");
  grub_loader_register_preboot_hook (grub_ehci_fini_hw, grub_ehci_restore_hw,
				     GRUB_LOADER_PREBOOT_HOOK_PRIO_DISK);
}

GRUB_MOD_FINI (ehci)
{
  grub_ehci_fini_hw (0);
  grub_usb_controller_dev_unregister (&usb_controller);
}
