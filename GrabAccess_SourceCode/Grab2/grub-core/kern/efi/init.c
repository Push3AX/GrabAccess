/* init.c - generic EFI initialization and finalization */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007  Free Software Foundation, Inc.
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

#include <grub/efi/efi.h>
#include <grub/efi/console.h>
#include <grub/efi/disk.h>
#include <grub/term.h>
#include <grub/misc.h>
#include <grub/env.h>
#include <grub/mm.h>
#include <grub/kernel.h>
#include <grub/stack_protector.h>

#ifdef GRUB_STACK_PROTECTOR

static grub_efi_guid_t rng_protocol_guid = GRUB_EFI_RNG_PROTOCOL_GUID;

/*
 * Don't put this on grub_efi_init()'s local stack to avoid it
 * getting a stack check.
 */
static grub_efi_uint8_t stack_chk_guard_buf[32];

grub_addr_t __stack_chk_guard;

void __attribute__ ((noreturn))
__stack_chk_fail (void)
{
  /*
   * Assume it's not safe to call into EFI Boot Services. Sorry, that
   * means no console message here.
   */
  do
    {
      /* Do not optimize out the loop. */
      asm volatile ("");
    }
  while (1);
}

static void
stack_protector_init (void)
{
  grub_efi_rng_protocol_t *rng;

  /* Set up the stack canary. Make errors here non-fatal for now. */
  rng = grub_efi_locate_protocol (&rng_protocol_guid, NULL);
  if (rng != NULL)
    {
      grub_efi_status_t status;

      status = efi_call_4 (rng->get_rng, rng, NULL, sizeof (stack_chk_guard_buf),
			   stack_chk_guard_buf);
      if (status == GRUB_EFI_SUCCESS)
	grub_memcpy (&__stack_chk_guard, stack_chk_guard_buf, sizeof (__stack_chk_guard));
    }
}
#else
static void
stack_protector_init (void)
{
}
#endif

grub_addr_t grub_modbase;

void
grub_efi_init (void)
{
  grub_modbase = grub_efi_modules_addr ();
  /* First of all, initialize the console so that GRUB can display
     messages.  */
  grub_console_init ();

  stack_protector_init ();

  /* Initialize the memory management system.  */
  grub_efi_mm_init ();

  efi_call_4 (grub_efi_system_table->boot_services->set_watchdog_timer,
              0, 0, 0, NULL);

  grub_efidisk_init ();
}

void (*grub_efi_net_config) (grub_efi_handle_t hnd, char **device, char **path);

void
grub_machine_get_bootlocation (char **device, char **path)
{
  grub_efi_loaded_image_t *image = NULL;
  char *p;

  image = grub_efi_get_loaded_image (grub_efi_image_handle);
  if (!image)
    return;
  *device = grub_efidisk_get_device_name (image->device_handle);
  if (!*device && grub_efi_net_config)
    {
      grub_efi_net_config (image->device_handle, device, path);
      return;
    }

  *path = grub_efi_get_filename (image->file_path);
  if (*path)
    {
      /* Get the directory.  */
      p = grub_strrchr (*path, '/');
      if (p)
        *p = '\0';
    }
}

void
grub_efi_fini (void)
{
  grub_efidisk_fini ();
  grub_console_fini ();
}
