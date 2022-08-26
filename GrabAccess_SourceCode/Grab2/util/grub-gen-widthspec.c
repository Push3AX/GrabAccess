/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009,2010 Free Software Foundation, Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TAGS_H
#include FT_TRUETYPE_TABLES_H
#include FT_SYNTHESIS_H

#undef __FTERRORS_H__
#define FT_ERROR_START_LIST   const char *ft_errmsgs[] = { 
#define FT_ERRORDEF(e, v, s)  [e] = s,
#define FT_ERROR_END_LIST     };
#include FT_ERRORS_H   

#define GRUB_FONT_DEFAULT_SIZE		16

#define ARRAY_SIZE(array) (sizeof (array) / sizeof (array[0]))

#define MAX_CODE 65536
static unsigned char result[MAX_CODE / 8];

static void
add_glyph (FT_Face face,
	   unsigned int char_code)
{
  int flag = FT_LOAD_RENDER | FT_LOAD_MONOCHROME;
  FT_Error err;
  FT_UInt glyph_idx;

  glyph_idx = FT_Get_Char_Index (face, char_code);
  if (!glyph_idx)
    return;

  err = FT_Load_Glyph (face, glyph_idx, flag);
  if (err)
    {
      printf ("Freetype Error %d loading glyph 0x%x for U+0x%x",
	      err, glyph_idx, char_code);

      if (err > 0 && err < (signed) ARRAY_SIZE (ft_errmsgs))
	printf (": %s\n", ft_errmsgs[err]);
      else
	printf ("\n");
      return;
    }

  if (face->glyph->bitmap.width > 12 && char_code < MAX_CODE)
    result[char_code >> 3] |= (1 << (char_code & 7));
}

int
main (int argc, char *argv[])
{
  FT_Library ft_lib;
  FILE *file;
  int i;

  if (argc != 3)
    {
      fprintf (stderr, "grub-gen-widthspec: usage: INPUT OUTPUT");
      return 1;
    }

  if (FT_Init_FreeType (&ft_lib))
    {
      fprintf (stderr, "grub-gen-widthspec: error: FT_Init_FreeType fails");
      return 1;
    }

  {
    FT_Face ft_face;
    int size;
    FT_Error err;
    unsigned int char_code, glyph_index;

    err = FT_New_Face (ft_lib, argv[1], 0, &ft_face);
    if (err)
      {
	fprintf (stderr, "can't open file %s, index %d: error %d",
		 argv[1], 0, err);
	if (err > 0 && err < (signed) ARRAY_SIZE (ft_errmsgs))
	  fprintf (stderr, ": %s\n", ft_errmsgs[err]);
	else
	  fprintf (stderr, "\n");

	return 1;
      }

    if ((ft_face->face_flags & FT_FACE_FLAG_SCALABLE) ||
	(! ft_face->num_fixed_sizes))
      size = GRUB_FONT_DEFAULT_SIZE;
    else
      size = ft_face->available_sizes[0].height;

    if (FT_Set_Pixel_Sizes (ft_face, size, size))
      {
	fprintf (stderr, "grub-gen-widthspec: error: can't set %dx%d font size", size, size);
	return 1;
      }
  
    for (char_code = FT_Get_First_Char (ft_face, &glyph_index);
	 glyph_index;
	 char_code = FT_Get_Next_Char (ft_face, char_code, &glyph_index))
      add_glyph (ft_face, char_code);

    FT_Done_Face (ft_face);
  }

  FT_Done_FreeType (ft_lib);

  file = fopen (argv[2], "w");
  if (! file)
    {
      fprintf (stderr, "grub-gen-asciih: error: cannot write to `%s': %s", argv[2],
	       strerror (errno));
      return 1;
    }

  fprintf (file, "/* THIS CHUNK OF BYTES IS AUTOMATICALLY GENERATED */\n");
  fprintf (file, "unsigned char widthspec[] =\n");
  fprintf (file, "{\n");

  for (i = 0; i < MAX_CODE / 8; i++)
    fprintf (file, "0x%02x,%c", result[i], ((i & 0xf) == 0xf) ? '\n' : ' ');

  fprintf (file, "};\n");

  fclose (file);

  return 0;
}
