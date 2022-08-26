/* multiboot.c - boot a multiboot OS image. */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2004,2005,2007,2008,2009,2010  Free Software Foundation, Inc.
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

/*
 *  FIXME: The following features from the Multiboot specification still
 *         need to be implemented:
 *  - drives table
 *  - ROM configuration table
 *  - Networking information
 */

#include <grub/loader.h>
#include <grub/command.h>
#include <grub/multiboot.h>
#include <grub/multiboot2.h>
#ifdef GRUB_USE_MULTIBOOT2
#define GRUB_MBDEF(x) GRUB_MULTIBOOT2_ ## x
#define MBDEF(x) MULTIBOOT2_ ## x
#define GRUB_MULTIBOOT(x) grub_multiboot2_ ## x
#else
#define GRUB_MBDEF(x) GRUB_MULTIBOOT_ ## x
#define MBDEF(x) MULTIBOOT_ ## x
#define GRUB_MULTIBOOT(x) grub_multiboot_ ## x
#endif
#include <grub/cpu/multiboot.h>
#include <grub/elf.h>
#include <grub/aout.h>
#include <grub/file.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/env.h>
#include <grub/cpu/relocator.h>
#include <grub/video.h>
#include <grub/memory.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

#ifdef GRUB_MACHINE_EFI
#include <grub/efi/efi.h>
#endif

struct grub_relocator *GRUB_MULTIBOOT (relocator) = NULL;
grub_uint32_t GRUB_MULTIBOOT (payload_eip);
#if defined (GRUB_MACHINE_PCBIOS) || defined (GRUB_MACHINE_MULTIBOOT) || defined (GRUB_MACHINE_COREBOOT) || defined (GRUB_MACHINE_QEMU)
#define DEFAULT_VIDEO_MODE "text"
#else
#define DEFAULT_VIDEO_MODE "auto"
#endif

static int accepts_video;
static int accepts_ega_text;
static int console_required;
static grub_dl_t my_mod;


/* Helper for grub_get_multiboot_mmap_count.  */
static int
count_hook (grub_uint64_t addr __attribute__ ((unused)),
        grub_uint64_t size __attribute__ ((unused)),
        grub_memory_type_t type __attribute__ ((unused)), void *data)
{
  grub_size_t *count = data;

  (*count)++;
  return 0;
}

/* Return the length of the Multiboot mmap that will be needed to allocate
   our platform's map.  */
grub_uint32_t
GRUB_MULTIBOOT (get_mmap_count) (void)
{
  grub_size_t count = 0;

  grub_mmap_iterate (count_hook, &count);

  return count;
}

grub_err_t
GRUB_MULTIBOOT (set_video_mode) (void)
{
  grub_err_t err;
  const char *modevar;

#if GRUB_MACHINE_HAS_VGA_TEXT
  if (accepts_video)
#endif
    {
      modevar = grub_env_get ("gfxpayload");
      if (! modevar || *modevar == 0)
    err = grub_video_set_mode (DEFAULT_VIDEO_MODE, 0, 0);
      else
    {
      char *tmp;
      tmp = grub_xasprintf ("%s;" DEFAULT_VIDEO_MODE, modevar);
      if (! tmp)
        return grub_errno;
      err = grub_video_set_mode (tmp, 0, 0);
      grub_free (tmp);
    }
    }
#if GRUB_MACHINE_HAS_VGA_TEXT
  else
    err = grub_video_set_mode ("text", 0, 0);
#endif

  return err;
}

#ifdef GRUB_MACHINE_EFI
#ifdef __x86_64__
#define grub_relocator_efi_boot        grub_relocator64_efi_boot
#define grub_relocator_efi_state    grub_relocator64_efi_state
#endif
#endif

#ifdef grub_relocator_efi_boot
static void
efi_boot (struct grub_relocator *rel,
      grub_uint32_t target)
{
  struct grub_relocator_efi_state state_efi = MBDEF(EFI_INITIAL_STATE);
  state_efi.MULTIBOOT_EFI_ENTRY_REGISTER = GRUB_MULTIBOOT (payload_eip);
  state_efi.MULTIBOOT_EFI_MBI_REGISTER = target;
  grub_relocator_efi_boot (rel, state_efi);
}
#else
#define grub_efi_is_finished    1
static void
efi_boot (struct grub_relocator *rel __attribute__ ((unused)),
      grub_uint32_t target __attribute__ ((unused)))
{
}
#endif

#if defined (__i386__) || defined (__x86_64__)
static void
normal_boot (struct grub_relocator *rel, struct grub_relocator32_state state)
{
  grub_relocator32_boot (rel, state, 0);
}
#else
static void
normal_boot (struct grub_relocator *rel, struct grub_relocator32_state state)
{
  grub_relocator32_boot (rel, state);
}
#endif

static grub_err_t
grub_multiboot_boot (void)
{
  grub_err_t err;
  struct grub_relocator32_state state = MBDEF(INITIAL_STATE);
  state.MULTIBOOT_ENTRY_REGISTER = GRUB_MULTIBOOT (payload_eip);

  err = GRUB_MULTIBOOT (make_mbi) (&state.MULTIBOOT_MBI_REGISTER);

  if (err)
    return err;

#if defined (GRUB_MACHINE_EFI) && ! defined (GRUB_USE_MULTIBOOT2)
  if (grub_multiboot_no_exit || grub_efi_is_finished)
#else
  if (grub_efi_is_finished)
#endif
    normal_boot (GRUB_MULTIBOOT (relocator), state);
  else
    efi_boot (GRUB_MULTIBOOT (relocator), state.MULTIBOOT_MBI_REGISTER);

  /* Not reached.  */
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_multiboot_unload (void)
{
  GRUB_MULTIBOOT (free_mbi) ();

  grub_relocator_unload (GRUB_MULTIBOOT (relocator));
  GRUB_MULTIBOOT (relocator) = NULL;

  grub_dl_unref (my_mod);

  return GRUB_ERR_NONE;
}

static grub_uint64_t highest_load;

#define MULTIBOOT_LOAD_ELF64
#include "multiboot_elfxx.c"
#undef MULTIBOOT_LOAD_ELF64

#define MULTIBOOT_LOAD_ELF32
#include "multiboot_elfxx.c"
#undef MULTIBOOT_LOAD_ELF32

/* Load ELF32 or ELF64.  */
grub_err_t
GRUB_MULTIBOOT (load_elf) (mbi_load_data_t *mld)
{
  if (grub_multiboot_is_elf32 (mld->buffer))
    return grub_multiboot_load_elf32 (mld);
  else if (grub_multiboot_is_elf64 (mld->buffer))
    return grub_multiboot_load_elf64 (mld);

  return grub_error (GRUB_ERR_UNKNOWN_OS, N_("invalid arch-dependent ELF magic"));
}

grub_err_t
GRUB_MULTIBOOT (set_console) (int console_type, int accepted_consoles,
                  int width, int height, int depth,
                  int console_req)
{
  console_required = console_req;
  if (!(accepted_consoles 
    & (GRUB_MBDEF(CONSOLE_FRAMEBUFFER)
       | (GRUB_MACHINE_HAS_VGA_TEXT ? GRUB_MBDEF(CONSOLE_EGA_TEXT) : 0))))
    {
      if (console_required)
    return grub_error (GRUB_ERR_BAD_OS,
               "OS requires a console but none is available");
      grub_puts_ (N_("WARNING: no console will be available to OS"));
      accepts_video = 0;
      accepts_ega_text = 0;
      return GRUB_ERR_NONE;
    }

  if (console_type == GRUB_MBDEF(CONSOLE_FRAMEBUFFER))
    {
      char *buf;
      if (depth && width && height)
    buf = grub_xasprintf ("%dx%dx%d,%dx%d,auto", width,
                  height, depth, width, height);
      else if (width && height)
    buf = grub_xasprintf ("%dx%d,auto", width, height);
      else
    buf = grub_strdup ("auto");

      if (!buf)
    return grub_errno;
      grub_env_set ("gfxpayload", buf);
      grub_free (buf);
    }
  else
    {
#if GRUB_MACHINE_HAS_VGA_TEXT
      grub_env_set ("gfxpayload", "text");
#else
      /* Always use video if no VGA text is available.  */
      grub_env_set ("gfxpayload", "auto");
#endif
    }

  accepts_video = !!(accepted_consoles & GRUB_MBDEF(CONSOLE_FRAMEBUFFER));
  accepts_ega_text = !!(accepted_consoles & GRUB_MBDEF(CONSOLE_EGA_TEXT));
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_multiboot (grub_command_t cmd __attribute__ ((unused)),
            int argc, char *argv[])
{
  grub_file_t file = 0;
  grub_err_t err;

  grub_loader_unset ();

  highest_load = 0;

#ifndef GRUB_USE_MULTIBOOT2
  grub_multiboot_quirks = GRUB_MBDEF(QUIRKS_NONE);
  int option_found = 0;

  do
  {
    option_found = 0;
    if (argc != 0 && grub_strcmp (argv[0], "--quirk-bad-kludge") == 0)
    {
      argc--;
      argv++;
      option_found = 1;
      grub_multiboot_quirks |= GRUB_MBDEF(QUIRK_BAD_KLUDGE);
    }

    if (argc != 0 && grub_strcmp (argv[0], "--quirk-modules-after-kernel") == 0)
    {
      argc--;
      argv++;
      option_found = 1;
      grub_multiboot_quirks |= GRUB_MBDEF(QUIRK_MODULES_AFTER_KERNEL);
    }
#ifdef GRUB_MACHINE_EFI
#if defined (__i386__) || defined (__x86_64__)
    if (argc != 0 && grub_strcmp (argv[0], "--fake-bios") == 0)
    {
      argc--;
      argv++;
      option_found = 1;
      grub_efi_unlock_rom_area ();
      grub_efi_fake_bios_data (1);
      grub_efi_lock_rom_area ();
    }
#endif
    if (argc != 0 && grub_strcmp (argv[0], "--no-exit") == 0)
    {
      argc--;
      argv++;
      option_found = 1;
      grub_multiboot_no_exit = 1;
    }
#endif
  } while (option_found);

#endif

  if (argc == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  file = grub_file_open (argv[0], GRUB_FILE_TYPE_MULTIBOOT_KERNEL);
  if (! file)
    return grub_errno;

  grub_dl_ref (my_mod);

  /* Skip filename.  */
  GRUB_MULTIBOOT (init_mbi) (argc - 1, argv + 1);

  grub_relocator_unload (GRUB_MULTIBOOT (relocator));
  GRUB_MULTIBOOT (relocator) = grub_relocator_new ();

  if (!GRUB_MULTIBOOT (relocator))
    goto fail;

  err = GRUB_MULTIBOOT (load) (file, argv[0]);
  if (err)
    goto fail;

  GRUB_MULTIBOOT (set_bootdev) ();

  grub_loader_set (grub_multiboot_boot, grub_multiboot_unload, 0);

 fail:
  if (file)
    grub_file_close (file);

  if (grub_errno != GRUB_ERR_NONE)
    {
      grub_relocator_unload (GRUB_MULTIBOOT (relocator));
      GRUB_MULTIBOOT (relocator) = NULL;
      grub_dl_unref (my_mod);
    }

  return grub_errno;
}

static grub_err_t
grub_cmd_module (grub_command_t cmd __attribute__ ((unused)),
         int argc, char *argv[])
{
  grub_file_t file = 0;
  grub_ssize_t size;
  void *module = NULL;
  grub_addr_t target;
  grub_err_t err;
  int nounzip = 0;
  grub_uint64_t lowest_addr = 0;

  if (argc == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  if (grub_strcmp (argv[0], "--nounzip") == 0)
    {
      argv++;
      argc--;
      nounzip = 1;
    }

  if (argc == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  if (!GRUB_MULTIBOOT (relocator))
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
               N_("you need to load the kernel first"));

  file = grub_file_open (argv[0], GRUB_FILE_TYPE_MULTIBOOT_MODULE
             | (nounzip ? GRUB_FILE_TYPE_NO_DECOMPRESS : GRUB_FILE_TYPE_NONE));
  if (! file)
    return grub_errno;

#ifndef GRUB_USE_MULTIBOOT2
  lowest_addr = 0x100000;
  if (grub_multiboot_quirks & GRUB_MBDEF(QUIRK_MODULES_AFTER_KERNEL))
    lowest_addr = ALIGN_UP (highest_load + 1048576, 4096);
#endif

  size = grub_file_size (file);
  if (size)
  {
    grub_relocator_chunk_t ch;
    err = grub_relocator_alloc_chunk_align (GRUB_MULTIBOOT (relocator), &ch,
                        lowest_addr, UP_TO_TOP32 (size),
                        size, MBDEF(MOD_ALIGN),
                        GRUB_RELOCATOR_PREFERENCE_NONE, 1);
    if (err)
      {
    grub_file_close (file);
    return err;
      }
    module = get_virtual_current_address (ch);
    target = get_physical_target_address (ch);
  }
  else
    {
      module = 0;
      target = 0;
    }

  err = GRUB_MULTIBOOT (add_module) (target, size, argc - 1, argv + 1);
  if (err)
    {
      grub_file_close (file);
      return err;
    }

  if (size && grub_file_read (file, module, size) != size)
    {
      grub_file_close (file);
      if (!grub_errno)
    grub_error (GRUB_ERR_FILE_READ_ERROR, N_("premature end of file %s"),
            argv[0]);
      return grub_errno;
    }

  grub_file_close (file);
  return GRUB_ERR_NONE;
}

static grub_command_t cmd_multiboot, cmd_module;

GRUB_MOD_INIT(multiboot)
{
  cmd_multiboot =
#ifdef GRUB_USE_MULTIBOOT2
    grub_register_command ("multiboot2", grub_cmd_multiboot,
               0, N_("Load a multiboot 2 kernel."));
  cmd_module =
    grub_register_command ("module2", grub_cmd_module,
               0, N_("Load a multiboot 2 module."));
#else
    grub_register_command ("multiboot", grub_cmd_multiboot,
               0, N_("Load a multiboot kernel."));
  cmd_module =
    grub_register_command ("module", grub_cmd_module,
               0, N_("Load a multiboot module."));
#endif

  my_mod = mod;
}

GRUB_MOD_FINI(multiboot)
{
  grub_unregister_command (cmd_multiboot);
  grub_unregister_command (cmd_module);
}
