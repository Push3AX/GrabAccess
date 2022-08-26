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

struct grub_glyph_info
{
  int width;
  int height;
  int x_ofs;
  int y_ofs;
  int device_width;
  int bitmap_size;
  unsigned char *bitmap;
};

static void
add_pixel (unsigned char **data, int *mask, int not_blank)
{
  if (*mask == 0)
    {
      (*data)++;
      **data = 0;
      *mask = 128;
    }

  if (not_blank)
    **data |= *mask;

  *mask >>= 1;
}

static void
add_glyph (FT_UInt glyph_idx, FT_Face face,
	   unsigned int char_code,
	   struct grub_glyph_info *glyph_info)
{
  int width, height;
  int cuttop, cutbottom, cutleft, cutright;
  unsigned char *data;
  int mask, i, j, bitmap_size;
  FT_GlyphSlot glyph;
  int flag = FT_LOAD_RENDER | FT_LOAD_MONOCHROME;
  FT_Error err;

  err = FT_Load_Glyph (face, glyph_idx, flag);
  if (err)
    {
      fprintf (stderr, "Freetype Error %d loading glyph 0x%x for U+0x%x",
	      err, glyph_idx, char_code);

      if (err > 0 && err < (signed) ARRAY_SIZE (ft_errmsgs))
	fprintf (stderr, ": %s\n", ft_errmsgs[err]);
      else
	fprintf (stderr, "\n");
      exit (1);
    }

  glyph = face->glyph;

  if (glyph->next)
    printf ("%x\n", char_code);

  cuttop = cutbottom = cutleft = cutright = 0;

  width = glyph->bitmap.width;
  height = glyph->bitmap.rows;

  bitmap_size = ((width * height + 7) / 8);
  glyph_info->bitmap = malloc (bitmap_size);
  if (!glyph_info->bitmap)
    {
      fprintf (stderr, "grub-gen-asciih: error: out of memory");
      exit (1);
    }
  glyph_info->bitmap_size = bitmap_size;

  glyph_info->width = width;
  glyph_info->height = height;
  glyph_info->x_ofs = glyph->bitmap_left;
  glyph_info->y_ofs = glyph->bitmap_top - height;
  glyph_info->device_width = glyph->metrics.horiAdvance / 64;

  mask = 0;
  data = &glyph_info->bitmap[0] - 1;
  for (j = cuttop; j < height + cuttop; j++)
    for (i = cutleft; i < width + cutleft; i++)
      add_pixel (&data, &mask,
		 glyph->bitmap.buffer[i / 8 + j * glyph->bitmap.pitch] &
		 (1 << (7 - (i & 7))));
}

static void
write_font_ascii_bitmap (FILE *file, FT_Face face)
{
  int char_code;

  fprintf (file, "/* THIS CHUNK OF BYTES IS AUTOMATICALLY GENERATED */\n");
  fprintf (file, "unsigned char ascii_bitmaps[] =\n");
  fprintf (file, "{\n");

  for (char_code = 0; char_code <= 0x7f; char_code++)
    {
      FT_UInt glyph_idx;
      struct grub_glyph_info glyph;

      glyph_idx = FT_Get_Char_Index (face, char_code);
      if (!glyph_idx)
	return;

      memset (&glyph, 0, sizeof(glyph));

      add_glyph (glyph_idx, face, char_code, &glyph);

      if (glyph.width == 8 && glyph.height == 16
	  && glyph.x_ofs == 0 && glyph.y_ofs == 0)
	{
	  int row;
	  for (row = 0; row < 16; row++)
	    fprintf (file, "0x%02x, ", glyph.bitmap[row]);
	}
      else
	{
	  unsigned char glph[16];
	  int p = 0, mask = 0x80;
	  int row, col;
	  int dy = 12 - glyph.height - glyph.y_ofs;
	  for (row = 0; row < 16; row++)
	    glph[row] = 0;
	  for (row = 0; row < glyph.height; row++)
	    for (col = 0; col < glyph.width; col++)
	      {
		int val = glyph.bitmap[p] & mask;
		mask >>= 1;
		if (mask == 0)
		  {
		    mask = 0x80;
		    p++;
		  }
		if (val && dy + row >= 0
		    && dy + row < 16
		    && glyph.x_ofs + col >= 0
		    && glyph.x_ofs + col < 8)
		  glph[dy + row] |= 1 << (7 - (glyph.x_ofs + col));
	      }
	  for (row = 0; row < 16; row++)
	    fprintf (file, "0x%02x, ", glph[row]);
	}
      fprintf (file, "\n");
      free (glyph.bitmap);
    }
  fprintf (file, "};\n");
}

int
main (int argc, char *argv[])
{
  FT_Library ft_lib;
  FT_Face ft_face;
  FILE *file;

  if (argc != 3)
    {
      fprintf (stderr, "grub-gen-asciih: usage: INPUT OUTPUT");
      return 1;
    }

  if (FT_Init_FreeType (&ft_lib))
    {
      fprintf (stderr, "grub-gen-asciih: error: FT_Init_FreeType fails");
      return 1;
    }

  {
    int size;
    FT_Error err;

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
	fprintf (stderr, "grub-gen-asciih: error: can't set %dx%d font size", size, size);
	return 1;
      }
  }

  file = fopen (argv[2], "w");
  if (! file)
    {
      fprintf (stderr, "grub-gen-asciih: error: cannot write to `%s': %s", argv[2],
	       strerror (errno));
      return 1;
    }

  write_font_ascii_bitmap (file, ft_face);

  fclose (file);

  FT_Done_Face (ft_face);

  FT_Done_FreeType (ft_lib);

  return 0;
}
