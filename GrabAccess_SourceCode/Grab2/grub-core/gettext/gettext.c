/* gettext.c - gettext module */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009 Free Software Foundation, Inc.
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
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/normal.h>
#include <grub/file.h>
#include <grub/kernel.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

/*
   .mo file information from:
   http://www.gnu.org/software/autoconf/manual/gettext/MO-Files.html .
*/

static const char *(*grub_gettext_original) (const char *s);

struct grub_gettext_msg
{
  char *name;
  char *translated;
};

struct header
{
  grub_uint32_t magic;
  grub_uint32_t version;
  grub_uint32_t number_of_strings;
  grub_uint32_t offset_original;
  grub_uint32_t offset_translation;
};

struct string_descriptor 
{
  grub_uint32_t length;
  grub_uint32_t offset;
};

struct grub_gettext_context
{
  grub_file_t fd_mo;
  grub_off_t grub_gettext_offset_original;
  grub_off_t grub_gettext_offset_translation;
  grub_size_t grub_gettext_max;
  int grub_gettext_max_log;
  struct grub_gettext_msg *grub_gettext_msg_list;
};

static struct grub_gettext_context main_context, secondary_context;

#define MO_MAGIC_NUMBER 		0x950412de

static grub_err_t
grub_gettext_pread (grub_file_t file, void *buf, grub_size_t len,
		    grub_off_t offset)
{
  if (len == 0)
    return GRUB_ERR_NONE;
  if (grub_file_seek (file, offset) == (grub_off_t) - 1)
    return grub_errno;
  if (grub_file_read (file, buf, len) != (grub_ssize_t) len)
    {
      if (!grub_errno)
	grub_error (GRUB_ERR_READ_ERROR, N_("premature end of file"));
      return grub_errno;
    }
  return GRUB_ERR_NONE;
}

static char *
grub_gettext_getstr_from_position (struct grub_gettext_context *ctx,
				   grub_off_t off,
				   grub_size_t position)
{
  grub_off_t internal_position;
  grub_size_t length;
  grub_off_t offset;
  char *translation;
  struct string_descriptor desc;
  grub_err_t err;

  internal_position = (off + position * sizeof (desc));

  err = grub_gettext_pread (ctx->fd_mo, (char *) &desc,
			    sizeof (desc), internal_position);
  if (err)
    return NULL;
  length = grub_cpu_to_le32 (desc.length);
  offset = grub_cpu_to_le32 (desc.offset);

  translation = grub_malloc (length + 1);
  if (!translation)
    return NULL;

  err = grub_gettext_pread (ctx->fd_mo, translation, length, offset);
  if (err)
    {
      grub_free (translation);
      return NULL;
    }
  translation[length] = '\0';

  return translation;
}

static const char *
grub_gettext_gettranslation_from_position (struct grub_gettext_context *ctx,
					   grub_size_t position)
{
  if (!ctx->grub_gettext_msg_list[position].translated)
    ctx->grub_gettext_msg_list[position].translated
      = grub_gettext_getstr_from_position (ctx,
					   ctx->grub_gettext_offset_translation,
					   position);
  return ctx->grub_gettext_msg_list[position].translated;
}

static const char *
grub_gettext_getstring_from_position (struct grub_gettext_context *ctx,
				      grub_size_t position)
{
  if (!ctx->grub_gettext_msg_list[position].name)
    ctx->grub_gettext_msg_list[position].name
      = grub_gettext_getstr_from_position (ctx,
					   ctx->grub_gettext_offset_original,
					   position);
  return ctx->grub_gettext_msg_list[position].name;
}

static const char *
grub_gettext_translate_real (struct grub_gettext_context *ctx,
			     const char *orig)
{
  grub_size_t current = 0;
  int i;
  const char *current_string;
  static int depth = 0;

  if (!ctx->grub_gettext_msg_list || !ctx->fd_mo)
    return NULL;

  /* Shouldn't happen. Just a precaution if our own code
     calls gettext somehow.  */
  if (depth > 2)
    return NULL;
  depth++;

  /* Make sure we can use grub_gettext_translate for error messages.  Push
     active error message to error stack and reset error message.  */
  grub_error_push ();

  for (i = ctx->grub_gettext_max_log; i >= 0; i--)
    {
      grub_size_t test;
      int cmp;

      test = current | (1 << i);
      if (test >= ctx->grub_gettext_max)
	continue;

      current_string = grub_gettext_getstring_from_position (ctx, test);

      if (!current_string)
	{
	  grub_errno = GRUB_ERR_NONE;
	  grub_error_pop ();
	  depth--;
	  return NULL;
	}

      /* Search by bisection.  */
      cmp = grub_strcmp (current_string, orig);
      if (cmp <= 0)
	current = test;
      if (cmp == 0)
	{
	  const char *ret = 0;
	  ret = grub_gettext_gettranslation_from_position (ctx, current);
	  if (!ret)
	    {
	      grub_errno = GRUB_ERR_NONE;
	      grub_error_pop ();
	      depth--;
	      return NULL;
	    }
	  grub_error_pop ();
	  depth--;
	  return ret;      
	}
    }

  if (current == 0 && ctx->grub_gettext_max != 0)
    {
      current_string = grub_gettext_getstring_from_position (ctx, 0);

      if (!current_string)
	{
	  grub_errno = GRUB_ERR_NONE;
	  grub_error_pop ();
	  depth--;
	  return NULL;
	}

      if (grub_strcmp (current_string, orig) == 0)
	{
	  const char *ret = 0;
	  ret = grub_gettext_gettranslation_from_position (ctx, current);
	  if (!ret)
	    {
	      grub_errno = GRUB_ERR_NONE;
	      grub_error_pop ();
	      depth--;
	      return NULL;
	    }
	  grub_error_pop ();
	  depth--;
	  return ret;      
	}
    }

  grub_error_pop ();
  depth--;
  return NULL;
}

static const char *
grub_gettext_translate (const char *orig)
{
  const char *ret;
  if (orig[0] == 0)
    return orig;

  ret = grub_gettext_translate_real (&main_context, orig);
  if (ret)
    return ret;
  ret = grub_gettext_translate_real (&secondary_context, orig);
  if (ret)
    return ret;
  return orig;
}

static void
grub_gettext_delete_list (struct grub_gettext_context *ctx)
{
  struct grub_gettext_msg *l = ctx->grub_gettext_msg_list;
  grub_size_t i;

  if (!l)
    return;
  ctx->grub_gettext_msg_list = 0;
  for (i = 0; i < ctx->grub_gettext_max; i++)
    grub_free (l[i].name);
  /* Don't delete the translated message because could be in use.  */
  grub_free (l);
  if (ctx->fd_mo)
    grub_file_close (ctx->fd_mo);
  ctx->fd_mo = 0;
  grub_memset (ctx, 0, sizeof (*ctx));
}

/* This is similar to grub_file_open. */
static grub_err_t
grub_mofile_open (struct grub_gettext_context *ctx,
		  const char *filename)
{
  struct header head;
  grub_err_t err;
  grub_file_t fd;

  /* Using fd_mo and not another variable because
     it's needed for grub_gettext_get_info.  */

  fd = grub_file_open (filename, GRUB_FILE_TYPE_GETTEXT_CATALOG);

  if (!fd)
    return grub_errno;

  err = grub_gettext_pread (fd, &head, sizeof (head), 0);
  if (err)
    {
      grub_file_close (fd);
      return err;
    }

  if (head.magic != grub_cpu_to_le32_compile_time (MO_MAGIC_NUMBER))
    {
      grub_file_close (fd);
      return grub_error (GRUB_ERR_BAD_FILE_TYPE,
			 "mo: invalid mo magic in file: %s", filename);
    }

  if (head.version != 0)
    {
      grub_file_close (fd);
      return grub_error (GRUB_ERR_BAD_FILE_TYPE,
			 "mo: invalid mo version in file: %s", filename);
    }

  ctx->grub_gettext_offset_original = grub_le_to_cpu32 (head.offset_original);
  ctx->grub_gettext_offset_translation = grub_le_to_cpu32 (head.offset_translation);
  ctx->grub_gettext_max = grub_le_to_cpu32 (head.number_of_strings);
  for (ctx->grub_gettext_max_log = 0; ctx->grub_gettext_max >> ctx->grub_gettext_max_log;
       ctx->grub_gettext_max_log++);

  ctx->grub_gettext_msg_list = grub_zalloc (ctx->grub_gettext_max
					    * sizeof (ctx->grub_gettext_msg_list[0]));
  if (!ctx->grub_gettext_msg_list)
    {
      grub_file_close (fd);
      return grub_errno;
    }
  ctx->fd_mo = fd;
  if (grub_gettext != grub_gettext_translate)
    {
      grub_gettext_original = grub_gettext;
      grub_gettext = grub_gettext_translate;
    }
  return 0;
}

/* Returning grub_file_t would be more natural, but grub_mofile_open assigns
   to fd_mo anyway ...  */
static grub_err_t
grub_mofile_open_lang (struct grub_gettext_context *ctx,
		       const char *part1, const char *part2, const char *locale)
{
  char *mo_file;
  grub_err_t err;

  /* mo_file e.g.: /boot/grub/locale/ca.mo   */

  mo_file = grub_xasprintf ("%s%s/%s.mo", part1, part2, locale);
  if (!mo_file)
    return grub_errno;

  err = grub_mofile_open (ctx, mo_file);
  grub_free (mo_file);

  /* Will try adding .gz as well.  */
  if (err)
    {
      grub_errno = GRUB_ERR_NONE;
      mo_file = grub_xasprintf ("%s%s/%s.mo.gz", part1, part2, locale);
      if (!mo_file)
	return grub_errno;
      err = grub_mofile_open (ctx, mo_file);
      grub_free (mo_file);
    }

  /* Will try adding .gmo as well.  */
  if (err)
    {
      grub_errno = GRUB_ERR_NONE;
      mo_file = grub_xasprintf ("%s%s/%s.gmo", part1, part2, locale);
      if (!mo_file)
	return grub_errno;
      err = grub_mofile_open (ctx, mo_file);
      grub_free (mo_file);
    }

  return err;
}

static grub_err_t
grub_gettext_init_ext (struct grub_gettext_context *ctx,
		       const char *locale,
		       const char *locale_dir, const char *prefix)
{
  const char *part1, *part2;
  grub_err_t err;

  grub_gettext_delete_list (ctx);

  if (!locale || locale[0] == 0)
    return 0;

  part1 = locale_dir;
  part2 = "";
  if (!part1 || part1[0] == 0)
    {
      part1 = prefix;
      part2 = "/locale";
    }

  if (!part1 || part1[0] == 0)
    return 0;

  err = grub_mofile_open_lang (ctx, part1, part2, locale);

  /* ll_CC didn't work, so try ll.  */
  if (err)
    {
      char *lang = grub_strdup (locale);
      char *underscore = lang ? grub_strchr (lang, '_') : 0;

      if (underscore)
	{
	  *underscore = '\0';
	  grub_errno = GRUB_ERR_NONE;
	  err = grub_mofile_open_lang (ctx, part1, part2, lang);
	}

      grub_free (lang);
    }

  /* If no translations are available, fall back to untranslated text. */
  if (err == GRUB_ERR_FILE_NOT_FOUND)
    {
      grub_errno = GRUB_ERR_NONE;
      return 0;
    }

  return err;
}

static char *
grub_gettext_env_write_lang (struct grub_env_var *var
			     __attribute__ ((unused)), const char *val)
{
  grub_err_t __attribute__((__unused__)) err;
  err = grub_gettext_init_ext (&main_context, val, grub_env_get ("locale_dir"),
			       grub_env_get ("prefix"));

  err = grub_gettext_init_ext (&secondary_context, val,
			       grub_env_get ("secondary_locale_dir"), 0);

  return grub_strdup (val);
}

void
grub_gettext_reread_prefix (const char *val)
{
  grub_err_t __attribute__((__unused__)) err;
  err = grub_gettext_init_ext (&main_context, grub_env_get ("lang"), 
			       grub_env_get ("locale_dir"),
			       val);
}

static char *
read_main (struct grub_env_var *var
	   __attribute__ ((unused)), const char *val)
{
  grub_err_t __attribute__((__unused__)) err;
  err = grub_gettext_init_ext (&main_context, grub_env_get ("lang"), val,
			       grub_env_get ("prefix"));
  return grub_strdup (val);
}

static char *
read_secondary (struct grub_env_var *var
		__attribute__ ((unused)), const char *val)
{
  grub_err_t __attribute__((__unused__)) err;
  err = grub_gettext_init_ext (&secondary_context, grub_env_get ("lang"), val,
			       0);
  return grub_strdup (val);
}

static grub_err_t
grub_cmd_translate (grub_command_t cmd __attribute__ ((unused)),
		    int argc, char **args)
{
  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("one argument expected"));

  const char *translation;
  translation = grub_gettext_translate (args[0]);
  grub_printf ("%s\n", translation);
  return 0;
}

GRUB_MOD_INIT (gettext)
{
  const char *lang;
  grub_err_t __attribute__((__unused__)) err;

  lang = grub_env_get ("lang");

  err = grub_gettext_init_ext (&main_context, lang, grub_env_get ("locale_dir"),
			       grub_env_get ("prefix"));
  err = grub_gettext_init_ext (&secondary_context, lang,
			       grub_env_get ("secondary_locale_dir"), 0);

  grub_register_variable_hook ("locale_dir", NULL, read_main);
  grub_register_variable_hook ("secondary_locale_dir", NULL, read_secondary);

  grub_register_command_p1 ("gettext", grub_cmd_translate,
			    N_("STRING"),
			    /* TRANSLATORS: It refers to passing the string through gettext.
			       So it's "translate" in the same meaning as in what you're 
			       doing now.
			     */
			    N_("Translates the string with the current settings."));

  /* Reload .mo file information if lang changes.  */
  grub_register_variable_hook ("lang", NULL, grub_gettext_env_write_lang);

  /* Preserve hooks after context changes.  */
  grub_env_export ("lang");
  grub_env_export ("locale_dir");
  grub_env_export ("secondary_locale_dir");
}

GRUB_MOD_FINI (gettext)
{
  grub_gettext_delete_list (&main_context);
  grub_gettext_delete_list (&secondary_context);

  grub_gettext = grub_gettext_original;
}
