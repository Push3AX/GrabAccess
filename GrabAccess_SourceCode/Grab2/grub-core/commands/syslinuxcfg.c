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

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/extcmd.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/file.h>
#include <grub/normal.h>
#include <grub/script_sh.h>
#include <grub/i18n.h>
#include <grub/term.h>
#include <grub/syslinux_parse.h>
#include <grub/crypto.h>
#include <grub/auth.h>
#include <grub/disk.h>
#include <grub/partition.h>

GRUB_MOD_LICENSE ("GPLv3+");

/* Helper for syslinux_file.  */
static grub_err_t
syslinux_file_getline (char **line, int cont __attribute__ ((unused)),
		     void *data __attribute__ ((unused)))
{
  *line = 0;
  return GRUB_ERR_NONE;
}

static const struct grub_arg_option options[] =
  {
    {"root",  'r', 0,
     N_("root directory of the syslinux disk [default=/]."),
     N_("DIR"), ARG_TYPE_STRING},
    {"cwd",  'c', 0,
     N_("current directory of syslinux [default is parent directory of input file]."),
     N_("DIR"), ARG_TYPE_STRING},
    {"isolinux",     'i',  0, N_("assume input is an isolinux configuration file."), 0, 0},
    {"pxelinux",     'p',  0, N_("assume input is a pxelinux configuration file."), 0, 0},
    {"syslinux",     's',  0, N_("assume input is a syslinux configuration file."), 0, 0},
    {0, 0, 0, 0, 0, 0}
  };

enum
  {
    OPTION_ROOT,
    OPTION_CWD,
    OPTION_ISOLINUX,
    OPTION_PXELINUX,
    OPTION_SYSLINUX
  };

static grub_err_t
syslinux_file (grub_extcmd_context_t ctxt, const char *filename)
{
  char *result;
  const char *root = ctxt->state[OPTION_ROOT].set ? ctxt->state[OPTION_ROOT].arg : "/";
  const char *cwd = ctxt->state[OPTION_CWD].set ? ctxt->state[OPTION_CWD].arg : NULL;
  grub_syslinux_flavour_t flav = GRUB_SYSLINUX_UNKNOWN;
  char *cwdf = NULL;
  grub_menu_t menu;

  if (ctxt->state[OPTION_ISOLINUX].set)
    flav = GRUB_SYSLINUX_ISOLINUX;
  if (ctxt->state[OPTION_PXELINUX].set)
    flav = GRUB_SYSLINUX_PXELINUX;
  if (ctxt->state[OPTION_SYSLINUX].set)
    flav = GRUB_SYSLINUX_SYSLINUX;

  if (!cwd)
    {
      char *p;
      cwdf = grub_strdup (filename);
      if (!cwdf)
	return grub_errno;
      p = grub_strrchr (cwdf, '/');
      if (!p)
	{
	  grub_free (cwdf);
	  cwd = "/";
	  cwdf = NULL;
	}
      else
	{
	  *p = '\0';
	  cwd = cwdf;
	}
    }

  grub_dprintf ("syslinux",
		"transforming syslinux config %s, root = %s, cwd = %s\n",
		filename, root, cwd);

  result = grub_syslinux_config_file (root, root, cwd, cwd, filename, flav);
  if (!result)
    return grub_errno;

  grub_dprintf ("syslinux", "syslinux config transformed\n");

  menu = grub_env_get_menu ();
  if (! menu)
    {
      menu = grub_zalloc (sizeof (*menu));
      if (! menu)
	{
	  grub_free (result);
	  return grub_errno;
	}

      grub_env_set_menu (menu);
    }

  grub_normal_parse_line (result, syslinux_file_getline, NULL);
  grub_print_error ();
  grub_free (result);
  grub_free (cwdf);

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_syslinux_source (grub_extcmd_context_t ctxt,
			  int argc, char **args)
{
  int new_env, extractor;
  grub_err_t ret;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("filename expected"));

  extractor = (ctxt->extcmd->cmd->name[0] == 'e');
  new_env = (ctxt->extcmd->cmd->name[extractor ? (sizeof ("extract_syslinux_entries_") - 1)
			     : (sizeof ("syslinux_") - 1)] == 'c');

  if (new_env)
    grub_cls ();

  if (new_env && !extractor)
    grub_env_context_open ();
  if (extractor)
    grub_env_extractor_open (!new_env);

  ret = syslinux_file (ctxt, args[0]);

  if (new_env)
    {
      grub_menu_t menu;
      menu = grub_env_get_menu ();
      if (menu && menu->size)
	grub_show_menu (menu, 1, 0);
      if (!extractor)
	grub_env_context_close ();
    }
  if (extractor)
    grub_env_extractor_close (!new_env);

  return ret;
}


static grub_extcmd_t cmd_source, cmd_configfile;
static grub_extcmd_t cmd_source_extract, cmd_configfile_extract;

GRUB_MOD_INIT(syslinuxcfg)
{
  cmd_source
    = grub_register_extcmd ("syslinux_source",
			    grub_cmd_syslinux_source, 0,
			    N_("FILE"),
			    /* TRANSLATORS: "syslinux config" means
			       "config as used by syslinux".  */
			    N_("Execute syslinux config in same context"),
			    options);
  cmd_configfile
    = grub_register_extcmd ("syslinux_configfile",
			    grub_cmd_syslinux_source, 0,
			    N_("FILE"),
			    N_("Execute syslinux config in new context"),
			    options);
  cmd_source_extract
    = grub_register_extcmd ("extract_syslinux_entries_source",
			    grub_cmd_syslinux_source, 0,
			    N_("FILE"),
			    N_("Execute syslinux config in same context taking only menu entries"),
			    options);
  cmd_configfile_extract
    = grub_register_extcmd ("extract_syslinux_entries_configfile",
			    grub_cmd_syslinux_source, 0,
			    N_("FILE"),
			    N_("Execute syslinux config in new context taking only menu entries"),
			    options);
}

GRUB_MOD_FINI(syslinuxcfg)
{
  grub_unregister_extcmd (cmd_source);
  grub_unregister_extcmd (cmd_configfile);
  grub_unregister_extcmd (cmd_source_extract);
  grub_unregister_extcmd (cmd_configfile_extract);
}
