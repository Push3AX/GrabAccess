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

#include <config.h>
#include <grub/types.h>
#include <grub/misc.h>
#include <grub/emu/misc.h>
#include <grub/util/misc.h>
#include <grub/misc.h>
#include <grub/i18n.h>
#include <grub/fontformat.h>
#include <grub/font.h>
#include <grub/unicode.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GRUB_BUILD
#define _GNU_SOURCE	1
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#include <argp.h>
#pragma GCC diagnostic error "-Wmissing-prototypes"
#pragma GCC diagnostic error "-Wmissing-declarations"
#endif
#include <assert.h>

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

#ifndef GRUB_BUILD
#include "progname.h"
#endif

#ifdef GRUB_BUILD
#define grub_util_fopen fopen
#endif

#define GRUB_FONT_DEFAULT_SIZE		16

#define GRUB_FONT_RANGE_BLOCK		1024

struct grub_glyph_info
{
  struct grub_glyph_info *next;
  grub_uint32_t char_code;
  int width;
  int height;
  int x_ofs;
  int y_ofs;
  int device_width;
  int bitmap_size;
  grub_uint8_t *bitmap;
};

enum file_formats
{
  PF2
};

enum
  {
    GRUB_FONT_FLAG_BOLD	= 1,
    GRUB_FONT_FLAG_NOBITMAP = 2,
    GRUB_FONT_FLAG_NOHINTING = 4,
    GRUB_FONT_FLAG_FORCEHINT = 8
  };

struct grub_font_info
{
  const char *name;
  int style;
  int desc;
  int asce;
  int size;
  int max_width;
  int max_height;
  int min_y;
  int max_y;
  int flags;
  int num_range;
  grub_uint32_t *ranges;
  struct grub_glyph_info *glyphs_unsorted;
  struct grub_glyph_info *glyphs_sorted;
  int num_glyphs;
};

static int font_verbosity;

static void
add_pixel (grub_uint8_t **data, int *mask, int not_blank)
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
add_glyph (struct grub_font_info *font_info, FT_UInt glyph_idx, FT_Face face,
	   grub_uint32_t char_code, int nocut)
{
  struct grub_glyph_info *glyph_info;
  int width, height;
  int cuttop, cutbottom, cutleft, cutright;
  grub_uint8_t *data;
  int mask, i, j, bitmap_size;
  FT_GlyphSlot glyph;
  int flag = FT_LOAD_RENDER | FT_LOAD_MONOCHROME;
  FT_Error err;

  if (font_info->flags & GRUB_FONT_FLAG_NOBITMAP)
    flag |= FT_LOAD_NO_BITMAP;

  if (font_info->flags & GRUB_FONT_FLAG_NOHINTING)
    flag |= FT_LOAD_NO_HINTING;
  else if (font_info->flags & GRUB_FONT_FLAG_FORCEHINT)
    flag |= FT_LOAD_FORCE_AUTOHINT;

  err = FT_Load_Glyph (face, glyph_idx, flag);
  if (err)
    {
      printf (_("Freetype Error %d loading glyph 0x%x for U+0x%x%s"),
	      err, glyph_idx, char_code & GRUB_FONT_CODE_CHAR_MASK,
	      char_code & GRUB_FONT_CODE_RIGHT_JOINED
	      /* TRANSLATORS: These qualifiers are used for cursive typography,
		 mainly Arabic. Note that the terms refer to the visual position
		 and not logical order and if used in left-to-right script then
		 leftmost is initial but with right-to-left script like Arabic
		 rightmost is the initial.  */
	      ? ((char_code & GRUB_FONT_CODE_LEFT_JOINED) ? _(" (medial)"):
		 _(" (leftmost)"))
	      : ((char_code & GRUB_FONT_CODE_LEFT_JOINED) ? _(" (rightmost)"):
		 ""));

      if (err > 0 && err < (signed) ARRAY_SIZE (ft_errmsgs))
	printf (": %s\n", ft_errmsgs[err]);
      else
	printf ("\n");
      return;
    }

  glyph = face->glyph;

  if (font_info->flags & GRUB_FONT_FLAG_BOLD)
    FT_GlyphSlot_Embolden (glyph);

  if (nocut)
    cuttop = cutbottom = cutleft = cutright = 0;
  else
    {
      for (cuttop = 0; cuttop < glyph->bitmap.rows; cuttop++)
	{
	  for (j = 0; j < glyph->bitmap.width; j++)
	    if (glyph->bitmap.buffer[j / 8 + cuttop * glyph->bitmap.pitch]
		& (1 << (7 - (j & 7))))
	      break;
	  if (j != glyph->bitmap.width)
	    break;
	}

      for (cutbottom = glyph->bitmap.rows - 1; cutbottom >= 0; cutbottom--)
	{
	  for (j = 0; j < glyph->bitmap.width; j++)
	    if (glyph->bitmap.buffer[j / 8 + cutbottom * glyph->bitmap.pitch]
		& (1 << (7 - (j & 7))))
	      break;
	  if (j != glyph->bitmap.width)
	    break;
	}
      cutbottom = glyph->bitmap.rows - 1 - cutbottom;
      if (cutbottom + cuttop >= glyph->bitmap.rows)
	cutbottom = 0;

      for (cutleft = 0; cutleft < glyph->bitmap.width; cutleft++)
	{
	  for (j = 0; j < glyph->bitmap.rows; j++)
	    if (glyph->bitmap.buffer[cutleft / 8 + j * glyph->bitmap.pitch]
		& (1 << (7 - (cutleft & 7))))
	      break;
	  if (j != glyph->bitmap.rows)
	    break;
	}
      for (cutright = glyph->bitmap.width - 1; cutright >= 0; cutright--)
	{
	  for (j = 0; j < glyph->bitmap.rows; j++)
	    if (glyph->bitmap.buffer[cutright / 8 + j * glyph->bitmap.pitch]
		& (1 << (7 - (cutright & 7))))
	      break;
	  if (j != glyph->bitmap.rows)
	    break;
	}
      cutright = glyph->bitmap.width - 1 - cutright;
      if (cutright + cutleft >= glyph->bitmap.width)
	cutright = 0;
    }

  width = glyph->bitmap.width - cutleft - cutright;
  height = glyph->bitmap.rows - cutbottom - cuttop;

  bitmap_size = ((width * height + 7) / 8);
  glyph_info = xmalloc (sizeof (struct grub_glyph_info));
  glyph_info->bitmap = xmalloc (bitmap_size);
  glyph_info->bitmap_size = bitmap_size;

  glyph_info->next = font_info->glyphs_unsorted;
  font_info->glyphs_unsorted = glyph_info;
  font_info->num_glyphs++;

  glyph_info->char_code = char_code;
  glyph_info->width = width;
  glyph_info->height = height;
  glyph_info->x_ofs = glyph->bitmap_left + cutleft;
  glyph_info->y_ofs = glyph->bitmap_top - height - cuttop;
  glyph_info->device_width = glyph->metrics.horiAdvance / 64;

  if (width > font_info->max_width)
    font_info->max_width = width;

  if (height > font_info->max_height)
    font_info->max_height = height;

  if (glyph_info->y_ofs < font_info->min_y && glyph_info->y_ofs > -font_info->size)
    font_info->min_y = glyph_info->y_ofs;

  if (glyph_info->y_ofs + height > font_info->max_y)
    font_info->max_y = glyph_info->y_ofs + height;

  mask = 0;
  data = &glyph_info->bitmap[0] - 1;
  for (j = cuttop; j < height + cuttop; j++)
    for (i = cutleft; i < width + cutleft; i++)
      add_pixel (&data, &mask,
		 glyph->bitmap.buffer[i / 8 + j * glyph->bitmap.pitch] &
		 (1 << (7 - (i & 7))));
}

struct glyph_replace *subst_rightjoin, *subst_leftjoin, *subst_medijoin;

struct glyph_replace
{
  struct glyph_replace *next;
  grub_uint32_t from, to;
};

/* TODO: sort glyph_replace and use binary search if necessary.  */
static void
add_char (struct grub_font_info *font_info, FT_Face face,
	  grub_uint32_t char_code, int nocut)
{
  FT_UInt glyph_idx;
  struct glyph_replace *cur;

  glyph_idx = FT_Get_Char_Index (face, char_code);
  if (!glyph_idx)
    return;
  add_glyph (font_info, glyph_idx, face, char_code, nocut);
  for (cur = subst_rightjoin; cur; cur = cur->next)
    if (cur->from == glyph_idx)
      {
	add_glyph (font_info, cur->to, face,
		   char_code | GRUB_FONT_CODE_RIGHT_JOINED, nocut);
	break;
      }
  if (!cur && char_code >= GRUB_UNICODE_ARABIC_START
      && char_code < GRUB_UNICODE_ARABIC_END)
    {
      int i;
      for (i = 0; grub_unicode_arabic_shapes[i].code; i++)
	if (grub_unicode_arabic_shapes[i].code == char_code
	    && grub_unicode_arabic_shapes[i].right_linked)
	  {
	    FT_UInt idx2;
	    idx2 = FT_Get_Char_Index (face, grub_unicode_arabic_shapes[i]
				      .right_linked);
	    if (idx2)
	      add_glyph (font_info, idx2, face,
			 char_code | GRUB_FONT_CODE_RIGHT_JOINED, nocut);
	    break;
	  }
	      
    }

  for (cur = subst_leftjoin; cur; cur = cur->next)
    if (cur->from == glyph_idx)
      {
	add_glyph (font_info, cur->to, face,
		   char_code | GRUB_FONT_CODE_LEFT_JOINED, nocut);
	break;
      }
  if (!cur && char_code >= GRUB_UNICODE_ARABIC_START
      && char_code < GRUB_UNICODE_ARABIC_END)
    {
      int i;
      for (i = 0; grub_unicode_arabic_shapes[i].code; i++)
	if (grub_unicode_arabic_shapes[i].code == char_code
	    && grub_unicode_arabic_shapes[i].left_linked)
	  {
	    FT_UInt idx2;
	    idx2 = FT_Get_Char_Index (face, grub_unicode_arabic_shapes[i]
				      .left_linked);
	    if (idx2)
	      add_glyph (font_info, idx2, face,
			 char_code | GRUB_FONT_CODE_LEFT_JOINED, nocut);
	    break;
	  }
	      
    }
  for (cur = subst_medijoin; cur; cur = cur->next)
    if (cur->from == glyph_idx)
      {
	add_glyph (font_info, cur->to, face,
		   char_code | GRUB_FONT_CODE_LEFT_JOINED
		   | GRUB_FONT_CODE_RIGHT_JOINED, nocut);
	break;
      }
  if (!cur && char_code >= GRUB_UNICODE_ARABIC_START
      && char_code < GRUB_UNICODE_ARABIC_END)
    {
      int i;
      for (i = 0; grub_unicode_arabic_shapes[i].code; i++)
	if (grub_unicode_arabic_shapes[i].code == char_code
	    && grub_unicode_arabic_shapes[i].both_linked)
	  {
	    FT_UInt idx2;
	    idx2 = FT_Get_Char_Index (face, grub_unicode_arabic_shapes[i]
				      .both_linked);
	    if (idx2)
	      add_glyph (font_info, idx2, face,
			 char_code | GRUB_FONT_CODE_LEFT_JOINED
			 | GRUB_FONT_CODE_RIGHT_JOINED, nocut);
	    break;
	  }
	      
    }
}

struct gsub_header
{
  grub_uint32_t version;
  grub_uint16_t scripts_off;
  grub_uint16_t features_off;
  grub_uint16_t lookups_off;
} GRUB_PACKED;

struct gsub_features
{
  grub_uint16_t count;
  struct
  {
#define FEATURE_FINA 0x66696e61
#define FEATURE_INIT 0x696e6974
#define FEATURE_MEDI 0x6d656469
#define FEATURE_AALT 0x61616c74
#define FEATURE_LIGA 0x6c696761
#define FEATURE_RLIG 0x726c6967
    grub_uint32_t feature_tag;
    grub_uint16_t offset;
  } GRUB_PACKED features[0];
} GRUB_PACKED;

struct gsub_feature
{
  grub_uint16_t params;
  grub_uint16_t lookupcount;
  grub_uint16_t lookupindices[0];
} GRUB_PACKED;

struct gsub_lookup_list
{
  grub_uint16_t count;
  grub_uint16_t offsets[0];
} GRUB_PACKED;

struct gsub_lookup
{
  grub_uint16_t type;
  grub_uint16_t flag;
  grub_uint16_t subtablecount;
  grub_uint16_t subtables[0];
} GRUB_PACKED;

struct gsub_substitution
{
  grub_uint16_t type;
  grub_uint16_t coverage_off;
  union
  {
    grub_int16_t delta;
    struct
    {
      grub_int16_t count;
      grub_uint16_t repl[0];
    };
  };
} GRUB_PACKED;

struct gsub_coverage_list
{
  grub_uint16_t type;
  grub_uint16_t count;
  grub_uint16_t glyphs[0];
} GRUB_PACKED;

struct gsub_coverage_ranges
{
  grub_uint16_t type;
  grub_uint16_t count;
  struct 
  {
    grub_uint16_t start;
    grub_uint16_t end;
    grub_uint16_t start_index;
  } GRUB_PACKED ranges[0];
} GRUB_PACKED;

#define GSUB_SINGLE_SUBSTITUTION 1

#define GSUB_SUBSTITUTION_DELTA 1
#define GSUB_SUBSTITUTION_MAP 2

#define GSUB_COVERAGE_LIST 1
#define GSUB_COVERAGE_RANGE 2

#define GSUB_RTL_CHAR 1

static void
add_subst (grub_uint32_t from, grub_uint32_t to, struct glyph_replace **target)
{
  struct glyph_replace *new = xmalloc (sizeof (*new));
  new->next = *target;
  new->from = from;
  new->to = to;
  *target = new;
}

static void
subst (const struct gsub_substitution *sub, grub_uint32_t glyph,
       struct glyph_replace **target, int *i)
{
  grub_uint16_t substtype;
  substtype = grub_be_to_cpu16 (sub->type);

  if (substtype == GSUB_SUBSTITUTION_DELTA)
    add_subst (glyph, glyph + grub_be_to_cpu16 (sub->delta), target);
  else if (*i >= grub_be_to_cpu16 (sub->count))
    printf (_("Out of range substitution (%d, %d)\n"), *i,
	    grub_be_to_cpu16 (sub->count));
  else
    add_subst (glyph, grub_be_to_cpu16 (sub->repl[(*i)++]), target);
}

static void
process_cursive (struct gsub_feature *feature,
		 struct gsub_lookup_list *lookups,
		 grub_uint32_t feattag)
{
  int j, k;
  int i;
  struct glyph_replace **target = NULL;
  struct gsub_substitution *sub;

  for (j = 0; j < grub_be_to_cpu16 (feature->lookupcount); j++)
    {
      int lookup_index = grub_be_to_cpu16 (feature->lookupindices[j]);
      struct gsub_lookup *lookup;
      if (lookup_index >= grub_be_to_cpu16 (lookups->count))
	{
	  /* TRANSLATORS: "lookup" is taken directly from font specifications
	   which are formulated as "Under condition X replace LOOKUP with 
	   SUBSTITUITION".  "*/
	  printf (_("Out of range lookup: %d\n"), lookup_index);
	  continue;
	}
      lookup = (struct gsub_lookup *)
	((grub_uint8_t *) lookups 
	 + grub_be_to_cpu16 (lookups->offsets[lookup_index]));
      if (grub_be_to_cpu16 (lookup->type) != GSUB_SINGLE_SUBSTITUTION)
	{
	  printf (_("Unsupported substitution type: %d\n"),
		  grub_be_to_cpu16 (lookup->type));
	  continue;
	}		      
      if (grub_be_to_cpu16 (lookup->flag) & ~GSUB_RTL_CHAR)
	{
	  grub_util_info ("unsupported substitution flag: 0x%x",
			  grub_be_to_cpu16 (lookup->flag));
	}
      switch (feattag)
	{
	case FEATURE_INIT:
	  if (grub_be_to_cpu16 (lookup->flag) & GSUB_RTL_CHAR)
	    target = &subst_leftjoin;
	  else
	    target = &subst_rightjoin;
	  break;
	case FEATURE_FINA:
	  if (grub_be_to_cpu16 (lookup->flag) & GSUB_RTL_CHAR)
	    target = &subst_rightjoin;
	  else
	    target = &subst_leftjoin;
	  break;
	case FEATURE_MEDI:
	  target = &subst_medijoin;
	  break;	  
	}
      for (k = 0; k < grub_be_to_cpu16 (lookup->subtablecount); k++)
	{
	  sub = (struct gsub_substitution *)
	    ((grub_uint8_t *) lookup + grub_be_to_cpu16 (lookup->subtables[k]));
	  grub_uint16_t substtype;
	  substtype = grub_be_to_cpu16 (sub->type);
	  if (substtype != GSUB_SUBSTITUTION_MAP
	      && substtype != GSUB_SUBSTITUTION_DELTA)
	    {
	      printf (_("Unsupported substitution specification: %d\n"),
		      substtype);
	      continue;
	    }
	  void *coverage = (grub_uint8_t *) sub
	    + grub_be_to_cpu16 (sub->coverage_off);
	  grub_uint32_t covertype;
	  covertype = grub_be_to_cpu16 (grub_get_unaligned16 (coverage));
	  i = 0;
	  if (covertype == GSUB_COVERAGE_LIST)
	    {
	      struct gsub_coverage_list *cover = coverage;
	      int l;
	      for (l = 0; l < grub_be_to_cpu16 (cover->count); l++)
		subst (sub, grub_be_to_cpu16 (cover->glyphs[l]), target, &i);
	    }
	  else if (covertype == GSUB_COVERAGE_RANGE)
	    {
	      struct gsub_coverage_ranges *cover = coverage;
	      int l, m;
	      for (l = 0; l < grub_be_to_cpu16 (cover->count); l++)
		for (m = grub_be_to_cpu16 (cover->ranges[l].start);
		     m <= grub_be_to_cpu16 (cover->ranges[l].end); m++)
		  subst (sub, m, target, &i);
	    }
	  else
	    /* TRANSLATORS: most font transformations apply only to
	       some glyphs. Those glyphs are described as "coverage".
	       There are 2 coverage specifications: list and range.
	       This warning is thrown when another coverage specification
	       is detected.  */
	    fprintf (stderr,
		     _("Unsupported coverage specification: %d\n"), covertype);
	}
    }
}

static void
add_font (struct grub_font_info *font_info, FT_Face face, int nocut)
{
  struct gsub_header *gsub = NULL;
  FT_ULong gsub_len = 0;

  if (!FT_Load_Sfnt_Table (face, TTAG_GSUB, 0, NULL, &gsub_len))
    {
      gsub = xmalloc (gsub_len);
      if (FT_Load_Sfnt_Table (face, TTAG_GSUB, 0, (void *) gsub, &gsub_len))
	{
	  free (gsub);
	  gsub = NULL;
	  gsub_len = 0;
	}
    }
  if (gsub)
    {
      struct gsub_features *features 
	= (struct gsub_features *) (((grub_uint8_t *) gsub)
				    + grub_be_to_cpu16 (gsub->features_off));
      struct gsub_lookup_list *lookups
	= (struct gsub_lookup_list *) (((grub_uint8_t *) gsub)
				       + grub_be_to_cpu16 (gsub->lookups_off));
      int i;
      int nfeatures = grub_be_to_cpu16 (features->count);
      for (i = 0; i < nfeatures; i++)
	{
	  struct gsub_feature *feature = (struct gsub_feature *)
	    ((grub_uint8_t *) features
	     + grub_be_to_cpu16 (features->features[i].offset));
	  grub_uint32_t feattag
	    = grub_be_to_cpu32 (features->features[i].feature_tag);
	  if (feature->params)
	    fprintf (stderr,
		     _("WARNING: unsupported font feature parameters: %x\n"),
		    grub_be_to_cpu16 (feature->params));
	  switch (feattag)
	    {
	      /* Used for retrieving all possible variants. Useless in grub.  */
	    case FEATURE_AALT:
	      break;

	      /* FIXME: Add ligature support.  */
	    case FEATURE_LIGA:
	    case FEATURE_RLIG:
	      break;

	      /* Cursive form variants.  */
	    case FEATURE_FINA:
	    case FEATURE_INIT:
	    case FEATURE_MEDI:
	      process_cursive (feature, lookups, feattag);
	      break;

	    default:
	      {
		char str[5];
		int j;
		memcpy (str, &features->features[i].feature_tag,
			sizeof (features->features[i].feature_tag));
		str[4] = 0;
		for (j = 0; j < 4; j++)
		  if (!grub_isgraph (str[j]))
		    str[j] = '?';
		/* TRANSLATORS: It's gsub feature, not gsub font.  */
		grub_util_info ("Unknown gsub font feature 0x%x (%s)",
				feattag, str);
	      }
	    }
	}
    }

  if (font_info->num_range)
    {
      int i;
      grub_uint32_t j;

      for (i = 0; i < font_info->num_range; i++)
	for (j = font_info->ranges[i * 2]; j <= font_info->ranges[i * 2 + 1];
	     j++)
	  add_char (font_info, face, j, nocut);
    }
  else
    {
      grub_uint32_t char_code, glyph_index;

      for (char_code = FT_Get_First_Char (face, &glyph_index);
	   glyph_index;
	   char_code = FT_Get_Next_Char (face, char_code, &glyph_index))
	add_char (font_info, face, char_code, nocut);
    }
}

static void
write_string_section (const char *name, const char *str,
		      int *offset, FILE *file,
		      const char *filename)
{
  grub_uint32_t leng, leng_be32;

  leng = strlen (str) + 1;
  leng_be32 = grub_cpu_to_be32 (leng);

  grub_util_write_image (name, 4, file, filename);
  grub_util_write_image ((char *) &leng_be32, 4, file, filename);
  grub_util_write_image (str, leng, file, filename);

  *offset += 8 + leng;
}

static void
write_be16_section (const char *name, grub_uint16_t data, int* offset,
		    FILE *file, const char *filename)
{
  grub_uint32_t leng;

  leng = grub_cpu_to_be32_compile_time (2);
  data = grub_cpu_to_be16 (data);
  grub_util_write_image (name, 4, file, filename);
  grub_util_write_image ((char *) &leng, 4, file, filename);
  grub_util_write_image ((char *) &data, 2, file, filename);

  *offset += 10;
}

static void
print_glyphs (struct grub_font_info *font_info)
{
  int num;
  struct grub_glyph_info *glyph;
  char line[512];

  for (glyph = font_info->glyphs_sorted, num = 0; num < font_info->num_glyphs;
       glyph++, num++)
    {
      int x, y, xmax, xmin, ymax, ymin;
      grub_uint8_t *bitmap, mask;

      printf ("\nGlyph #%d, U+%04x\n", num, glyph->char_code);
      printf ("Width %d, Height %d, X offset %d, Y offset %d, Device width %d\n",
	      glyph->width, glyph->height, glyph->x_ofs, glyph->y_ofs,
	      glyph->device_width);

      xmax = glyph->x_ofs + glyph->width;
      if (xmax < glyph->device_width)
	xmax = glyph->device_width;

      xmin = glyph->x_ofs;
      if (xmin > 0)
	xmin = 0;

      ymax = glyph->y_ofs + glyph->height;
      if (ymax < font_info->asce)
	ymax = font_info->asce;

      ymin = glyph->y_ofs;
      if (ymin > - font_info->desc)
	ymin = - font_info->desc;

      bitmap = glyph->bitmap;
      mask = 0x80;
      for (y = ymax - 1; y > ymin - 1; y--)
	{
	  int line_pos;

	  line_pos = 0;
	  for (x = xmin; x < xmax; x++)
	    {
	      if ((x >= glyph->x_ofs) &&
		  (x < glyph->x_ofs + glyph->width) &&
		  (y >= glyph->y_ofs) &&
		  (y < glyph->y_ofs + glyph->height))
		{
		  line[line_pos++] = (*bitmap & mask) ? '#' : '_';
		  mask >>= 1;
		  if (mask == 0)
		    {
		      mask = 0x80;
		      bitmap++;
		    }
		}
	      else if ((x >= 0) &&
		       (x < glyph->device_width) &&
		       (y >= - font_info->desc) &&
		       (y < font_info->asce))
		{
		  line[line_pos++] = ((x == 0) || (y == 0)) ? '+' : '.';
		}
	      else
		line[line_pos++] = '*';
	    }
	  line[line_pos] = 0;
	  printf ("%s\n", line);
	}
    }
}

static void
write_font_pf2 (struct grub_font_info *font_info, char *output_file)
{
  FILE *file;
  grub_uint32_t leng;
  char style_name[20], *font_name, *ptr;
  int offset;
  struct grub_glyph_info *cur;

  file = grub_util_fopen (output_file, "wb");
  if (! file)
    grub_util_error (_("cannot write to `%s': %s"), output_file,
		     strerror (errno));

  offset = 0;

  leng = grub_cpu_to_be32_compile_time (4);
  grub_util_write_image (FONT_FORMAT_SECTION_NAMES_FILE,
  			 sizeof(FONT_FORMAT_SECTION_NAMES_FILE) - 1, file,
			 output_file);
  grub_util_write_image ((char *) &leng, 4, file, output_file);
  grub_util_write_image (FONT_FORMAT_PFF2_MAGIC, 4, file, output_file);
  offset += 12;

  if (! font_info->name)
    font_info->name = "Unknown";

  if (font_info->flags & GRUB_FONT_FLAG_BOLD)
    font_info->style |= FT_STYLE_FLAG_BOLD;

  style_name[0] = 0;
  if (font_info->style & FT_STYLE_FLAG_BOLD)
    strcpy (style_name, " Bold");

  if (font_info->style & FT_STYLE_FLAG_ITALIC)
    strcat (style_name, " Italic");

  if (! style_name[0])
    strcpy (style_name, " Regular");

  font_name = xmalloc (strlen (font_info->name) + strlen (&style_name[1])
		       + 3 + 20);
  ptr = grub_stpcpy (font_name, font_info->name);
  *ptr++ = ' ';
  ptr = grub_stpcpy (ptr, &style_name[1]);
  *ptr++ = ' ';
  snprintf (ptr, 20, "%d", font_info->size);

  write_string_section (FONT_FORMAT_SECTION_NAMES_FONT_NAME,
  			font_name, &offset, file, output_file);
  write_string_section (FONT_FORMAT_SECTION_NAMES_FAMILY,
  			font_info->name, &offset, file, output_file);
  write_string_section (FONT_FORMAT_SECTION_NAMES_WEIGHT,
			(font_info->style & FT_STYLE_FLAG_BOLD) ?
			"bold" : "normal",
			&offset, file, output_file);
  write_string_section (FONT_FORMAT_SECTION_NAMES_SLAN,
			(font_info->style & FT_STYLE_FLAG_ITALIC) ?
			"italic" : "normal",
			&offset, file, output_file);

  write_be16_section (FONT_FORMAT_SECTION_NAMES_POINT_SIZE,
  		      font_info->size, &offset, file, output_file);
  write_be16_section (FONT_FORMAT_SECTION_NAMES_MAX_CHAR_WIDTH,
  		      font_info->max_width, &offset, file, output_file);
  write_be16_section (FONT_FORMAT_SECTION_NAMES_MAX_CHAR_HEIGHT,
  		      font_info->max_height, &offset, file, output_file);

  if (! font_info->desc)
    {
      if (font_info->min_y >= 0)
	font_info->desc = 1;
      else
	font_info->desc = - font_info->min_y;
    }

  if (! font_info->asce)
    {
      if (font_info->max_y <= 0)
	font_info->asce = 1;
      else
	font_info->asce = font_info->max_y;
    }

  write_be16_section (FONT_FORMAT_SECTION_NAMES_ASCENT,
  		      font_info->asce, &offset, file, output_file);
  write_be16_section (FONT_FORMAT_SECTION_NAMES_DESCENT,
  		      font_info->desc, &offset, file, output_file);

  if (font_verbosity > 0)
    {
      printf ("Font name: %s\n", font_name);
      printf ("Max width: %d\n", font_info->max_width);
      printf ("Max height: %d\n", font_info->max_height);
      printf ("Font ascent: %d\n", font_info->asce);
      printf ("Font descent: %d\n", font_info->desc);
    }

  if (font_verbosity > 0)
    printf ("Number of glyph: %d\n", font_info->num_glyphs);

  leng = grub_cpu_to_be32 (font_info->num_glyphs * 9);
  grub_util_write_image (FONT_FORMAT_SECTION_NAMES_CHAR_INDEX,
  			 sizeof(FONT_FORMAT_SECTION_NAMES_CHAR_INDEX) - 1,
			 file, output_file);
  grub_util_write_image ((char *) &leng, 4, file, output_file);
  offset += 8 + font_info->num_glyphs * 9 + 8;

  for (cur = font_info->glyphs_sorted;
       cur < font_info->glyphs_sorted + font_info->num_glyphs; cur++)
    {
      grub_uint32_t data32;
      grub_uint8_t data8;
      data32 = grub_cpu_to_be32 (cur->char_code);
      grub_util_write_image ((char *) &data32, 4, file, output_file);
      data8 = 0;
      grub_util_write_image ((char *) &data8, 1, file, output_file);
      data32 = grub_cpu_to_be32 (offset);
      grub_util_write_image ((char *) &data32, 4, file, output_file);
      offset += 10 + cur->bitmap_size;
    }

  leng = 0xffffffff;
  grub_util_write_image (FONT_FORMAT_SECTION_NAMES_DATA,
  			 sizeof(FONT_FORMAT_SECTION_NAMES_DATA) - 1,
			 file, output_file);
  grub_util_write_image ((char *) &leng, 4, file, output_file);

  for (cur = font_info->glyphs_sorted;
       cur < font_info->glyphs_sorted + font_info->num_glyphs; cur++)
    {
      grub_uint16_t data;
      data = grub_cpu_to_be16 (cur->width);
      grub_util_write_image ((char *) &data, 2, file, output_file);
      data = grub_cpu_to_be16 (cur->height);
      grub_util_write_image ((char *) &data, 2, file, output_file);
      data = grub_cpu_to_be16 (cur->x_ofs);
      grub_util_write_image ((char *) &data, 2, file, output_file);
      data = grub_cpu_to_be16 (cur->y_ofs);
      grub_util_write_image ((char *) &data, 2, file, output_file);
      data = grub_cpu_to_be16 (cur->device_width);
      grub_util_write_image ((char *) &data, 2, file, output_file);
      grub_util_write_image ((char *) &cur->bitmap[0], cur->bitmap_size,
			     file, output_file);
    }

  free (font_name);
  fclose (file);
}

#ifndef GRUB_BUILD
static struct argp_option options[] = {
  {"output",  'o', N_("FILE"), 0, N_("save output in FILE [required]"), 0},
  /* TRANSLATORS: bitmaps are images like e.g. in JPEG.  */
  {"index",  'i', N_("NUM"), 0,
   /* TRANSLATORS: some font files may have multiple faces (fonts).
      This option is used to chose among them, the first face being '0'.
      Rarely used.  */
   N_("select face index"), 0},
  {"range",  'r', N_("FROM-TO[,FROM-TO]"), 0, 
   /* TRANSLATORS: It refers to the range of characters in font.  */
   N_("set font range"), 0},
  {"name",  'n', N_("NAME"), 0, 
   /* TRANSLATORS: "family name" for font is just a generic name without suffix
      like "Bold".  */
   N_("set font family name"), 0},
  {"size",  's', N_("SIZE"), 0, N_("set font size"), 0},
  {"desc",  'd', N_("NUM"), 0, N_("set font descent"), 0},
  {"asce",  'c', N_("NUM"), 0, N_("set font ascent"), 0},
  {"bold",  'b', 0, 0, N_("convert to bold font"), 0},
  {"force-autohint",  'a', 0, 0, N_("force autohint"), 0},
  {"no-hinting",  0x101, 0, 0, N_("disable hinting"), 0},
  {"no-bitmap",  0x100, 0, 0,
   /* TRANSLATORS: some fonts contain bitmap rendering for
      some sizes. This option forces rerendering even if
      pre-rendered bitmap is available.
    */
   N_("ignore bitmap strikes when loading"), 0},
  {"verbose",  'v', 0, 0, N_("print verbose messages."), 0},
  { 0, 0, 0, 0, 0, 0 }
};
#define my_argp_parse argp_parse
#define MY_ARGP_KEY_ARG ARGP_KEY_ARG
#define my_error_t error_t
#define MY_ARGP_ERR_UNKNOWN ARGP_ERR_UNKNOWN
#define my_argp_state argp_state

#else

#define my_error_t int
#define MY_ARGP_ERR_UNKNOWN -1
#define MY_ARGP_KEY_ARG -1
#define my_argp_parse(a, argc, argv, b, c, st) my_argp_parse_real(argc, argv, st)
struct my_argp_state
{
  void *input;
};

#endif

struct arguments
{
  struct grub_font_info font_info;
  size_t nfiles;
  size_t files_max;
  char **files;
  char *output_file;
  int font_index;
  int font_size;
  enum file_formats file_format;
};

#ifdef GRUB_BUILD

static int
has_argument (int v)
{
  return v =='o' || v == 'i' || v == 'r' || v == 'n' || v == 's'
    || v == 'd' || v == 'c';
}

#endif

static my_error_t
argp_parser (int key, char *arg, struct my_argp_state *state)
{
  /* Get the input argument from argp_parse, which we
     know is a pointer to our arguments structure. */
  struct arguments *arguments = state->input;

  switch (key)
    {
    case 'b':
      arguments->font_info.flags |= GRUB_FONT_FLAG_BOLD;
      break;

    case 0x100:
      arguments->font_info.flags |= GRUB_FONT_FLAG_NOBITMAP;
      break;

    case 0x101:
      arguments->font_info.flags |= GRUB_FONT_FLAG_NOHINTING;
      break;

    case 'a':
      arguments->font_info.flags |= GRUB_FONT_FLAG_FORCEHINT;
      break;

    case 'o':
      arguments->output_file = xstrdup (arg);
      break;

    case 'n':
      arguments->font_info.name = xstrdup (arg);
      break;

    case 'i':
      arguments->font_index = strtoul (arg, NULL, 0);
      break;

    case 's':
      arguments->font_size = strtoul (arg, NULL, 0);
      break;

    case 'r':
      {
	char *p = arg;

	while (1)
	  {
	    grub_uint32_t a, b;

	    a = strtoul (p, &p, 0);
	    if (*p != '-')
	      /* TRANSLATORS: It refers to the range of characters in font.  */
	      grub_util_error ("%s", _("invalid font range"));
	    b = strtoul (p + 1, &p, 0);
	    if ((arguments->font_info.num_range
		 & (GRUB_FONT_RANGE_BLOCK - 1)) == 0)
	      arguments->font_info.ranges = xrealloc (arguments->font_info.ranges,
						      (arguments->font_info.num_range +
						       GRUB_FONT_RANGE_BLOCK) *
						      sizeof (grub_uint32_t) * 2);

	    arguments->font_info.ranges[arguments->font_info.num_range * 2] = a;
	    arguments->font_info.ranges[arguments->font_info.num_range * 2 + 1] = b;
	    arguments->font_info.num_range++;

	    if (*p)
	      {
		if (*p != ',')
		  grub_util_error ("%s", _("invalid font range"));
		p++;
	      }
	    else
	      break;
	  }
	break;
      }

    case 'd':
      arguments->font_info.desc = strtoul (arg, NULL, 0);
      break;

    case 'c':
      arguments->font_info.asce = strtoul (arg, NULL, 0);
      break;

    case 'v':
      font_verbosity++;
      break;

    case MY_ARGP_KEY_ARG:
      assert (arguments->nfiles < arguments->files_max);
      arguments->files[arguments->nfiles++] = xstrdup(arg);
      break;

    default:
      return MY_ARGP_ERR_UNKNOWN;
    }
  return 0;
}

#ifdef GRUB_BUILD

/* We don't require host platform to have argp. In the same time configuring
   gnulib for build would result in even worse mess. So we have our
   minimalistic argp replacement just enough for build system. Most
   argp features are omitted.  */

static int
my_argp_parse_real (int argc, char **argv, void *st)
{
  int curar;
  struct my_argp_state p;

  p.input = st;

  for (curar = 1; curar < argc; )
    {
      if (argv[curar][0] == '-')
	{
	  if (has_argument (argv[curar][1])
	      && curar + 1 >= argc)
	    return 1;
	  if (has_argument (argv[curar][1]))
	    {
	      if (argp_parser (argv[curar][1], argv[curar + 1], &p))
		return 1;
	      curar += 2;
	      continue;
	    }
	  if (argp_parser (argv[curar][1], NULL, &p))
	    return 1;
	  curar++;
	  continue;
	}
      if (argp_parser (MY_ARGP_KEY_ARG, argv[curar], &p))
	return 1;
      curar++;
    }
  return 0;
}
#endif

#ifndef GRUB_BUILD
static struct argp argp = {
  options, argp_parser, N_("[OPTIONS] FONT_FILES"),
  N_("Convert common font file formats into PF2"),
  NULL, NULL, NULL
};
#endif

int
main (int argc, char *argv[])
{
  FT_Library ft_lib;
  struct arguments arguments;

#ifndef GRUB_BUILD
  grub_util_host_init (&argc, &argv);
#endif

  memset (&arguments, 0, sizeof (struct arguments));
  arguments.file_format = PF2;
  arguments.files_max = argc + 1;
  arguments.files = xmalloc ((arguments.files_max + 1)
			     * sizeof (arguments.files[0]));
  memset (arguments.files, 0, (arguments.files_max + 1)
	  * sizeof (arguments.files[0]));

  if (my_argp_parse (&argp, argc, argv, 0, 0, &arguments) != 0)
    {
      fprintf (stderr, "%s", _("Error in parsing command line arguments\n"));
      exit(1);
    }

  if (! arguments.output_file)
    grub_util_error ("%s", _("output file must be specified"));

  if (FT_Init_FreeType (&ft_lib))
    grub_util_error ("%s", _("FT_Init_FreeType fails"));

  {
    size_t i;      
    for (i = 0; i < arguments.nfiles; i++)
      {
	FT_Face ft_face;
	int size;
	FT_Error err;

	err = FT_New_Face (ft_lib, arguments.files[i],
			   arguments.font_index, &ft_face);
	if (err)
	  {
	    printf (_("can't open file %s, index %d: error %d"),
		    arguments.files[i],
		    arguments.font_index, err);
	    if (err > 0 && err < (signed) ARRAY_SIZE (ft_errmsgs))
	      printf (": %s\n", ft_errmsgs[err]);
	    else
	      printf ("\n");

	    continue;
	  }

	if ((! arguments.font_info.name) && (ft_face->family_name))
	  arguments.font_info.name = xstrdup (ft_face->family_name);

	size = arguments.font_size;
	if (! size)
	  {
	    if ((ft_face->face_flags & FT_FACE_FLAG_SCALABLE) ||
		(! ft_face->num_fixed_sizes))
	      size = GRUB_FONT_DEFAULT_SIZE;
	    else
	      size = ft_face->available_sizes[0].height;
	  }

	arguments.font_info.style = ft_face->style_flags;
	arguments.font_info.size = size;

	err = FT_Set_Pixel_Sizes (ft_face, size, size);

	if (err)
	  grub_util_error (_("can't set %dx%d font size: Freetype error %d: %s"),
			   size, size, err,
			   (err > 0 && err < (signed) ARRAY_SIZE (ft_errmsgs))
			   ? ft_errmsgs[err] : "");
	add_font (&arguments.font_info, ft_face, arguments.file_format != PF2);
	FT_Done_Face (ft_face);
      }
  }

  FT_Done_FreeType (ft_lib);

  {
    int counter[65537];
    struct grub_glyph_info *tmp, *cur;
    int i;

    memset (counter, 0, sizeof (counter));

    for (cur = arguments.font_info.glyphs_unsorted; cur; cur = cur->next)
      counter[(cur->char_code & 0xffff) + 1]++;
    for (i = 0; i < 0x10000; i++)
      counter[i+1] += counter[i];
    tmp = xmalloc (arguments.font_info.num_glyphs
		   * sizeof (tmp[0]));
    for (cur = arguments.font_info.glyphs_unsorted; cur; cur = cur->next)
      tmp[counter[(cur->char_code & 0xffff)]++] = *cur;

    memset (counter, 0, sizeof (counter));

    for (cur = tmp; cur < tmp + arguments.font_info.num_glyphs; cur++)
      counter[((cur->char_code & 0xffff0000) >> 16) + 1]++;
    for (i = 0; i < 0x10000; i++)
      counter[i+1] += counter[i];
    arguments.font_info.glyphs_sorted = xmalloc (arguments.font_info.num_glyphs
						 * sizeof (arguments.font_info.glyphs_sorted[0]));
    for (cur = tmp; cur < tmp + arguments.font_info.num_glyphs; cur++)
      arguments.font_info.glyphs_sorted[counter[(cur->char_code & 0xffff0000)
						>> 16]++] = *cur;
    free (tmp);
  }

  switch (arguments.file_format)
    {
    case PF2:
      write_font_pf2 (&arguments.font_info, arguments.output_file);
      break;
    }

  if (font_verbosity > 1)
    print_glyphs (&arguments.font_info);

  {
    size_t i;
    for (i = 0; i < arguments.nfiles; i++)
      free (arguments.files[i]);
  }

  return 0;
}
