/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2003,2007,2010,2011  Free Software Foundation, Inc.
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

/* This file is loosely based on FreeBSD geli implementation
   (but no code was directly copied). FreeBSD geli is distributed under
   following terms:  */
/*-
 * Copyright (c) 2005-2006 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <grub/cryptodisk.h>
#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/dl.h>
#include <grub/err.h>
#include <grub/disk.h>
#include <grub/crypto.h>
#include <grub/partition.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

/* Dirty trick to solve circular dependency.  */
#ifdef GRUB_UTIL

#include <grub/util/misc.h>

#undef GRUB_MD_SHA256
#undef GRUB_MD_SHA512

static const gcry_md_spec_t *
grub_md_sha256_real (void)
{
  const gcry_md_spec_t *ret;
  ret = grub_crypto_lookup_md_by_name ("sha256");
  if (!ret)
    grub_util_error ("%s", _("Couldn't load sha256"));
  return ret;
}

static const gcry_md_spec_t *
grub_md_sha512_real (void)
{
  const gcry_md_spec_t *ret;
  ret = grub_crypto_lookup_md_by_name ("sha512");
  if (!ret)
    grub_util_error ("%s", _("Couldn't load sha512"));
  return ret;
}

#define GRUB_MD_SHA256 grub_md_sha256_real()
#define GRUB_MD_SHA512 grub_md_sha512_real()
#endif

struct grub_geli_key
{
  grub_uint8_t iv_key[64];
  grub_uint8_t cipher_key[64];
  grub_uint8_t hmac[64];
} GRUB_PACKED;

struct grub_geli_phdr
{
  grub_uint8_t magic[16];
#define GELI_MAGIC "GEOM::ELI"
  grub_uint32_t version;
  grub_uint32_t flags;
  grub_uint16_t alg;
  grub_uint16_t keylen;
  grub_uint16_t unused3[5];
  grub_uint32_t sector_size;
  grub_uint8_t keys_used;
  grub_uint32_t niter;
  grub_uint8_t salt[64];
  struct grub_geli_key keys[2];
} GRUB_PACKED;

enum
  {
    GRUB_GELI_FLAGS_ONETIME = 1,
    GRUB_GELI_FLAGS_BOOT = 2,
  };

/* FIXME: support version 0.  */
/* FIXME: support big-endian pre-version-4 volumes.  */
/* FIXME: support for keyfiles.  */
/* FIXME: support for HMAC.  */
const char *algorithms[] = {
  [0x01] = "des",
  [0x02] = "3des",
  [0x03] = "blowfish",
  [0x04] = "cast5",
  /* FIXME: 0x05 is skipjack, but we don't have it.  */
  [0x0b] = "aes",
  /* FIXME: 0x10 is null.  */
  [0x15] = "camellia128",
  [0x16] = "aes"
};

static gcry_err_code_t
geli_rekey (struct grub_cryptodisk *dev, grub_uint64_t zoneno)
{
  gcry_err_code_t gcry_err;
  const struct {
    char magic[4];
    grub_uint64_t zone;
  } GRUB_PACKED tohash
      = { {'e', 'k', 'e', 'y'}, grub_cpu_to_le64 (zoneno) };
  GRUB_PROPERLY_ALIGNED_ARRAY (key, GRUB_CRYPTO_MAX_MDLEN);

  if (dev->hash->mdlen > GRUB_CRYPTO_MAX_MDLEN)
    return GPG_ERR_INV_ARG;

  grub_dprintf ("geli", "rekeying %" PRIuGRUB_UINT64_T " keysize=%d\n",
		zoneno, dev->rekey_derived_size);
  gcry_err = grub_crypto_hmac_buffer (dev->hash, dev->rekey_key, 64,
				      &tohash, sizeof (tohash), key);
  if (gcry_err)
    return gcry_err;

  return grub_cryptodisk_setkey (dev, (grub_uint8_t *) key,
				 dev->rekey_derived_size); 
}

static inline gcry_err_code_t
make_uuid (const struct grub_geli_phdr *header,
	   char *uuid)
{
  grub_uint8_t uuidbin[GRUB_CRYPTODISK_MAX_UUID_LENGTH];
  gcry_err_code_t err;
  grub_uint8_t *iptr;
  char *optr;

  if (2 * GRUB_MD_SHA256->mdlen + 1 > GRUB_CRYPTODISK_MAX_UUID_LENGTH)
    return GPG_ERR_TOO_LARGE;
  err = grub_crypto_hmac_buffer (GRUB_MD_SHA256,
				 header->salt, sizeof (header->salt),
				 "uuid", sizeof ("uuid") - 1, uuidbin);
  if (err)
    return err;

  optr = uuid;
  for (iptr = uuidbin; iptr < &uuidbin[GRUB_MD_SHA256->mdlen]; iptr++)
    {
      grub_snprintf (optr, 3, "%02x", *iptr);
      optr += 2;
    }
  *optr = 0;
  return GPG_ERR_NO_ERROR;
}

#ifdef GRUB_UTIL

#include <grub/emu/hostdisk.h>
#include <grub/emu/misc.h>

char *
grub_util_get_geli_uuid (const char *dev)
{
  grub_util_fd_t fd;
  grub_uint64_t s;
  unsigned log_secsize;
  grub_uint8_t hdr[512];
  struct grub_geli_phdr *header;
  char *uuid; 
  gcry_err_code_t err;

  fd = grub_util_fd_open (dev, GRUB_UTIL_FD_O_RDONLY);

  if (!GRUB_UTIL_FD_IS_VALID (fd))
    return NULL;

  s = grub_util_get_fd_size (fd, dev, &log_secsize);
  s >>= log_secsize;
  if (grub_util_fd_seek (fd, (s << log_secsize) - 512) < 0)
    grub_util_error ("%s", _("couldn't read ELI metadata"));

  uuid = xmalloc (GRUB_MD_SHA256->mdlen * 2 + 1);
  if (grub_util_fd_read (fd, (void *) &hdr, 512) < 0)
    grub_util_error ("%s", _("couldn't read ELI metadata"));

  grub_util_fd_close (fd);
	  
  COMPILE_TIME_ASSERT (sizeof (header) <= 512);
  header = (void *) &hdr;

  /* Look for GELI magic sequence.  */
  if (grub_memcmp (header->magic, GELI_MAGIC, sizeof (GELI_MAGIC))
      || grub_le_to_cpu32 (header->version) > 7
      || grub_le_to_cpu32 (header->version) < 1)
    grub_util_error ("%s", _("wrong ELI magic or version"));

  err = make_uuid ((void *) &hdr, uuid);
  if (err)
    {
      grub_free (uuid);
      return NULL;
    }

  return uuid;
}
#endif

static grub_cryptodisk_t
configure_ciphers (grub_disk_t disk, grub_cryptomount_args_t cargs)
{
  grub_cryptodisk_t newdev;
  struct grub_geli_phdr header;
  grub_crypto_cipher_handle_t cipher = NULL, secondary_cipher = NULL;
  const struct gcry_cipher_spec *ciph;
  const char *ciphername = NULL;
  gcry_err_code_t gcry_err;
  char uuid[GRUB_CRYPTODISK_MAX_UUID_LENGTH];
  grub_disk_addr_t sector;
  grub_err_t err;

  if (2 * GRUB_MD_SHA256->mdlen + 1 > GRUB_CRYPTODISK_MAX_UUID_LENGTH)
    return NULL;

  sector = grub_disk_native_sectors (disk);
  if (sector == GRUB_DISK_SIZE_UNKNOWN || sector == 0)
    return NULL;

  /* Read the GELI header.  */
  err = grub_disk_read (disk, sector - 1, 0, sizeof (header), &header);
  if (err)
    return NULL;

  /* Look for GELI magic sequence.  */
  if (grub_memcmp (header.magic, GELI_MAGIC, sizeof (GELI_MAGIC))
      || grub_le_to_cpu32 (header.version) > 7
      || grub_le_to_cpu32 (header.version) < 1)
    {
      grub_dprintf ("geli", "wrong magic %02x\n", header.magic[0]);
      return NULL;
    }

  if ((grub_le_to_cpu32 (header.sector_size)
       & (grub_le_to_cpu32 (header.sector_size) - 1))
      || grub_le_to_cpu32 (header.sector_size) == 0)
    {
      grub_dprintf ("geli", "incorrect sector size %d\n",
		    grub_le_to_cpu32 (header.sector_size));
      return NULL;
    }

  if (grub_le_to_cpu32 (header.flags) & GRUB_GELI_FLAGS_ONETIME)
    {
      grub_dprintf ("geli", "skipping one-time volume\n");
      return NULL;
    }

  if (cargs->check_boot && !(grub_le_to_cpu32 (header.flags) & GRUB_GELI_FLAGS_BOOT))
    {
      grub_dprintf ("geli", "not a boot volume\n");
      return NULL;
    }    

  gcry_err = make_uuid (&header, uuid);
  if (gcry_err)
    {
      grub_crypto_gcry_error (gcry_err);
      return NULL;
    }

  if (cargs->search_uuid != NULL && grub_strcasecmp (cargs->search_uuid, uuid) != 0)
    {
      grub_dprintf ("geli", "%s != %s\n", uuid, cargs->search_uuid);
      return NULL;
    }

  if (grub_le_to_cpu16 (header.alg) >= ARRAY_SIZE (algorithms)
      || algorithms[grub_le_to_cpu16 (header.alg)] == NULL)
    {
      grub_error (GRUB_ERR_FILE_NOT_FOUND, "Cipher 0x%x unknown",
		  grub_le_to_cpu16 (header.alg));
      return NULL;
    }

  ciphername = algorithms[grub_le_to_cpu16 (header.alg)];
  ciph = grub_crypto_lookup_cipher_by_name (ciphername);
  if (!ciph)
    {
      grub_error (GRUB_ERR_FILE_NOT_FOUND, "Cipher %s isn't available",
		  ciphername);
      return NULL;
    }

  /* Configure the cipher used for the bulk data.  */
  cipher = grub_crypto_cipher_open (ciph);
  if (!cipher)
    return NULL;

  if (grub_le_to_cpu16 (header.alg) == 0x16)
    {
      secondary_cipher = grub_crypto_cipher_open (ciph);
      if (!secondary_cipher)
	{
	  grub_crypto_cipher_close (cipher);
	  return NULL;
	}

    }

  if (grub_le_to_cpu16 (header.keylen) > 1024)
    {
      grub_error (GRUB_ERR_BAD_ARGUMENT, "invalid keysize %d",
		  grub_le_to_cpu16 (header.keylen));
      grub_crypto_cipher_close (cipher);
      grub_crypto_cipher_close (secondary_cipher);
      return NULL;
    }

  newdev = grub_zalloc (sizeof (struct grub_cryptodisk));
  if (!newdev)
    {
      grub_crypto_cipher_close (cipher);
      grub_crypto_cipher_close (secondary_cipher);
      return NULL;
    }
  newdev->cipher = cipher;
  newdev->secondary_cipher = secondary_cipher;
  newdev->offset_sectors = 0;
  newdev->source_disk = NULL;
  newdev->benbi_log = 0;
  if (grub_le_to_cpu16 (header.alg) == 0x16)
    {
      newdev->mode = GRUB_CRYPTODISK_MODE_XTS;
      newdev->mode_iv = GRUB_CRYPTODISK_MODE_IV_BYTECOUNT64;
    }
  else
    {
      newdev->mode = GRUB_CRYPTODISK_MODE_CBC;
      newdev->mode_iv = GRUB_CRYPTODISK_MODE_IV_BYTECOUNT64_HASH;
    }
  newdev->essiv_cipher = NULL;
  newdev->essiv_hash = NULL;
  newdev->hash = GRUB_MD_SHA512;
  newdev->iv_hash = GRUB_MD_SHA256;

  for (newdev->log_sector_size = 0;
       (1U << newdev->log_sector_size) < grub_le_to_cpu32 (header.sector_size);
       newdev->log_sector_size++);

  if (grub_le_to_cpu32 (header.version) >= 5)
    {
      newdev->rekey = geli_rekey;
      newdev->rekey_shift = 20;
    }

  newdev->modname = "geli";

  newdev->total_sectors = grub_disk_native_sectors (disk) - 1;
  grub_memcpy (newdev->uuid, uuid, sizeof (newdev->uuid));
  COMPILE_TIME_ASSERT (sizeof (newdev->uuid) >= 32 * 2 + 1);
  return newdev;
}

static grub_err_t
recover_key (grub_disk_t source, grub_cryptodisk_t dev, grub_cryptomount_args_t cargs)
{
  grub_size_t keysize;
  grub_uint8_t digest[GRUB_CRYPTO_MAX_MDLEN];
  grub_uint8_t geomkey[GRUB_CRYPTO_MAX_MDLEN];
  grub_uint8_t verify_key[GRUB_CRYPTO_MAX_MDLEN];
  grub_uint8_t zero[GRUB_CRYPTO_MAX_CIPHER_BLOCKSIZE];
  grub_uint8_t geli_cipher_key[64];
  unsigned i;
  gcry_err_code_t gcry_err;
  struct grub_geli_phdr header;
  grub_disk_addr_t sector;
  grub_err_t err;

  if (cargs->key_data == NULL || cargs->key_len == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "no key data");

  if (dev->cipher->cipher->blocksize > GRUB_CRYPTO_MAX_CIPHER_BLOCKSIZE)
    return grub_error (GRUB_ERR_BUG, "cipher block is too long");

  if (dev->hash->mdlen > GRUB_CRYPTO_MAX_MDLEN)
    return grub_error (GRUB_ERR_BUG, "mdlen is too long");

  sector = grub_disk_native_sectors (source);
  if (sector == GRUB_DISK_SIZE_UNKNOWN || sector == 0)
    return grub_error (GRUB_ERR_BUG, "not a geli");

  /* Read the GELI header.  */
  err = grub_disk_read (source, sector - 1, 0, sizeof (header), &header);
  if (err)
    return err;

  keysize = grub_le_to_cpu16 (header.keylen) / GRUB_CHAR_BIT;
  grub_memset (zero, 0, sizeof (zero));

  grub_puts_ (N_("Attempting to decrypt master key..."));

  /* Calculate the PBKDF2 of the user supplied passphrase.  */
  if (grub_le_to_cpu32 (header.niter) != 0)
    {
      grub_uint8_t pbkdf_key[64];
      gcry_err = grub_crypto_pbkdf2 (dev->hash, cargs->key_data,
				     cargs->key_len,
				     header.salt,
				     sizeof (header.salt),
				     grub_le_to_cpu32 (header.niter),
				     pbkdf_key, sizeof (pbkdf_key));

      if (gcry_err)
	return grub_crypto_gcry_error (gcry_err);

      gcry_err = grub_crypto_hmac_buffer (dev->hash, NULL, 0, pbkdf_key,
					  sizeof (pbkdf_key), geomkey);
      if (gcry_err)
	return grub_crypto_gcry_error (gcry_err);
    }
  else
    {
      struct grub_crypto_hmac_handle *hnd;

      hnd = grub_crypto_hmac_init (dev->hash, NULL, 0);
      if (!hnd)
	return grub_crypto_gcry_error (GPG_ERR_OUT_OF_MEMORY);

      grub_crypto_hmac_write (hnd, header.salt, sizeof (header.salt));
      grub_crypto_hmac_write (hnd, cargs->key_data, cargs->key_len);

      gcry_err = grub_crypto_hmac_fini (hnd, geomkey);
      if (gcry_err)
	return grub_crypto_gcry_error (gcry_err);
    }

  gcry_err = grub_crypto_hmac_buffer (dev->hash, geomkey,
				      dev->hash->mdlen, "\1", 1, digest);
  if (gcry_err)
    return grub_crypto_gcry_error (gcry_err);

  gcry_err = grub_crypto_hmac_buffer (dev->hash, geomkey,
				      dev->hash->mdlen, "\0", 1, verify_key);
  if (gcry_err)
    return grub_crypto_gcry_error (gcry_err);

  grub_dprintf ("geli", "keylen = %" PRIuGRUB_SIZE "\n", keysize);

  /* Try to recover master key from each active keyslot.  */
  for (i = 0; i < ARRAY_SIZE (header.keys); i++)
    {
      struct grub_geli_key candidate_key;
      grub_uint8_t key_hmac[GRUB_CRYPTO_MAX_MDLEN];

      /* Check if keyslot is enabled.  */
      if (! (header.keys_used & (1 << i)))
	  continue;

      grub_dprintf ("geli", "Trying keyslot %d\n", i);

      gcry_err = grub_crypto_cipher_set_key (dev->cipher,
					     digest, keysize);
      if (gcry_err)
	return grub_crypto_gcry_error (gcry_err);

      gcry_err = grub_crypto_cbc_decrypt (dev->cipher, &candidate_key,
					  &header.keys[i],
					  sizeof (candidate_key),
					  zero);
      if (gcry_err)
	return grub_crypto_gcry_error (gcry_err);

      gcry_err = grub_crypto_hmac_buffer (dev->hash, verify_key,
					  dev->hash->mdlen,
					  &candidate_key,
					  (sizeof (candidate_key)
					   - sizeof (candidate_key.hmac)),
					  key_hmac);
      if (gcry_err)
	return grub_crypto_gcry_error (gcry_err);

      if (grub_memcmp (candidate_key.hmac, key_hmac, dev->hash->mdlen) != 0)
	continue;
      grub_printf_ (N_("Slot %d opened\n"), i);

      if (grub_le_to_cpu32 (header.version) >= 7)
        {
          /* GELI >=7 uses the cipher_key */
	  grub_memcpy (geli_cipher_key, candidate_key.cipher_key,
		sizeof (candidate_key.cipher_key));
        }
      else
        {
          /* GELI <=6 uses the iv_key */
	  grub_memcpy (geli_cipher_key, candidate_key.iv_key,
		sizeof (candidate_key.iv_key));
        }

      /* Set the master key.  */
      if (!dev->rekey)
	{
	  grub_size_t real_keysize = keysize;
	  if (grub_le_to_cpu16 (header.alg) == 0x16)
	    real_keysize *= 2;
	  gcry_err = grub_cryptodisk_setkey (dev, candidate_key.cipher_key,
					     real_keysize); 
	  if (gcry_err)
	    return grub_crypto_gcry_error (gcry_err);
	}
      else
	{
	  grub_size_t real_keysize = keysize;
	  if (grub_le_to_cpu16 (header.alg) == 0x16)
	    real_keysize *= 2;

	  grub_memcpy (dev->rekey_key, geli_cipher_key,
		       sizeof (geli_cipher_key));
	  dev->rekey_derived_size = real_keysize;
	  dev->last_rekey = -1;
	  COMPILE_TIME_ASSERT (sizeof (dev->rekey_key)
		       >= sizeof (geli_cipher_key));
	}

      dev->iv_prefix_len = sizeof (candidate_key.iv_key);
      grub_memcpy (dev->iv_prefix, candidate_key.iv_key,
		   sizeof (candidate_key.iv_key));

      COMPILE_TIME_ASSERT (sizeof (dev->iv_prefix) >= sizeof (candidate_key.iv_key));

      return GRUB_ERR_NONE;
    }

  return GRUB_ACCESS_DENIED;
}

struct grub_cryptodisk_dev geli_crypto = {
  .scan = configure_ciphers,
  .recover_key = recover_key
};

GRUB_MOD_INIT (geli)
{
  grub_cryptodisk_dev_register (&geli_crypto);
}

GRUB_MOD_FINI (geli)
{
  grub_cryptodisk_dev_unregister (&geli_crypto);
}
