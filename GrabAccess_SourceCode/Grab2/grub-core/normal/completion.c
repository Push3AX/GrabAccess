/* completion.c - complete a command, a disk, a partition or a file */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2004,2005,2007,2008,2009  Free Software Foundation, Inc.
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
#include <grub/misc.h>
#include <grub/env.h>
#include <grub/err.h>
#include <grub/mm.h>
#include <grub/partition.h>
#include <grub/disk.h>
#include <grub/file.h>
#include <grub/parser.h>
#include <grub/extcmd.h>
#include <grub/charset.h>

/* The current word.  */
static const char *current_word;

/* The matched string.  */
static char *match;

/* The count of candidates.  */
static int num_found;

/* The string to be appended.  */
static const char *suffix;

/* The callback function to print items.  */
static void (*print_func) (const char *, grub_completion_type_t, int);

/* The state the command line is in.  */
static grub_parser_state_t cmdline_state;


/* Add a string to the list of possible completions. COMPLETION is the
   string that should be added. EXTRA will be appended if COMPLETION
   matches uniquely. The type TYPE specifies what kind of data is added.  */
static int
add_completion (const char *completion, const char *extra,
		grub_completion_type_t type)
{
  if (grub_strncmp (current_word, completion, grub_strlen (current_word)) == 0)
    {
      num_found++;

      switch (num_found)
	{
	case 1:
	  match = grub_strdup (completion);
	  if (! match)
	    return 1;
	  suffix = extra;
	  break;

	case 2:
	  if (print_func)
	    print_func (match, type, 0);

	  /* Fall through.  */

	default:
	  {
	    char *s = match;
	    const char *t = completion;

	    if (print_func)
	      print_func (completion, type, num_found - 1);

	    /* Detect the matched portion.  */
	    while (*s && *t && *s == *t)
	      {
		s++;
		t++;
	      }
	    s = match + grub_getend (match, s);

	    *s = '\0';
	  }
	  break;
	}
    }

  return 0;
}

static int
iterate_partition (grub_disk_t disk, const grub_partition_t p,
		   void *data __attribute__ ((unused)))
{
  const char *disk_name = disk->name;
  char *name;
  int ret;
  char *part_name;

  part_name = grub_partition_get_name (p);
  if (! part_name)
    return 1;

  name = grub_xasprintf ("%s,%s", disk_name, part_name);
  grub_free (part_name);

  if (! name)
    return 1;

  ret = add_completion (name, ")", GRUB_COMPLETION_TYPE_PARTITION);
  grub_free (name);
  return ret;
}

static int
iterate_dir (const char *filename, const struct grub_dirhook_info *info,
	     void *data __attribute__ ((unused)))
{
  if (! info->dir)
    {
      const char *prefix;
      if (cmdline_state == GRUB_PARSER_STATE_DQUOTE)
	prefix = "\" ";
      else if (cmdline_state == GRUB_PARSER_STATE_QUOTE)
	prefix = "\' ";
      else
	prefix = " ";

      if (add_completion (filename, prefix, GRUB_COMPLETION_TYPE_FILE))
	return 1;
    }
  else if (grub_strcmp (filename, ".") && grub_strcmp (filename, ".."))
    {
      char *fname;

      fname = grub_xasprintf ("%s/", filename);
      if (add_completion (fname, "", GRUB_COMPLETION_TYPE_FILE))
	{
	  grub_free (fname);
	  return 1;
	}
      grub_free (fname);
    }

  return 0;
}

static int
iterate_dev (const char *devname, void *data __attribute__ ((unused)))
{
  grub_device_t dev;

  /* Complete the partition part.  */
  dev = grub_device_open (devname);

  if (!dev)
    {
      grub_errno = GRUB_ERR_NONE;
      return 0;
    }

  if (grub_strcmp (devname, current_word) == 0)
    {
      if (add_completion (devname, ")", GRUB_COMPLETION_TYPE_PARTITION))
	{
	  grub_device_close (dev);
	  return 1;
	}

      if (dev->disk)
	if (grub_partition_iterate (dev->disk, iterate_partition, NULL))
	  {
	    grub_device_close (dev);
	    return 1;
	  }
    }
  else if (add_completion (devname, "", GRUB_COMPLETION_TYPE_DEVICE))
    {
      grub_device_close (dev);
      return 1;
    }

  grub_device_close (dev);
  grub_errno = GRUB_ERR_NONE;
  return 0;
}

/* Complete a device.  */
static int
complete_device (void)
{
  /* Check if this is a device or a partition.  */
  char *p = grub_strchr (++current_word, ',');
  grub_device_t dev;

  if (! p)
    {
      /* Complete the disk part.  */
      if (grub_disk_dev_iterate (iterate_dev, NULL))
	return 1;
    }
  else
    {
      /* Complete the partition part.  */
      *p = '\0';
      dev = grub_device_open (current_word);
      *p = ',';
      grub_errno = GRUB_ERR_NONE;

      if (dev)
	{
	  if (dev->disk)
	    {
	      if (grub_partition_iterate (dev->disk, iterate_partition, NULL))
		{
		  grub_device_close (dev);
		  return 1;
		}
	    }

	  grub_device_close (dev);
	}
      else
	return 1;
    }

  return 0;
}

/* Complete a file.  */
static int
complete_file (void)
{
  char *device;
  char *dir;
  char *last_dir;
  grub_fs_t fs;
  grub_device_t dev;
  int ret = 0;

  device = grub_file_get_device_name (current_word);
  if (grub_errno != GRUB_ERR_NONE)
    return 1;

  dev = grub_device_open (device);
  if (! dev)
    {
      ret = 1;
      goto fail;
    }

  fs = grub_fs_probe (dev);
  if (! fs)
    {
      ret = 1;
      goto fail;
    }

  dir = grub_strchr (current_word + (device ? 2 + grub_strlen (device) : 0),
		     '/');
  last_dir = grub_strrchr (current_word, '/');
  if (dir)
    {
      char *dirfile;

      current_word = last_dir + 1;

      dir = grub_strdup (dir);
      if (! dir)
	{
	  ret = 1;
	  goto fail;
	}

      /* Cut away the filename part.  */
      dirfile = grub_strrchr (dir, '/');
      if (dirfile)
	dirfile[1] = '\0';

      /* Iterate the directory.  */
      (fs->fs_dir) (dev, dir, iterate_dir, NULL);

      grub_free (dir);

      if (grub_errno)
	{
	  ret = 1;
	  goto fail;
	}
    }
  else
    {
      current_word += grub_strlen (current_word);
      match = grub_strdup ("/");
      if (! match)
	{
	  ret = 1;
	  goto fail;
	}

      suffix = "";
      num_found = 1;
    }

 fail:
  if (dev)
    grub_device_close (dev);
  grub_free (device);
  return ret;
}

static int
complete_variables (int add_equal)
{
  struct grub_env_var *env;
  char *p = NULL;
  FOR_SORTED_ENV (env)
  {
    p = grub_strstr (env->name, current_word);
    if (p)
      add_completion (env->name, add_equal ? "=": "", GRUB_COMPLETION_TYPE_ENV);
  }
  return p ? 1 : 0;
}

/* Complete an argument.  */
static int
complete_arguments (char *command)
{
  grub_command_t cmd;
  grub_extcmd_t ext;
  const struct grub_arg_option *option;
  char shortarg[] = "- ";

  cmd = grub_command_find (command);

  if (!cmd || !(cmd->flags & GRUB_COMMAND_FLAG_EXTCMD))
    return 0;

  ext = cmd->data;
  if (!ext->options)
    return 0;

  if (add_completion ("-u", " ", GRUB_COMPLETION_TYPE_ARGUMENT))
    return 1;

  /* Add the short arguments.  */
  for (option = ext->options; option->doc; option++)
    {
      if (! option->shortarg)
	continue;

      shortarg[1] = option->shortarg;
      if (add_completion (shortarg, " ", GRUB_COMPLETION_TYPE_ARGUMENT))
	return 1;

    }

  /* First add the built-in arguments.  */
  if (add_completion ("--help", " ", GRUB_COMPLETION_TYPE_ARGUMENT))
    return 1;
  if (add_completion ("--usage", " ", GRUB_COMPLETION_TYPE_ARGUMENT))
    return 1;

  /* Add the long arguments.  */
  for (option = ext->options; option->doc; option++)
    {
      char *longarg;
      if (!option->longarg)
	continue;

      longarg = grub_xasprintf ("--%s", option->longarg);
      if (!longarg)
	return 1;

      if (add_completion (longarg, " ", GRUB_COMPLETION_TYPE_ARGUMENT))
	{
	  grub_free (longarg);
	  return 1;
	}
      grub_free (longarg);
    }

  return 0;
}


static grub_parser_state_t
get_state (const char *cmdline)
{
  grub_parser_state_t state = GRUB_PARSER_STATE_TEXT;
  char use;

  while (*cmdline)
    state = grub_parser_cmdline_state (state, *(cmdline++), &use);
  return state;
}

/* Try to complete the string in BUF. Return the characters that
   should be added to the string.  This command outputs the possible
   completions by calling HOOK, in that case set RESTORE to 1 so the
   caller can restore the prompt.  */
char *
grub_normal_do_completion (char *buf, int *restore,
			   void (*hook) (const char *, grub_completion_type_t, int))
{
  int argc = 0;
  char **argv = NULL;

  /* Initialize variables.  */
  match = 0;
  num_found = 0;
  suffix = "";
  print_func = hook;

  *restore = 1;

  if (grub_parser_split_cmdline (buf, 0, 0, &argc, &argv))
    return 0;

  if (argc == 0)
    current_word = "";
  else
    current_word = argv[argc - 1];

  if (argc > 1 && ! grub_strcmp (argv[0], "set"))
    {
      char *equals = grub_strchr (current_word, '=');
      if (equals)
	/* Complete the value of the variable.  */
	current_word = equals + 1;
    }

  /* Determine the state the command line is in, depending on the
     state, it can be determined how to complete.  */
  cmdline_state = get_state (buf);

  if (argc > 1 && (! grub_strcmp (argv[0], "set") ||
                   ! grub_strcmp (argv[0], "unset") ||
                   ! grub_strcmp (argv[0], "export")))
  {
    char *equals = grub_strchr (argv[1], '=');
    if (equals || complete_variables (argv[0][0] == 's'? 1: 0))
      goto fail;
  }
  else if (argc == 1 || argc == 0)
    {
      /* Complete a command.  */
      grub_command_t cmd;
      FOR_COMMANDS(cmd)
      {
	if (cmd->prio & GRUB_COMMAND_FLAG_ACTIVE)
	  {
	    if (add_completion (cmd->name, " ", GRUB_COMPLETION_TYPE_COMMAND))
	      goto fail;
	  }
      }
    }
  else if (*current_word == '-')
    {
      if (complete_arguments (buf))
	goto fail;
    }
  else if (*current_word == '(' && ! grub_strchr (current_word, ')'))
    {
      /* Complete a device.  */
      if (complete_device ())
	    goto fail;
    }
  else
    {
      /* Complete a file.  */
      if (complete_file ())
	goto fail;
    }

  /* If more than one match is found those matches will be printed and
     the prompt should be restored.  */
  if (num_found > 1)
    *restore = 1;
  else
    *restore = 0;

  /* Return the part that matches.  */
  if (match)
    {
      char *ret;
      char *escstr;
      char *newstr;
      int current_len;
      int match_len;
      int spaces = 0;

      current_len = grub_strlen (current_word);
      match_len = grub_strlen (match);

      /* Count the number of spaces that have to be escaped.  XXX:
	 More than just spaces have to be escaped.  */
      for (escstr = match + current_len; *escstr; escstr++)
	if (*escstr == ' ')
	  spaces++;

      ret = grub_malloc (match_len - current_len + grub_strlen (suffix) + spaces + 1);
      newstr = ret;
      for (escstr = match + current_len; *escstr; escstr++)
	{
	  if (*escstr == ' ' && cmdline_state != GRUB_PARSER_STATE_QUOTE
	      && cmdline_state != GRUB_PARSER_STATE_QUOTE)
	    *(newstr++) = '\\';
	  *(newstr++) = *escstr;
	}
      *newstr = '\0';

      if (num_found == 1)
	grub_strcpy (newstr, suffix);

      if (*ret == '\0')
	{
	  grub_free (ret);
          goto fail;
	}

      if (argc != 0)
	grub_free (argv[0]);
      grub_free (match);
      return ret;
    }

 fail:
  if (argc != 0)
    grub_free (argv[0]);
  grub_free (argv);
  grub_free (match);
  grub_errno = GRUB_ERR_NONE;

  return 0;
}
