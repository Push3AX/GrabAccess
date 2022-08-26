/* lexer.c - The scripting lexer.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2006,2007,2008,2009,2010  Free Software Foundation, Inc.
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

#include <config.h>

#include <grub/parser.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/script_sh.h>
#include <grub/i18n.h>
#include <grub/safemath.h>

#define yytext_ptr char *
#include "grub_script.tab.h"
#include "grub_script.yy.h"

void
grub_script_lexer_ref (struct grub_lexer_param *state)
{
  state->refs++;
}

void
grub_script_lexer_deref (struct grub_lexer_param *state)
{
  state->refs--;
}

/* Start recording all characters passing through the lexer.  */
unsigned
grub_script_lexer_record_start (struct grub_parser_param *parser)
{
  struct grub_lexer_param *lexer = parser->lexerstate;

  lexer->record++;
  if (lexer->recording)
    return lexer->recordpos;

  lexer->recordpos = 0;
  lexer->recordlen = GRUB_LEXER_INITIAL_RECORD_SIZE;
  lexer->recording = grub_malloc (lexer->recordlen);
  if (!lexer->recording)
    {
      grub_script_yyerror (parser, 0);
      lexer->recordlen = 0;
    }
  return lexer->recordpos;
}

char *
grub_script_lexer_record_stop (struct grub_parser_param *parser, unsigned offset)
{
  int count;
  char *result;
  struct grub_lexer_param *lexer = parser->lexerstate;

  if (!lexer->record)
    return 0;

  lexer->record--;
  if (!lexer->recording)
    return 0;

  count = lexer->recordpos - offset;
  result = grub_script_malloc (parser, count + 1);
  if (result) {
    grub_strncpy (result, lexer->recording + offset, count);
    result[count] = '\0';
  }

  if (lexer->record == 0)
    {
      grub_free (lexer->recording);
      lexer->recording = 0;
      lexer->recordlen = 0;
      lexer->recordpos = 0;
    }
  return result;
}

/* Record STR if input recording is enabled.  */
void
grub_script_lexer_record (struct grub_parser_param *parser, char *str)
{
  int len;
  char *old;
  struct grub_lexer_param *lexer = parser->lexerstate;

  if (!lexer->record || !lexer->recording)
    return;

  len = grub_strlen (str);
  if (lexer->recordpos + len + 1 > lexer->recordlen)
    {
      old = lexer->recording;
      if (lexer->recordlen < len)
	lexer->recordlen = len;

      if (grub_mul (lexer->recordlen, 2, &lexer->recordlen))
	goto fail;

      lexer->recording = grub_realloc (lexer->recording, lexer->recordlen);
      if (!lexer->recording)
	{
 fail:
	  grub_free (old);
	  lexer->recordpos = 0;
	  lexer->recordlen = 0;
	  grub_script_yyerror (parser, 0);
	  return;
	}
    }
  grub_strcpy (lexer->recording + lexer->recordpos, str);
  lexer->recordpos += len;
}

/* Read next line of input if necessary, and set yyscanner buffers.  */
int
grub_script_lexer_yywrap (struct grub_parser_param *parserstate,
			  const char *input)
{
  grub_size_t len = 0, sz;
  char *p = 0;
  char *line = 0;
  YY_BUFFER_STATE buffer;
  struct grub_lexer_param *lexerstate = parserstate->lexerstate;

  if (! lexerstate->refs && ! lexerstate->prefix && ! input)
    return 1;

  if (! lexerstate->getline && ! input)
    {
      grub_script_yyerror (parserstate, N_("unexpected end of file"));
      return 1;
    }

  line = 0;
  if (! input)
    lexerstate->getline (&line, 1, lexerstate->getline_data);
  else
    line = grub_strdup (input);

  if (! line)
    {
      grub_script_yyerror (parserstate, N_("out of memory"));
      return 1;
    }

  len = grub_strlen (line);

  /* Ensure '\n' at the end.  */
  if (line[0] == '\0')
    {
      grub_free (line);
      line = grub_strdup ("\n");
      len = 1;
    }
  else if (len && line[len - 1] != '\n')
    {
      if (grub_add (len, 2, &sz))
	{
	  grub_free (line);
	  grub_script_yyerror (parserstate, N_("overflow is detected"));
	  return 1;
	}

      p = grub_realloc (line, sz);
      if (p)
	{
	  p[len++] = '\n';
	  p[len] = '\0';
	}
      else
	grub_free (line);

      line = p;
    }

  if (! line)
    {
      grub_script_yyerror (parserstate, N_("out of memory"));
      return 1;
    }

  /* Prepend any left over unput-text.  */
  if (lexerstate->prefix)
    {
      int plen = grub_strlen (lexerstate->prefix);

      p = grub_malloc (len + plen + 1);
      if (! p)
	{
	  grub_free (line);
	  return 1;
	}
      grub_strcpy (p, lexerstate->prefix);
      lexerstate->prefix = 0;

      grub_strcpy (p + plen, line);
      grub_free (line);

      line = p;
      len = len + plen;
    }

  buffer = yy_scan_string (line, lexerstate->yyscanner);
  grub_free (line);

  if (! buffer)
    {
      grub_script_yyerror (parserstate, 0);
      return 1;
    }
  return 0;
}

struct grub_lexer_param *
grub_script_lexer_init (struct grub_parser_param *parser, char *script,
			grub_reader_getline_t arg_getline, void *getline_data)
{
  struct grub_lexer_param *lexerstate;

  lexerstate = grub_zalloc (sizeof (*lexerstate));
  if (!lexerstate)
    return 0;

  lexerstate->size = GRUB_LEXER_INITIAL_TEXT_SIZE;
  lexerstate->text = grub_malloc (lexerstate->size);
  if (!lexerstate->text)
    {
      grub_free (lexerstate);
      return 0;
    }

  lexerstate->getline = arg_getline;
  lexerstate->getline_data = getline_data;
  /* The other elements of lexerstate are all zeros already.  */

  if (yylex_init (&lexerstate->yyscanner))
    {
      grub_free (lexerstate->text);
      grub_free (lexerstate);
      return 0;
    }

  yyset_extra (parser, lexerstate->yyscanner);
  parser->lexerstate = lexerstate;

  if (grub_script_lexer_yywrap (parser, script ?: "\n"))
    {
      parser->lexerstate = 0;
      yylex_destroy (lexerstate->yyscanner);
      grub_free (lexerstate->text);
      grub_free (lexerstate);
      return 0;
    }

  return lexerstate;
}

void
grub_script_lexer_fini (struct grub_lexer_param *lexerstate)
{
  if (!lexerstate)
    return;

  yylex_destroy (lexerstate->yyscanner);

  grub_free (lexerstate->recording);
  grub_free (lexerstate->text);
  grub_free (lexerstate);
}

int
grub_script_yylex (union YYSTYPE *value,
		   struct grub_parser_param *parserstate)
{
  char *str;
  int token;
  grub_script_arg_type_t type;
  struct grub_lexer_param *lexerstate = parserstate->lexerstate;

  value->arg = 0;
  if (parserstate->err)
    return GRUB_PARSER_TOKEN_BAD;

  if (lexerstate->eof)
    return GRUB_PARSER_TOKEN_EOF;

  /* 
   * Words with environment variables, like foo${bar}baz needs
   * multiple tokens to be merged into a single grub_script_arg.  We
   * use two variables to achieve this: lexerstate->merge_start and
   * lexerstate->merge_end
   */

  lexerstate->merge_start = 0;
  lexerstate->merge_end = 0;
  do
    {
      /* Empty lexerstate->text.  */
      lexerstate->used = 1;
      lexerstate->text[0] = '\0';

      token = yylex (value, lexerstate->yyscanner);
      if (token == GRUB_PARSER_TOKEN_BAD)
	break;

      /* Merging feature uses lexerstate->text instead of yytext.  */
      if (lexerstate->merge_start)
	{
	  str = lexerstate->text;
	  type = lexerstate->type;
	}
      else
	{
	  str = yyget_text (lexerstate->yyscanner);
	  type = GRUB_SCRIPT_ARG_TYPE_TEXT;
	}
      grub_dprintf("lexer", "token %u text [%s]\n", token, str);

      value->arg = grub_script_arg_add (parserstate, value->arg, type, str);
    }
  while (lexerstate->merge_start && !lexerstate->merge_end);

  if (!value->arg || parserstate->err)
    return GRUB_PARSER_TOKEN_BAD;

  return token;
}

void
grub_script_yyerror (struct grub_parser_param *state, const char *err)
{
  if (err)
    grub_error (GRUB_ERR_INVALID_COMMAND, "%s", err);

  grub_print_error ();
  state->err++;
}
