/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008,2009,2010  Free Software Foundation, Inc.
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

#include <grub/loader.h>
#include <grub/i386/bsd.h>
#include <grub/i386/cpuid.h>
#include <grub/memory.h>
#include <grub/i386/memory.h>
#include <grub/file.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/mm.h>
#include <grub/elfload.h>
#include <grub/env.h>
#include <grub/misc.h>
#include <grub/aout.h>
#include <grub/command.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/ns8250.h>
#include <grub/bsdlabel.h>
#include <grub/crypto.h>
#include <grub/safemath.h>
#include <grub/verify.h>
#ifdef GRUB_MACHINE_PCBIOS
#include <grub/machine/int.h>
#endif

GRUB_MOD_LICENSE ("GPLv3+");

#include <grub/video.h>
#ifdef GRUB_MACHINE_PCBIOS
#include <grub/machine/biosnum.h>
#endif
#ifdef GRUB_MACHINE_EFI
#include <grub/efi/efi.h>
#define BSD_DEFAULT_VIDEO_MODE "800x600"
#else
#define BSD_DEFAULT_VIDEO_MODE "text"
#include <grub/i386/pc/vbe.h>
#endif
#include <grub/video.h>

#include <grub/disk.h>
#include <grub/device.h>
#include <grub/partition.h>
#include <grub/relocator.h>
#include <grub/i386/relocator.h>

#ifdef GRUB_MACHINE_EFI
#include <grub/acpi.h>
#endif

#define ALIGN_DWORD(a)	ALIGN_UP (a, 4)
#define ALIGN_QWORD(a)	ALIGN_UP (a, 8)
#define ALIGN_VAR(a)	((is_64bit) ? (ALIGN_QWORD(a)) : (ALIGN_DWORD(a)))
#define ALIGN_PAGE(a)	ALIGN_UP (a, 4096)

static int kernel_type = KERNEL_TYPE_NONE;
static grub_dl_t my_mod;
static grub_addr_t entry, entry_hi, kern_start, kern_end;
static void *kern_chunk_src;
static grub_uint32_t bootflags;
static int is_elf_kernel, is_64bit;
static grub_uint32_t openbsd_root;
static struct grub_relocator *relocator = NULL;
static struct grub_openbsd_ramdisk_descriptor openbsd_ramdisk;

struct bsd_tag
{
  struct bsd_tag *next;
  grub_size_t len;
  grub_uint32_t type;
  union {
    grub_uint8_t a;
    grub_uint16_t b;
    grub_uint32_t c;
  } data[0];
};

static struct bsd_tag *tags, *tags_last;

struct netbsd_module
{
  struct netbsd_module *next;
  struct grub_netbsd_btinfo_module mod;
};

static struct netbsd_module *netbsd_mods, *netbsd_mods_last;

static const struct grub_arg_option freebsd_opts[] =
  {
    {"dual", 'D', 0, N_("Display output on all consoles."), 0, 0},
    {"serial", 'h', 0, N_("Use serial console."), 0, 0},
    {"askname", 'a', 0, N_("Ask for file name to reboot from."), 0, 0},
    {"cdrom", 'C', 0, N_("Use CD-ROM as root."), 0, 0},
    {"config", 'c', 0, N_("Invoke user configuration routing."), 0, 0},
    {"kdb", 'd', 0, N_("Enter in KDB on boot."), 0, 0},
    {"gdb", 'g', 0, N_("Use GDB remote debugger instead of DDB."), 0, 0},
    {"mute", 'm', 0, N_("Disable all boot output."), 0, 0},
    {"nointr", 'n', 0, "", 0, 0},
    {"pause", 'p', 0, N_("Wait for keypress after every line of output."), 0, 0},
    {"quiet", 'q', 0, "", 0, 0},
    {"dfltroot", 'r', 0, N_("Use compiled-in root device."), 0, 0},
    {"single", 's', 0, N_("Boot into single mode."), 0, 0},
    {"verbose", 'v', 0, N_("Boot with verbose messages."), 0, 0},
    {0, 0, 0, 0, 0, 0}
  };

static const grub_uint32_t freebsd_flags[] =
{
  FREEBSD_RB_DUAL, FREEBSD_RB_SERIAL, FREEBSD_RB_ASKNAME,
  FREEBSD_RB_CDROM, FREEBSD_RB_CONFIG, FREEBSD_RB_KDB,
  FREEBSD_RB_GDB, FREEBSD_RB_MUTE, FREEBSD_RB_NOINTR,
  FREEBSD_RB_PAUSE, FREEBSD_RB_QUIET, FREEBSD_RB_DFLTROOT,
  FREEBSD_RB_SINGLE, FREEBSD_RB_VERBOSE, 0
};

static const struct grub_arg_option openbsd_opts[] =
  {
    {"askname", 'a', 0, N_("Ask for file name to reboot from."), 0, 0},
    {"halt", 'b', 0, N_("Don't reboot, just halt."), 0, 0},
    {"config", 'c', 0, N_("Change configured devices."), 0, 0},
    {"single", 's', 0, N_("Boot into single mode."), 0, 0},
    {"kdb", 'd', 0, N_("Enter in KDB on boot."), 0, 0},
    {"root", 'r', 0, N_("Set root device."), "wdXY", ARG_TYPE_STRING},
    {"serial", 'h', GRUB_ARG_OPTION_OPTIONAL, 
     N_("Use serial console."),
     /* TRANSLATORS: "com" is static and not to be translated. It refers to
	serial ports e.g. com1.
      */
     N_("comUNIT[,SPEED]"), ARG_TYPE_STRING},
    {0, 0, 0, 0, 0, 0}
  };

static const grub_uint32_t openbsd_flags[] =
{
  OPENBSD_RB_ASKNAME, OPENBSD_RB_HALT, OPENBSD_RB_CONFIG,
  OPENBSD_RB_SINGLE, OPENBSD_RB_KDB, 0
};

#define OPENBSD_ROOT_ARG (ARRAY_SIZE (openbsd_flags) - 1)
#define OPENBSD_SERIAL_ARG (ARRAY_SIZE (openbsd_flags))

static const struct grub_arg_option netbsd_opts[] =
  {
    {"no-smp", '1', 0, N_("Disable SMP."), 0, 0},
    {"no-acpi", '2', 0, N_("Disable ACPI."), 0, 0},
    {"askname", 'a', 0, N_("Ask for file name to reboot from."), 0, 0},
    {"halt", 'b', 0, N_("Don't reboot, just halt."), 0, 0},
    {"config", 'c', 0, N_("Change configured devices."), 0, 0},
    {"kdb", 'd', 0, N_("Enter in KDB on boot."), 0, 0},
    {"miniroot", 'm', 0, "", 0, 0},
    {"quiet", 'q', 0, N_("Don't display boot diagnostic messages."), 0, 0},
    {"single", 's', 0, N_("Boot into single mode."), 0, 0},
    {"verbose", 'v', 0, N_("Boot with verbose messages."), 0, 0},
    {"debug", 'x', 0, N_("Boot with debug messages."), 0, 0},
    {"silent", 'z', 0, N_("Suppress normal output (warnings remain)."), 0, 0},
    {"root", 'r', 0, N_("Set root device."), N_("DEVICE"), ARG_TYPE_STRING},
    {"serial", 'h', GRUB_ARG_OPTION_OPTIONAL, 
     N_("Use serial console."),
     /* TRANSLATORS: "com" is static and not to be translated. It refers to
	serial ports e.g. com1.
      */
     N_("[ADDR|comUNIT][,SPEED]"), ARG_TYPE_STRING},
    {0, 0, 0, 0, 0, 0}
  };

static const grub_uint32_t netbsd_flags[] =
{
  NETBSD_AB_NOSMP, NETBSD_AB_NOACPI, NETBSD_RB_ASKNAME,
  NETBSD_RB_HALT, NETBSD_RB_USERCONFIG, NETBSD_RB_KDB,
  NETBSD_RB_MINIROOT, NETBSD_AB_QUIET, NETBSD_RB_SINGLE,
  NETBSD_AB_VERBOSE, NETBSD_AB_DEBUG, NETBSD_AB_SILENT, 0
};

#define NETBSD_ROOT_ARG (ARRAY_SIZE (netbsd_flags) - 1)
#define NETBSD_SERIAL_ARG (ARRAY_SIZE (netbsd_flags))

static void
grub_bsd_get_device (grub_uint32_t * biosdev,
		     grub_uint32_t * unit,
		     grub_uint32_t * slice, grub_uint32_t * part)
{
  grub_device_t dev;

#ifdef GRUB_MACHINE_PCBIOS
  *biosdev = grub_get_root_biosnumber () & 0xff;
#else
  *biosdev = 0xff;
#endif
  *unit = (*biosdev & 0x7f);
  *slice = 0xff;
  *part = 0xff;
  dev = grub_device_open (0);
  if (dev && dev->disk && dev->disk->partition)
    {
      if (dev->disk->partition->parent)
	{
	  *part = dev->disk->partition->number;
	  *slice = dev->disk->partition->parent->number + 1;
	}
      else
	*slice = dev->disk->partition->number + 1;
    }
  if (dev)
    grub_device_close (dev);
}

static grub_err_t
grub_bsd_add_meta_ptr (grub_uint32_t type, void **ptr, grub_uint32_t len)
{
  struct bsd_tag *newtag;

  newtag = grub_malloc (len + sizeof (struct bsd_tag));
  if (!newtag)
    return grub_errno;
  newtag->len = len;
  newtag->type = type;
  newtag->next = NULL;
  *ptr = newtag->data;

  if (kernel_type == KERNEL_TYPE_FREEBSD
      && (type == (FREEBSD_MODINFO_METADATA | FREEBSD_MODINFOMD_SMAP)
          || type == (FREEBSD_MODINFO_METADATA | FREEBSD_MODINFOMD_VBE_FB)
          || type == (FREEBSD_MODINFO_METADATA | FREEBSD_MODINFOMD_EFI_FB)))
    {
      struct bsd_tag *p;
      for (p = tags;
	   p && p->type != (FREEBSD_MODINFO_METADATA
			    | FREEBSD_MODINFOMD_KERNEND);
	   p = p->next);

      if (p)
	{
	  newtag->next = p->next;
	  p->next = newtag;
	  if (newtag->next == NULL)
	    tags_last = newtag;
	  return GRUB_ERR_NONE;
	}
    }

  if (tags_last)
    tags_last->next = newtag;
  else
    tags = newtag;
  tags_last = newtag;

  return GRUB_ERR_NONE;
}

grub_err_t
grub_bsd_add_meta (grub_uint32_t type, const void *data, grub_uint32_t len)
{
  grub_err_t err;
  void *ptr;

  err = grub_bsd_add_meta_ptr (type, &ptr, len);
  if (err)
    return err;
  if (len)
    grub_memcpy (ptr, data, len);
  return GRUB_ERR_NONE;
}


struct grub_e820_mmap
{
  grub_uint64_t addr;
  grub_uint64_t size;
  grub_uint32_t type;
} GRUB_PACKED;
#define GRUB_E820_RAM        1
#define GRUB_E820_RESERVED   2
#define GRUB_E820_ACPI       3
#define GRUB_E820_NVS        4
#define GRUB_E820_BADRAM     5
#define GRUB_E820_COREBOOT_TABLES 0x10

/* Context for generate_e820_mmap.  */
struct generate_e820_mmap_ctx
{
  int count;
  struct grub_e820_mmap *mmap;
  struct grub_e820_mmap prev, cur;
};

/* Helper for generate_e820_mmap.  */
static int
generate_e820_mmap_iter (grub_uint64_t addr, grub_uint64_t size,
			 grub_memory_type_t type, void *data)
{
  struct generate_e820_mmap_ctx *ctx = data;

  ctx->cur.addr = addr;
  ctx->cur.size = size;

  if (type == GRUB_MEMORY_COREBOOT_TABLES
      && addr == 0)
      /* Nowadays the tables at 0 don't contain anything important but
       *BSD needs the memory at 0 for own needs.
       */
    type = GRUB_E820_RAM;

  ctx->cur.type = type;

  /* Merge regions if possible. */
  if (ctx->count && ctx->cur.type == ctx->prev.type
      && ctx->cur.addr == ctx->prev.addr + ctx->prev.size)
    {
      ctx->prev.size += ctx->cur.size;
      if (ctx->mmap)
	ctx->mmap[-1] = ctx->prev;
    }
  else
    {
      if (ctx->mmap)
	*ctx->mmap++ = ctx->cur;
      ctx->prev = ctx->cur;
      ctx->count++;
    }

  if (kernel_type == KERNEL_TYPE_OPENBSD && ctx->prev.addr < 0x100000
      && ctx->prev.addr + ctx->prev.size > 0x100000)
    {
      ctx->cur.addr = 0x100000;
      ctx->cur.size = ctx->prev.addr + ctx->prev.size - 0x100000;
      ctx->cur.type = ctx->prev.type;
      ctx->prev.size = 0x100000 - ctx->prev.addr;
      if (ctx->mmap)
	{
	  ctx->mmap[-1] = ctx->prev;
	  ctx->mmap[0] = ctx->cur;
	  ctx->mmap++;
	}
      ctx->prev = ctx->cur;
      ctx->count++;
    }

  return 0;
}

static void
generate_e820_mmap (grub_size_t *len, grub_size_t *cnt, void *buf)
{
  struct generate_e820_mmap_ctx ctx = {
    .count = 0,
    .mmap = buf
  };

  grub_mmap_iterate (generate_e820_mmap_iter, &ctx);

  if (len)
    *len = ctx.count * sizeof (struct grub_e820_mmap);
  *cnt = ctx.count;

  return;
}

static grub_err_t
grub_bsd_add_mmap (void)
{
  grub_size_t len, cnt;
  void *buf = NULL, *buf0;

  generate_e820_mmap (&len, &cnt, buf);

  if (kernel_type == KERNEL_TYPE_NETBSD)
    len += sizeof (grub_uint32_t);

  if (kernel_type == KERNEL_TYPE_OPENBSD)
    len += sizeof (struct grub_e820_mmap);

  buf = grub_malloc (len);
  if (!buf)
    return grub_errno;

  buf0 = buf;
  if (kernel_type == KERNEL_TYPE_NETBSD)
    {
      *(grub_uint32_t *) buf = cnt;
      buf = ((grub_uint32_t *) buf + 1);
    }

  generate_e820_mmap (NULL, &cnt, buf);

  if (kernel_type == KERNEL_TYPE_OPENBSD)
    grub_memset ((grub_uint8_t *) buf + len - sizeof (struct grub_e820_mmap), 0,
		 sizeof (struct grub_e820_mmap));

  grub_dprintf ("bsd", "%u entries in smap\n", (unsigned) cnt);
  if (kernel_type == KERNEL_TYPE_NETBSD)
    grub_bsd_add_meta (NETBSD_BTINFO_MEMMAP, buf0, len);
  else if (kernel_type == KERNEL_TYPE_OPENBSD)
    grub_bsd_add_meta (OPENBSD_BOOTARG_MMAP, buf0, len);
  else
    grub_bsd_add_meta (FREEBSD_MODINFO_METADATA |
		       FREEBSD_MODINFOMD_SMAP, buf0, len);

  grub_free (buf0);

  return grub_errno;
}

grub_err_t
grub_freebsd_add_meta_module (const char *filename, const char *type,
			      int argc, char **argv,
			      grub_addr_t addr, grub_uint32_t size)
{
  const char *name;
  grub_err_t err;

  name = grub_strrchr (filename, '/');
  if (name)
    name++;
  else
    name = filename;
  if (grub_strcmp (type, "/boot/zfs/zpool.cache") == 0)
    name = "/boot/zfs/zpool.cache";

  if (grub_bsd_add_meta (FREEBSD_MODINFO_NAME, name, grub_strlen (name) + 1))
    return grub_errno;

  if (is_64bit)
    {
      grub_uint64_t addr64 = addr, size64 = size;
      if (grub_bsd_add_meta (FREEBSD_MODINFO_TYPE, type, grub_strlen (type) + 1)
	  || grub_bsd_add_meta (FREEBSD_MODINFO_ADDR, &addr64, sizeof (addr64)) 
	  || grub_bsd_add_meta (FREEBSD_MODINFO_SIZE, &size64, sizeof (size64)))
	return grub_errno;
    }
  else
    {
      if (grub_bsd_add_meta (FREEBSD_MODINFO_TYPE, type, grub_strlen (type) + 1)
	  || grub_bsd_add_meta (FREEBSD_MODINFO_ADDR, &addr, sizeof (addr))
	  || grub_bsd_add_meta (FREEBSD_MODINFO_SIZE, &size, sizeof (size)))
	return grub_errno;
    }

  if (argc)
    {
      int i, n;

      n = 0;
      for (i = 0; i < argc; i++)
	{
	  n += grub_strlen (argv[i]) + 1;
	}

      if (n)
	{
	  void *cmdline;
	  char *p;

	  if (grub_bsd_add_meta_ptr (FREEBSD_MODINFO_ARGS, &cmdline, n))
	    return grub_errno;

	  p = cmdline;
	  for (i = 0; i < argc; i++)
	    {
	      grub_strcpy (p, argv[i]);
	      p += grub_strlen (argv[i]);
	      *(p++) = ' ';
	    }
	  *p = 0;
	  err = grub_verify_string (cmdline, GRUB_VERIFY_MODULE_CMDLINE);
	  if (err)
	    return err;
	}
    }

  return GRUB_ERR_NONE;
}

static void
grub_freebsd_list_modules (void)
{
  struct bsd_tag *tag;

  grub_printf ("  %-18s  %-18s%14s%14s\n", _("name"), _("type"), _("addr"),
	       _("size"));

  for (tag = tags; tag; tag = tag->next)
    {
      switch (tag->type)
	{
	case FREEBSD_MODINFO_NAME:
	case FREEBSD_MODINFO_TYPE:
	  grub_printf ("  %-18s", (char *) tag->data);
	  break;
	case FREEBSD_MODINFO_ADDR:
	  {
	    grub_uint32_t addr;

	    addr = *((grub_uint32_t *) tag->data);
	    grub_printf ("    0x%08x", addr);
	    break;
	  }
	case FREEBSD_MODINFO_SIZE:
	  {
	    grub_uint32_t len;

	    len = *((grub_uint32_t *) tag->data);
	    grub_printf ("    0x%08x\n", len);
	  }
	}
    }
}

static grub_err_t
grub_netbsd_add_meta_module (char *filename, grub_uint32_t type,
			     grub_addr_t addr, grub_uint32_t size)
{
  char *name;
  struct netbsd_module *mod;
  name = grub_strrchr (filename, '/');

  if (name)
    name++;
  else
    name = filename;

  mod = grub_zalloc (sizeof (*mod));
  if (!mod)
    return grub_errno;

  grub_strncpy (mod->mod.name, name, sizeof (mod->mod.name) - 1);
  mod->mod.addr = addr;
  mod->mod.type = type;
  mod->mod.size = size;

  if (netbsd_mods_last)
    netbsd_mods_last->next = mod;
  else
    netbsd_mods = mod;
  netbsd_mods_last = mod;

  return GRUB_ERR_NONE;
}

static void
grub_netbsd_list_modules (void)
{
  struct netbsd_module *mod;

  grub_printf ("  %-18s%14s%14s%14s\n", _("name"), _("type"), _("addr"),
	       _("size"));

  for (mod = netbsd_mods; mod; mod = mod->next)
    grub_printf ("  %-18s  0x%08x  0x%08x  0x%08x", mod->mod.name,
		 mod->mod.type, mod->mod.addr, mod->mod.size);
}

/* This function would be here but it's under different license. */
#include "bsd_pagetable.c"

static grub_uint32_t freebsd_bootdev, freebsd_biosdev;
static grub_uint64_t freebsd_zfsguid;

static void
freebsd_get_zfs (void)
{
  grub_device_t dev;
  grub_fs_t fs;
  char *uuid;
  grub_err_t err;

  dev = grub_device_open (0);
  if (!dev)
    return;
  fs = grub_fs_probe (dev);
  if (!fs)
    return;
  if (!fs->fs_uuid || grub_strcmp (fs->name, "zfs") != 0)
    return;
  err = fs->fs_uuid (dev, &uuid);
  if (err)
    return;
  if (!uuid)
    return;
  freebsd_zfsguid = grub_strtoull (uuid, 0, 16);
  grub_free (uuid);
}

static grub_err_t
grub_bsd_setup_video_common (struct grub_video_mode_info *mode_info,
                             void **framebuffer,
                             grub_video_driver_id_t* driv_id)
{
  const char *modevar;
  grub_err_t err;

  *driv_id = GRUB_VIDEO_DRIVER_NONE;

  modevar = grub_env_get ("gfxpayload");

  /* Now all graphical modes are acceptable.
     May change in future if we have modes without framebuffer.  */
  if (modevar && *modevar != 0)
  {
    char *tmp;
    tmp = grub_xasprintf ("%s;" BSD_DEFAULT_VIDEO_MODE, modevar);
    if (! tmp)
      return grub_errno;
    err = grub_video_set_mode (tmp, 0, 0);
    grub_free (tmp);
  }
  else
    err = grub_video_set_mode (BSD_DEFAULT_VIDEO_MODE, 0, 0);

  if (err)
    return err;

  *driv_id = grub_video_get_driver_id ();
  if (*driv_id == GRUB_VIDEO_DRIVER_NONE)
    return GRUB_ERR_NONE;

  err = grub_video_get_info_and_fini (mode_info, framebuffer);

  if (err)
    return err;

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_freebsd_setup_video (void)
{
  struct grub_video_mode_info mode_info;
  void *framebuffer;
  union grub_freebsd_fb params;
  grub_err_t err;
  grub_video_driver_id_t driv_id;

  err = grub_bsd_setup_video_common (&mode_info, &framebuffer, &driv_id);

  if (err || driv_id == GRUB_VIDEO_DRIVER_NONE)
    return err;

  params.vbe.fb_width = mode_info.width;
  params.vbe.fb_height = mode_info.height;
  params.vbe.fb_bpp = mode_info.bpp;
  params.vbe.fb_stride = mode_info.pitch / ((params.vbe.fb_bpp + 7) / 8);

  params.vbe.fb_addr = (grub_addr_t) framebuffer;

  params.vbe.fb_mask_red =
      ((1ULL << mode_info.red_mask_size) - 1) << mode_info.red_field_pos;
  params.vbe.fb_mask_green =
      ((1ULL << mode_info.green_mask_size) - 1) << mode_info.green_field_pos;
  params.vbe.fb_mask_blue =
      ((1ULL << mode_info.blue_mask_size) - 1) << mode_info.blue_field_pos;
  params.vbe.fb_mask_reserved =
      ~(params.vbe.fb_mask_red | params.vbe.fb_mask_green|params.vbe.fb_mask_blue);
  params.vbe.fb_size =
      params.vbe.fb_height * params.vbe.fb_stride * ((params.vbe.fb_bpp + 7) / 8);

#ifdef GRUB_MACHINE_PCBIOS
  err = grub_bsd_add_meta (FREEBSD_MODINFO_METADATA | FREEBSD_MODINFOMD_VBE_FB,
                           &params.vbe, sizeof (params.vbe));
#else
  err = grub_bsd_add_meta (FREEBSD_MODINFO_METADATA | FREEBSD_MODINFOMD_EFI_FB,
                           &params.efi, sizeof (params.efi));
#endif
  return err;
}

static grub_err_t
grub_freebsd_boot (void)
{
  struct grub_freebsd_bootinfo bi;
  grub_uint8_t *p, *p0;
  grub_addr_t p_target;
  grub_size_t p_size = 0;
  grub_err_t err;
  grub_size_t tag_buf_len = 0;

  struct grub_env_var *var;

#ifdef GRUB_MACHINE_EFI
  /* Pass RSDP to FreeBSD. It cannot find it in EFI mode. */
  if (!(grub_env_get ("kFreeBSD.hint.acpi.0.rsdp")))
  {
    char rsdp[20];
    grub_snprintf (rsdp, sizeof(rsdp), "%p", grub_acpi_get_rsdpv2 ());
    grub_env_set ("kFreeBSD.hint.acpi.0.rsdp", rsdp);
  }
#endif

  grub_memset (&bi, 0, sizeof (bi));
  bi.version = FREEBSD_BOOTINFO_VERSION;
  bi.length = sizeof (bi);

  bi.boot_device = freebsd_biosdev;

  p_size = 0;
  FOR_SORTED_ENV (var)
    if ((grub_memcmp (var->name, "kFreeBSD.", sizeof("kFreeBSD.") - 1) == 0) && (var->name[sizeof("kFreeBSD.") - 1]))
      {
	p_size += grub_strlen (&var->name[sizeof("kFreeBSD.") - 1]);
	p_size++;
	p_size += grub_strlen (var->value) + 1;
      }

  if (p_size)
    p_size = ALIGN_PAGE (kern_end + p_size + 1) - kern_end;

  if (is_elf_kernel)
    {
      struct bsd_tag *tag;

      err = grub_bsd_add_mmap ();
      if (err)
	return err;

      err = grub_freebsd_setup_video ();
      if (err)
	{
	  grub_print_error ();
	  grub_puts_ (N_("Booting in blind mode"));
	  grub_errno = GRUB_ERR_NONE;
	}

      err = grub_bsd_add_meta (FREEBSD_MODINFO_END, 0, 0);
      if (err)
	return err;

      tag_buf_len = 0;
      for (tag = tags; tag; tag = tag->next)
	tag_buf_len = ALIGN_VAR (tag_buf_len
				 + sizeof (struct freebsd_tag_header)
				 + tag->len);
      p_size = ALIGN_PAGE (kern_end + p_size + tag_buf_len) - kern_end;
    }

  if (is_64bit)
    p_size += 4096 * 3;

  {
    grub_relocator_chunk_t ch;
    err = grub_relocator_alloc_chunk_addr (relocator, &ch,
					   kern_end, p_size);
    if (err)
      return err;
    p = get_virtual_current_address (ch);
  }
  p_target = kern_end;
  p0 = p;
  kern_end += p_size;

  FOR_SORTED_ENV (var)
    if ((grub_memcmp (var->name, "kFreeBSD.", sizeof("kFreeBSD.") - 1) == 0) && (var->name[sizeof("kFreeBSD.") - 1]))
      {
	grub_strcpy ((char *) p, &var->name[sizeof("kFreeBSD.") - 1]);
	p += grub_strlen ((char *) p);
	*(p++) = '=';
	grub_strcpy ((char *) p, var->value);
	p += grub_strlen ((char *) p) + 1;
      }

  if (p != p0)
    {
      *(p++) = 0;

      bi.environment = p_target;
    }

  if (is_elf_kernel)
    {
      grub_uint8_t *p_tag = p;
      struct bsd_tag *tag;
      
      for (tag = tags; tag; tag = tag->next)
	{
	  struct freebsd_tag_header *head
	    = (struct freebsd_tag_header *) p_tag;
	  head->type = tag->type;
	  head->len = tag->len;
	  p_tag += sizeof (struct freebsd_tag_header);
	  switch (tag->type)
	    {
	    case FREEBSD_MODINFO_METADATA | FREEBSD_MODINFOMD_HOWTO:
	      if (is_64bit)
		*(grub_uint64_t *) p_tag = bootflags;
	      else
		*(grub_uint32_t *) p_tag = bootflags;
	      break;

	    case FREEBSD_MODINFO_METADATA | FREEBSD_MODINFOMD_ENVP:
	      if (is_64bit)
		*(grub_uint64_t *) p_tag = bi.environment;
	      else
		*(grub_uint32_t *) p_tag = bi.environment;
	      break;

	    case FREEBSD_MODINFO_METADATA | FREEBSD_MODINFOMD_KERNEND:
	      if (is_64bit)
		*(grub_uint64_t *) p_tag = kern_end;
	      else
		*(grub_uint32_t *) p_tag = kern_end;
	      break;

	    default:
	      grub_memcpy (p_tag, tag->data, tag->len);
	      break;
	    }
	  p_tag += tag->len;
	  p_tag = ALIGN_VAR (p_tag - p) + p;
	}

      bi.tags = (p - p0) + p_target;

      p = (ALIGN_PAGE ((p_tag - p0) + p_target) - p_target) + p0;
    }

  bi.kern_end = kern_end;

  grub_video_set_mode ("text", 0, 0);

  if (is_64bit)
    {
      struct grub_relocator64_state state;
      grub_uint8_t *pagetable;
      grub_uint32_t *stack;
      grub_addr_t stack_target;

      {
	grub_relocator_chunk_t ch;
	err = grub_relocator_alloc_chunk_align (relocator, &ch,
						0x10000, 0x90000,
						3 * sizeof (grub_uint32_t)
						+ sizeof (bi), 4,
						GRUB_RELOCATOR_PREFERENCE_NONE,
						0);
	if (err)
	  return err;
	stack = get_virtual_current_address (ch);
	stack_target = get_physical_target_address (ch);
      }

#ifdef GRUB_MACHINE_EFI
      err = grub_efi_finish_boot_services (NULL, NULL, NULL, NULL, NULL);
      if (err)
	return err;
#endif

      pagetable = p;
      fill_bsd64_pagetable (pagetable, (pagetable - p0) + p_target);

      state.cr3 = (pagetable - p0) + p_target;
      state.rsp = stack_target;
      state.rip = (((grub_uint64_t) entry_hi) << 32) | entry;

      stack[0] = entry;
      stack[1] = bi.tags;
      stack[2] = kern_end;
      return grub_relocator64_boot (relocator, state, 0, 0x40000000);
    }
  else
    {
      struct grub_relocator32_state state;
      grub_uint32_t *stack;
      grub_addr_t stack_target;

      {
	grub_relocator_chunk_t ch;
	err = grub_relocator_alloc_chunk_align (relocator, &ch,
						0x10000, 0x90000,
						9 * sizeof (grub_uint32_t)
						+ sizeof (bi), 4,
						GRUB_RELOCATOR_PREFERENCE_NONE,
						0);
	if (err)
	  return err;
	stack = get_virtual_current_address (ch);
	stack_target = get_physical_target_address (ch);
      }

#ifdef GRUB_MACHINE_EFI
      err = grub_efi_finish_boot_services (NULL, NULL, NULL, NULL, NULL);
      if (err)
	return err;
#endif

      grub_memcpy (&stack[9], &bi, sizeof (bi));
      state.eip = entry;
      state.esp = stack_target;
      state.ebp = stack_target;
      stack[0] = entry; /* "Return" address.  */
      stack[1] = bootflags | FREEBSD_RB_BOOTINFO;
      stack[2] = freebsd_bootdev;
      stack[3] = freebsd_zfsguid ? 4 : 0;
      stack[4] = freebsd_zfsguid;
      stack[5] = freebsd_zfsguid >> 32;
      stack[6] = stack_target + 9 * sizeof (grub_uint32_t);
      stack[7] = bi.tags;
      stack[8] = kern_end;
      return grub_relocator32_boot (relocator, state, 0);
    }

  /* Not reached.  */
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_openbsd_boot (void)
{
  grub_uint32_t *stack;
  struct grub_relocator32_state state;
  void *curarg, *buf0, *arg0;
  grub_addr_t buf_target;
  grub_err_t err;
  grub_size_t tag_buf_len;

  err = grub_bsd_add_mmap ();
  if (err)
    return err;

#ifdef GRUB_MACHINE_PCBIOS
  {
    struct grub_bios_int_registers regs;

    regs.flags = GRUB_CPU_INT_FLAGS_DEFAULT;

    regs.ebx = 0;
    regs.ecx = 0;
    regs.eax = 0xb101;
    regs.es = 0;
    regs.edi = 0;
    regs.edx = 0;

    grub_bios_interrupt (0x1a, &regs);
    if (regs.edx == 0x20494350)
      {
	struct grub_openbsd_bootarg_pcibios pcibios;
	
	pcibios.characteristics = regs.eax & 0xff;
	pcibios.revision = regs.ebx & 0xffff;
	pcibios.pm_entry = regs.edi;
	pcibios.last_bus = regs.ecx & 0xff;

	grub_bsd_add_meta (OPENBSD_BOOTARG_PCIBIOS, &pcibios,
			   sizeof (pcibios));
      }
  }
#endif

  {
    struct bsd_tag *tag;
    tag_buf_len = 0;
    for (tag = tags; tag; tag = tag->next)
      tag_buf_len = ALIGN_VAR (tag_buf_len
			       + sizeof (struct grub_openbsd_bootargs)
			       + tag->len);
  }

  buf_target = GRUB_BSD_TEMP_BUFFER - 9 * sizeof (grub_uint32_t);
  {
    grub_relocator_chunk_t ch;
    err = grub_relocator_alloc_chunk_addr (relocator, &ch, buf_target,
					   tag_buf_len
					   + sizeof (struct grub_openbsd_bootargs)
					   + 9 * sizeof (grub_uint32_t));
    if (err)
      return err;
    buf0 = get_virtual_current_address (ch);
  }

  stack = (grub_uint32_t *) buf0;
  arg0 = curarg = stack + 9;

  {
    struct bsd_tag *tag;
    struct grub_openbsd_bootargs *head;

    for (tag = tags; tag; tag = tag->next)
      {
	head = curarg;
	head->ba_type = tag->type;
	head->ba_size = tag->len + sizeof (*head);
	curarg = head + 1;
	grub_memcpy (curarg, tag->data, tag->len);
	curarg = (grub_uint8_t *) curarg + tag->len;
	head->ba_next = (grub_uint8_t *) curarg - (grub_uint8_t *) buf0
	  + buf_target;
      }
    head = curarg;
    head->ba_type = OPENBSD_BOOTARG_END;
    head->ba_size = 0;
    head->ba_next = 0;
  }

  grub_video_set_mode ("text", 0, 0);

#ifdef GRUB_MACHINE_EFI
  err = grub_efi_finish_boot_services (NULL, NULL, NULL, NULL, NULL);
  if (err)
    return err;
#endif

  state.eip = entry;
  state.ebp = state.esp
    = ((grub_uint8_t *) stack - (grub_uint8_t *) buf0) + buf_target;
  stack[0] = entry;
  stack[1] = bootflags;
  stack[2] = openbsd_root;
  stack[3] = OPENBSD_BOOTARG_APIVER;
  stack[4] = 0;
  stack[5] = grub_mmap_get_upper () >> 10;
  stack[6] = grub_mmap_get_lower () >> 10;
  stack[7] = (grub_uint8_t *) curarg - (grub_uint8_t *) arg0;
  stack[8] = ((grub_uint8_t *) arg0 - (grub_uint8_t *) buf0) + buf_target;

  return grub_relocator32_boot (relocator, state, 0);
}

static grub_err_t
grub_netbsd_setup_video (void)
{
  struct grub_video_mode_info mode_info;
  void *framebuffer;
  struct grub_netbsd_btinfo_framebuf params;
  grub_err_t err;
  grub_video_driver_id_t driv_id;

  err = grub_bsd_setup_video_common (&mode_info, &framebuffer, &driv_id);

  if (err || driv_id == GRUB_VIDEO_DRIVER_NONE)
    return err;

  params.width = mode_info.width;
  params.height = mode_info.height;
  params.bpp = mode_info.bpp;
  params.pitch = mode_info.pitch;
  params.flags = 0;

  params.fbaddr = (grub_addr_t) framebuffer;

  params.red_mask_size = mode_info.red_mask_size;
  params.red_field_pos = mode_info.red_field_pos;
  params.green_mask_size = mode_info.green_mask_size;
  params.green_field_pos = mode_info.green_field_pos;
  params.blue_mask_size = mode_info.blue_mask_size;
  params.blue_field_pos = mode_info.blue_field_pos;

#ifdef GRUB_MACHINE_PCBIOS
  /* VESA packed modes may come with zeroed mask sizes, which need
     to be set here according to DAC Palette width.  If we don't,
     this results in Linux displaying a black screen.  */
  if (mode_info.bpp <= 8 && driv_id == GRUB_VIDEO_DRIVER_VBE)
    {
      struct grub_vbe_info_block controller_info;
      int status;
      int width = 8;

      status = grub_vbe_bios_get_controller_info (&controller_info);

      if (status == GRUB_VBE_STATUS_OK &&
	  (controller_info.capabilities & GRUB_VBE_CAPABILITY_DACWIDTH))
	status = grub_vbe_bios_set_dac_palette_width (&width);

      if (status != GRUB_VBE_STATUS_OK)
	/* 6 is default after mode reset.  */
	width = 6;

      params.red_mask_size = params.green_mask_size
	= params.blue_mask_size = width;
    }
#endif

  err = grub_bsd_add_meta (NETBSD_BTINFO_FRAMEBUF, &params, sizeof (params));
  return err;
}

static grub_err_t
grub_netbsd_add_modules (void)
{
  struct netbsd_module *mod;
  unsigned modcnt = 0;
  struct grub_netbsd_btinfo_modules *mods;
  unsigned i;
  grub_err_t err;
  grub_size_t sz;

  for (mod = netbsd_mods; mod; mod = mod->next)
    modcnt++;

  if (grub_mul (modcnt, sizeof (mods->mods[0]), &sz) ||
      grub_add (sz, sizeof (*mods), &sz))
    return GRUB_ERR_OUT_OF_RANGE;

  mods = grub_malloc (sz);
  if (!mods)
    return grub_errno;

  mods->num = modcnt;
  mods->last_addr = kern_end;
  for (mod = netbsd_mods, i = 0; mod; i++, mod = mod->next)
    mods->mods[i] = mod->mod;

  err = grub_bsd_add_meta (NETBSD_BTINFO_MODULES, mods,
			   sizeof (*mods) + sizeof (mods->mods[0]) * modcnt);
  grub_free (mods);
  return err;
}

/*
 * Adds NetBSD bootinfo bootdisk and bootwedge.  The partition identified
 * in these bootinfo fields is the root device.
 */
static void
grub_netbsd_add_boot_disk_and_wedge (void)
{
  grub_device_t dev;
  grub_disk_t disk;
  grub_partition_t part;
  grub_uint32_t biosdev;
  grub_uint32_t partmapsector;
  union {
    grub_uint64_t raw[GRUB_DISK_SECTOR_SIZE / 8];
    struct grub_partition_bsd_disk_label label;
  } buf;

  if (GRUB_MD_MD5->mdlen > GRUB_CRYPTO_MAX_MDLEN)
    {
      grub_error (GRUB_ERR_BUG, "mdlen too long");
      return;
    }

  dev = grub_device_open (0);
  if (! (dev && dev->disk && dev->disk->partition))
    goto fail;

  disk = dev->disk;
  part = disk->partition;

  if (disk->dev && disk->dev->id == GRUB_DISK_DEVICE_BIOSDISK_ID)
    biosdev = (grub_uint32_t) disk->id & 0xff;
  else
    biosdev = 0xff;

  /* Absolute sector of the partition map describing this partition.  */
  partmapsector = grub_partition_get_start (part->parent) + part->offset;

  disk->partition = part->parent;
  if (grub_disk_read (disk, part->offset, 0, GRUB_DISK_SECTOR_SIZE, buf.raw)
      != GRUB_ERR_NONE)
    goto fail;
  disk->partition = part;

  /* Fill bootwedge.  */
  {
    struct grub_netbsd_btinfo_bootwedge biw;
    grub_uint8_t hash[GRUB_CRYPTO_MAX_MDLEN];

    grub_memset (&biw, 0, sizeof (biw));
    biw.biosdev = biosdev;
    biw.startblk = grub_partition_get_start (part);
    biw.nblks = part->len;
    biw.matchblk = partmapsector;
    biw.matchnblks = 1;

    grub_crypto_hash (GRUB_MD_MD5, hash,
		      buf.raw, GRUB_DISK_SECTOR_SIZE);
    grub_memcpy (biw.matchhash, hash, 16);

    grub_bsd_add_meta (NETBSD_BTINFO_BOOTWEDGE, &biw, sizeof (biw));
  }

  /* Fill bootdisk.  */
  {
    struct grub_netbsd_btinfo_bootdisk bid;

    grub_memset (&bid, 0, sizeof (bid));
    /* Check for a NetBSD disk label.  */
    if (part->partmap != NULL &&
	(grub_strcmp (part->partmap->name, "netbsd") == 0 ||
	 (part->parent == NULL && grub_strcmp (part->partmap->name, "bsd") == 0)))
      {
	bid.labelsector = partmapsector;
	bid.label.type = buf.label.type;
	bid.label.checksum = buf.label.checksum;
	grub_memcpy (bid.label.packname, buf.label.packname, 16);
      }
    else
      {
	bid.labelsector = -1;
      }
    bid.biosdev = biosdev;
    bid.partition = part->number;

    grub_bsd_add_meta (NETBSD_BTINFO_BOOTDISK, &bid, sizeof (bid));
  }

fail:
  if (dev)
    grub_device_close (dev);
}

static grub_err_t
grub_netbsd_boot (void)
{
  struct grub_netbsd_bootinfo *bootinfo;
  void *curarg, *arg0;
  grub_addr_t arg_target, stack_target;
  grub_uint32_t *stack;
  grub_err_t err;
  struct grub_relocator32_state state;
  grub_size_t tag_buf_len = 0;
  int tag_count = 0;

  err = grub_bsd_add_mmap ();
  if (err)
    return err;

  err = grub_netbsd_setup_video ();
  if (err)
    {
      grub_print_error ();
      grub_puts_ (N_("Booting in blind mode"));
      grub_errno = GRUB_ERR_NONE;
    }

  err = grub_netbsd_add_modules ();
  if (err)
    return err;

#ifdef GRUB_MACHINE_EFI
  err = grub_bsd_add_meta (NETBSD_BTINFO_EFI,
			   &grub_efi_system_table,
			   sizeof (grub_efi_system_table));
  if (err)
    return err;
#endif

  {
    struct bsd_tag *tag;
    tag_buf_len = 0;
    for (tag = tags; tag; tag = tag->next)
      {
	tag_buf_len = ALIGN_VAR (tag_buf_len
				 + sizeof (struct grub_netbsd_btinfo_common)
				 + tag->len);
	tag_count++;
      }
  }

  arg_target = kern_end;
  {
    grub_relocator_chunk_t ch;
    err = grub_relocator_alloc_chunk_addr (relocator, &ch,
					   arg_target, tag_buf_len
					   + sizeof (struct grub_netbsd_bootinfo)
					   + tag_count * sizeof (grub_uint32_t));
    if (err)
      return err;
    curarg = get_virtual_current_address (ch);
  }

  arg0 = curarg;
  bootinfo = (void *) ((grub_uint8_t *) arg0 + tag_buf_len);

  {
    struct bsd_tag *tag;
    unsigned i;

    bootinfo->bi_count = tag_count;
    for (tag = tags, i = 0; tag; i++, tag = tag->next)
      {
	struct grub_netbsd_btinfo_common *head = curarg;
	bootinfo->bi_data[i] = ((grub_uint8_t *) curarg - (grub_uint8_t *) arg0)
	  + arg_target;
	head->type = tag->type;
	head->len = tag->len + sizeof (*head);
	curarg = head + 1;
	grub_memcpy (curarg, tag->data, tag->len);
	curarg = (grub_uint8_t *) curarg + tag->len;
      }
  }

  {
    grub_relocator_chunk_t ch;
    err = grub_relocator_alloc_chunk_align (relocator, &ch, 0x10000, 0x90000,
					    7 * sizeof (grub_uint32_t), 4,
					    GRUB_RELOCATOR_PREFERENCE_NONE,
					    0);
    if (err)
      return err;
    stack = get_virtual_current_address (ch);
    stack_target = get_physical_target_address (ch);
  }

#ifdef GRUB_MACHINE_EFI
  err = grub_efi_finish_boot_services (NULL, NULL, NULL, NULL, NULL);
  if (err)
    return err;
#endif

  state.eip = entry;
  state.esp = stack_target;
  state.ebp = stack_target;
  stack[0] = entry;
  stack[1] = bootflags;
  stack[2] = 0;
  stack[3] = ((grub_uint8_t *) bootinfo - (grub_uint8_t *) arg0) + arg_target;
  stack[4] = 0;
  stack[5] = grub_mmap_get_upper () >> 10;
  stack[6] = grub_mmap_get_lower () >> 10;

  return grub_relocator32_boot (relocator, state, 0);
}

static grub_err_t
grub_bsd_unload (void)
{
  struct bsd_tag *tag, *next;
  for (tag = tags; tag; tag = next)
    {
      next = tag->next;
      grub_free (tag);
    }
  tags = NULL;
  tags_last = NULL;

  kernel_type = KERNEL_TYPE_NONE;
  grub_dl_unref (my_mod);

  grub_relocator_unload (relocator);
  relocator = NULL;

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_bsd_load_aout (grub_file_t file, const char *filename)
{
  grub_addr_t load_addr, load_end;
  int ofs, align_page;
  union grub_aout_header ah;
  grub_err_t err;
  grub_size_t bss_size;

  if ((grub_file_seek (file, 0)) == (grub_off_t) - 1)
    return grub_errno;

  if (grub_file_read (file, &ah, sizeof (ah)) != sizeof (ah))
    {
      if (!grub_errno)
	grub_error (GRUB_ERR_READ_ERROR, N_("premature end of file %s"),
		    filename);
      return grub_errno;
    }

  if (grub_aout_get_type (&ah) != AOUT_TYPE_AOUT32)
    return grub_error (GRUB_ERR_BAD_OS, "invalid a.out header");

  entry = ah.aout32.a_entry & 0xFFFFFF;

  if (AOUT_GETMAGIC (ah.aout32) == AOUT32_ZMAGIC)
    {
      load_addr = entry;
      ofs = 0x1000;
      align_page = 0;
    }
  else
    {
      load_addr = entry & 0xF00000;
      ofs = sizeof (struct grub_aout32_header);
      align_page = 1;
    }

  if (load_addr < 0x100000)
    return grub_error (GRUB_ERR_BAD_OS, "load address below 1M");

  kern_start = load_addr;
  load_end = kern_end = load_addr + ah.aout32.a_text + ah.aout32.a_data;
  if (align_page)
    kern_end = ALIGN_PAGE (kern_end);

  if (ah.aout32.a_bss)
    {
      kern_end += ah.aout32.a_bss;
      if (align_page)
	kern_end = ALIGN_PAGE (kern_end);

      bss_size = kern_end - load_end;
    }
  else
    bss_size = 0;

  {
    grub_relocator_chunk_t ch;

    err = grub_relocator_alloc_chunk_addr (relocator, &ch,
					   kern_start, kern_end - kern_start);
    if (err)
      return err;
    kern_chunk_src = get_virtual_current_address (ch);
  }

  return grub_aout_load (file, ofs, kern_chunk_src,
			 ah.aout32.a_text + ah.aout32.a_data,
			 bss_size);
}

static grub_err_t
grub_bsd_load_elf (grub_elf_t elf, const char *filename)
{
  grub_err_t err;

  kern_end = 0;
  kern_start = ~0;

  if (grub_elf_is_elf32 (elf))
    {
      grub_relocator_chunk_t ch;
      Elf32_Phdr *phdr;

      entry = elf->ehdr.ehdr32.e_entry & 0xFFFFFFF;

      FOR_ELF32_PHDRS (elf, phdr)
	{
	  Elf32_Addr paddr;

	  if (phdr->p_type != PT_LOAD
	      && phdr->p_type != PT_DYNAMIC)
	    continue;

	  paddr = phdr->p_paddr & 0xFFFFFFF;

	  if (paddr < kern_start)
	    kern_start = paddr;

	  if (paddr + phdr->p_memsz > kern_end)
	    kern_end = paddr + phdr->p_memsz;
	}

      if (grub_errno)
	return grub_errno;
      err = grub_relocator_alloc_chunk_addr (relocator, &ch,
					     kern_start, kern_end - kern_start);
      if (err)
	return err;

      kern_chunk_src = get_virtual_current_address (ch);

      err = grub_elf32_load (elf, filename, (grub_uint8_t *) kern_chunk_src - kern_start, GRUB_ELF_LOAD_FLAGS_LOAD_PT_DYNAMIC | GRUB_ELF_LOAD_FLAGS_28BITS, 0, 0);
      if (err)
	return err;
      if (kernel_type != KERNEL_TYPE_OPENBSD)
	return GRUB_ERR_NONE;
      return grub_openbsd_find_ramdisk32 (elf->file, filename, kern_start,
					  kern_chunk_src, &openbsd_ramdisk);
    }
  else if (grub_elf_is_elf64 (elf))
    {
      Elf64_Phdr *phdr;

      is_64bit = 1;

      if (! grub_cpuid_has_longmode)
	return grub_error (GRUB_ERR_BAD_OS, "your CPU does not implement AMD64 architecture");

      /* FreeBSD has 64-bit entry point.  */
      if (kernel_type == KERNEL_TYPE_FREEBSD)
	{
	  entry = elf->ehdr.ehdr64.e_entry & 0xffffffff;
	  entry_hi = (elf->ehdr.ehdr64.e_entry >> 32) & 0xffffffff;
	}
      else
	{
	  entry = elf->ehdr.ehdr64.e_entry & 0x0fffffff;
	  entry_hi = 0;
	}

      FOR_ELF64_PHDRS (elf, phdr)
	{
	  Elf64_Addr paddr;

	  if (phdr->p_type != PT_LOAD
	      && phdr->p_type != PT_DYNAMIC)
	    continue;

	  paddr = phdr->p_paddr & 0xFFFFFFF;

	  if (paddr < kern_start)
	    kern_start = paddr;

	  if (paddr + phdr->p_memsz > kern_end)
	    kern_end = paddr + phdr->p_memsz;
	}

      if (grub_errno)
	return grub_errno;

      grub_dprintf ("bsd", "kern_start = %lx, kern_end = %lx\n",
		    (unsigned long) kern_start, (unsigned long) kern_end);
      {
	grub_relocator_chunk_t ch;

	err = grub_relocator_alloc_chunk_addr (relocator, &ch, kern_start,
					       kern_end - kern_start);
	if (err)
	  return err;
	kern_chunk_src = get_virtual_current_address (ch);
      }

      err = grub_elf64_load (elf, filename,
			     (grub_uint8_t *) kern_chunk_src - kern_start, GRUB_ELF_LOAD_FLAGS_LOAD_PT_DYNAMIC | GRUB_ELF_LOAD_FLAGS_28BITS, 0, 0);
      if (err)
	return err;
      if (kernel_type != KERNEL_TYPE_OPENBSD)
	return GRUB_ERR_NONE;
      return grub_openbsd_find_ramdisk64 (elf->file, filename, kern_start,
					  kern_chunk_src, &openbsd_ramdisk);
    }
  else
    return grub_error (GRUB_ERR_BAD_OS, N_("invalid arch-dependent ELF magic"));
}

static grub_err_t
grub_bsd_load (int argc, char *argv[])
{
  grub_file_t file;
  grub_elf_t elf;

  grub_dl_ref (my_mod);

  grub_loader_unset ();

  grub_memset (&openbsd_ramdisk, 0, sizeof (openbsd_ramdisk));

  if (argc == 0)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));
      goto fail;
    }

  file = grub_file_open (argv[0], GRUB_FILE_TYPE_BSD_KERNEL);
  if (!file)
    goto fail;

  relocator = grub_relocator_new ();
  if (!relocator)
    {
      grub_file_close (file);
      goto fail;
    }

  elf = grub_elf_file (file, argv[0]);
  if (elf)
    {
      is_elf_kernel = 1;
      grub_bsd_load_elf (elf, argv[0]);
      grub_elf_close (elf);
    }
  else
    {
      is_elf_kernel = 0;
      grub_errno = 0;
      grub_bsd_load_aout (file, argv[0]);
      grub_file_close (file);
    }

  kern_end = ALIGN_PAGE (kern_end);

fail:

  if (grub_errno != GRUB_ERR_NONE)
    grub_dl_unref (my_mod);	

  return grub_errno;
}

static grub_uint32_t
grub_bsd_parse_flags (const struct grub_arg_list *state,
		      const grub_uint32_t * flags)
{
  grub_uint32_t result = 0;
  unsigned i;

  for (i = 0; flags[i]; i++)
    if (state[i].set)
      result |= flags[i];

  return result;
}

static grub_err_t
grub_cmd_freebsd (grub_extcmd_context_t ctxt, int argc, char *argv[])
{
  kernel_type = KERNEL_TYPE_FREEBSD;
  bootflags = grub_bsd_parse_flags (ctxt->state, freebsd_flags);

  if (grub_bsd_load (argc, argv) == GRUB_ERR_NONE)
    {
      grub_uint32_t unit, slice, part;

      kern_end = ALIGN_PAGE (kern_end);
      if (is_elf_kernel)
	{
	  grub_err_t err;
	  grub_uint64_t data = 0;
	  grub_file_t file;
	  int len = is_64bit ? 8 : 4;

	  err = grub_freebsd_add_meta_module (argv[0], is_64bit
					      ? FREEBSD_MODTYPE_KERNEL64
					      : FREEBSD_MODTYPE_KERNEL,
					      argc - 1, argv + 1,
					      kern_start,
					      kern_end - kern_start);
	  if (err)
	    return err;

	  file = grub_file_open (argv[0], GRUB_FILE_TYPE_BSD_KERNEL);
	  if (! file)
	    return grub_errno;

	  if (is_64bit)
	    err = grub_freebsd_load_elf_meta64 (relocator, file, argv[0],
						&kern_end);
	  else
	    err = grub_freebsd_load_elf_meta32 (relocator, file, argv[0],
						&kern_end);
	  if (err)
	    return err;

	  err = grub_bsd_add_meta (FREEBSD_MODINFO_METADATA |
				   FREEBSD_MODINFOMD_HOWTO, &data, 4);
	  if (err)
	    return err;

	  err = grub_bsd_add_meta (FREEBSD_MODINFO_METADATA |
				       FREEBSD_MODINFOMD_ENVP, &data, len);
	  if (err)
	    return err;

	  err = grub_bsd_add_meta (FREEBSD_MODINFO_METADATA |
				   FREEBSD_MODINFOMD_KERNEND, &data, len);
	  if (err)
	    return err;
	}
      grub_bsd_get_device (&freebsd_biosdev, &unit, &slice, &part);
      freebsd_zfsguid = 0;
      if (!is_64bit)
	freebsd_get_zfs ();
      grub_print_error ();
      freebsd_bootdev = (FREEBSD_B_DEVMAGIC + ((slice + 1) << FREEBSD_B_SLICESHIFT) +
			 (unit << FREEBSD_B_UNITSHIFT) + (part << FREEBSD_B_PARTSHIFT));

      grub_loader_set (grub_freebsd_boot, grub_bsd_unload, 0);
    }

  return grub_errno;
}

static const char *types[] = {
  [0] = "wd",
  [2] = "fd",
  [4] = "sd",
  [6] = "cd",
  [14] = "vnd",
  [17] = "rd"
};

static grub_err_t
grub_cmd_openbsd (grub_extcmd_context_t ctxt, int argc, char *argv[])
{
  grub_uint32_t bootdev;

  kernel_type = KERNEL_TYPE_OPENBSD;
  bootflags = grub_bsd_parse_flags (ctxt->state, openbsd_flags);

  if (ctxt->state[OPENBSD_ROOT_ARG].set && ctxt->state[OPENBSD_ROOT_ARG].arg != NULL)
    {
      const char *arg = ctxt->state[OPENBSD_ROOT_ARG].arg;
      unsigned type, unit, part;
      for (type = 0; type < ARRAY_SIZE (types); type++)
	if (types[type]
	    && grub_strncmp (arg, types[type],
			     grub_strlen (types[type])) == 0)
	  {
	    arg += grub_strlen (types[type]);
	    break;
	  }
      if (type == ARRAY_SIZE (types))
	return grub_error (GRUB_ERR_BAD_ARGUMENT,
			   "unknown disk type name");

      unit = grub_strtoul (arg, &arg, 10);
      if (! (*arg >= 'a' && *arg <= 'z'))
	return grub_error (GRUB_ERR_BAD_ARGUMENT,
			   "only device specifications of form "
			   "<type><number><lowercase letter> are supported");

      part = *arg - 'a';

      bootdev = (OPENBSD_B_DEVMAGIC | (type << OPENBSD_B_TYPESHIFT)
		 | (unit << OPENBSD_B_UNITSHIFT)
		 | (part << OPENBSD_B_PARTSHIFT));
    }
  else
    bootdev = 0;

  if (ctxt->state[OPENBSD_SERIAL_ARG].set)
    {
      struct grub_openbsd_bootarg_console serial;
      const char *ptr;
      unsigned port = 0;
      unsigned speed = 9600;

      grub_memset (&serial, 0, sizeof (serial));

      if (ctxt->state[OPENBSD_SERIAL_ARG].arg)
	{
	  ptr = ctxt->state[OPENBSD_SERIAL_ARG].arg;
	  if (grub_memcmp (ptr, "com", sizeof ("com") - 1) != 0)
	    return grub_error (GRUB_ERR_BAD_ARGUMENT,
			       "only com0-com3 are supported");
	  ptr += sizeof ("com") - 1;
	  port = grub_strtoul (ptr, &ptr, 0);
	  if (port >= 4)
	    return grub_error (GRUB_ERR_BAD_ARGUMENT,
			       "only com0-com3 are supported");
	  if (*ptr == ',')
	    {
	      ptr++; 
	      speed = grub_strtoul (ptr, &ptr, 0);
	      if (grub_errno)
		return grub_errno;
	    }
	}

      serial.device = (GRUB_OPENBSD_COM_MAJOR << 8) | port;
      serial.speed = speed;
      serial.addr = grub_ns8250_hw_get_port (port);
	  
      grub_bsd_add_meta (OPENBSD_BOOTARG_CONSOLE, &serial, sizeof (serial));
      bootflags |= OPENBSD_RB_SERCONS;
    }
  else
    {
      struct grub_openbsd_bootarg_console serial;

      grub_memset (&serial, 0, sizeof (serial));
      serial.device = (GRUB_OPENBSD_VGA_MAJOR << 8);
      serial.addr = 0xffffffff;
      grub_bsd_add_meta (OPENBSD_BOOTARG_CONSOLE, &serial, sizeof (serial));
      bootflags &= ~OPENBSD_RB_SERCONS;
    }

  if (grub_bsd_load (argc, argv) == GRUB_ERR_NONE)
    {
      grub_loader_set (grub_openbsd_boot, grub_bsd_unload, 0);
      openbsd_root = bootdev;
    }

  return grub_errno;
}

static grub_err_t
grub_cmd_netbsd (grub_extcmd_context_t ctxt, int argc, char *argv[])
{
  grub_err_t err;
  kernel_type = KERNEL_TYPE_NETBSD;
  bootflags = grub_bsd_parse_flags (ctxt->state, netbsd_flags);

  if (grub_bsd_load (argc, argv) == GRUB_ERR_NONE)
    {
      if (is_elf_kernel)
	{
	  grub_file_t file;

	  file = grub_file_open (argv[0], GRUB_FILE_TYPE_BSD_KERNEL);
	  if (! file)
	    return grub_errno;

	  if (is_64bit)
	    err = grub_netbsd_load_elf_meta64 (relocator, file, argv[0], &kern_end);
	  else
	    err = grub_netbsd_load_elf_meta32 (relocator, file, argv[0], &kern_end);
	  if (err)
	    return err;
	}

      {
	char bootpath[GRUB_NETBSD_MAX_BOOTPATH_LEN];
	char *name;
	name = grub_strrchr (argv[0], '/');
	if (name)
	  name++;
	else
	  name = argv[0];
	grub_memset (bootpath, 0, sizeof (bootpath));
	grub_strncpy (bootpath, name, sizeof (bootpath) - 1);
	grub_bsd_add_meta (NETBSD_BTINFO_BOOTPATH, bootpath, sizeof (bootpath));
      }

      if (ctxt->state[NETBSD_ROOT_ARG].set)
	{
	  char root[GRUB_NETBSD_MAX_ROOTDEVICE_LEN];
	  grub_memset (root, 0, sizeof (root));
	  grub_strncpy (root, ctxt->state[NETBSD_ROOT_ARG].arg,
			sizeof (root) - 1);
	  grub_bsd_add_meta (NETBSD_BTINFO_ROOTDEVICE, root, sizeof (root));
	}
      if (ctxt->state[NETBSD_SERIAL_ARG].set)
	{
	  struct grub_netbsd_btinfo_serial serial;
	  const char *ptr;

	  grub_memset (&serial, 0, sizeof (serial));
	  grub_strcpy (serial.devname, "com");

	  serial.addr = grub_ns8250_hw_get_port (0);
	  serial.speed = 9600;

	  if (ctxt->state[NETBSD_SERIAL_ARG].arg)
	    {
	      ptr = ctxt->state[NETBSD_SERIAL_ARG].arg;
	      if (grub_memcmp (ptr, "com", sizeof ("com") - 1) == 0)
		{
		  ptr += sizeof ("com") - 1;
		  serial.addr 
		    = grub_ns8250_hw_get_port (grub_strtoul (ptr, &ptr, 0));
		}
	      else
		serial.addr = grub_strtoul (ptr, &ptr, 0);
	      if (grub_errno)
		return grub_errno;

	      if (*ptr == ',')
		{
		  ptr++;
		  serial.speed = grub_strtoul (ptr, &ptr, 0);
		  if (grub_errno)
		    return grub_errno;
		}
	    }

 	  grub_bsd_add_meta (NETBSD_BTINFO_CONSOLE, &serial, sizeof (serial));
	}
      else
	{
	  struct grub_netbsd_btinfo_serial cons;

	  grub_memset (&cons, 0, sizeof (cons));
	  grub_strcpy (cons.devname, "pc");

	  grub_bsd_add_meta (NETBSD_BTINFO_CONSOLE, &cons, sizeof (cons));
	}

      grub_netbsd_add_boot_disk_and_wedge ();

      grub_loader_set (grub_netbsd_boot, grub_bsd_unload, 0);
    }

  return grub_errno;
}

static grub_err_t
grub_cmd_freebsd_loadenv (grub_command_t cmd __attribute__ ((unused)),
			  int argc, char *argv[])
{
  grub_file_t file = 0;
  char *buf = 0, *curr, *next;
  int len;

  if (! grub_loader_is_loaded ())
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       N_("you need to load the kernel first"));

  if (kernel_type != KERNEL_TYPE_FREEBSD)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       "only FreeBSD supports environment");

  if (argc == 0)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));
      goto fail;
    }

  file = grub_file_open (argv[0], GRUB_FILE_TYPE_FREEBSD_ENV);
  if ((!file) || (!file->size))
    goto fail;

  len = file->size;
  buf = grub_malloc (len + 1);
  if (!buf)
    goto fail;

  if (grub_file_read (file, buf, len) != len)
    goto fail;

  buf[len] = 0;

  next = buf;
  while (next)
    {
      char *p;

      curr = next;
      next = grub_strchr (curr, '\n');
      if (next)
	{

	  p = next - 1;
	  while (p > curr)
	    {
	      if ((*p != '\r') && (*p != ' ') && (*p != '\t'))
		break;
	      p--;
	    }

	  if ((p > curr) && (*p == '"'))
	    p--;

	  *(p + 1) = 0;
	  next++;
	}

      if (*curr == '#')
	continue;

      p = grub_strchr (curr, '=');
      if (!p)
	continue;

      *(p++) = 0;

      if (*curr)
	{
	  char *name;

	  if (*p == '"')
	    p++;

	  name = grub_xasprintf ("kFreeBSD.%s", curr);
	  if (!name)
	    goto fail;
	  if (grub_env_set (name, p))
	    {
	      grub_free (name);
	      goto fail;
	    }
	  grub_free (name);
	}
    }

fail:
  grub_free (buf);

  if (file)
    grub_file_close (file);

  return grub_errno;
}

static grub_err_t
grub_cmd_freebsd_module (grub_command_t cmd __attribute__ ((unused)),
			 int argc, char *argv[])
{
  grub_file_t file = 0;
  int modargc;
  char **modargv;
  const char *type;
  grub_err_t err;
  void *src;

  if (! grub_loader_is_loaded ())
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       N_("you need to load the kernel first"));

  if (kernel_type != KERNEL_TYPE_FREEBSD)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "no FreeBSD loaded");

  if (!is_elf_kernel)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       "only ELF kernel supports module");

  /* List the current modules if no parameter.  */
  if (!argc)
    {
      grub_freebsd_list_modules ();
      return 0;
    }

  file = grub_file_open (argv[0], GRUB_FILE_TYPE_FREEBSD_MODULE);
  if ((!file) || (!file->size))
    goto fail;

  {
    grub_relocator_chunk_t ch;
    err = grub_relocator_alloc_chunk_addr (relocator, &ch, kern_end, 
					   file->size);
    if (err)
      goto fail;
    src = get_virtual_current_address (ch);
  }


  grub_file_read (file, src, file->size);
  if (grub_errno)
    goto fail;

  modargc = argc - 1;
  modargv = argv + 1;

  if (modargc && (! grub_memcmp (modargv[0], "type=", 5)))
    {
      type = &modargv[0][5];
      modargc--;
      modargv++;
    }
  else
    type = FREEBSD_MODTYPE_RAW;

  err = grub_freebsd_add_meta_module (argv[0], type, modargc, modargv,
				      kern_end, file->size);
  if (err)
    goto fail;

  kern_end = ALIGN_PAGE (kern_end + file->size);

fail:
  if (file)
    grub_file_close (file);

  return grub_errno;
}

static grub_err_t
grub_netbsd_module_load (char *filename, grub_uint32_t type)
{
  grub_file_t file = 0;
  void *src;
  grub_err_t err;

  file = grub_file_open (filename, GRUB_FILE_TYPE_NETBSD_MODULE);
  if ((!file) || (!file->size))
    goto fail;

  {
    grub_relocator_chunk_t ch;
    err = grub_relocator_alloc_chunk_addr (relocator, &ch, kern_end, 
					   file->size);
    if (err)
      goto fail;

    src = get_virtual_current_address (ch);
  }

  grub_file_read (file, src, file->size);
  if (grub_errno)
    goto fail;

  err = grub_netbsd_add_meta_module (filename, type, kern_end, file->size);

  if (err)
    goto fail;

  kern_end = ALIGN_PAGE (kern_end + file->size);

fail:
  if (file)
    grub_file_close (file);

  return grub_errno;
}

static grub_err_t
grub_cmd_netbsd_module (grub_command_t cmd,
			int argc, char *argv[])
{
  grub_uint32_t type;

  if (! grub_loader_is_loaded ())
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       N_("you need to load the kernel first"));

  if (kernel_type != KERNEL_TYPE_NETBSD)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "no NetBSD loaded");

  if (!is_elf_kernel)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       "only ELF kernel supports module");

  /* List the current modules if no parameter.  */
  if (!argc)
    {
      grub_netbsd_list_modules ();
      return 0;
    }

  if (grub_strcmp (cmd->name, "knetbsd_module_elf") == 0)
    type = GRUB_NETBSD_MODULE_ELF;
  else
    type = GRUB_NETBSD_MODULE_RAW;

  return grub_netbsd_module_load (argv[0], type);
}

static grub_err_t
grub_cmd_freebsd_module_elf (grub_command_t cmd __attribute__ ((unused)),
			     int argc, char *argv[])
{
  grub_file_t file = 0;
  grub_err_t err;

  if (! grub_loader_is_loaded ())
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       N_("you need to load the kernel first"));

  if (kernel_type != KERNEL_TYPE_FREEBSD)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       "only FreeBSD supports module");

  if (! is_elf_kernel)
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       "only ELF kernel supports module");

  /* List the current modules if no parameter.  */
  if (! argc)
    {
      grub_freebsd_list_modules ();
      return 0;
    }

  file = grub_file_open (argv[0], GRUB_FILE_TYPE_FREEBSD_MODULE_ELF);
  if (!file)
    return grub_errno;
  if (!file->size)
    {
      grub_file_close (file);
      return grub_errno;
    }

  if (is_64bit)
    err = grub_freebsd_load_elfmodule_obj64 (relocator, file,
					     argc, argv, &kern_end);
  else
    err = grub_freebsd_load_elfmodule32 (relocator, file,
					 argc, argv, &kern_end);
  grub_file_close (file);

  return err;
}

static grub_err_t
grub_cmd_openbsd_ramdisk (grub_command_t cmd __attribute__ ((unused)),
		      int argc, char *args[])
{
  grub_file_t file;
  grub_size_t size;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  if (! grub_loader_is_loaded ())
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       N_("you need to load the kernel first"));

  if (kernel_type != KERNEL_TYPE_OPENBSD)
    return grub_error (GRUB_ERR_BAD_OS, "no kOpenBSD loaded");

  if (!openbsd_ramdisk.max_size)
    return grub_error (GRUB_ERR_BAD_OS, "your kOpenBSD doesn't support ramdisk");

  file = grub_file_open (args[0], GRUB_FILE_TYPE_OPENBSD_RAMDISK);
  if (! file)
    return grub_errno;

  size = grub_file_size (file);

  if (size > openbsd_ramdisk.max_size)
    {
      grub_file_close (file);
      return grub_error (GRUB_ERR_BAD_OS, "your kOpenBSD supports ramdisk only"
			 " up to %" PRIuGRUB_SIZE " bytes, however you supplied"
			 "a %" PRIuGRUB_SIZE " bytes one",
			 openbsd_ramdisk.max_size, size);
    }

  if (grub_file_read (file, openbsd_ramdisk.target, size)
      != (grub_ssize_t) (size))
    {
      grub_file_close (file);
      if (!grub_errno)
	grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"), args[0]);
      return grub_errno;
    }
  grub_memset (openbsd_ramdisk.target + size, 0,
	       openbsd_ramdisk.max_size - size);
  *openbsd_ramdisk.size = ALIGN_UP (size, 512);

  return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd_freebsd, cmd_openbsd, cmd_netbsd;
static grub_command_t cmd_freebsd_loadenv, cmd_freebsd_module;
static grub_command_t cmd_netbsd_module, cmd_freebsd_module_elf;
static grub_command_t cmd_netbsd_module_elf, cmd_openbsd_ramdisk;

GRUB_MOD_INIT (bsd)
{
  /* Net and OpenBSD kernels are often compressed.  */
  grub_dl_load ("gzio");

  cmd_freebsd = grub_register_extcmd ("kfreebsd", grub_cmd_freebsd, 0,
				      N_("FILE"), N_("Load kernel of FreeBSD."),
				      freebsd_opts);
  cmd_openbsd = grub_register_extcmd ("kopenbsd", grub_cmd_openbsd, 0,
				      N_("FILE"), N_("Load kernel of OpenBSD."),
				      openbsd_opts);
  cmd_netbsd = grub_register_extcmd ("knetbsd", grub_cmd_netbsd,  0,
				     N_("FILE"), N_("Load kernel of NetBSD."),
				     netbsd_opts);
  cmd_freebsd_loadenv =
    grub_register_command ("kfreebsd_loadenv", grub_cmd_freebsd_loadenv,
			   0, N_("Load FreeBSD env."));
  cmd_freebsd_module =
    grub_register_command ("kfreebsd_module", grub_cmd_freebsd_module,
			   0, N_("Load FreeBSD kernel module."));
  cmd_netbsd_module =
    grub_register_command ("knetbsd_module", grub_cmd_netbsd_module,
			   0, N_("Load NetBSD kernel module."));
  cmd_netbsd_module_elf =
    grub_register_command ("knetbsd_module_elf", grub_cmd_netbsd_module,
			   0, N_("Load NetBSD kernel module (ELF)."));
  cmd_freebsd_module_elf =
    grub_register_command ("kfreebsd_module_elf", grub_cmd_freebsd_module_elf,
			   0, N_("Load FreeBSD kernel module (ELF)."));

  cmd_openbsd_ramdisk = grub_register_command ("kopenbsd_ramdisk",
					       grub_cmd_openbsd_ramdisk, 0,
					       /* TRANSLATORS: ramdisk isn't identifier,
						it can be translated.  */
					       N_("Load kOpenBSD ramdisk."));

  my_mod = mod;
}

GRUB_MOD_FINI (bsd)
{
  grub_unregister_extcmd (cmd_freebsd);
  grub_unregister_extcmd (cmd_openbsd);
  grub_unregister_extcmd (cmd_netbsd);

  grub_unregister_command (cmd_freebsd_loadenv);
  grub_unregister_command (cmd_freebsd_module);
  grub_unregister_command (cmd_netbsd_module);
  grub_unregister_command (cmd_freebsd_module_elf);
  grub_unregister_command (cmd_netbsd_module_elf);
  grub_unregister_command (cmd_openbsd_ramdisk);

  grub_bsd_unload ();
}
