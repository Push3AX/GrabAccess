/* drivemap.c - command to manage the BIOS drive mappings.  */
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

#include <grub/extcmd.h>
#include <grub/dl.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/disk.h>
#include <grub/loader.h>
#include <grub/env.h>
#include <grub/machine/biosnum.h>
#include <grub/i18n.h>
#include <grub/memory.h>
#include <grub/machine/memory.h>
#include <grub/machine/kernel.h>
#include <grub/ventoy.h>

GRUB_MOD_LICENSE ("GPLv3+");

/* Real mode IVT slot (seg:off far pointer) for interrupt 0x13.  */
static grub_uint32_t *const int13slot = (grub_uint32_t *) (4 * 0x13);

/* Remember to update enum opt_idxs accordingly.  */
static const struct grub_arg_option options[] = {
  /* TRANSLATORS: In this file "mapping" refers to a change GRUB makes so if
     your language doesn't have an equivalent of "mapping" you can
     use the word like "rerouting".
   */
  {"list", 'l', 0, N_("Show the current mappings."), 0, 0},
  {"reset", 'r', 0, N_("Reset all mappings to the default values."), 0, 0},
  {"swap", 's', 0, N_("Perform both direct and reverse mappings."), 0, 0},
  {0, 0, 0, 0, 0, 0}
};

/* Remember to update options[] accordingly.  */
enum opt_idxs
{
  OPTIDX_LIST = 0,
  OPTIDX_RESET,
  OPTIDX_SWAP,
};

/* Realmode far ptr (2 * 16b) to the previous INT13h handler.  */
extern grub_uint32_t grub_drivemap_oldhandler;

/* The type "void" is used for imported assembly labels, takes no storage and
   serves just to take the address with &label.  */

/* The assembly function to replace the old INT13h handler. It does not follow
   any C callspecs and returns with IRET.  */
extern const void grub_drivemap_handler;

/* Start of the drive mappings area (space reserved at runtime).  */
extern const void grub_drivemap_mapstart;

typedef struct drivemap_node
{
  struct drivemap_node *next;
  grub_uint8_t newdrive;
  grub_uint8_t redirto;
} drivemap_node_t;

typedef struct GRUB_PACKED int13map_node
{
  grub_uint8_t disknum;
  grub_uint8_t mapto;
} int13map_node_t;

#define INT13H_OFFSET(x) \
	(((grub_uint8_t *)(x)) - ((grub_uint8_t *)&grub_drivemap_handler))

static drivemap_node_t *map_head;
static void *drivemap_hook;
static int drivemap_mmap;

/* Puts the specified mapping into the table, replacing an existing mapping
   for newdrive or adding a new one if required.  */
static grub_err_t
drivemap_set (grub_uint8_t newdrive, grub_uint8_t redirto)
{
  drivemap_node_t *mapping = 0;
  drivemap_node_t *search = map_head;
  while (search)
    {
      if (search->newdrive == newdrive)
	{
	  mapping = search;
	  break;
	}
      search = search->next;
    }

  /* Check for pre-existing mappings to modify before creating a new one.  */
  if (mapping)
    mapping->redirto = redirto;
  else
    {
      mapping = grub_malloc (sizeof (drivemap_node_t));
      if (! mapping)
	return grub_errno;
      mapping->newdrive = newdrive;
      mapping->redirto = redirto;
      mapping->next = map_head;
      map_head = mapping;
    }
  return GRUB_ERR_NONE;
}

/* Removes the mapping for newdrive from the table.  If there is no mapping,
   then this function behaves like a no-op on the map.  */
static void
drivemap_remove (grub_uint8_t newdrive)
{
  drivemap_node_t *mapping = 0;
  drivemap_node_t *search = map_head;
  drivemap_node_t *previous = 0;

  while (search)
    {
      if (search->newdrive == newdrive)
	{
	  mapping = search;
	  break;
	}
      previous = search;
      search = search->next;
    }

  if (mapping)
    {
      if (previous)
	previous->next = mapping->next;
      else
	map_head = mapping->next;
      grub_free (mapping);
    }
}

/* Given a GRUB-like device name and a convenient location, stores the
   related BIOS disk number.  Accepts devices like \((f|h)dN\), with
   0 <= N < 128.  */
static grub_err_t
tryparse_diskstring (const char *str, grub_uint8_t *output)
{
  /* Skip opening paren in order to allow both (hd0) and hd0.  */
  if (*str == '(')
    str++;
  if ((str[0] == 'f' || str[0] == 'h') && str[1] == 'd')
    {
      grub_uint8_t bios_num = (str[0] == 'h') ? 0x80 : 0x00;
      unsigned long drivenum = grub_strtoul (str + 2, 0, 0);
      if (grub_errno == GRUB_ERR_NONE && drivenum < 128)
	{
	  bios_num |= drivenum;
	  if (output)
	    *output = bios_num;
	  return GRUB_ERR_NONE;
	}
    }
  return grub_error (GRUB_ERR_BAD_ARGUMENT, "device format \"%s\" "
		     "invalid: must be (f|h)dN, with 0 <= N < 128", str);
}

static grub_err_t
list_mappings (void)
{
  /* Show: list mappings.  */
  if (! map_head)
    {
      grub_puts_ (N_("No drives have been remapped"));
      return GRUB_ERR_NONE;
    }

  /* TRANSLATORS:  This is the header of mapping list. 
     On the left is how OS will see the disks and
     on the right current GRUB vision.  */
  grub_puts_ (N_("OS disk #num ------> GRUB/BIOS device"));
  drivemap_node_t *curnode = map_head;
  while (curnode)
    {
      grub_printf ("%cD #%-3u (0x%02x)       %cd%d\n",
		   (curnode->newdrive & 0x80) ? 'H' : 'F',
		   curnode->newdrive & 0x7F, curnode->newdrive,
		   (curnode->redirto & 0x80) ? 'h' : 'f',
		   curnode->redirto & 0x7F
		   );
      curnode = curnode->next;
    }
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_drivemap (struct grub_extcmd_context *ctxt, int argc, char **args)
{
  if (ctxt->state[OPTIDX_LIST].set)
    {
      return list_mappings ();
    }
  else if (ctxt->state[OPTIDX_RESET].set)
    {
      /* Reset: just delete all mappings, freeing their memory.  */
      drivemap_node_t *curnode = map_head;
      drivemap_node_t *prevnode = 0;
      while (curnode)
	{
	  prevnode = curnode;
	  curnode = curnode->next;
	  grub_free (prevnode);
	}
      map_head = 0;
      return GRUB_ERR_NONE;
    }
  else if (!ctxt->state[OPTIDX_SWAP].set && argc == 0)
    {
      /* No arguments */
      return list_mappings ();
    }

  /* Neither flag: put mapping.  */
  grub_uint8_t mapfrom = 0;
  grub_uint8_t mapto = 0xFF;
  grub_err_t err;

  if (argc != 2)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("two arguments expected"));

  err = tryparse_diskstring (args[0], &mapfrom);
  if (err != GRUB_ERR_NONE)
    return err;

  err = tryparse_diskstring (args[1], &mapto);
  if (err != GRUB_ERR_NONE)
    return err;

  if (mapto == mapfrom)
    {
      /* Reset to default.  */
      grub_dprintf ("drivemap", "Removing mapping for %s (%02x)\n",
		    args[0], mapfrom);
      drivemap_remove (mapfrom);
      return GRUB_ERR_NONE;
    }
  /* Set the mapping for the disk (overwrites any existing mapping).  */
  grub_dprintf ("drivemap", "%s %s (%02x) = %s (%02x)\n",
		ctxt->state[OPTIDX_SWAP].set ? "Swapping" : "Mapping",
		args[1], mapto, args[0], mapfrom);
  err = drivemap_set (mapto, mapfrom);
  /* If -s, perform the reverse mapping too (only if the first was OK).  */
  if (ctxt->state[OPTIDX_SWAP].set && err == GRUB_ERR_NONE)
    err = drivemap_set (mapfrom, mapto);
  return err;
}

/* Int13h handler installer - reserves conventional memory for the handler,
   copies it over and sets the IVT entry for int13h.
   This code rests on the assumption that GRUB does not activate any kind
   of memory mapping apart from identity paging, since it accesses
   realmode structures by their absolute addresses, like the IVT at 0;
   and transforms a pmode pointer into a rmode seg:off far ptr.  */
static grub_err_t
install_int13_handler (int noret __attribute__ ((unused)))
{
  /* Size of the full int13 handler "bundle", including code and map.  */
  grub_uint32_t total_size;
  /* Base address of the space reserved for the handler bundle.  */
  grub_uint8_t *handler_base = 0;
  /* Address of the map within the deployed bundle.  */
  int13map_node_t *handler_map;

  int i;
  int entries = 0;
  drivemap_node_t *curentry = map_head;

  /* Count entries to prepare a contiguous map block.  */
  while (curentry)
    {
      entries++;
      curentry = curentry->next;
    }
  if (entries == 0)
    {
      /* No need to install the int13h handler.  */
      grub_dprintf ("drivemap", "No drives marked as remapped, not installing "
		    "our int13h handler.\n");
      return GRUB_ERR_NONE;
    }

  grub_dprintf ("drivemap", "Installing our int13h handler\n");

  /* Save the pointer to the old handler.  */
  grub_drivemap_oldhandler = *int13slot;
  grub_dprintf ("drivemap", "Original int13 handler: %04x:%04x\n",
		(grub_drivemap_oldhandler >> 16) & 0x0ffff,
		grub_drivemap_oldhandler & 0x0ffff);

  /* Find a rmode-segment-aligned zone in conventional memory big
     enough to hold the handler and its data.  */
  total_size = INT13H_OFFSET (&grub_drivemap_mapstart)
    + (entries + 1) * sizeof (int13map_node_t);
  grub_dprintf ("drivemap", "Payload is %u bytes long\n", total_size);
  handler_base = grub_mmap_malign_and_register (16, ALIGN_UP (total_size, 16),
						&drivemap_mmap,
						GRUB_MEMORY_RESERVED,
						GRUB_MMAP_MALLOC_LOW);
  if (! handler_base)
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, "couldn't reserve "
		       "memory for the int13h handler");

  /* Copy int13h handler bundle to reserved area.  */
  grub_dprintf ("drivemap", "Reserved memory at %p, copying handler\n",
		handler_base);
  grub_memcpy (handler_base, &grub_drivemap_handler,
	       INT13H_OFFSET (&grub_drivemap_mapstart));

  /* Copy the mappings to the reserved area.  */
  curentry = map_head;
  handler_map = (int13map_node_t *) (handler_base +
				     INT13H_OFFSET (&grub_drivemap_mapstart));
  grub_dprintf ("drivemap", "Target map at %p, copying mappings\n", handler_map);
  for (i = 0; i < entries; ++i, curentry = curentry->next)
    {
      handler_map[i].disknum = curentry->newdrive;
      handler_map[i].mapto = curentry->redirto;
      grub_dprintf ("drivemap", "\t#%d: 0x%02x <- 0x%02x\n", i,
		    handler_map[i].disknum, handler_map[i].mapto);
    }
  /* Signal end-of-map.  */
  handler_map[i].disknum = 0;
  handler_map[i].mapto = 0;
  grub_dprintf ("drivemap", "\t#%d: 0x00 <- 0x00 (end)\n", i);

  /* Install our function as the int13h handler in the IVT.  */
  *int13slot = ((grub_uint32_t) handler_base) << 12;	/* Segment address.  */
  grub_dprintf ("drivemap", "New int13 handler: %04x:%04x\n",
		(*int13slot >> 16) & 0x0ffff, *int13slot & 0x0ffff);

  return GRUB_ERR_NONE;
}

static grub_err_t
uninstall_int13_handler (void)
{
  if (! grub_drivemap_oldhandler)
    return GRUB_ERR_NONE;

  *int13slot = grub_drivemap_oldhandler;
  grub_mmap_free_and_unregister (drivemap_mmap);
  grub_drivemap_oldhandler = 0;
  grub_dprintf ("drivemap", "Restored int13 handler: %04x:%04x\n",
		(*int13slot >> 16) & 0x0ffff, *int13slot & 0x0ffff);

  return GRUB_ERR_NONE;
}

static int
grub_get_root_biosnumber_drivemap (void)
{
  const char *biosnum;
  int ret = -1;
  grub_device_t dev;

  biosnum = grub_env_get ("biosnum");

  if (biosnum)
    return grub_strtoul (biosnum, 0, 0);

  dev = grub_device_open (0);
  if (dev && dev->disk && dev->disk->dev
      && dev->disk->dev->id == GRUB_DISK_DEVICE_BIOSDISK_ID)
    {
      drivemap_node_t *curnode = map_head;
      ret = (int) dev->disk->id;
      while (curnode)
	{
	  if (curnode->redirto == ret)
	    {
	      ret = curnode->newdrive;
	      break;
	    }
	  curnode = curnode->next;
	}

    }

  if (dev)
    grub_device_close (dev);

  return ret;
}

static grub_err_t
grub_cmd_vt_acpi (struct grub_extcmd_context *ctxt __attribute__ ((unused)),
                  int argc, char **args)
{
  if (argc != 1)
    return GRUB_ERR_NONE;
  grub_ventoy_set_acpi_osparam (args[0]);
  return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd, cmd1, cmd_vtacpi;
static int (*grub_get_root_biosnumber_saved) (void);

GRUB_MOD_INIT (map)
{
#ifdef GRUB_MACHINE_MULTIBOOT
  if (!grub_mb_check_bios_int (0x13))
    return;
#endif
  grub_get_root_biosnumber_saved = grub_get_root_biosnumber;
  grub_get_root_biosnumber = grub_get_root_biosnumber_drivemap;
  cmd = grub_register_extcmd ("map", grub_cmd_drivemap, 0,
                    N_("-l | -r | [-s] grubdev osdisk."),
                    N_("Manage the BIOS drive mappings."),
                    options);
  cmd1 = grub_register_extcmd ("drivemap", grub_cmd_drivemap, 0,
                    N_("-l | -r | [-s] grubdev osdisk."),
                    N_("Manage the BIOS drive mappings."),
                    options);
  cmd_vtacpi = grub_register_extcmd ("vt_acpi", grub_cmd_vt_acpi, 0,
                    N_("FILE"), N_("Do not use this command."), 0);
  drivemap_hook =
    grub_loader_register_preboot_hook (&install_int13_handler,
				       &uninstall_int13_handler,
				       GRUB_LOADER_PREBOOT_HOOK_PRIO_NORMAL);
}

GRUB_MOD_FINI (map)
{
#ifdef GRUB_MACHINE_MULTIBOOT
  if (!grub_mb_check_bios_int (0x13))
    return;
#endif
  grub_get_root_biosnumber = grub_get_root_biosnumber_saved;
  grub_loader_unregister_preboot_hook (drivemap_hook);
  drivemap_hook = 0;
  grub_unregister_extcmd (cmd);
  grub_unregister_extcmd (cmd1);
  grub_unregister_extcmd (cmd_vtacpi);
}
