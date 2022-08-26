/* parser.y - The scripting parser.  */
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

%{
#include <grub/script_sh.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/i18n.h>

#define YYFREE          grub_free
#define YYMALLOC        grub_malloc
#define YYLTYPE_IS_TRIVIAL      0
#define YYENABLE_NLS    0

#include "grub_script.tab.h"

#pragma GCC diagnostic ignored "-Wmissing-declarations"

%}

%union {
  struct grub_script_cmd *cmd;
  struct grub_script_arglist *arglist;
  struct grub_script_arg *arg;
  char *string;
  struct {
    unsigned offset;
    struct grub_script_mem *memory;
    struct grub_script *scripts;
  };
}

%token GRUB_PARSER_TOKEN_BAD
%token GRUB_PARSER_TOKEN_EOF 0 "end-of-input"

%token GRUB_PARSER_TOKEN_NEWLINE "\n"
%token GRUB_PARSER_TOKEN_AND     "&&"
%token GRUB_PARSER_TOKEN_OR      "||"
%token GRUB_PARSER_TOKEN_SEMI2   ";;"
%token GRUB_PARSER_TOKEN_PIPE    "|"
%token GRUB_PARSER_TOKEN_AMP     "&"
%token GRUB_PARSER_TOKEN_SEMI    ";"
%token GRUB_PARSER_TOKEN_LBR     "{"
%token GRUB_PARSER_TOKEN_RBR     "}"
%token GRUB_PARSER_TOKEN_NOT     "!"
%token GRUB_PARSER_TOKEN_LSQBR2  "["
%token GRUB_PARSER_TOKEN_RSQBR2  "]"
%token GRUB_PARSER_TOKEN_LT      "<"
%token GRUB_PARSER_TOKEN_GT      ">"

%token <arg> GRUB_PARSER_TOKEN_CASE      "case"
%token <arg> GRUB_PARSER_TOKEN_DO        "do"
%token <arg> GRUB_PARSER_TOKEN_DONE      "done"
%token <arg> GRUB_PARSER_TOKEN_ELIF      "elif"
%token <arg> GRUB_PARSER_TOKEN_ELSE      "else"
%token <arg> GRUB_PARSER_TOKEN_ESAC      "esac"
%token <arg> GRUB_PARSER_TOKEN_FI        "fi"
%token <arg> GRUB_PARSER_TOKEN_FOR       "for"
%token <arg> GRUB_PARSER_TOKEN_IF        "if"
%token <arg> GRUB_PARSER_TOKEN_IN        "in"
%token <arg> GRUB_PARSER_TOKEN_SELECT    "select"
%token <arg> GRUB_PARSER_TOKEN_THEN      "then"
%token <arg> GRUB_PARSER_TOKEN_UNTIL     "until"
%token <arg> GRUB_PARSER_TOKEN_WHILE     "while"
%token <arg> GRUB_PARSER_TOKEN_FUNCTION  "function"
%token <arg> GRUB_PARSER_TOKEN_NAME      "name"
%token <arg> GRUB_PARSER_TOKEN_WORD      "word"

%type <arg> block block0
%type <arglist> word argument arguments0 arguments1

%type <cmd> script_init script
%type <cmd> grubcmd ifclause ifcmd forcmd whilecmd untilcmd
%type <cmd> command commands1 statement

%define api.pure
%lex-param   { struct grub_parser_param *state };
%parse-param { struct grub_parser_param *state };

%start script_init

%%
/* It should be possible to do this in a clean way...  */
script_init: { state->err = 0; } script { state->parsed = $2; state->err = 0; }
;

script: newlines0
        {
          $$ = 0;
        }
      | script statement delimiter newlines0
        {
          $$ = grub_script_append_cmd (state, $1, $2);
        }
      | error
        {
          $$ = 0;
          yyerror (state, N_("Incorrect command"));
          yyerrok;
        }
;

newlines0: /* Empty */ | newlines1 ;
newlines1: newlines0 "\n" ;

delimiter: ";"
         | "\n"
;
delimiters0: /* Empty */ | delimiters1 ;
delimiters1: delimiter
          | delimiters1 "\n"
;

word: GRUB_PARSER_TOKEN_NAME { $$ = grub_script_add_arglist (state, 0, $1); }
    | GRUB_PARSER_TOKEN_WORD { $$ = grub_script_add_arglist (state, 0, $1); }
;

statement: command   { $$ = $1; }
         | function  { $$ = 0;  }
;

argument : "case"      { $$ = grub_script_add_arglist (state, 0, $1); }
         | "do"        { $$ = grub_script_add_arglist (state, 0, $1); }
         | "done"      { $$ = grub_script_add_arglist (state, 0, $1); }
         | "elif"      { $$ = grub_script_add_arglist (state, 0, $1); }
         | "else"      { $$ = grub_script_add_arglist (state, 0, $1); }
         | "esac"      { $$ = grub_script_add_arglist (state, 0, $1); }
         | "fi"        { $$ = grub_script_add_arglist (state, 0, $1); }
         | "for"       { $$ = grub_script_add_arglist (state, 0, $1); }
         | "if"        { $$ = grub_script_add_arglist (state, 0, $1); }
         | "in"        { $$ = grub_script_add_arglist (state, 0, $1); }
         | "select"    { $$ = grub_script_add_arglist (state, 0, $1); }
         | "then"      { $$ = grub_script_add_arglist (state, 0, $1); }
         | "until"     { $$ = grub_script_add_arglist (state, 0, $1); }
         | "while"     { $$ = grub_script_add_arglist (state, 0, $1); }
         | "function"  { $$ = grub_script_add_arglist (state, 0, $1); }
         | word { $$ = $1; }
;

/*
  Block parameter is passed to commands in two forms: as unparsed
  string and as pre-parsed grub_script object.  Passing as grub_script
  object makes memory management difficult, because:

  (1) Command may want to keep a reference to grub_script objects for
      later use, so script framework may not free the grub_script
      object after command completes.

  (2) Command may get called multiple times with same grub_script
      object under loops, so we should not let command implementation
      to free the grub_script object.

  To solve above problems, we rely on reference counting for
  grub_script objects.  Commands that want to keep the grub_script
  object must take a reference to it.

  Other complexity comes with arbitrary nesting of grub_script
  objects: a grub_script object may have commands with several block
  parameters, and each block parameter may further contain multiple
  block parameters nested.  We use temporary variable, state->scripts
  to collect nested child scripts (that are linked by siblings and
  children members), and will build grub_scripts tree from bottom.
 */
block: "{"
       {
         grub_script_lexer_ref (state->lexerstate);
         $<offset>$ = grub_script_lexer_record_start (state);
	 $<memory>$ = grub_script_mem_record (state);

	 /* save currently known scripts.  */
	 $<scripts>$ = state->scripts;
	 state->scripts = 0;
       }
       commands1 delimiters0 "}"
       {
         char *p;
	 struct grub_script_mem *memory;
	 struct grub_script *s = $<scripts>2;

	 memory = grub_script_mem_record_stop (state, $<memory>2);
         if ((p = grub_script_lexer_record_stop (state, $<offset>2)))
	   *grub_strrchr (p, '}') = '\0';

	 $$ = grub_script_arg_add (state, 0, GRUB_SCRIPT_ARG_TYPE_BLOCK, p);
	 if (! $$ || ! ($$->script = grub_script_create ($3, memory)))
	   grub_script_mem_free (memory);

	 else {
	   /* attach nested scripts to $$->script as children */
	   $$->script->children = state->scripts;

	   /* restore old scripts; append $$->script to siblings. */
	   state->scripts = $<scripts>2 ?: $$->script;
	   if (s) {
	     while (s->next_siblings)
	       s = s->next_siblings;
	     s->next_siblings = $$->script;
	   }
	 }

         grub_script_lexer_deref (state->lexerstate);
       }
;
block0: /* Empty */ { $$ = 0; }
      | block { $$ = $1; }
;

arguments0: /* Empty */ { $$ = 0; }
          | arguments1  { $$ = $1; }
;
arguments1: argument arguments0
            {
	      if ($1 && $2)
		{
		  $1->next = $2;
		  $1->argcount += $2->argcount;
		  $2->argcount = 0;
		}
	      $$ = $1;
            }
;

grubcmd: word arguments0 block0
         {
	   struct grub_script_arglist *x = $2;

	   if ($3)
	     x = grub_script_add_arglist (state, $2, $3);

           if ($1 && x) {
             $1->next = x;
             $1->argcount += x->argcount;
             x->argcount = 0;
           }
           $$ = grub_script_create_cmdline (state, $1);
         }
;

/* A single command.  */
command: grubcmd  { $$ = $1; }
       | ifcmd    { $$ = $1; }
       | forcmd   { $$ = $1; }
       | whilecmd { $$ = $1; }
       | untilcmd { $$ = $1; }
;

/* A list of commands. */
commands1: newlines0 command
           {
             $$ = grub_script_append_cmd (state, 0, $2);
           }
         | commands1 delimiters1 command
           {
	     $$ = grub_script_append_cmd (state, $1, $3);
           }
;

function: "function" "name"
          {
            grub_script_lexer_ref (state->lexerstate);
            state->func_mem = grub_script_mem_record (state);

	    $<scripts>$ = state->scripts;
	    state->scripts = 0;
          }
          newlines0 "{" commands1 delimiters1 "}"
          {
            struct grub_script *script;
            state->func_mem = grub_script_mem_record_stop (state,
                                                           state->func_mem);
            script = grub_script_create ($6, state->func_mem);
            if (! script)
	      grub_script_mem_free (state->func_mem);
	    else {
	      script->children = state->scripts;
	      if (!grub_script_function_create ($2, script))
		grub_script_free (script);
	    }

	    state->scripts = $<scripts>3;
            grub_script_lexer_deref (state->lexerstate);
          }
;

ifcmd: "if"
	{
	  grub_script_lexer_ref (state->lexerstate);
	}
	ifclause "fi"
	{
	  $$ = $3;
	  grub_script_lexer_deref (state->lexerstate);
	}
;
ifclause: commands1 delimiters1 "then" commands1 delimiters1
	  {
	    $$ = grub_script_create_cmdif (state, $1, $4, 0);
	  }
	| commands1 delimiters1 "then" commands1 delimiters1 "else" commands1 delimiters1
	  {
	    $$ = grub_script_create_cmdif (state, $1, $4, $7);
	  }
	| commands1 delimiters1 "then" commands1 delimiters1 "elif" ifclause
	  {
	    $$ = grub_script_create_cmdif (state, $1, $4, $7);
	  }
;

forcmd: "for" "name"
        {
	  grub_script_lexer_ref (state->lexerstate);
        }
        "in" arguments0 delimiters1 "do" commands1 delimiters1 "done"
	{
	  $$ = grub_script_create_cmdfor (state, $2, $5, $8);
	  grub_script_lexer_deref (state->lexerstate);
	}
;

whilecmd: "while"
          {
	    grub_script_lexer_ref (state->lexerstate);
          }
          commands1 delimiters1 "do" commands1 delimiters1 "done"
	  {
	    $$ = grub_script_create_cmdwhile (state, $3, $6, 0);
	    grub_script_lexer_deref (state->lexerstate);
	  }
;

untilcmd: "until"
          {
	    grub_script_lexer_ref (state->lexerstate);
          }
          commands1 delimiters1 "do" commands1 delimiters1 "done"
	  {
	    $$ = grub_script_create_cmdwhile (state, $3, $6, 1);
	    grub_script_lexer_deref (state->lexerstate);
	  }
;
