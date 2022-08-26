 /*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019  Free Software Foundation, Inc.
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
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/conv.h>
#include <grub/conv_private.h>
#include <grub/env.h>
#include <grub/lua.h>

GRUB_MOD_LICENSE ("GPLv3+");

/* by Cnangel https://github.com/KyleRicardo/strnormalize */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const unsigned short *_pTrad2Simp_gbk;
static const unsigned short *_pTrad2Simp_utf16;
static const unsigned short *_pPlain_gbk;
static const unsigned short *_pPlain_utf16;
static const char *_pUpper2Lower;
static const char *_pLower2Upper;
static const char *_pPlain;
static const unsigned short *_pGbk2Utf16;
static const unsigned short *_pUtf162Gbk;

static const unsigned short *_pTns;
static unsigned short _tns_size;
static const unsigned short *_pGbk2utf16_2;
static unsigned short _gbk2utf16_2_size;
static const unsigned short *_pGbk2utf16_3;
static unsigned short _gbk2utf16_3_size;

static const unsigned short *_initTns(void)
{
	_tns_size = sizeof(_tns) / sizeof(short);
	return _tns;
}

static const unsigned short *_initGbk2utf16_2(void)
{
	_gbk2utf16_2_size = sizeof(_gbk2utf16_2) / sizeof(short);
	return _gbk2utf16_2;
}

static const unsigned short *_initGbk2utf16_3(void)
{
	_gbk2utf16_3_size = sizeof(_gbk2utf16_3) / sizeof(short);
	return _gbk2utf16_3;
}

static const unsigned short *_initTrad2Simp_utf16(void)
{
	unsigned short *gbk2utf16 = (unsigned short *)malloc(0x10000);
	unsigned short n;
	unsigned short d;
	unsigned short c = 0;
	n = _gbk2utf16_2_size;

	for (c = 0; c < n; c += 2)
		gbk2utf16[_pGbk2utf16_2[c] - 0x8000] = _pGbk2utf16_2[c + 1];

	n = _gbk2utf16_3_size;

	for (c = 0; c < n; c += 3)
		for (d = _pGbk2utf16_3[c]; d <= _pGbk2utf16_3[c + 1]; d ++)
			gbk2utf16[d - 0x8000] = _pGbk2utf16_3[c + 2] + d - _pGbk2utf16_3[c];

	static unsigned short t2s[0x10000];

	for (c = 1; c; c ++)
		t2s[c] = c;

	t2s[0] = 0;
	n = _tns_size;

	for (c = 0; c < n; c += 2)

		// do not convert GB2312 (Simplified Chinese)
		if (!((_pTns[c] >> 8) >= 0xA1 && (_pTns[c] >> 8) < 0xF8
		      && (_pTns[c] & 0xFF) >= 0xA1))
			t2s[gbk2utf16[_pTns[c] - 0x8000]] = gbk2utf16[_pTns[c + 1] - 0x8000];

	free(gbk2utf16);
	return t2s;
}

static const unsigned short *_initTrad2Simp_gbk(void)
{
	static unsigned short t2s[0x8000];
	unsigned short n;
	unsigned short c = 0;

	for (c = 0; c < 0x8000; c ++)
		t2s[c] = SWAPBYTE(c | 0x8000);

	n = _tns_size;

	for (c = 0; c < n; c += 2)

		// do not convert GB2312 (Simplified Chinese)
		if (!((_pTns[c] >> 8) >= 0xA1 && (_pTns[c] >> 8) < 0xF8
		      && (_pTns[c] & 0xFF) >= 0xA1))
			t2s[_pTns[c] - 0x8000] = SWAPBYTE(_pTns[c + 1]);

	return t2s;
}

static const unsigned short *_initPlain_utf16(void)
{
	static unsigned short plain[0x10000];
	unsigned short c = 0;

	for (c = 1; c; c ++)
		plain[c] = c;

	plain[0] = 0;
	return plain;
}

static const unsigned short *_initPlain_gbk(void)
{
	static unsigned short plain[0x8000];
	unsigned short c = 0;

	for (c = 0; c < 0x8000; c ++)
		plain[c] = SWAPBYTE(c | 0x8000);

	return plain;
}

static const char *_initUpper2Lower(void)
{
	static char u2l[0x80];
	unsigned short c = 0;

	for (c = 0; c < 0x80; c ++)
		if (c >= 'A' && c <= 'Z') u2l[c] = c + 'a' - 'A';
		else u2l[c] = c;

	return u2l;
}

static const char *_initLower2Upper(void)
{
	static char l2u[0x80];
	unsigned short c = 0;

	for (c = 0; c < 0x80; c ++)
		if (c >= 'a' && c <= 'z') l2u[c] = c + 'A' - 'a';
		else l2u[c] = c;

	return l2u;
}

static const char *_initPlain(void)
{
	static char plain[0x80];
	unsigned short c = 0;

	for (c = 0; c < 0x80; c ++)
		plain[c] = c;

	return plain;
}

static const unsigned short *_initGbk2Utf16(void)
{
	static unsigned short gbk2utf16[0x8000] = { 0 };
	unsigned short c = 0;
	unsigned short d = 0;

	for (c = 0; c < _gbk2utf16_2_size; c += 2)
		gbk2utf16[_pGbk2utf16_2[c] - 0x8000] = _pGbk2utf16_2[c + 1];

	for (c = 0; c < _gbk2utf16_3_size; c += 3)
		for (d = _pGbk2utf16_3[c]; d <= _pGbk2utf16_3[c + 1]; d ++)
			gbk2utf16[d - 0x8000] = _pGbk2utf16_3[c + 2] + d - _pGbk2utf16_3[c];

	return gbk2utf16;
}

static const unsigned short *_initUtf162Gbk(void)
{
	static unsigned short utf162gbk[0x10000] = { 0 };
	unsigned short c = 0;
	unsigned short d = 0;

	for (c = 0; c <  _gbk2utf16_2_size; c += 2)
		utf162gbk[_pGbk2utf16_2[c + 1]] = _pGbk2utf16_2[c];

	for ( c = 0; c < _gbk2utf16_3_size; c += 3)
		for ( d = _pGbk2utf16_3[c]; d <= _pGbk2utf16_3[c + 1]; d ++)
			utf162gbk[_pGbk2utf16_3[c + 2] + d - _pGbk2utf16_3[c]] = d;

	return utf162gbk;
}

void str_normalize_init()
{
	_pTns               = _initTns();
	_pGbk2utf16_2       = _initGbk2utf16_2();
	_pGbk2utf16_3       = _initGbk2utf16_3();
	_pTrad2Simp_gbk     = _initTrad2Simp_gbk();
	_pTrad2Simp_utf16   = _initTrad2Simp_utf16();
	_pPlain_gbk         = _initPlain_gbk();
	_pPlain_utf16       = _initPlain_utf16();
	_pUpper2Lower       = _initUpper2Lower();
	_pLower2Upper       = _initLower2Upper();
	_pPlain             = _initPlain();
	_pGbk2Utf16       = _initGbk2Utf16();
	_pUtf162Gbk       = _initUtf162Gbk();
}

void str_normalize_utf8(char *text, unsigned options)
{
	const char *pTransTable =
	    (options & SNO_TO_LOWER) ? _pUpper2Lower : (
	        (options & SNO_TO_UPPER) ? _pLower2Upper :
	        _pPlain);
	const unsigned short *pTransTable_utf16 =
	    (options & SNO_TO_SIMPLIFIED) ? _pTrad2Simp_utf16 : _pPlain_utf16;
	unsigned i_from = 0, i_to = 0;

	while (text[i_from])
	{
		if ((text[i_from] & 0x80) == 0)
		{
			text[i_to ++] = pTransTable[(unsigned int)text[i_from ++]];
		}
		else if ((text[i_from] & 0xF0) == 0xE0
		         && (text[i_from + 1] & 0xC0) == 0x80
		         && (text[i_from + 2] & 0xC0) == 0x80)
		{
			unsigned short utf16 = (unsigned short)(text[i_from] & 0x0F) << 12
			                       | (unsigned short)(text[i_from + 1] & 0x3F) << 6
			                       | (unsigned short)(text[i_from + 2] & 0x3F);

			if (options & SNO_TO_HALF)
			{
				if (utf16 == 0x3001)
					utf16 = ' ';
				else if (utf16 > 0xFF00 && utf16 < 0xFF60)
					utf16 = ' ' + (utf16 & 0xFF);
			}

			if (utf16 < 0x80)
			{
				utf16 = pTransTable[utf16];
				text[i_to ++] = utf16;
			}
			else
			{
				utf16 = pTransTable_utf16[utf16];
				text[i_to ++] = (utf16 & 0xF000) >> 12 | 0xE0;
				text[i_to ++] = (utf16 & 0x0FC0) >>  6 | 0x80;
				text[i_to ++] = (utf16 & 0x003F)       | 0x80;
			}

			i_from += 3;
		}
		else if ((text[i_from] & 0xE0) == 0xC0
		         && (text[i_from + 1] & 0xC0) == 0x80)
		{
			text[i_to ++] = text[i_from ++];
			text[i_to ++] = text[i_from ++];
		}
		else
		{
			text[i_to ++] = text[i_from ++];
		}
	}

	text[i_to] = 0;
}

int gbk_to_utf8(const char *from, unsigned int from_len, char **to, unsigned int *to_len)
{
	char *result = *to;
	unsigned i_from = 0;
	unsigned i_to = 0;
	unsigned flag = 0;

	if (from_len == 0 || from == NULL || to == NULL || result == NULL)
	{
		return -1;
	}

	for (i_from = 0; i_from < from_len; i_from ++)
	{
		if (flag)
		{
			flag = 0;
			unsigned short tmp =
			    _pGbk2Utf16[COMPBYTE(from[i_from - 1], from[i_from]) & ~0x8000];

			if (tmp == 0)
				continue;
			else if (tmp >= 0x800)
			{
				result[i_to ++] = 0xE0 | (tmp >> 12);
				result[i_to ++] = 0x80 | ((tmp >> 6) & 0x3F);
				result[i_to ++] = 0x80 | (tmp & 0x3F);
			}
			else if (tmp >= 0x80)
			{
				result[i_to ++] = 0xC0 | (tmp >> 6);
				result[i_to ++] = 0x80 | (tmp & 0x3F);
			}
			else
			{
				result[i_to ++] = tmp;
			}
		}
		else if (from[i_from] < 0)
			flag = 1;
		else
			result[i_to ++] = from[i_from];
	}

	result[i_to] = 0;
	*to_len = i_to;
	return 0;
}


int utf8_to_gbk(const char *from, unsigned int from_len, char **to, unsigned int *to_len)
{
	char *result = *to;
	unsigned i_from = 0;
	unsigned i_to = 0;

	if (from_len == 0 || from == NULL || to == NULL || result == NULL)
	{
		return -1;
	}

	for (i_from = 0; i_from < from_len; )
	{
		if ((unsigned char)from[i_from] < 0x80)
		{
			result[i_to ++] = from[i_from ++];
		}
		else if ((unsigned char)from[i_from] < 0xC2)
		{
			i_from ++;
		}
		else if ((unsigned char)from[i_from] < 0xE0)
		{
			if (i_from >= from_len - 1) break;

			unsigned short tmp = _pUtf162Gbk[
			                         ((from[i_from] & 0x1F) << 6) | (from[i_from + 1] & 0x3F)];

			if (tmp)
			{
				result[i_to ++] = tmp >> 8;
				result[i_to ++] = tmp & 0xFF;
			}

			i_from += 2;
		}
		else if ((unsigned char)from[i_from] < 0xF0)
		{
			if (i_from >= from_len - 2) break;

			unsigned short tmp = _pUtf162Gbk[((from[i_from] & 0x0F) << 12)
			                                 | ((from[i_from + 1] & 0x3F) << 6) | (from[i_from + 2] & 0x3F)];

			if (tmp)
			{
				result[i_to ++] = tmp >> 8;
				result[i_to ++] = tmp & 0xFF;
			}

			i_from += 3;
		}
		else
		{
			i_from += 4;
		}
	}

	result[i_to] = 0;
	*to_len = i_to;
	return 0;
}

static const struct grub_arg_option options_conv[] = {
  {"gbk", 'g', 0, N_("UTF-8 to GBK"), 0, 0},
  {"utf8", 'u', 0, N_("GBK to UTF-8 [default]"), 0, 0},
  {"set", 's', 0,
      N_("Set a variable to return value."), N_("VARNAME"), ARG_TYPE_STRING},
  {0, 0, 0, 0, 0, 0}
};

enum options_conv
{
  CONV_GBK,
  CONV_UTF8,
  CONV_SET,
};

static grub_err_t
grub_cmd_conv (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "string required");

  uint32_t len = strlen (args[0]);
  uint32_t buf_len;
  char *buffer = NULL;
  if (state[CONV_GBK].set)
  {
    buf_len = len * 2 + 1;
    buffer = (char *) grub_zalloc (buf_len);
    utf8_to_gbk (args[0], len, &buffer, &buf_len);
  }
  else
  {
    buf_len = len * 3 + 1;
    buffer = (char *) grub_zalloc (buf_len);
    gbk_to_utf8 (args[0], len, &buffer, &buf_len);
  }
  if (!buffer)
    return 0;
  if (state[CONV_SET].set)
    grub_env_set (state[CONV_SET].arg, buffer);
  else
    grub_printf ("%s\n", buffer);
  free (buffer);
  return 0;
}

static grub_extcmd_t cmd;

static int lua_gbk_fromutf8(lua_State *state)
{
  const char *str;
  char *buffer = NULL;
  uint32_t len, buf_len;
  str = luaL_checkstring (state, 1);
  len = strlen (str);
  buf_len = len * 2 + 1;
  buffer = (char *) grub_zalloc (buf_len);
  if (!buffer)
    return 0;
  utf8_to_gbk (str, len, &buffer, &buf_len);
  lua_pushstring (state, buffer);
  grub_free (buffer);
  return 1;
}

static int lua_gbk_toutf8 (lua_State *state)
{
  const char *str;
  char *buffer = NULL;
  uint32_t len, buf_len;
  str = luaL_checkstring (state, 1);
  len = strlen (str);
  buf_len = len * 3 + 1;
  buffer = (char *) grub_zalloc (buf_len);
  if (!buffer)
    return 0;
  gbk_to_utf8 (str, len, &buffer, &buf_len);
  lua_pushstring (state, buffer);
  grub_free (buffer);
  return 1;
}

static int lua_gbk_tosimp (lua_State *state)
{
  const char *str;
  char *buf = NULL;
  str = luaL_checkstring (state, 1);
  buf = grub_strdup (str);
  if (!buf)
    return 0;
  str_normalize_utf8 (buf, SNO_TO_SIMPLIFIED);
  lua_pushstring (state, buf);
  grub_free (buf);
  return 1;
}

static luaL_Reg gbklib[] =
{
  {"fromutf8", lua_gbk_fromutf8},
  {"toutf8", lua_gbk_toutf8},
  {"tosimp", lua_gbk_tosimp},
  {0, 0}
};


GRUB_MOD_INIT(conv)
{
  str_normalize_init();
  cmd = grub_register_extcmd ("strconv", grub_cmd_conv, 0, 0,
                              N_("convert string between GBK and UTF-8."),
                              options_conv);
  if (grub_lua_global_state)
  {
    lua_gc (grub_lua_global_state, LUA_GCSTOP, 0);
    luaL_register (grub_lua_global_state, "gbk", gbklib);
    lua_gc (grub_lua_global_state, LUA_GCRESTART, 0);
  }
}

GRUB_MOD_FINI(conv)
{
  grub_unregister_extcmd (cmd);
}
