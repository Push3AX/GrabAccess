/* play.c - command to play a tune  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2007,2009  Free Software Foundation, Inc.
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

/* Lots of this file is borrowed from GNU/Hurd generic-speaker driver.  */

#include <grub/dl.h>
#include <grub/file.h>
#include <grub/disk.h>
#include <grub/term.h>
#include <grub/misc.h>
#include <grub/cpu/io.h>
#include <grub/command.h>
#include <grub/i18n.h>
#include <grub/time.h>
#include <grub/speaker.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define BASE_TEMPO (60 * 1000)


#define T_REST			((grub_uint16_t) 0)
#define T_FINE			((grub_uint16_t) -1)

struct note
{
  grub_uint16_t pitch;
  grub_uint16_t duration;
};

/* Returns whether playing should continue.  */
static int
play (unsigned tempo, struct note *note)
{
  grub_uint64_t to;

  if (note->pitch == T_FINE || grub_getkey_noblock () != GRUB_TERM_NO_KEY)
    return 1;

  grub_dprintf ("play", "pitch = %d, duration = %d\n", note->pitch,
                note->duration);

  switch (note->pitch)
    {
      case T_REST:
        grub_speaker_beep_off ();
        break;

      default:
        grub_speaker_beep_on (note->pitch);
        break;
    }

  to = grub_get_time_ms () + BASE_TEMPO * note->duration / tempo;
  while ((grub_get_time_ms () <= to)
	 && (grub_getkey_noblock () == GRUB_TERM_NO_KEY));

  return 0;
}

static grub_err_t
grub_cmd_play (grub_command_t cmd __attribute__ ((unused)),
	       int argc, char **args)
{

  if (argc < 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, 
		       /* TRANSLATORS: It's musical notes, not the notes
			  you take. Play command expects arguments which can
			  be either a filename or tempo+notes.
			  This error happens if none is specified.  */
		       N_("filename or tempo and notes expected"));

  if (argc == 1)
    {
      struct note buf;
      grub_uint32_t tempo;
      grub_file_t file;

      file = grub_file_open (args[0], GRUB_FILE_TYPE_AUDIO);

      if (! file)
        return grub_errno;

      if (grub_file_read (file, &tempo, sizeof (tempo)) != sizeof (tempo))
        {
          grub_file_close (file);
	  if (!grub_errno)
	    grub_error (GRUB_ERR_FILE_READ_ERROR, N_("premature end of file %s"),
			args[0]);
          return grub_errno;
        }

      if (!tempo)
        {
          grub_file_close (file);
	  grub_error (GRUB_ERR_BAD_ARGUMENT, N_("Invalid tempo in %s"),
		      args[0]);
          return grub_errno;
        }

      tempo = grub_le_to_cpu32 (tempo);
      grub_dprintf ("play","tempo = %d\n", tempo);

      while (grub_file_read (file, &buf,
                             sizeof (struct note)) == sizeof (struct note))
        {
          buf.pitch = grub_le_to_cpu16 (buf.pitch);
          buf.duration = grub_le_to_cpu16 (buf.duration);

          if (play (tempo, &buf))
            break;
        }

      grub_file_close (file);
    }
  else
    {
      const char *end;
      unsigned tempo;
      struct note note;
      int i;

      tempo = grub_strtoul (args[0], &end, 0);

      if (!tempo)
        {
	  grub_error (GRUB_ERR_BAD_ARGUMENT, N_("Invalid tempo in %s"),
		      args[0]);
          return grub_errno;
        }

      if (*end)
        /* Was not a number either, assume it was supposed to be a file name.  */
        return grub_error (GRUB_ERR_FILE_NOT_FOUND, N_("file `%s' not found"), args[0]);

      grub_dprintf ("play","tempo = %d\n", tempo);

      for (i = 1; i + 1 < argc; i += 2)
        {
          note.pitch = grub_strtoul (args[i], &end, 0);
	  if (grub_errno)
	    break;
          if (*end)
            {
              grub_error (GRUB_ERR_BAD_NUMBER, N_("unrecognized number"));
              break;
            }

          note.duration = grub_strtoul (args[i + 1], &end, 0);
	  if (grub_errno)
	    break;
          if (*end)
            {
              grub_error (GRUB_ERR_BAD_NUMBER, N_("unrecognized number"));
              break;
            }

          if (play (tempo, &note))
            break;
        }
    }

  grub_speaker_beep_off ();

  return 0;
}

static grub_command_t cmd;

GRUB_MOD_INIT(play)
{
  cmd = grub_register_command ("play", grub_cmd_play,
			       N_("FILE | TEMPO [PITCH1 DURATION1] [PITCH2 DURATION2] ... "),
			       N_("Play a tune."));
}

GRUB_MOD_FINI(play)
{
  grub_unregister_command (cmd);
}
