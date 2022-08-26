/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008  Free Software Foundation, Inc.
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

#include <grub/bitmap.h>
#include <grub/types.h>
#include <grub/normal.h>
#include <grub/dl.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/bufio.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define JPEG_ESC_CHAR		0xFF

#define JPEG_SAMPLING_1x1	0x11

enum
  {
    JPEG_MARKER_SOF0 = 0xc0,
    JPEG_MARKER_DHT  = 0xc4,
    JPEG_MARKER_SOI  = 0xd8,
    JPEG_MARKER_EOI  = 0xd9,
    JPEG_MARKER_RST0 = 0xd0,
    JPEG_MARKER_RST1 = 0xd1,
    JPEG_MARKER_RST2 = 0xd2,
    JPEG_MARKER_RST3 = 0xd3,
    JPEG_MARKER_RST4 = 0xd4,
    JPEG_MARKER_RST5 = 0xd5,
    JPEG_MARKER_RST6 = 0xd6,
    JPEG_MARKER_RST7 = 0xd7,
    JPEG_MARKER_SOS  = 0xda,
    JPEG_MARKER_DQT  = 0xdb,
    JPEG_MARKER_DRI  = 0xdd,
  };

#define SHIFT_BITS		8
#define CONST(x)		((int) ((x) * (1L << SHIFT_BITS) + 0.5))

#define JPEG_UNIT_SIZE		8

static const grub_uint8_t jpeg_zigzag_order[64] = {
  0, 1, 8, 16, 9, 2, 3, 10,
  17, 24, 32, 25, 18, 11, 4, 5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13, 6, 7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63
};

typedef int jpeg_data_unit_t[64];

struct grub_jpeg_data
{
  grub_file_t file;
  struct grub_video_bitmap **bitmap;
  grub_uint8_t *bitmap_ptr;

  unsigned image_width;
  unsigned image_height;

  grub_uint8_t *huff_value[4];
  int huff_offset[4][16];
  int huff_maxval[4][16];

  grub_uint8_t quan_table[2][64];
  int comp_index[3][3];

  jpeg_data_unit_t ydu[4];
  jpeg_data_unit_t crdu;
  jpeg_data_unit_t cbdu;

  unsigned log_vs, log_hs;
  int dri;
  unsigned r1;

  int dc_value[3];

  int color_components;

  int bit_mask, bit_save;
};

static grub_uint8_t
grub_jpeg_get_byte (struct grub_jpeg_data *data)
{
  grub_uint8_t r;

  r = 0;
  grub_file_read (data->file, &r, 1);

  return r;
}

static grub_uint16_t
grub_jpeg_get_word (struct grub_jpeg_data *data)
{
  grub_uint16_t r;

  r = 0;
  grub_file_read (data->file, &r, sizeof (grub_uint16_t));

  return grub_be_to_cpu16 (r);
}

static int
grub_jpeg_get_bit (struct grub_jpeg_data *data)
{
  int ret;

  if (data->bit_mask == 0)
    {
      data->bit_save = grub_jpeg_get_byte (data);
      if (data->bit_save == JPEG_ESC_CHAR)
	{
	  if (grub_jpeg_get_byte (data) != 0)
	    {
	      grub_error (GRUB_ERR_BAD_FILE_TYPE,
			  "jpeg: invalid 0xFF in data stream");
	      return 0;
	    }
	}
      data->bit_mask = 0x80;
    }

  ret = ((data->bit_save & data->bit_mask) != 0);
  data->bit_mask >>= 1;
  return ret;
}

static int
grub_jpeg_get_number (struct grub_jpeg_data *data, int num)
{
  int value, i, msb;

  if (num == 0)
    return 0;

  msb = value = grub_jpeg_get_bit (data);
  for (i = 1; i < num; i++)
    value = (value << 1) + (grub_jpeg_get_bit (data) != 0);
  if (!msb)
    value += 1 - (1 << num);

  return value;
}

static int
grub_jpeg_get_huff_code (struct grub_jpeg_data *data, int id)
{
  int code;
  unsigned i;

  code = 0;
  for (i = 0; i < ARRAY_SIZE (data->huff_maxval[id]); i++)
    {
      code <<= 1;
      if (grub_jpeg_get_bit (data))
	code++;
      if (code < data->huff_maxval[id][i])
	return data->huff_value[id][code + data->huff_offset[id][i]];
    }
  grub_error (GRUB_ERR_BAD_FILE_TYPE, "jpeg: huffman decode fails");
  return 0;
}

static grub_err_t
grub_jpeg_decode_huff_table (struct grub_jpeg_data *data)
{
  int id, ac, n, base, ofs;
  grub_uint32_t next_marker;
  grub_uint8_t count[16];
  unsigned i;

  next_marker = data->file->offset;
  next_marker += grub_jpeg_get_word (data);

  while (data->file->offset + sizeof (count) + 1 <= next_marker)
    {
      id = grub_jpeg_get_byte (data);
      ac = (id >> 4) & 1;
      id &= 0xF;
      if (id > 1)
	return grub_error (GRUB_ERR_BAD_FILE_TYPE,
			   "jpeg: too many huffman tables");

      if (grub_file_read (data->file, &count, sizeof (count)) !=
	  sizeof (count))
	return grub_errno;

      n = 0;
      for (i = 0; i < ARRAY_SIZE (count); i++)
	n += count[i];

      id += ac * 2;
      data->huff_value[id] = grub_malloc (n);
      if (grub_errno)
	return grub_errno;

      if (grub_file_read (data->file, data->huff_value[id], n) != n)
	return grub_errno;

      base = 0;
      ofs = 0;
      for (i = 0; i < ARRAY_SIZE (count); i++)
	{
	  base += count[i];
	  ofs += count[i];

	  data->huff_maxval[id][i] = base;
	  data->huff_offset[id][i] = ofs - base;

	  base <<= 1;
	}
    }

  if (data->file->offset != next_marker)
    grub_error (GRUB_ERR_BAD_FILE_TYPE, "jpeg: extra byte in huffman table");

  return grub_errno;
}

static grub_err_t
grub_jpeg_decode_quan_table (struct grub_jpeg_data *data)
{
  int id;
  grub_uint32_t next_marker;

  next_marker = data->file->offset;
  next_marker += grub_jpeg_get_word (data);

  if (next_marker > data->file->size)
    {
      /* Should never be set beyond the size of the file. */
      return grub_error (GRUB_ERR_BAD_FILE_TYPE, "jpeg: invalid next reference");
    }

  while (data->file->offset + sizeof (data->quan_table[id]) + 1
	 <= next_marker)
    {
      id = grub_jpeg_get_byte (data);
      if (id >= 0x10)		/* Upper 4-bit is precision.  */
	return grub_error (GRUB_ERR_BAD_FILE_TYPE,
			   "jpeg: only 8-bit precision is supported");

      if (id > 1)
	return grub_error (GRUB_ERR_BAD_FILE_TYPE,
			   "jpeg: too many quantization tables");

      if (grub_file_read (data->file, &data->quan_table[id],
			  sizeof (data->quan_table[id]))
	  != sizeof (data->quan_table[id]))
	return grub_errno;

    }

  if (data->file->offset != next_marker)
    grub_error (GRUB_ERR_BAD_FILE_TYPE,
		"jpeg: extra byte in quantization table");

  return grub_errno;
}

static grub_err_t
grub_jpeg_decode_sof (struct grub_jpeg_data *data)
{
  int i, cc;
  grub_uint32_t next_marker;

  next_marker = data->file->offset;
  next_marker += grub_jpeg_get_word (data);

  if (grub_jpeg_get_byte (data) != 8)
    return grub_error (GRUB_ERR_BAD_FILE_TYPE,
		       "jpeg: only 8-bit precision is supported");

  data->image_height = grub_jpeg_get_word (data);
  data->image_width = grub_jpeg_get_word (data);

  if ((!data->image_height) || (!data->image_width))
    return grub_error (GRUB_ERR_BAD_FILE_TYPE, "jpeg: invalid image size");

  cc = grub_jpeg_get_byte (data);
  if (cc != 1 && cc != 3)
    return grub_error (GRUB_ERR_BAD_FILE_TYPE,
		       "jpeg: component count must be 1 or 3");
  data->color_components = cc;

  for (i = 0; i < cc; i++)
    {
      int id, ss;

      id = grub_jpeg_get_byte (data) - 1;
      if ((id < 0) || (id >= 3))
	return grub_error (GRUB_ERR_BAD_FILE_TYPE, "jpeg: invalid index");

      ss = grub_jpeg_get_byte (data);	/* Sampling factor.  */
      if (!id)
	{
	  grub_uint8_t vs, hs;
	  vs = ss & 0xF;	/* Vertical sampling.  */
	  hs = ss >> 4;	/* Horizontal sampling.  */
	  if ((vs > 2) || (hs > 2) || (vs == 0) || (hs == 0))
	    return grub_error (GRUB_ERR_BAD_FILE_TYPE,
			       "jpeg: sampling method not supported");
	  data->log_vs = (vs == 2);
	  data->log_hs = (hs == 2);
	}
      else if (ss != JPEG_SAMPLING_1x1)
	return grub_error (GRUB_ERR_BAD_FILE_TYPE,
			   "jpeg: sampling method not supported");

      data->comp_index[id][0] = grub_jpeg_get_byte (data);
      if (data->comp_index[id][0] > 1)
	return grub_error (GRUB_ERR_BAD_FILE_TYPE,
			   "jpeg: too many quantization tables");
    }

  if (data->file->offset != next_marker)
    grub_error (GRUB_ERR_BAD_FILE_TYPE, "jpeg: extra byte in sof");

  return grub_errno;
}

static grub_err_t
grub_jpeg_decode_dri (struct grub_jpeg_data *data)
{
  if (grub_jpeg_get_word (data) != 4)
    return grub_error (GRUB_ERR_BAD_FILE_TYPE,
	"jpeg: DRI marker length must be 4");

  data->dri = grub_jpeg_get_word (data);

  return grub_errno;
}

static void
grub_jpeg_idct_transform (jpeg_data_unit_t du)
{
  int *pd;
  int i;
  int t0, t1, t2, t3, t4, t5, t6, t7;
  int v0, v1, v2, v3, v4;

  pd = du;
  for (i = 0; i < JPEG_UNIT_SIZE; i++, pd++)
    {
      if ((pd[JPEG_UNIT_SIZE * 1] | pd[JPEG_UNIT_SIZE * 2] |
	   pd[JPEG_UNIT_SIZE * 3] | pd[JPEG_UNIT_SIZE * 4] |
	   pd[JPEG_UNIT_SIZE * 5] | pd[JPEG_UNIT_SIZE * 6] |
	   pd[JPEG_UNIT_SIZE * 7]) == 0)
	{
	  pd[JPEG_UNIT_SIZE * 0] <<= SHIFT_BITS;

	  pd[JPEG_UNIT_SIZE * 1] = pd[JPEG_UNIT_SIZE * 2]
	    = pd[JPEG_UNIT_SIZE * 3] = pd[JPEG_UNIT_SIZE * 4]
	    = pd[JPEG_UNIT_SIZE * 5] = pd[JPEG_UNIT_SIZE * 6]
	    = pd[JPEG_UNIT_SIZE * 7] = pd[JPEG_UNIT_SIZE * 0];

	  continue;
	}

      t0 = pd[JPEG_UNIT_SIZE * 0];
      t1 = pd[JPEG_UNIT_SIZE * 2];
      t2 = pd[JPEG_UNIT_SIZE * 4];
      t3 = pd[JPEG_UNIT_SIZE * 6];

      v4 = (t1 + t3) * CONST (0.541196100);

      v0 = ((t0 + t2) << SHIFT_BITS);
      v1 = ((t0 - t2) << SHIFT_BITS);
      v2 = v4 - t3 * CONST (1.847759065);
      v3 = v4 + t1 * CONST (0.765366865);

      t0 = v0 + v3;
      t3 = v0 - v3;
      t1 = v1 + v2;
      t2 = v1 - v2;

      t4 = pd[JPEG_UNIT_SIZE * 7];
      t5 = pd[JPEG_UNIT_SIZE * 5];
      t6 = pd[JPEG_UNIT_SIZE * 3];
      t7 = pd[JPEG_UNIT_SIZE * 1];

      v0 = t4 + t7;
      v1 = t5 + t6;
      v2 = t4 + t6;
      v3 = t5 + t7;

      v4 = (v2 + v3) * CONST (1.175875602);

      v0 *= CONST (0.899976223);
      v1 *= CONST (2.562915447);
      v2 = v2 * CONST (1.961570560) - v4;
      v3 = v3 * CONST (0.390180644) - v4;

      t4 = t4 * CONST (0.298631336) - v0 - v2;
      t5 = t5 * CONST (2.053119869) - v1 - v3;
      t6 = t6 * CONST (3.072711026) - v1 - v2;
      t7 = t7 * CONST (1.501321110) - v0 - v3;

      pd[JPEG_UNIT_SIZE * 0] = t0 + t7;
      pd[JPEG_UNIT_SIZE * 7] = t0 - t7;
      pd[JPEG_UNIT_SIZE * 1] = t1 + t6;
      pd[JPEG_UNIT_SIZE * 6] = t1 - t6;
      pd[JPEG_UNIT_SIZE * 2] = t2 + t5;
      pd[JPEG_UNIT_SIZE * 5] = t2 - t5;
      pd[JPEG_UNIT_SIZE * 3] = t3 + t4;
      pd[JPEG_UNIT_SIZE * 4] = t3 - t4;
    }

  pd = du;
  for (i = 0; i < JPEG_UNIT_SIZE; i++, pd += JPEG_UNIT_SIZE)
    {
      if ((pd[1] | pd[2] | pd[3] | pd[4] | pd[5] | pd[6] | pd[7]) == 0)
	{
	  pd[0] >>= (SHIFT_BITS + 3);
	  pd[1] = pd[2] = pd[3] = pd[4] = pd[5] = pd[6] = pd[7] = pd[0];
	  continue;
	}

      v4 = (pd[2] + pd[6]) * CONST (0.541196100);

      v0 = (pd[0] + pd[4]) << SHIFT_BITS;
      v1 = (pd[0] - pd[4]) << SHIFT_BITS;
      v2 = v4 - pd[6] * CONST (1.847759065);
      v3 = v4 + pd[2] * CONST (0.765366865);

      t0 = v0 + v3;
      t3 = v0 - v3;
      t1 = v1 + v2;
      t2 = v1 - v2;

      t4 = pd[7];
      t5 = pd[5];
      t6 = pd[3];
      t7 = pd[1];

      v0 = t4 + t7;
      v1 = t5 + t6;
      v2 = t4 + t6;
      v3 = t5 + t7;

      v4 = (v2 + v3) * CONST (1.175875602);

      v0 *= CONST (0.899976223);
      v1 *= CONST (2.562915447);
      v2 = v2 * CONST (1.961570560) - v4;
      v3 = v3 * CONST (0.390180644) - v4;

      t4 = t4 * CONST (0.298631336) - v0 - v2;
      t5 = t5 * CONST (2.053119869) - v1 - v3;
      t6 = t6 * CONST (3.072711026) - v1 - v2;
      t7 = t7 * CONST (1.501321110) - v0 - v3;

      pd[0] = (t0 + t7) >> (SHIFT_BITS * 2 + 3);
      pd[7] = (t0 - t7) >> (SHIFT_BITS * 2 + 3);
      pd[1] = (t1 + t6) >> (SHIFT_BITS * 2 + 3);
      pd[6] = (t1 - t6) >> (SHIFT_BITS * 2 + 3);
      pd[2] = (t2 + t5) >> (SHIFT_BITS * 2 + 3);
      pd[5] = (t2 - t5) >> (SHIFT_BITS * 2 + 3);
      pd[3] = (t3 + t4) >> (SHIFT_BITS * 2 + 3);
      pd[4] = (t3 - t4) >> (SHIFT_BITS * 2 + 3);
    }

  for (i = 0; i < JPEG_UNIT_SIZE * JPEG_UNIT_SIZE; i++)
    {
      du[i] += 128;

      if (du[i] < 0)
	du[i] = 0;
      if (du[i] > 255)
	du[i] = 255;
    }
}

static void
grub_jpeg_decode_du (struct grub_jpeg_data *data, int id, jpeg_data_unit_t du)
{
  int h1, h2, qt;
  unsigned pos;

  grub_memset (du, 0, sizeof (jpeg_data_unit_t));

  qt = data->comp_index[id][0];
  h1 = data->comp_index[id][1];
  h2 = data->comp_index[id][2];

  data->dc_value[id] +=
    grub_jpeg_get_number (data, grub_jpeg_get_huff_code (data, h1));

  du[0] = data->dc_value[id] * (int) data->quan_table[qt][0];
  pos = 1;
  while (pos < ARRAY_SIZE (data->quan_table[qt]))
    {
      int num, val;

      num = grub_jpeg_get_huff_code (data, h2);
      if (!num)
	break;

      val = grub_jpeg_get_number (data, num & 0xF);
      num >>= 4;
      pos += num;

      if (pos >= ARRAY_SIZE (jpeg_zigzag_order))
	{
	  grub_error (GRUB_ERR_BAD_FILE_TYPE,
		      "jpeg: invalid position in zigzag order!?");
	  return;
	}

      du[jpeg_zigzag_order[pos]] = val * (int) data->quan_table[qt][pos];
      pos++;
    }

  grub_jpeg_idct_transform (du);
}

static void
grub_jpeg_ycrcb_to_rgb (int yy, int cr, int cb, grub_uint8_t * rgb)
{
  int dd;

  cr -= 128;
  cb -= 128;

  /* Red  */
  dd = yy + ((cr * CONST (1.402)) >> SHIFT_BITS);
  if (dd < 0)
    dd = 0;
  if (dd > 255)
    dd = 255;
#ifdef GRUB_CPU_WORDS_BIGENDIAN
  rgb[2] = dd;
#else
  *(rgb++) = dd;
#endif

  /* Green  */
  dd = yy - ((cb * CONST (0.34414) + cr * CONST (0.71414)) >> SHIFT_BITS);
  if (dd < 0)
    dd = 0;
  if (dd > 255)
    dd = 255;
#ifdef GRUB_CPU_WORDS_BIGENDIAN
  rgb[1] = dd;
#else
  *(rgb++) = dd;
#endif

  /* Blue  */
  dd = yy + ((cb * CONST (1.772)) >> SHIFT_BITS);
  if (dd < 0)
    dd = 0;
  if (dd > 255)
    dd = 255;
#ifdef GRUB_CPU_WORDS_BIGENDIAN
  rgb[0] = dd;
  rgb += 3;
#else
  *(rgb++) = dd;
#endif
}

static grub_err_t
grub_jpeg_decode_sos (struct grub_jpeg_data *data)
{
  int i, cc;
  grub_uint32_t data_offset;

  data_offset = data->file->offset;
  data_offset += grub_jpeg_get_word (data);

  cc = grub_jpeg_get_byte (data);

  if (cc != 3 && cc != 1)
    return grub_error (GRUB_ERR_BAD_FILE_TYPE,
		       "jpeg: component count must be 1 or 3");
  data->color_components = cc;

  for (i = 0; i < cc; i++)
    {
      int id, ht;

      id = grub_jpeg_get_byte (data) - 1;
      if ((id < 0) || (id >= 3))
	return grub_error (GRUB_ERR_BAD_FILE_TYPE, "jpeg: invalid index");

      ht = grub_jpeg_get_byte (data);
      data->comp_index[id][1] = (ht >> 4);
      data->comp_index[id][2] = (ht & 0xF) + 2;

      if ((data->comp_index[id][1] < 0) || (data->comp_index[id][1] > 3) ||
	  (data->comp_index[id][2] < 0) || (data->comp_index[id][2] > 3))
	return grub_error (GRUB_ERR_BAD_FILE_TYPE, "jpeg: invalid hufftable index");
    }

  grub_jpeg_get_byte (data);	/* Skip 3 unused bytes.  */
  grub_jpeg_get_word (data);

  if (data->file->offset != data_offset)
    return grub_error (GRUB_ERR_BAD_FILE_TYPE, "jpeg: extra byte in sos");

  if (grub_video_bitmap_create (data->bitmap, data->image_width,
				data->image_height,
				GRUB_VIDEO_BLIT_FORMAT_RGB_888))
    return grub_errno;

  data->bitmap_ptr = (*data->bitmap)->data;
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_jpeg_decode_data (struct grub_jpeg_data *data)
{
  unsigned c1, vb, hb, nr1, nc1;
  int rst = data->dri;

  vb = 8 << data->log_vs;
  hb = 8 << data->log_hs;
  nr1 = (data->image_height + vb - 1) >> (3 + data->log_vs);
  nc1 = (data->image_width + hb - 1)  >> (3 + data->log_hs);

  if (data->bitmap_ptr == NULL)
    return grub_error(GRUB_ERR_BAD_FILE_TYPE,
		      "jpeg: attempted to decode data before start of stream");

  for (; data->r1 < nr1 && (!data->dri || rst);
       data->r1++, data->bitmap_ptr += (vb * data->image_width - hb * nc1) * 3)
    for (c1 = 0;  c1 < nc1 && (!data->dri || rst);
	c1++, rst--, data->bitmap_ptr += hb * 3)
      {
	unsigned r2, c2, nr2, nc2;
	grub_uint8_t *ptr2;

	for (r2 = 0; r2 < (1U << data->log_vs); r2++)
	  for (c2 = 0; c2 < (1U << data->log_hs); c2++)
	    grub_jpeg_decode_du (data, 0, data->ydu[r2 * 2 + c2]);

	if (data->color_components >= 3)
	  {
	    grub_jpeg_decode_du (data, 1, data->cbdu);
	    grub_jpeg_decode_du (data, 2, data->crdu);
	  }

	if (grub_errno)
	  return grub_errno;

	nr2 = (data->r1 == nr1 - 1) ? (data->image_height - data->r1 * vb) : vb;
	nc2 = (c1 == nc1 - 1) ? (data->image_width - c1 * hb) : hb;

	ptr2 = data->bitmap_ptr;
	for (r2 = 0; r2 < nr2; r2++, ptr2 += (data->image_width - nc2) * 3)
	  for (c2 = 0; c2 < nc2; c2++, ptr2 += 3)
	    {
	      unsigned i0;
	      int yy;

	      i0 = (r2 >> data->log_vs) * 8 + (c2 >> data->log_hs);
	      yy = data->ydu[(r2 / 8) * 2 + (c2 / 8)][(r2 % 8) * 8 + (c2 % 8)];

	      if (data->color_components >= 3)
		{
		  int cr, cb;
		  cr = data->crdu[i0];
		  cb = data->cbdu[i0];
		  grub_jpeg_ycrcb_to_rgb (yy, cr, cb, ptr2);
		}
	      else
		{
		  ptr2[0] = yy;
		  ptr2[1] = yy;
		  ptr2[2] = yy;
		}
	    }
      }

  return grub_errno;
}

static void
grub_jpeg_reset (struct grub_jpeg_data *data)
{
  data->bit_mask = 0x0;

  data->dc_value[0] = 0;
  data->dc_value[1] = 0;
  data->dc_value[2] = 0;
}

static grub_uint8_t
grub_jpeg_get_marker (struct grub_jpeg_data *data)
{
  grub_uint8_t r;

  r = grub_jpeg_get_byte (data);

  if (r != JPEG_ESC_CHAR)
    {
      grub_error (GRUB_ERR_BAD_FILE_TYPE, "jpeg: invalid maker");
      return 0;
    }

  return grub_jpeg_get_byte (data);
}

static grub_err_t
grub_jpeg_decode_jpeg (struct grub_jpeg_data *data)
{
  if (grub_jpeg_get_marker (data) != JPEG_MARKER_SOI)	/* Start Of Image.  */
    return grub_error (GRUB_ERR_BAD_FILE_TYPE, "jpeg: invalid jpeg file");

  while (grub_errno == 0)
    {
      grub_uint8_t marker;

      marker = grub_jpeg_get_marker (data);
      if (grub_errno)
	break;

      grub_dprintf ("jpeg", "jpeg marker: %x\n", marker);

      switch (marker)
	{
	case JPEG_MARKER_DHT:	/* Define Huffman Table.  */
	  grub_jpeg_decode_huff_table (data);
	  break;
	case JPEG_MARKER_DQT:	/* Define Quantization Table.  */
	  grub_jpeg_decode_quan_table (data);
	  break;
	case JPEG_MARKER_SOF0:	/* Start Of Frame 0.  */
	  grub_jpeg_decode_sof (data);
	  break;
	case JPEG_MARKER_DRI:	/* Define Restart Interval.  */
	  grub_jpeg_decode_dri (data);
	  break;
	case JPEG_MARKER_SOS:	/* Start Of Scan.  */
	  if (grub_jpeg_decode_sos (data))
	    break;
	  /* FALLTHROUGH */
	case JPEG_MARKER_RST0:	/* Restart.  */
	case JPEG_MARKER_RST1:
	case JPEG_MARKER_RST2:
	case JPEG_MARKER_RST3:
	case JPEG_MARKER_RST4:
	case JPEG_MARKER_RST5:
	case JPEG_MARKER_RST6:
	case JPEG_MARKER_RST7:
	  grub_jpeg_decode_data (data);
	  grub_jpeg_reset (data);
	  break;
	case JPEG_MARKER_EOI:	/* End Of Image.  */
	  return grub_errno;
	default:		/* Skip unrecognized marker.  */
	  {
	    grub_uint16_t sz;

	    sz = grub_jpeg_get_word (data);
	    if (grub_errno)
	      return (grub_errno);
	    grub_file_seek (data->file, data->file->offset + sz - 2);
	  }
	}
    }

  return grub_errno;
}

static grub_err_t
grub_video_reader_jpeg (struct grub_video_bitmap **bitmap,
			const char *filename)
{
  grub_file_t file;
  struct grub_jpeg_data *data;

  file = grub_buffile_open (filename, GRUB_FILE_TYPE_PIXMAP, 0);
  if (!file)
    return grub_errno;

  data = grub_zalloc (sizeof (*data));
  if (data != NULL)
    {
      int i;

      data->file = file;
      data->bitmap = bitmap;
      grub_jpeg_decode_jpeg (data);

      for (i = 0; i < 4; i++)
	grub_free (data->huff_value[i]);

      grub_free (data);
    }

  if (grub_errno != GRUB_ERR_NONE)
    {
      grub_video_bitmap_destroy (*bitmap);
      *bitmap = 0;
    }

  grub_file_close (file);
  return grub_errno;
}

static struct grub_video_bitmap_reader jpg_reader = {
  .extension = ".jpg",
  .reader = grub_video_reader_jpeg,
  .next = 0
};

static struct grub_video_bitmap_reader jpeg_reader = {
  .extension = ".jpeg",
  .reader = grub_video_reader_jpeg,
  .next = 0
};

GRUB_MOD_INIT (jpeg)
{
  grub_video_bitmap_reader_register (&jpg_reader);
  grub_video_bitmap_reader_register (&jpeg_reader);
}

GRUB_MOD_FINI (jpeg)
{
  grub_video_bitmap_reader_unregister (&jpeg_reader);
  grub_video_bitmap_reader_unregister (&jpg_reader);
}
