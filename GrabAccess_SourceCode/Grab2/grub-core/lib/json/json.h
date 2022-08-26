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

#ifndef GRUB_JSON_JSON_H
#define GRUB_JSON_JSON_H	1

#include <grub/types.h>

enum grub_json_type
{
  /* Unordered collection of key-value pairs. */
  GRUB_JSON_OBJECT,
  /* Ordered list of zero or more values. */
  GRUB_JSON_ARRAY,
  /* Zero or more Unicode characters. */
  GRUB_JSON_STRING,
  /* Number, boolean or empty value. */
  GRUB_JSON_PRIMITIVE,
  /* Invalid token. */
  GRUB_JSON_UNDEFINED,
};
typedef enum grub_json_type grub_json_type_t;

/* Forward-declaration to avoid including jsmn.h. */
struct jsmntok;

struct grub_json
{
  struct jsmntok *tokens;
  char		 *string;
  grub_size_t	 idx;
};
typedef struct grub_json grub_json_t;

/*
 * Parse a JSON-encoded string. Note that the string passed to
 * this function will get modified on subsequent calls to
 * grub_json_get*(). Returns the root object of the parsed JSON
 * object, which needs to be free'd via grub_json_free(). Callers
 * must ensure that the string outlives the returned root object,
 * and that child objects must not be used after the root object
 * has been free'd.
 */
extern grub_err_t EXPORT_FUNC(grub_json_parse) (grub_json_t **out,
					        char *string,
						grub_size_t string_len);

/*
 * Free the structure and its contents. The string passed to
 * grub_json_parse() will not be free'd.
 */
extern void EXPORT_FUNC(grub_json_free) (grub_json_t *json);

/*
 * Get the child count of a valid grub_json_t instance. Children
 * are present for arrays, objects (dicts) and keys of a dict.
 */
extern grub_err_t EXPORT_FUNC(grub_json_getsize) (grub_size_t *out,
						  const grub_json_t *json);

/* Get the type of a valid grub_json_t instance. */
extern grub_err_t EXPORT_FUNC(grub_json_gettype) (grub_json_type_t *out,
						  const grub_json_t *json);

/*
 * Get n'th child of a valid object, array or key. Will return an
 * error if no such child exists. The result does not need to be
 * free'd.
 */
extern grub_err_t EXPORT_FUNC(grub_json_getchild) (grub_json_t *out,
						   const grub_json_t *parent,
						   grub_size_t n);

/*
 * Get value of key from a valid grub_json_t instance. The result
 * does not need to be free'd.
 */
extern grub_err_t EXPORT_FUNC(grub_json_getvalue) (grub_json_t *out,
						   const grub_json_t *parent,
						   const char *key);

/*
 * Get the string representation of a valid grub_json_t instance.
 * If a key is given and parent is a JSON object, this function
 * will return the string value of a child mapping to the key.
 * If no key is given, it will return the string value of the
 * parent itself.
 */
extern grub_err_t EXPORT_FUNC(grub_json_getstring) (const char **out,
						    const grub_json_t *parent,
						    const char *key);

/*
 * Get the uint64 representation of a valid grub_json_t instance.
 * Returns an error if the value pointed to by `parent` cannot be
 * converted to an uint64. See grub_json_getstring() for details
 * on the key parameter.
 */
extern grub_err_t EXPORT_FUNC(grub_json_getuint64) (grub_uint64_t *out,
						    const grub_json_t *parent,
						    const char *key);

/*
 * Get the int64 representation of a valid grub_json_t instance.
 * Returns an error if the value pointed to by `parent` cannot be
 * converted to an int64. See grub_json_getstring() for
 * details on the key parameter.
 */
extern grub_err_t EXPORT_FUNC(grub_json_getint64) (grub_int64_t *out,
						   const grub_json_t *parent,
						   const char *key);

#endif
