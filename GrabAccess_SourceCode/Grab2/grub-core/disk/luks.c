/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2003,2007,2010,2011,2019  Free Software Foundation, Inc.
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

#define LUKS_KEY_ENABLED  0x00AC71F3

/* On disk LUKS header */
struct grub_luks_phdr
{
  grub_uint8_t magic[6];
#define LUKS_MAGIC        "LUKS\xBA\xBE"
  grub_uint16_t version;
  char cipherName[32];
  char cipherMode[32];
  char hashSpec[32];
  grub_uint32_t payloadOffset;
  grub_uint32_t keyBytes;
  grub_uint8_t mkDigest[20];
  grub_uint8_t mkDigestSalt[32];
  grub_uint32_t mkDigestIterations;
  char uuid[40];
  struct
  {
    grub_uint32_t active;
    grub_uint32_t passwordIterations;
    grub_uint8_t passwordSalt[32];
    grub_uint32_t keyMaterialOffset;
    grub_uint32_t stripes;
  } keyblock[8];
} GRUB_PACKED;

typedef struct grub_luks_phdr *grub_luks_phdr_t;

gcry_err_code_t AF_merge (const gcry_md_spec_t * hash, grub_uint8_t * src,
			  grub_uint8_t * dst, grub_size_t blocksize,
			  grub_size_t blocknumbers);

static grub_cryptodisk_t
configure_ciphers (grub_disk_t disk, grub_cryptomount_args_t cargs)
{
  grub_cryptodisk_t newdev;
  const char *iptr;
  struct grub_luks_phdr header;
  char *optr;
  char uuid[sizeof (header.uuid) + 1];
  char ciphername[sizeof (header.cipherName) + 1];
  char ciphermode[sizeof (header.cipherMode) + 1];
  char hashspec[sizeof (header.hashSpec) + 1];
  grub_err_t err;

  if (cargs->check_boot)
    return NULL;

  /* Read the LUKS header.  */
  err = grub_disk_read (disk, 0, 0, sizeof (header), &header);
  if (err)
    {
      if (err == GRUB_ERR_OUT_OF_RANGE)
	grub_errno = GRUB_ERR_NONE;
      return NULL;
    }

  /* Look for LUKS magic sequence.  */
  if (grub_memcmp (header.magic, LUKS_MAGIC, sizeof (header.magic))
      || grub_be_to_cpu16 (header.version) != 1)
    return NULL;

  grub_memset (uuid, 0, sizeof (uuid));
  optr = uuid;
  for (iptr = header.uuid; iptr < &header.uuid[ARRAY_SIZE (header.uuid)];
       iptr++)
    {
      if (*iptr != '-')
	*optr++ = *iptr;
    }
  *optr = 0;

  if (cargs->search_uuid != NULL && grub_strcasecmp (cargs->search_uuid, uuid) != 0)
    {
      grub_dprintf ("luks", "%s != %s\n", uuid, cargs->search_uuid);
      return NULL;
    }

  /* Make sure that strings are null terminated.  */
  grub_memcpy (ciphername, header.cipherName, sizeof (header.cipherName));
  ciphername[sizeof (header.cipherName)] = 0;
  grub_memcpy (ciphermode, header.cipherMode, sizeof (header.cipherMode));
  ciphermode[sizeof (header.cipherMode)] = 0;
  grub_memcpy (hashspec, header.hashSpec, sizeof (header.hashSpec));
  hashspec[sizeof (header.hashSpec)] = 0;

  newdev = grub_zalloc (sizeof (struct grub_cryptodisk));
  if (!newdev)
      return NULL;
  newdev->offset_sectors = grub_be_to_cpu32 (header.payloadOffset);
  newdev->source_disk = NULL;
  newdev->log_sector_size = GRUB_LUKS1_LOG_SECTOR_SIZE;
  newdev->total_sectors = grub_disk_native_sectors (disk) - newdev->offset_sectors;
  grub_memcpy (newdev->uuid, uuid, sizeof (uuid));
  newdev->modname = "luks";

  /* Configure the hash used for the AF splitter and HMAC.  */
  newdev->hash = grub_crypto_lookup_md_by_name (hashspec);
  if (!newdev->hash)
    {
      grub_free (newdev);
      grub_error (GRUB_ERR_FILE_NOT_FOUND, "Couldn't load %s hash",
		  hashspec);
      return NULL;
    }

  err = grub_cryptodisk_setcipher (newdev, ciphername, ciphermode);
  if (err)
    {
      grub_free (newdev);
      return NULL;
    }

  COMPILE_TIME_ASSERT (sizeof (newdev->uuid) >= sizeof (uuid));
  return newdev;
}

static grub_err_t
luks_recover_key (grub_disk_t source,
		  grub_cryptodisk_t dev,
		  grub_cryptomount_args_t cargs)
{
  struct grub_luks_phdr header;
  grub_size_t keysize;
  grub_uint8_t *split_key = NULL;
  grub_uint8_t candidate_digest[sizeof (header.mkDigest)];
  unsigned i;
  grub_size_t length;
  grub_err_t err;
  grub_size_t max_stripes = 1;

  if (cargs->key_data == NULL || cargs->key_len == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "no key data");

  err = grub_disk_read (source, 0, 0, sizeof (header), &header);
  if (err)
    return err;

  grub_puts_ (N_("Attempting to decrypt master key..."));
  keysize = grub_be_to_cpu32 (header.keyBytes);
  if (keysize > GRUB_CRYPTODISK_MAX_KEYLEN)
    return grub_error (GRUB_ERR_BAD_FS, "key is too long");

  for (i = 0; i < ARRAY_SIZE (header.keyblock); i++)
    if (grub_be_to_cpu32 (header.keyblock[i].active) == LUKS_KEY_ENABLED
	&& grub_be_to_cpu32 (header.keyblock[i].stripes) > max_stripes)
      max_stripes = grub_be_to_cpu32 (header.keyblock[i].stripes);

  split_key = grub_calloc (keysize, max_stripes);
  if (!split_key)
    return grub_errno;

  /* Try to recover master key from each active keyslot.  */
  for (i = 0; i < ARRAY_SIZE (header.keyblock); i++)
    {
      gcry_err_code_t gcry_err;
      grub_uint8_t candidate_key[GRUB_CRYPTODISK_MAX_KEYLEN];
      grub_uint8_t digest[GRUB_CRYPTODISK_MAX_KEYLEN];

      /* Check if keyslot is enabled.  */
      if (grub_be_to_cpu32 (header.keyblock[i].active) != LUKS_KEY_ENABLED)
	continue;

      grub_dprintf ("luks", "Trying keyslot %d\n", i);

      /* Calculate the PBKDF2 of the user supplied passphrase.  */
      gcry_err = grub_crypto_pbkdf2 (dev->hash, cargs->key_data,
				     cargs->key_len,
				     header.keyblock[i].passwordSalt,
				     sizeof (header.keyblock[i].passwordSalt),
				     grub_be_to_cpu32 (header.keyblock[i].
						       passwordIterations),
				     digest, keysize);

      if (gcry_err)
	{
	  grub_free (split_key);
	  return grub_crypto_gcry_error (gcry_err);
	}

      grub_dprintf ("luks", "PBKDF2 done\n");

      gcry_err = grub_cryptodisk_setkey (dev, digest, keysize); 
      if (gcry_err)
	{
	  grub_free (split_key);
	  return grub_crypto_gcry_error (gcry_err);
	}

      length = (keysize * grub_be_to_cpu32 (header.keyblock[i].stripes));

      /* Read and decrypt the key material from the disk.  */
      err = grub_disk_read (source,
			    grub_be_to_cpu32 (header.keyblock
					      [i].keyMaterialOffset), 0,
			    length, split_key);
      if (err)
	{
	  grub_free (split_key);
	  return err;
	}

      gcry_err = grub_cryptodisk_decrypt (dev, split_key, length, 0,
					  GRUB_LUKS1_LOG_SECTOR_SIZE);
      if (gcry_err)
	{
	  grub_free (split_key);
	  return grub_crypto_gcry_error (gcry_err);
	}

      /* Merge the decrypted key material to get the candidate master key.  */
      gcry_err = AF_merge (dev->hash, split_key, candidate_key, keysize,
			   grub_be_to_cpu32 (header.keyblock[i].stripes));
      if (gcry_err)
	{
	  grub_free (split_key);
	  return grub_crypto_gcry_error (gcry_err);
	}

      grub_dprintf ("luks", "candidate key recovered\n");

      /* Calculate the PBKDF2 of the candidate master key.  */
      gcry_err = grub_crypto_pbkdf2 (dev->hash, candidate_key,
				     grub_be_to_cpu32 (header.keyBytes),
				     header.mkDigestSalt,
				     sizeof (header.mkDigestSalt),
				     grub_be_to_cpu32
				     (header.mkDigestIterations),
				     candidate_digest,
				     sizeof (candidate_digest));
      if (gcry_err)
	{
	  grub_free (split_key);
	  return grub_crypto_gcry_error (gcry_err);
	}

      /* Compare the calculated PBKDF2 to the digest stored
         in the header to see if it's correct.  */
      if (grub_memcmp (candidate_digest, header.mkDigest,
		       sizeof (header.mkDigest)) != 0)
	{
	  grub_dprintf ("luks", "bad digest\n");
	  continue;
	}

      /* TRANSLATORS: It's a cryptographic key slot: one element of an array
	 where each element is either empty or holds a key.  */
      grub_printf_ (N_("Slot %d opened\n"), i);

      /* Set the master key.  */
      gcry_err = grub_cryptodisk_setkey (dev, candidate_key, keysize); 
      if (gcry_err)
	{
	  grub_free (split_key);
	  return grub_crypto_gcry_error (gcry_err);
	}

      grub_free (split_key);

      return GRUB_ERR_NONE;
    }

  grub_free (split_key);
  return GRUB_ACCESS_DENIED;
}

struct grub_cryptodisk_dev luks_crypto = {
  .scan = configure_ciphers,
  .recover_key = luks_recover_key
};

GRUB_MOD_INIT (luks)
{
  COMPILE_TIME_ASSERT (sizeof (((struct grub_luks_phdr *) 0)->uuid)
		       < GRUB_CRYPTODISK_MAX_UUID_LENGTH);
  grub_cryptodisk_dev_register (&luks_crypto);
}

GRUB_MOD_FINI (luks)
{
  grub_cryptodisk_dev_unregister (&luks_crypto);
}
