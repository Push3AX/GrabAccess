/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1998-2003,2004,2005 Free Software Foundation, Inc.
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

/**********************************************************************
 * This code is a modification of lib_tparm.c found in ncurses-5.2. The
 * modification are for use in grub by replacing all libc function through
 * special grub functions. This also meant to delete all dynamic memory
 * allocation and replace it by a number of fixed buffers.
 *
 * Modifications by Tilmann Bubeck <t.bubeck@reinform.de> 2002
 *
 * Resync with ncurses-5.4 by Omniflux <omniflux+devel@omniflux.com> 2005
 **********************************************************************/

/****************************************************************************
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 *     and: Thomas E. Dickey, 1996 on                                       *
 ****************************************************************************/

/*
 *	tparm.c
 *
 */

#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/types.h>
#include <grub/tparm.h>

/*
 * Common/troublesome character definitions
 */
typedef char grub_bool_t;
#ifndef FALSE
# define FALSE (0)
#endif
#ifndef TRUE
# define TRUE (!FALSE)
#endif

#define NUM_PARM 9
#define NUM_VARS 26
#define STACKSIZE 20
#define MAX_FORMAT_LEN 256

#define max(a,b) ((a) > (b) ? (a) : (b))
#define isdigit(c) ((c) >= '0' && (c) <= '9')
#define isUPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define isLOWER(c) ((c) >= 'a' && (c) <= 'z')

#define UChar(c) ((unsigned char)(c))

//MODULE_ID("$Id$")

/*
 *	char *
 *	tparm(string, ...)
 *
 *	Substitute the given parameters into the given string by the following
 *	rules (taken from terminfo(5)):
 *
 *	     Cursor addressing and other strings  requiring  parame-
 *	ters in the terminal are described by a parameterized string
 *	capability, with like escapes %x in  it.   For  example,  to
 *	address  the  cursor, the cup capability is given, using two
 *	parameters: the row and column to  address  to.   (Rows  and
 *	columns  are  numbered  from  zero and refer to the physical
 *	screen visible to the user, not to any  unseen  memory.)  If
 *	the terminal has memory relative cursor addressing, that can
 *	be indicated by
 *
 *	     The parameter mechanism uses  a  stack  and  special  %
 *	codes  to manipulate it.  Typically a sequence will push one
 *	of the parameters onto the stack and then print it  in  some
 *	format.  Often more complex operations are necessary.
 *
 *	     The % encodings have the following meanings:
 *
 *	     %%        outputs `%'
 *	     %c        print pop() like %c in printf()
 *	     %s        print pop() like %s in printf()
 *           %[[:]flags][width[.precision]][doxXs]
 *                     as in printf, flags are [-+#] and space
 *                     The ':' is used to avoid making %+ or %-
 *                     patterns (see below).
 *
 *	     %p[1-9]   push ith parm
 *	     %P[a-z]   set dynamic variable [a-z] to pop()
 *	     %g[a-z]   get dynamic variable [a-z] and push it
 *	     %P[A-Z]   set static variable [A-Z] to pop()
 *	     %g[A-Z]   get static variable [A-Z] and push it
 *	     %l        push strlen(pop)
 *	     %'c'      push char constant c
 *	     %{nn}     push integer constant nn
 *
 *	     %+ %- %* %/ %m
 *	               arithmetic (%m is mod): push(pop() op pop())
 *	     %& %| %^  bit operations: push(pop() op pop())
 *	     %= %> %<  logical operations: push(pop() op pop())
 *	     %A %O     logical and & or operations for conditionals
 *	     %! %~     unary operations push(op pop())
 *	     %i        add 1 to first two parms (for ANSI terminals)
 *
 *	     %? expr %t thenpart %e elsepart %;
 *	               if-then-else, %e elsepart is optional.
 *	               else-if's are possible ala Algol 68:
 *	               %? c1 %t b1 %e c2 %t b2 %e c3 %t b3 %e c4 %t b4 %e b5 %;
 *
 *	For those of the above operators which are binary and not commutative,
 *	the stack works in the usual way, with
 *			%gx %gy %m
 *	resulting in x mod y, not the reverse.
 */

typedef struct {
    union {
	int num;
	char *str;
    } data;
    grub_bool_t num_type;
} stack_frame;

static stack_frame stack[STACKSIZE];
static int stack_ptr;
static const char *tparam_base = "";

static char *out_buff;
static grub_size_t out_size;
static grub_size_t out_used;

static char *fmt_buff;
static grub_size_t fmt_size;

static inline void
get_space(grub_size_t need)
{
    need += out_used;
    if (need > out_size) {
	out_size = need * 2;
	out_buff = grub_realloc(out_buff, out_size*sizeof(char));
	/* FIX ME! handle out_buff == 0.  */
    }
}

#pragma GCC diagnostic ignored "-Wformat-nonliteral"

static inline void
save_text(const char *fmt, const char *s, int len)
{
    grub_size_t s_len = grub_strlen(s);
    if (len > (int) s_len)
	s_len = len;

    get_space(s_len + 1);

    (void) grub_snprintf(out_buff + out_used, s_len + 1, fmt, s);
    out_used += grub_strlen(out_buff + out_used);
}

static inline void
save_number(const char *fmt, int number, int len)
{
    if (len < 30)
	len = 30;		/* actually log10(MAX_INT)+1 */

    get_space((unsigned) len + 1);

    (void) grub_snprintf(out_buff + out_used, len + 1, fmt, number);
    out_used += grub_strlen(out_buff + out_used);
}

#pragma GCC diagnostic error "-Wformat-nonliteral"

static inline void
save_char(int c)
{
    if (c == 0)
	c = 0200;
    get_space(1);
    out_buff[out_used++] = c;
}

static inline void
npush(int x)
{
    if (stack_ptr < STACKSIZE) {
	stack[stack_ptr].num_type = TRUE;
	stack[stack_ptr].data.num = x;
	stack_ptr++;
    }
}

static inline int
npop(void)
{
    int result = 0;
    if (stack_ptr > 0) {
	stack_ptr--;
	if (stack[stack_ptr].num_type)
	    result = stack[stack_ptr].data.num;
    }
    return result;
}

static inline void
spush(char *x)
{
    if (stack_ptr < STACKSIZE) {
	stack[stack_ptr].num_type = FALSE;
	stack[stack_ptr].data.str = x;
	stack_ptr++;
    }
}

static inline char *
spop(void)
{
    static char dummy[] = "";	/* avoid const-cast */
    char *result = dummy;
    if (stack_ptr > 0) {
	stack_ptr--;
	if (!stack[stack_ptr].num_type && stack[stack_ptr].data.str != 0)
	    result = stack[stack_ptr].data.str;
    }
    return result;
}

static inline const char *
parse_format(const char *s, char *format, int *len)
{
    *len = 0;
    if (format != 0) {
	grub_bool_t done = FALSE;
	grub_bool_t allowminus = FALSE;
	grub_bool_t dot = FALSE;
	grub_bool_t err = FALSE;
	char *fmt = format;
	int my_width = 0;
	int my_prec = 0;
	int value = 0;

	*len = 0;
	*format++ = '%';
	while (*s != '\0' && !done) {
	    switch (*s) {
	    case 'c':		/* FALLTHRU */
	    case 'd':		/* FALLTHRU */
	    case 'o':		/* FALLTHRU */
	    case 'x':		/* FALLTHRU */
	    case 'X':		/* FALLTHRU */
	    case 's':
		*format++ = *s;
		done = TRUE;
		break;
	    case '.':
		*format++ = *s++;
		if (dot) {
		    err = TRUE;
		} else {	/* value before '.' is the width */
		    dot = TRUE;
		    my_width = value;
		}
		value = 0;
		break;
	    case '#':
		*format++ = *s++;
		break;
	    case ' ':
		*format++ = *s++;
		break;
	    case ':':
		s++;
		allowminus = TRUE;
		break;
	    case '-':
		if (allowminus) {
		    *format++ = *s++;
		} else {
		    done = TRUE;
		}
		break;
	    default:
		if (isdigit(UChar(*s))) {
		    value = (value * 10) + (*s - '0');
		    if (value > 10000)
			err = TRUE;
		    *format++ = *s++;
		} else {
		    done = TRUE;
		}
	    }
	}

	/*
	 * If we found an error, ignore (and remove) the flags.
	 */
	if (err) {
	    my_width = my_prec = value = 0;
	    format = fmt;
	    *format++ = '%';
	    *format++ = *s;
	}

	/*
	 * Any value after '.' is the precision.  If we did not see '.', then
	 * the value is the width.
	 */
	if (dot)
	    my_prec = value;
	else
	    my_width = value;

	*format = '\0';
	/* return maximum string length in print */
	*len = (my_width > my_prec) ? my_width : my_prec;
    }
    return s;
}

/*
 * Analyze the string to see how many parameters we need from the varargs list,
 * and what their types are.  We will only accept string parameters if they
 * appear as a %l or %s format following an explicit parameter reference (e.g.,
 * %p2%s).  All other parameters are numbers.
 *
 * 'number' counts coarsely the number of pop's we see in the string, and
 * 'popcount' shows the highest parameter number in the string.  We would like
 * to simply use the latter count, but if we are reading termcap strings, there
 * may be cases that we cannot see the explicit parameter numbers.
 */
static inline int
analyze(const char *string, char *p_is_s[NUM_PARM], int *popcount)
{
    grub_size_t len2;
    int i;
    int lastpop = -1;
    int len;
    int number = 0;
    const char *cp = string;
    static char dummy[] = "";

    *popcount = 0;

    if (cp == 0)
	return 0;

    if ((len2 = grub_strlen(cp)) > fmt_size) {
	fmt_size = len2 + fmt_size + 2;
	if ((fmt_buff = grub_realloc(fmt_buff, fmt_size*sizeof(char))) == 0)
	      return 0;
    }

    grub_memset(p_is_s, 0, sizeof(p_is_s[0]) * NUM_PARM);

    while ((cp - string) < (int) len2) {
	if (*cp == '%') {
	    cp++;
	    cp = parse_format(cp, fmt_buff, &len);
	    switch (*cp) {
	    default:
		break;

	    case 'd':		/* FALLTHRU */
	    case 'o':		/* FALLTHRU */
	    case 'x':		/* FALLTHRU */
	    case 'X':		/* FALLTHRU */
	    case 'c':		/* FALLTHRU */
		if (lastpop <= 0)
		    number++;
		lastpop = -1;
		break;

	    case 'l':
	    case 's':
		if (lastpop > 0)
		    p_is_s[lastpop - 1] = dummy;
		++number;
		break;

	    case 'p':
		cp++;
		i = (UChar(*cp) - '0');
		if (i >= 0 && i <= NUM_PARM) {
		    lastpop = i;
		    if (lastpop > *popcount)
			*popcount = lastpop;
		}
		break;

	    case 'P':
		++number;
		++cp;
		break;

	    case 'g':
		cp++;
		break;

	    case '\'':
		cp += 2;
		lastpop = -1;
		break;

	    case '{':
		cp++;
		while (isdigit(UChar(*cp))) {
		    cp++;
		}
		break;

	    case '+':
	    case '-':
	    case '*':
	    case '/':
	    case 'm':
	    case 'A':
	    case 'O':
	    case '&':
	    case '|':
	    case '^':
	    case '=':
	    case '<':
	    case '>':
		lastpop = -1;
		number += 2;
		break;

	    case '!':
	    case '~':
		lastpop = -1;
		++number;
		break;

	    case 'i':
		/* will add 1 to first (usually two) parameters */
		break;
	    }
	}
	if (*cp != '\0')
	    cp++;
    }

    if (number > NUM_PARM)
	number = NUM_PARM;
    return number;
}

static inline char *
tparam_internal(const char *string, va_list ap)
{
    char *p_is_s[NUM_PARM];
    long param[NUM_PARM];
    int popcount;
    int number;
    int len;
    int level;
    int x, y;
    int i;
    const char *cp = string;
    grub_size_t len2;
    static int dynamic_var[NUM_VARS];
    static int static_vars[NUM_VARS];

    if (cp == 0)
	return 0;

    out_used = out_size = fmt_size = 0;

    len2 = (int) grub_strlen(cp);

    /*
     * Find the highest parameter-number referred to in the format string.
     * Use this value to limit the number of arguments copied from the
     * variable-length argument list.
     */
    number = analyze(cp, p_is_s, &popcount);
    if (fmt_buff == 0)
	return 0;

    for (i = 0; i < max(popcount, number); i++) {
	/*
	 * A few caps (such as plab_norm) have string-valued parms.
	 * We'll have to assume that the caller knows the difference, since
	 * a char* and an int may not be the same size on the stack.
	 */
	if (p_is_s[i] != 0) {
	    p_is_s[i] = va_arg(ap, char *);
	} else {
	    param[i] = va_arg(ap, long int);
	}
    }

    /*
     * This is a termcap compatibility hack.  If there are no explicit pop
     * operations in the string, load the stack in such a way that
     * successive pops will grab successive parameters.  That will make
     * the expansion of (for example) \E[%d;%dH work correctly in termcap
     * style, which means tparam() will expand termcap strings OK.
     */
    stack_ptr = 0;
    if (popcount == 0) {
	popcount = number;
	for (i = number - 1; i >= 0; i--)
	    npush(param[i]);
    }

    while ((cp - string) < (int) len2) {
	if (*cp != '%') {
	    save_char(UChar(*cp));
	} else {
	    tparam_base = cp++;
	    cp = parse_format(cp, fmt_buff, &len);
	    switch (*cp) {
	    default:
		break;
	    case '%':
		save_char('%');
		break;

	    case 'd':		/* FALLTHRU */
	    case 'o':		/* FALLTHRU */
	    case 'x':		/* FALLTHRU */
	    case 'X':		/* FALLTHRU */
		save_number(fmt_buff, npop(), len);
		break;

	    case 'c':		/* FALLTHRU */
		save_char(npop());
		break;

	    case 'l':
		save_number("%d", (int) grub_strlen(spop()), 0);
		break;

	    case 's':
		save_text(fmt_buff, spop(), len);
		break;

	    case 'p':
		cp++;
		i = (UChar(*cp) - '1');
		if (i >= 0 && i < NUM_PARM) {
		    if (p_is_s[i])
			spush(p_is_s[i]);
		    else
			npush(param[i]);
		}
		break;

	    case 'P':
		cp++;
		if (isUPPER(*cp)) {
		    i = (UChar(*cp) - 'A');
		    static_vars[i] = npop();
		} else if (isLOWER(*cp)) {
		    i = (UChar(*cp) - 'a');
		    dynamic_var[i] = npop();
		}
		break;

	    case 'g':
		cp++;
		if (isUPPER(*cp)) {
		    i = (UChar(*cp) - 'A');
		    npush(static_vars[i]);
		} else if (isLOWER(*cp)) {
		    i = (UChar(*cp) - 'a');
		    npush(dynamic_var[i]);
		}
		break;

	    case '\'':
		cp++;
		npush(UChar(*cp));
		cp++;
		break;

	    case '{':
		number = 0;
		cp++;
		while (isdigit(UChar(*cp))) {
		    number = (number * 10) + (UChar(*cp) - '0');
		    cp++;
		}
		npush(number);
		break;

	    case '+':
		npush(npop() + npop());
		break;

	    case '-':
		y = npop();
		x = npop();
		npush(x - y);
		break;

	    case '*':
		npush(npop() * npop());
		break;

	    case '/':
		y = npop();
		x = npop();
		/* GRUB has no signed divisions. */
		npush(y ? ((unsigned)x / (unsigned)y) : 0);
		break;

	    case 'm':
		y = npop();
		x = npop();
		/* GRUB has no signed divisions. */
		npush(y ? ((unsigned)x % (unsigned)y) : 0);
		break;

	    case 'A':
		npush(npop() && npop());
		break;

	    case 'O':
		npush(npop() || npop());
		break;

	    case '&':
		npush(npop() & npop());
		break;

	    case '|':
		npush(npop() | npop());
		break;

	    case '^':
		npush(npop() ^ npop());
		break;

	    case '=':
		y = npop();
		x = npop();
		npush(x == y);
		break;

	    case '<':
		y = npop();
		x = npop();
		npush(x < y);
		break;

	    case '>':
		y = npop();
		x = npop();
		npush(x > y);
		break;

	    case '!':
		npush(!npop());
		break;

	    case '~':
		npush(~npop());
		break;

	    case 'i':
		if (p_is_s[0] == 0)
		    param[0]++;
		if (p_is_s[1] == 0)
		    param[1]++;
		break;

	    case '?':
		break;

	    case 't':
		x = npop();
		if (!x) {
		    /* scan forward for %e or %; at level zero */
		    cp++;
		    level = 0;
		    while (*cp) {
			if (*cp == '%') {
			    cp++;
			    if (*cp == '?')
				level++;
			    else if (*cp == ';') {
				if (level > 0)
				    level--;
				else
				    break;
			    } else if (*cp == 'e' && level == 0)
				break;
			}

			if (*cp)
			    cp++;
		    }
		}
		break;

	    case 'e':
		/* scan forward for a %; at level zero */
		cp++;
		level = 0;
		while (*cp) {
		    if (*cp == '%') {
			cp++;
			if (*cp == '?')
			    level++;
			else if (*cp == ';') {
			    if (level > 0)
				level--;
			    else
				break;
			}
		    }

		    if (*cp)
			cp++;
		}
		break;

	    case ';':
		break;

	    }			/* endswitch (*cp) */
	}			/* endelse (*cp == '%') */

	if (*cp == '\0')
	    break;

	cp++;
    }				/* endwhile (*cp) */

    get_space(1);
    out_buff[out_used] = '\0';

    return (out_buff);
}

const char *
grub_terminfo_tparm (const char *string, ...)
{
    va_list ap;
    char *result;

    if (!string)
      return "";

    va_start (ap, string);
    result = tparam_internal (string, ap);
    va_end (ap);
    return result;
}
