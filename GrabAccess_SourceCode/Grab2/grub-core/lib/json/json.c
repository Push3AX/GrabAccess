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

#include <grub/dl.h>
#include <grub/mm.h>

#define JSMN_STATIC
#include "jsmn.h"
#include "json.h"

GRUB_MOD_LICENSE ("GPLv3");

grub_err_t
grub_json_parse (grub_json_t **out, char *string, grub_size_t string_len)
{
  grub_json_t *json = NULL;
  jsmn_parser parser;
  grub_err_t ret = GRUB_ERR_NONE;
  int jsmn_ret;

  if (!string)
    return GRUB_ERR_BAD_ARGUMENT;

  json = grub_zalloc (sizeof (*json));
  if (!json)
    return GRUB_ERR_OUT_OF_MEMORY;
  json->string = string;

  /*
   * Parse the string twice: first to determine how many tokens
   * we need to allocate, second to fill allocated tokens.
   */
  jsmn_init (&parser);
  jsmn_ret = jsmn_parse (&parser, string, string_len, NULL, 0);
  if (jsmn_ret <= 0)
    {
      ret = GRUB_ERR_BAD_ARGUMENT;
      goto err;
    }

  json->tokens = grub_calloc (jsmn_ret, sizeof (jsmntok_t));
  if (!json->tokens)
    {
      ret = GRUB_ERR_OUT_OF_MEMORY;
      goto err;
    }

  jsmn_init (&parser);
  jsmn_ret = jsmn_parse (&parser, string, string_len, json->tokens, jsmn_ret);
  if (jsmn_ret <= 0)
    {
      ret = GRUB_ERR_BAD_ARGUMENT;
      goto err;
    }

  *out = json;

 err:
  if (ret)
    grub_json_free (json);

  return ret;
}

void
grub_json_free (grub_json_t *json)
{
  if (json)
    {
      grub_free (json->tokens);
      grub_free (json);
    }
}

grub_err_t
grub_json_getsize (grub_size_t *out, const grub_json_t *json)
{
  int size;

  size = json->tokens[json->idx].size;
  if (size < 0)
    return GRUB_ERR_OUT_OF_RANGE;

  *out = (grub_size_t) size;
  return GRUB_ERR_NONE;
}

grub_err_t
grub_json_gettype (grub_json_type_t *out, const grub_json_t *json)
{
  switch (json->tokens[json->idx].type)
    {
    case JSMN_OBJECT:
      *out = GRUB_JSON_OBJECT;
      break;
    case JSMN_ARRAY:
      *out = GRUB_JSON_ARRAY;
      break;
    case JSMN_STRING:
      *out = GRUB_JSON_STRING;
      break;
    case JSMN_PRIMITIVE:
      *out = GRUB_JSON_PRIMITIVE;
      break;
    default:
      return GRUB_ERR_BAD_ARGUMENT;
    }

  return GRUB_ERR_NONE;
}

grub_err_t
grub_json_getchild (grub_json_t *out, const grub_json_t *parent, grub_size_t n)
{
  grub_size_t offset = 1, size;
  jsmntok_t *p;

  if (grub_json_getsize (&size, parent) || n >= size)
    return GRUB_ERR_OUT_OF_RANGE;

  /*
   * Skip the first n children. For each of the children, we need
   * to skip their own potential children (e.g. if it's an
   * array), as well. We thus add the children's size to n on
   * each iteration.
   */
  p = &parent->tokens[parent->idx];
  while (n--)
    n += p[offset++].size;

  out->string = parent->string;
  out->tokens = parent->tokens;
  out->idx = parent->idx + offset;

  return GRUB_ERR_NONE;
}

grub_err_t
grub_json_getvalue (grub_json_t *out, const grub_json_t *parent, const char *key)
{
  grub_json_type_t type;
  grub_size_t i, size;

  if (grub_json_gettype (&type, parent) || type != GRUB_JSON_OBJECT)
    return GRUB_ERR_BAD_ARGUMENT;

  if (grub_json_getsize (&size, parent))
    return GRUB_ERR_BAD_ARGUMENT;

  for (i = 0; i < size; i++)
    {
      grub_json_t child;
      const char *s;

      if (grub_json_getchild (&child, parent, i) ||
	  grub_json_getstring (&s, &child, NULL) ||
          grub_strcmp (s, key) != 0)
	continue;

      return grub_json_getchild (out, &child, 0);
    }

  return GRUB_ERR_FILE_NOT_FOUND;
}

static grub_err_t
get_value (grub_json_type_t *out_type, const char **out_string, const grub_json_t *parent, const char *key)
{
  const grub_json_t *p = parent;
  grub_json_t child;
  grub_err_t ret;
  jsmntok_t *tok;

  if (key)
    {
      ret = grub_json_getvalue (&child, parent, key);
      if (ret)
	return ret;
      p = &child;
    }

  tok = &p->tokens[p->idx];
  p->string[tok->end] = '\0';

  *out_string = p->string + tok->start;

  return grub_json_gettype (out_type, p);
}

grub_err_t
grub_json_getstring (const char **out, const grub_json_t *parent, const char *key)
{
  grub_json_type_t type;
  const char *value;
  grub_err_t ret;

  ret = get_value (&type, &value, parent, key);
  if (ret)
    return ret;
  if (type != GRUB_JSON_STRING)
    return GRUB_ERR_BAD_ARGUMENT;

  *out = value;
  return GRUB_ERR_NONE;
}

grub_err_t
grub_json_getuint64 (grub_uint64_t *out, const grub_json_t *parent, const char *key)
{
  grub_json_type_t type;
  const char *value;
  const char *end;
  grub_err_t ret;

  ret = get_value (&type, &value, parent, key);
  if (ret)
    return ret;
  if (type != GRUB_JSON_STRING && type != GRUB_JSON_PRIMITIVE)
    return GRUB_ERR_BAD_ARGUMENT;

  grub_errno = GRUB_ERR_NONE;
  *out = grub_strtoul (value, &end, 10);
  if (grub_errno != GRUB_ERR_NONE || *end)
    return GRUB_ERR_BAD_NUMBER;

  return GRUB_ERR_NONE;
}

grub_err_t
grub_json_getint64 (grub_int64_t *out, const grub_json_t *parent, const char *key)
{
  grub_json_type_t type;
  const char *value;
  const char *end;
  grub_err_t ret;

  ret = get_value (&type, &value, parent, key);
  if (ret)
    return ret;
  if (type != GRUB_JSON_STRING && type != GRUB_JSON_PRIMITIVE)
    return GRUB_ERR_BAD_ARGUMENT;

  grub_errno = GRUB_ERR_NONE;
  *out = grub_strtol (value, &end, 10);
  if (grub_errno != GRUB_ERR_NONE || *end)
    return GRUB_ERR_BAD_NUMBER;

  return GRUB_ERR_NONE;
}
