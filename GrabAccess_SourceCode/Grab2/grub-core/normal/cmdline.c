/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2004,2005,2007,2009  Free Software Foundation, Inc.
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
#include <grub/term.h>
#include <grub/err.h>
#include <grub/types.h>
#include <grub/mm.h>
#include <grub/partition.h>
#include <grub/disk.h>
#include <grub/file.h>
#include <grub/env.h>
#include <grub/i18n.h>
#include <grub/charset.h>
#include <grub/safemath.h>

static grub_uint32_t *kill_buf;

static int hist_size;
static grub_uint32_t **hist_lines = 0;
static int hist_pos = 0;
static int hist_end = 0;
static int hist_used = 0;

grub_err_t
grub_set_history (int newsize)
{
  grub_uint32_t **old_hist_lines = hist_lines;
  hist_lines = grub_calloc (newsize, sizeof (grub_uint32_t *));

  /* Copy the old lines into the new buffer.  */
  if (old_hist_lines)
    {
      /* Remove the lines that don't fit in the new buffer.  */
      if (newsize < hist_used)
	{
	  grub_size_t i;
	  grub_size_t delsize = hist_used - newsize;
	  hist_used = newsize;

	  for (i = 1; i < delsize + 1; i++)
	    {
	      grub_ssize_t pos = hist_end - i;
	      if (pos < 0)
		pos += hist_size;
	      grub_free (old_hist_lines[pos]);
	    }

	  hist_end -= delsize;
	  if (hist_end < 0)
	    hist_end += hist_size;
	}

      if (hist_pos < hist_end)
	grub_memmove (hist_lines, old_hist_lines + hist_pos,
		      (hist_end - hist_pos) * sizeof (grub_uint32_t *));
      else if (hist_used)
	{
	  /* Copy the older part.  */
	  grub_memmove (hist_lines, old_hist_lines + hist_pos,
 			(hist_size - hist_pos) * sizeof (grub_uint32_t *));

	  /* Copy the newer part. */
	  grub_memmove (hist_lines + hist_size - hist_pos, old_hist_lines,
			hist_end * sizeof (grub_uint32_t *));
	}
    }

  grub_free (old_hist_lines);

  hist_size = newsize;
  hist_pos = 0;
  hist_end = hist_used;
  return 0;
}

/* Get the entry POS from the history where `0' is the newest
   entry.  */
static grub_uint32_t *
grub_history_get (unsigned pos)
{
  pos = (hist_pos + pos) % hist_size;
  return hist_lines[pos];
}

static grub_size_t
strlen_ucs4 (const grub_uint32_t *s)
{
  const grub_uint32_t *p = s;

  while (*p)
    p++;

  return p - s;
}

/* Replace the history entry on position POS with the string S.  */
static void
grub_history_set (int pos, grub_uint32_t *s, grub_size_t len)
{
  grub_free (hist_lines[pos]);
  hist_lines[pos] = grub_calloc (len + 1, sizeof (grub_uint32_t));
  if (!hist_lines[pos])
    {
      grub_print_error ();
      grub_errno = GRUB_ERR_NONE;
      return ;
    }
  grub_memcpy (hist_lines[pos], s, len * sizeof (grub_uint32_t));
  hist_lines[pos][len] = 0;
}

/* Insert a new history line S on the top of the history.  */
static void
grub_history_add (grub_uint32_t *s, grub_size_t len)
{
  /* Remove the oldest entry in the history to make room for a new
     entry.  */
  if (hist_used + 1 > hist_size)
    {
      hist_end--;
      if (hist_end < 0)
	hist_end = hist_size + hist_end;

      grub_free (hist_lines[hist_end]);
    }
  else
    hist_used++;

  /* Move to the next position.  */
  hist_pos--;
  if (hist_pos < 0)
    hist_pos = hist_size + hist_pos;

  /* Insert into history.  */
  hist_lines[hist_pos] = NULL;
  grub_history_set (hist_pos, s, len);
}

/* Replace the history entry on position POS with the string S.  */
static void
grub_history_replace (unsigned pos, grub_uint32_t *s, grub_size_t len)
{
  grub_history_set ((hist_pos + pos) % hist_size, s, len);
}

/* A completion hook to print items.  */
static void
print_completion (const char *item, grub_completion_type_t type, int count)
{
  if (count == 0)
    {
      /* If this is the first time, print a label.  */
      
      grub_puts ("");
      switch (type)
	{
	case GRUB_COMPLETION_TYPE_COMMAND:
	  grub_puts_ (N_("Possible commands are:"));
	  break;
	case GRUB_COMPLETION_TYPE_DEVICE:
	  grub_puts_ (N_("Possible devices are:"));
	  break;
	case GRUB_COMPLETION_TYPE_FILE:
	  grub_puts_ (N_("Possible files are:"));
	  break;
	case GRUB_COMPLETION_TYPE_ENV:
	  grub_puts_ (N_("Possible variables are:"));
	  break;
	case GRUB_COMPLETION_TYPE_PARTITION:
	  grub_puts_ (N_("Possible partitions are:"));
	  break;
	case GRUB_COMPLETION_TYPE_ARGUMENT:
	  grub_puts_ (N_("Possible arguments are:"));
	  break;
	default:
	  /* TRANSLATORS: this message is used if none of above matches.
	     This shouldn't happen but please use the general term for
	     "thing" or "object".  */
	  grub_puts_ (N_("Possible things are:"));
	  break;
	}
      grub_puts ("");
    }

  if (type == GRUB_COMPLETION_TYPE_PARTITION)
    {
      grub_normal_print_device_info (item);
      grub_errno = GRUB_ERR_NONE;
    }
  else
    grub_printf (" %s", item);
}

struct cmdline_term
{
  struct grub_term_coordinate pos;
  unsigned ystart, width, height;
  unsigned prompt_len;
  struct grub_term_output *term;
};

static inline void
cl_set_pos (struct cmdline_term *cl_term, grub_size_t lpos)
{
  cl_term->pos.x = (cl_term->prompt_len + lpos) % cl_term->width;
  cl_term->pos.y = cl_term->ystart
    + (cl_term->prompt_len + lpos) / cl_term->width;
  grub_term_gotoxy (cl_term->term, cl_term->pos);
}

static void
cl_set_pos_all (struct cmdline_term *cl_terms, unsigned nterms,
		grub_size_t lpos)
{
  unsigned i;
  for (i = 0; i < nterms; i++)
    cl_set_pos (&cl_terms[i], lpos);
}

static inline void __attribute__ ((always_inline))
cl_print (struct cmdline_term *cl_term, grub_uint32_t c,
	  grub_uint32_t *start, grub_uint32_t *end)
{
  grub_uint32_t *p;

  for (p = start; p < end; p++)
    {
      if (c)
	grub_putcode (c, cl_term->term);
      else
	grub_putcode (*p, cl_term->term);
      cl_term->pos.x++;
      if (cl_term->pos.x >= cl_term->width - 1)
	{
	  cl_term->pos.x = 0;
	  if (cl_term->pos.y >= (unsigned) (cl_term->height - 1))
	    cl_term->ystart--;
	  else
	    cl_term->pos.y++;
	  grub_putcode ('\n', cl_term->term);
	}
    }
}

static void
cl_print_all (struct cmdline_term *cl_terms, unsigned nterms,
	      grub_uint32_t c, grub_uint32_t *start, grub_uint32_t *end)
{
  unsigned i;
  for (i = 0; i < nterms; i++)
    cl_print (&cl_terms[i], c, start, end);
}

static void
init_clterm (struct cmdline_term *cl_term_cur)
{
  cl_term_cur->pos.x = cl_term_cur->prompt_len;
  cl_term_cur->pos.y = grub_term_getxy (cl_term_cur->term).y;
  cl_term_cur->ystart = cl_term_cur->pos.y;
  cl_term_cur->width = grub_term_width (cl_term_cur->term);
  cl_term_cur->height = grub_term_height (cl_term_cur->term);
}


static void
cl_delete (struct cmdline_term *cl_terms, unsigned nterms,
	   grub_uint32_t *buf,
	   grub_size_t lpos, grub_size_t *llen, unsigned len)
{
  if (lpos + len <= (*llen))
    {
      cl_set_pos_all (cl_terms, nterms, (*llen) - len);
      cl_print_all (cl_terms, nterms, ' ', buf + (*llen) - len, buf + (*llen));

      cl_set_pos_all (cl_terms, nterms, lpos);

      grub_memmove (buf + lpos, buf + lpos + len,
		    sizeof (grub_uint32_t) * ((*llen) - lpos + 1));
      (*llen) -= len;
      cl_print_all (cl_terms, nterms, 0, buf + lpos, buf + (*llen));
      cl_set_pos_all (cl_terms, nterms, lpos);
    }
}


static void
cl_insert (struct cmdline_term *cl_terms, unsigned nterms,
	   grub_size_t *lpos, grub_size_t *llen,
	   grub_size_t *max_len, grub_uint32_t **buf,
	   const grub_uint32_t *str)
{
  grub_size_t len = strlen_ucs4 (str);

  if (len + (*llen) >= (*max_len))
    {
      grub_uint32_t *nbuf;
      grub_size_t sz;

      if (grub_mul (*max_len, 2, max_len) ||
	  grub_mul (*max_len, sizeof (grub_uint32_t), &sz))
	{
	  grub_errno = GRUB_ERR_OUT_OF_RANGE;
	  goto fail;
	}

      nbuf = grub_realloc ((*buf), sz);
      if (nbuf)
	(*buf) = nbuf;
      else
	{
 fail:
	  grub_print_error ();
	  grub_errno = GRUB_ERR_NONE;
	  (*max_len) /= 2;
	}
    }

  if (len + (*llen) < (*max_len))
    {
      grub_memmove ((*buf) + (*lpos) + len, (*buf) + (*lpos),
		    ((*llen) - (*lpos) + 1) * sizeof (grub_uint32_t));
      grub_memmove ((*buf) + (*lpos), str, len * sizeof (grub_uint32_t));

      (*llen) += len;
      cl_set_pos_all (cl_terms, nterms, (*lpos));
      cl_print_all (cl_terms, nterms, 0, *buf + (*lpos), *buf + (*llen));
      (*lpos) += len;
      cl_set_pos_all (cl_terms, nterms, (*lpos));
    }
}


/* Get a command-line. If ESC is pushed, return zero,
   otherwise return command line.  */
/* FIXME: The dumb interface is not supported yet.  */
char *
grub_cmdline_get (const char *prompt_translated)
{
  grub_size_t lpos, llen;
  grub_uint32_t *buf;
  grub_size_t max_len = 256;
  int key;
  int histpos = 0;
  struct cmdline_term *cl_terms;
  char *ret;
  unsigned nterms;

  buf = grub_calloc (max_len, sizeof (grub_uint32_t));
  if (!buf)
    return 0;

  lpos = llen = 0;
  buf[0] = '\0';

  {
    grub_term_output_t term;

    FOR_ACTIVE_TERM_OUTPUTS(term)
      if ((grub_term_getxy (term).x) != 0)
	grub_putcode ('\n', term);
  }
  grub_xputs (prompt_translated);
  grub_xputs (" ");
  grub_normal_reset_more ();

  {
    struct cmdline_term *cl_term_cur;
    struct grub_term_output *cur;
    grub_uint32_t *unicode_msg;
    grub_size_t msg_len = grub_strlen (prompt_translated) + 3;

    nterms = 0;
    FOR_ACTIVE_TERM_OUTPUTS(cur)
      nterms++;

    cl_terms = grub_calloc (nterms, sizeof (cl_terms[0]));
    if (!cl_terms)
      {
	grub_free (buf);
	return 0;
      }
    cl_term_cur = cl_terms;

    unicode_msg = grub_calloc (msg_len, sizeof (grub_uint32_t));
    if (!unicode_msg)
      {
	grub_free (buf);
	grub_free (cl_terms);
	return 0;
      }
    msg_len = grub_utf8_to_ucs4 (unicode_msg, msg_len - 1,
				 (grub_uint8_t *) prompt_translated, -1, 0);
    unicode_msg[msg_len++] = ' ';

    FOR_ACTIVE_TERM_OUTPUTS(cur)
    {
      cl_term_cur->term = cur;
      cl_term_cur->prompt_len = grub_getstringwidth (unicode_msg,
						     unicode_msg + msg_len,
						     cur);
      init_clterm (cl_term_cur);
      cl_term_cur++;
    }
    grub_free (unicode_msg);
  }

  if (hist_used == 0)
    grub_history_add (buf, llen);

  grub_refresh ();

  while ((key = grub_getkey ()) != '\n' && key != '\r')
    {
      switch (key)
	{
	case GRUB_TERM_CTRL | 'a':
	case GRUB_TERM_KEY_HOME:
	  lpos = 0;
	  cl_set_pos_all (cl_terms, nterms, lpos);
	  break;

	case GRUB_TERM_CTRL | 'b':
	case GRUB_TERM_KEY_LEFT:
	  if (lpos > 0)
	    {
	      lpos--;
	      cl_set_pos_all (cl_terms, nterms, lpos);
	    }
	  break;

	case GRUB_TERM_CTRL | 'e':
	case GRUB_TERM_KEY_END:
	  lpos = llen;
	  cl_set_pos_all (cl_terms, nterms, lpos);
	  break;

	case GRUB_TERM_CTRL | 'f':
	case GRUB_TERM_KEY_RIGHT:
	  if (lpos < llen)
	    {
	      lpos++;
	      cl_set_pos_all (cl_terms, nterms, lpos);
	    }
	  break;

	case GRUB_TERM_CTRL | 'i':
	case '\t':
	  {
	    int restore;
	    char *insertu8;
	    char *bufu8;
	    grub_uint32_t c;

	    c = buf[lpos];
	    buf[lpos] = '\0';

	    bufu8 = grub_ucs4_to_utf8_alloc (buf, lpos);
	    buf[lpos] = c;
	    if (!bufu8)
	      {
		grub_print_error ();
		grub_errno = GRUB_ERR_NONE;
		break;
	      }

	    insertu8 = grub_normal_do_completion (bufu8, &restore,
						  print_completion);
	    grub_free (bufu8);

	    grub_normal_reset_more ();

	    if (restore)
	      {
		unsigned i;

		/* Restore the prompt.  */
		grub_xputs ("\n");
		grub_xputs (prompt_translated);
		grub_xputs (" ");

		for (i = 0; i < nterms; i++)
		  init_clterm (&cl_terms[i]);

		cl_print_all (cl_terms, nterms, 0, buf, buf + llen);
	      }

	    if (insertu8)
	      {
		grub_size_t insertlen;
		grub_ssize_t t;
		grub_uint32_t *insert;

		insertlen = grub_strlen (insertu8);
		insert = grub_calloc (insertlen + 1, sizeof (grub_uint32_t));
		if (!insert)
		  {
		    grub_free (insertu8);
		    grub_print_error ();
		    grub_errno = GRUB_ERR_NONE;
		    break;
		  }
		t = grub_utf8_to_ucs4 (insert, insertlen,
				       (grub_uint8_t *) insertu8,
				       insertlen, 0);
		if (t > 0)
		  {
		    if (insert[t-1] == ' ' && buf[lpos] == ' ')
		      {
			insert[t-1] = 0;
			if (t != 1)
			  cl_insert (cl_terms, nterms, &lpos, &llen, &max_len, &buf, insert);
			lpos++;
		      }
		    else
		      {
			insert[t] = 0;
			cl_insert (cl_terms, nterms, &lpos, &llen, &max_len, &buf, insert);
		      }
		  }

		grub_free (insertu8);
		grub_free (insert);
	      }
	    cl_set_pos_all (cl_terms, nterms, lpos);
	  }
	  break;

	case GRUB_TERM_CTRL | 'k':
	  if (lpos < llen)
	    {
	      grub_free (kill_buf);

	      kill_buf = grub_malloc ((llen - lpos + 1)
				      * sizeof (grub_uint32_t));
	      if (grub_errno)
		{
		  grub_print_error ();
		  grub_errno = GRUB_ERR_NONE;
		}
	      else
		{
		  grub_memcpy (kill_buf, buf + lpos,
			       (llen - lpos + 1) * sizeof (grub_uint32_t));
		  kill_buf[llen - lpos] = 0;
		}

	      cl_delete (cl_terms, nterms,
			 buf, lpos, &llen, llen - lpos);
	    }
	  break;

	case GRUB_TERM_CTRL | 'n':
	case GRUB_TERM_KEY_DOWN:
	  {
	    grub_uint32_t *hist;

	    lpos = 0;

	    if (histpos > 0)
	      {
		grub_history_replace (histpos, buf, llen);
		histpos--;
	      }

	    cl_delete (cl_terms, nterms,
		       buf, lpos, &llen, llen);
	    hist = grub_history_get (histpos);
	    cl_insert (cl_terms, nterms, &lpos, &llen, &max_len, &buf, hist);

	    break;
	  }

	case GRUB_TERM_KEY_UP:
	case GRUB_TERM_CTRL | 'p':
	  {
	    grub_uint32_t *hist;

	    lpos = 0;

	    if (histpos < hist_used - 1)
	      {
		grub_history_replace (histpos, buf, llen);
		histpos++;
	      }

	    cl_delete (cl_terms, nterms,
		       buf, lpos, &llen, llen);
	    hist = grub_history_get (histpos);

	    cl_insert (cl_terms, nterms, &lpos, &llen, &max_len, &buf, hist);
	  }
	  break;

	case GRUB_TERM_CTRL | 'u':
	  if (lpos > 0)
	    {
	      grub_size_t n = lpos;

	      grub_free (kill_buf);

	      kill_buf = grub_calloc (n + 1, sizeof (grub_uint32_t));
	      if (grub_errno)
		{
		  grub_print_error ();
		  grub_errno = GRUB_ERR_NONE;
		}
	      if (kill_buf)
		{
		  grub_memcpy (kill_buf, buf, n * sizeof(grub_uint32_t));
		  kill_buf[n] = 0;
		}

	      lpos = 0;
	      cl_set_pos_all (cl_terms, nterms, lpos);
	      cl_delete (cl_terms, nterms,
			 buf, lpos, &llen, n);
	    }
	  break;

	case GRUB_TERM_CTRL | 'y':
	  if (kill_buf)
	    cl_insert (cl_terms, nterms, &lpos, &llen, &max_len, &buf, kill_buf);
	  break;

	case GRUB_TERM_ESC:
	  grub_free (cl_terms);
	  grub_free (buf);
	  return 0;

	case GRUB_TERM_BACKSPACE:
	  if (lpos > 0)
	    {
	      lpos--;
	      cl_set_pos_all (cl_terms, nterms, lpos);
	    }
          else
            break;
	  /* fall through */

	case GRUB_TERM_CTRL | 'd':
	case GRUB_TERM_KEY_DC:
	  if (lpos < llen)
	    cl_delete (cl_terms, nterms,
		       buf, lpos, &llen, 1);
	  break;

	default:
	  if (grub_isprint (key))
	    {
	      grub_uint32_t str[2];

	      str[0] = key;
	      str[1] = '\0';
	      cl_insert (cl_terms, nterms, &lpos, &llen, &max_len, &buf, str);
	    }
	  break;
	}

      grub_refresh ();
    }

  grub_xputs ("\n");
  grub_refresh ();

  histpos = 0;
  if (strlen_ucs4 (buf) > 0)
    {
      grub_uint32_t empty[] = { 0 };
      grub_history_replace (histpos, buf, llen);
      grub_history_add (empty, 0);
    }

  ret = grub_ucs4_to_utf8_alloc (buf, llen + 1);
  grub_free (buf);
  grub_free (cl_terms);
  return ret;
}
