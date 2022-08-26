/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013  Free Software Foundation, Inc.
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

#include <grub/time.h>
#include <grub/misc.h>
#include <grub/dl.h>
#include <grub/command.h>
#include <grub/env.h>
#include <grub/test.h>
#include <grub/mm.h>
#include <grub/procfs.h>

#include "signatures.h"

GRUB_MOD_LICENSE ("GPLv3+");

static char *
get_hi_dsa_sig (grub_size_t *sz)
{
  char *ret;
  *sz = sizeof (hi_dsa_sig);
  ret = grub_malloc (sizeof (hi_dsa_sig));
  if (ret)
    grub_memcpy (ret, hi_dsa_sig, sizeof (hi_dsa_sig));
  return ret;
}

static struct grub_procfs_entry hi_dsa_sig_entry = 
{
  .name = "hi_dsa.sig",
  .get_contents = get_hi_dsa_sig
};

static char *
get_hi_dsa_pub (grub_size_t *sz)
{
  char *ret;
  *sz = sizeof (hi_dsa_pub);
  ret = grub_malloc (sizeof (hi_dsa_pub));
  if (ret)
    grub_memcpy (ret, hi_dsa_pub, sizeof (hi_dsa_pub));
  return ret;
}

static struct grub_procfs_entry hi_dsa_pub_entry = 
{
  .name = "hi_dsa.pub",
  .get_contents = get_hi_dsa_pub
};

static char *
get_hi_rsa_sig (grub_size_t *sz)
{
  char *ret;
  *sz = sizeof (hi_rsa_sig);
  ret = grub_malloc (sizeof (hi_rsa_sig));
  if (ret)
    grub_memcpy (ret, hi_rsa_sig, sizeof (hi_rsa_sig));
  return ret;
}

static struct grub_procfs_entry hi_rsa_sig_entry = 
{
  .name = "hi_rsa.sig",
  .get_contents = get_hi_rsa_sig
};

static char *
get_hi_rsa_pub (grub_size_t *sz)
{
  char *ret;
  *sz = sizeof (hi_rsa_pub);
  ret = grub_malloc (sizeof (hi_rsa_pub));
  if (ret)
    grub_memcpy (ret, hi_rsa_pub, sizeof (hi_rsa_pub));
  return ret;
}

static struct grub_procfs_entry hi_rsa_pub_entry = 
{
  .name = "hi_rsa.pub",
  .get_contents = get_hi_rsa_pub
};

static char *
get_hi (grub_size_t *sz)
{
  *sz = 3;
  return grub_strdup ("hi\n");
}

struct grub_procfs_entry hi = 
{
  .name = "hi",
  .get_contents = get_hi
};

static char *
get_hj (grub_size_t *sz)
{
  *sz = 3;
  return grub_strdup ("hj\n");
}

struct grub_procfs_entry hj = 
{
  .name = "hj",
  .get_contents = get_hj
};

static void
do_verify (const char *f, const char *sig, const char *pub, int is_valid)
{
  grub_command_t cmd;
  char *args[] = { (char *) f, (char *) sig,
		   (char *) pub, NULL };
  grub_err_t err;

  cmd = grub_command_find ("verify_detached");
  if (!cmd)
    {
      grub_test_assert (0, "can't find command `%s'", "verify_detached");
      return;
    }
  err = (cmd->func) (cmd, 3, args);

  grub_test_assert (err == (is_valid ? 0 : GRUB_ERR_BAD_SIGNATURE),
		    "verification failed: %d: %s", grub_errno, grub_errmsg);
  grub_errno = GRUB_ERR_NONE;

}
static void
signature_test (void)
{
  grub_procfs_register ("hi", &hi);
  grub_procfs_register ("hj", &hj);
  grub_procfs_register ("hi_dsa.pub", &hi_dsa_pub_entry);
  grub_procfs_register ("hi_dsa.sig", &hi_dsa_sig_entry);
  grub_procfs_register ("hi_rsa.pub", &hi_rsa_pub_entry);
  grub_procfs_register ("hi_rsa.sig", &hi_rsa_sig_entry);

  do_verify ("(proc)/hi", "(proc)/hi_dsa.sig", "(proc)/hi_dsa.pub", 1);
  do_verify ("(proc)/hi", "(proc)/hi_dsa.sig", "(proc)/hi_dsa.pub", 1);
  do_verify ("(proc)/hj", "(proc)/hi_dsa.sig", "(proc)/hi_dsa.pub", 0);

  do_verify ("(proc)/hi", "(proc)/hi_rsa.sig", "(proc)/hi_rsa.pub", 1);
  do_verify ("(proc)/hj", "(proc)/hi_rsa.sig", "(proc)/hi_rsa.pub", 0);

  grub_procfs_unregister (&hi);
  grub_procfs_unregister (&hj);
  grub_procfs_unregister (&hi_dsa_sig_entry);
  grub_procfs_unregister (&hi_dsa_pub_entry);
}

GRUB_FUNCTIONAL_TEST (signature_test, signature_test);
