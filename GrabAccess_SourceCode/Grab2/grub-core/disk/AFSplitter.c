/*
 * AFsplitter - Anti forensic information splitter
 * Copyright 2004, Clemens Fruhwirth <clemens@endorphin.org>
 *
 * AFsplitter diffuses information over a large stripe of data,
 * therefor supporting secure data destruction.
 *
 *  This program is grub_free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the grub_free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the grub_free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <grub/crypto.h>
#include <grub/dl.h>
#include <grub/mm.h>
#include <grub/misc.h>

GRUB_MOD_LICENSE ("GPLv2+");

gcry_err_code_t AF_merge (const gcry_md_spec_t * hash, grub_uint8_t * src,
			  grub_uint8_t * dst, grub_size_t blocksize,
			  grub_size_t blocknumbers);

static void
diffuse (const gcry_md_spec_t * hash, grub_uint8_t * src,
	 grub_uint8_t * dst, grub_size_t size)
{
  grub_size_t i;
  grub_uint32_t IV;		/* host byte order independend hash IV */

  grub_size_t fullblocks = size / hash->mdlen;
  int padding = size % hash->mdlen;
  grub_uint8_t final[GRUB_CRYPTO_MAX_MDLEN];
  grub_uint8_t temp[sizeof (IV) + GRUB_CRYPTO_MAX_MDLEN];

  /* hash block the whole data set with different IVs to produce
   * more than just a single data block
   */
  for (i = 0; i < fullblocks; i++)
    {
      IV = grub_cpu_to_be32 (i);
      grub_memcpy (temp, &IV, sizeof (IV));
      grub_memcpy (temp + sizeof (IV), src + hash->mdlen * i, hash->mdlen);
      grub_crypto_hash (hash, dst + hash->mdlen * i, temp,
			sizeof (IV) + hash->mdlen);
    }

  if (padding)
    {
      IV = grub_cpu_to_be32 (i);
      grub_memcpy (temp, &IV, sizeof (IV));
      grub_memcpy (temp + sizeof (IV), src + hash->mdlen * i, padding);
      grub_crypto_hash (hash, final, temp, sizeof (IV) + padding);
      grub_memcpy (dst + hash->mdlen * i, final, padding);
    }
}

/**
 * Merges the splitted master key stored on disk into the original key
 */
gcry_err_code_t
AF_merge (const gcry_md_spec_t * hash, grub_uint8_t * src, grub_uint8_t * dst,
	  grub_size_t blocksize, grub_size_t blocknumbers)
{
  grub_size_t i;
  grub_uint8_t *bufblock;

  if (hash->mdlen > GRUB_CRYPTO_MAX_MDLEN || hash->mdlen == 0)
    return GPG_ERR_INV_ARG;

  bufblock = grub_zalloc (blocksize);
  if (bufblock == NULL)
    return GPG_ERR_OUT_OF_MEMORY;

  grub_memset (bufblock, 0, blocksize);
  for (i = 0; i < blocknumbers - 1; i++)
    {
      grub_crypto_xor (bufblock, src + (blocksize * i), bufblock, blocksize);
      diffuse (hash, bufblock, bufblock, blocksize);
    }
  grub_crypto_xor (dst, src + (i * blocksize), bufblock, blocksize);

  grub_free (bufblock);
  return GPG_ERR_NO_ERROR;
}
