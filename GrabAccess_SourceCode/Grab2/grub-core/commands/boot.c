/* boot.c - command to boot an operating system */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2003,2004,2005,2007,2009  Free Software Foundation, Inc.
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

#include <grub/normal.h>
#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/loader.h>
#include <grub/kernel.h>
#include <grub/mm.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_err_t (*grub_loader_boot_func) (void);
static grub_err_t (*grub_loader_unload_func) (void);
static int grub_loader_flags;

struct grub_preboot
{
  grub_err_t (*preboot_func) (int);
  grub_err_t (*preboot_rest_func) (void);
  grub_loader_preboot_hook_prio_t prio;
  struct grub_preboot *next;
  struct grub_preboot *prev;
};

static int grub_loader_loaded;
static struct grub_preboot *preboots_head = 0,
  *preboots_tail = 0;

int
grub_loader_is_loaded (void)
{
  return grub_loader_loaded;
}

/* Register a preboot hook. */
struct grub_preboot *
grub_loader_register_preboot_hook (grub_err_t (*preboot_func) (int flags),
				   grub_err_t (*preboot_rest_func) (void),
				   grub_loader_preboot_hook_prio_t prio)
{
  struct grub_preboot *cur, *new_preboot;

  if (! preboot_func && ! preboot_rest_func)
    return 0;

  new_preboot = (struct grub_preboot *)
    grub_malloc (sizeof (struct grub_preboot));
  if (! new_preboot)
    return 0;

  new_preboot->preboot_func = preboot_func;
  new_preboot->preboot_rest_func = preboot_rest_func;
  new_preboot->prio = prio;

  for (cur = preboots_head; cur && cur->prio > prio; cur = cur->next);

  if (cur)
    {
      new_preboot->next = cur;
      new_preboot->prev = cur->prev;
      cur->prev = new_preboot;
    }
  else
    {
      new_preboot->next = 0;
      new_preboot->prev = preboots_tail;
      preboots_tail = new_preboot;
    }
  if (new_preboot->prev)
    new_preboot->prev->next = new_preboot;
  else
    preboots_head = new_preboot;

  return new_preboot;
}

void
grub_loader_unregister_preboot_hook (struct grub_preboot *hnd)
{
  struct grub_preboot *preb = hnd;

  if (preb->next)
    preb->next->prev = preb->prev;
  else
    preboots_tail = preb->prev;
  if (preb->prev)
    preb->prev->next = preb->next;
  else
    preboots_head = preb->next;

  grub_free (preb);
}

void
grub_loader_set (grub_err_t (*boot) (void),
		 grub_err_t (*unload) (void),
		 int flags)
{
  if (grub_loader_loaded && grub_loader_unload_func)
    grub_loader_unload_func ();

  grub_loader_boot_func = boot;
  grub_loader_unload_func = unload;
  grub_loader_flags = flags;

  grub_loader_loaded = 1;
}

void
grub_loader_unset(void)
{
  if (grub_loader_loaded && grub_loader_unload_func)
    grub_loader_unload_func ();

  grub_loader_boot_func = 0;
  grub_loader_unload_func = 0;

  grub_loader_loaded = 0;
}

grub_err_t
grub_loader_boot (void)
{
  grub_err_t err = GRUB_ERR_NONE;
  struct grub_preboot *cur;

  if (! grub_loader_loaded)
    return grub_error (GRUB_ERR_NO_KERNEL,
		       N_("you need to load the kernel first"));

  grub_machine_fini (grub_loader_flags);

  for (cur = preboots_head; cur; cur = cur->next)
    {
      err = cur->preboot_func (grub_loader_flags);
      if (err)
	{
	  for (cur = cur->prev; cur; cur = cur->prev)
	    cur->preboot_rest_func ();
	  return err;
	}
    }
  err = (grub_loader_boot_func) ();

  for (cur = preboots_tail; cur; cur = cur->prev)
    if (! err)
      err = cur->preboot_rest_func ();
    else
      cur->preboot_rest_func ();

  return err;
}

/* boot */
static grub_err_t
grub_cmd_boot (struct grub_command *cmd __attribute__ ((unused)),
		    int argc __attribute__ ((unused)),
		    char *argv[] __attribute__ ((unused)))
{
  return grub_loader_boot ();
}



static grub_command_t cmd_boot;

GRUB_MOD_INIT(boot)
{
  cmd_boot =
    grub_register_command ("boot", grub_cmd_boot,
			   0, N_("Boot an operating system."));
}

GRUB_MOD_FINI(boot)
{
  grub_unregister_command (cmd_boot);
}
