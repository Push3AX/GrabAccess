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

#include <base64.h>
#include <json.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define LUKS_MAGIC_1ST "LUKS\xBA\xBE"
#define LUKS_MAGIC_2ND "SKUL\xBA\xBE"

enum grub_luks2_kdf_type
{
  LUKS2_KDF_TYPE_ARGON2I,
  LUKS2_KDF_TYPE_PBKDF2
};
typedef enum grub_luks2_kdf_type grub_luks2_kdf_type_t;

/* On disk LUKS header */
struct grub_luks2_header
{
  char		magic[6];
  grub_uint16_t version;
  grub_uint64_t hdr_size;
  grub_uint64_t seqid;
  char		label[48];
  char		csum_alg[32];
  grub_uint8_t	salt[64];
  char		uuid[40];
  char		subsystem[48];
  grub_uint64_t	hdr_offset;
  char		_padding[184];
  grub_uint8_t	csum[64];
  char		_padding4096[7*512];
} GRUB_PACKED;
typedef struct grub_luks2_header grub_luks2_header_t;

struct grub_luks2_keyslot
{
  /* The integer key to the associative array of keyslots. */
  grub_uint64_t idx;
  grub_int64_t key_size;
  grub_int64_t priority;
  struct
  {
    const char	  *encryption;
    grub_uint64_t offset;
    grub_uint64_t size;
    grub_int64_t  key_size;
  } area;
  struct
  {
    const char	 *hash;
    grub_int64_t stripes;
  } af;
  struct
  {
    grub_luks2_kdf_type_t type;
    const char		  *salt;
    union
    {
      struct
      {
	grub_int64_t time;
	grub_int64_t memory;
	grub_int64_t cpus;
      } argon2i;
      struct
      {
	const char   *hash;
	grub_int64_t iterations;
      } pbkdf2;
    } u;
  } kdf;
};
typedef struct grub_luks2_keyslot grub_luks2_keyslot_t;

struct grub_luks2_segment
{
  grub_uint64_t idx;
  grub_uint64_t offset;
  const char	*size;
  const char	*encryption;
  grub_int64_t	sector_size;
};
typedef struct grub_luks2_segment grub_luks2_segment_t;

struct grub_luks2_digest
{
  grub_uint64_t idx;
  /* Both keyslots and segments are interpreted as bitfields here */
  grub_uint64_t	keyslots;
  grub_uint64_t	segments;
  const char	*salt;
  const char	*digest;
  const char	*hash;
  grub_int64_t	iterations;
};
typedef struct grub_luks2_digest grub_luks2_digest_t;

gcry_err_code_t AF_merge (const gcry_md_spec_t * hash, grub_uint8_t * src,
			  grub_uint8_t * dst, grub_size_t blocksize,
			  grub_size_t blocknumbers);

static grub_err_t
luks2_parse_keyslot (grub_luks2_keyslot_t *out, const grub_json_t *keyslot)
{
  grub_json_t area, af, kdf;
  const char *type;

  if (grub_json_getstring (&type, keyslot, "type"))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Missing or invalid keyslot");
  else if (grub_strcmp (type, "luks2"))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Unsupported keyslot type %s", type);
  else if (grub_json_getint64 (&out->key_size, keyslot, "key_size"))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Missing keyslot information");
  if (grub_json_getint64 (&out->priority, keyslot, "priority"))
    out->priority = 1;

  if (grub_json_getvalue (&area, keyslot, "area") ||
      grub_json_getstring (&type, &area, "type"))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Missing or invalid key area");
  else if (grub_strcmp (type, "raw"))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Unsupported key area type: %s", type);
  else if (grub_json_getuint64 (&out->area.offset, &area, "offset") ||
	   grub_json_getuint64 (&out->area.size, &area, "size") ||
	   grub_json_getstring (&out->area.encryption, &area, "encryption") ||
	   grub_json_getint64 (&out->area.key_size, &area, "key_size"))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Missing key area information");

  if (grub_json_getvalue (&kdf, keyslot, "kdf") ||
      grub_json_getstring (&type, &kdf, "type") ||
      grub_json_getstring (&out->kdf.salt, &kdf, "salt"))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Missing or invalid KDF");
  else if (!grub_strcmp (type, "argon2i") || !grub_strcmp (type, "argon2id"))
    {
      out->kdf.type = LUKS2_KDF_TYPE_ARGON2I;
      if (grub_json_getint64 (&out->kdf.u.argon2i.time, &kdf, "time") ||
	  grub_json_getint64 (&out->kdf.u.argon2i.memory, &kdf, "memory") ||
	  grub_json_getint64 (&out->kdf.u.argon2i.cpus, &kdf, "cpus"))
	return grub_error (GRUB_ERR_BAD_ARGUMENT, "Missing Argon2i parameters");
    }
  else if (!grub_strcmp (type, "pbkdf2"))
    {
      out->kdf.type = LUKS2_KDF_TYPE_PBKDF2;
      if (grub_json_getstring (&out->kdf.u.pbkdf2.hash, &kdf, "hash") ||
	  grub_json_getint64 (&out->kdf.u.pbkdf2.iterations, &kdf, "iterations"))
	return grub_error (GRUB_ERR_BAD_ARGUMENT, "Missing PBKDF2 parameters");
    }
  else
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Unsupported KDF type %s", type);

  if (grub_json_getvalue (&af, keyslot, "af") ||
      grub_json_getstring (&type, &af, "type"))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "missing or invalid area");
  if (grub_strcmp (type, "luks1"))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Unsupported AF type %s", type);
  if (grub_json_getint64 (&out->af.stripes, &af, "stripes") ||
      grub_json_getstring (&out->af.hash, &af, "hash"))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Missing AF parameters");

  return GRUB_ERR_NONE;
}

static grub_err_t
luks2_parse_segment (grub_luks2_segment_t *out, const grub_json_t *segment)
{
  const char *type;

  if (grub_json_getstring (&type, segment, "type"))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Invalid segment type");
  else if (grub_strcmp (type, "crypt"))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Unsupported segment type %s", type);

  if (grub_json_getuint64 (&out->offset, segment, "offset") ||
      grub_json_getstring (&out->size, segment, "size") ||
      grub_json_getstring (&out->encryption, segment, "encryption") ||
      grub_json_getint64 (&out->sector_size, segment, "sector_size"))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Missing segment parameters");

  return GRUB_ERR_NONE;
}

static grub_err_t
luks2_parse_digest (grub_luks2_digest_t *out, const grub_json_t *digest)
{
  grub_json_t segments, keyslots, o;
  grub_size_t i, size;
  grub_uint64_t bit;
  const char *type;

  if (grub_json_getstring (&type, digest, "type"))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Invalid digest type");
  else if (grub_strcmp (type, "pbkdf2"))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Unsupported digest type %s", type);

  if (grub_json_getvalue (&segments, digest, "segments") ||
      grub_json_getvalue (&keyslots, digest, "keyslots") ||
      grub_json_getstring (&out->salt, digest, "salt") ||
      grub_json_getstring (&out->digest, digest, "digest") ||
      grub_json_getstring (&out->hash, digest, "hash") ||
      grub_json_getint64 (&out->iterations, digest, "iterations"))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Missing digest parameters");

  if (grub_json_getsize (&size, &segments))
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       "Digest references no segments");

  out->segments = 0;
  for (i = 0; i < size; i++)
    {
      if (grub_json_getchild (&o, &segments, i) ||
	  grub_json_getuint64 (&bit, &o, NULL))
	return grub_error (GRUB_ERR_BAD_ARGUMENT, "Invalid segment");
      out->segments |= (1 << bit);
    }

  if (grub_json_getsize (&size, &keyslots))
    return grub_error (GRUB_ERR_BAD_ARGUMENT,
		       "Digest references no keyslots");

  out->keyslots = 0;
  for (i = 0; i < size; i++)
    {
      if (grub_json_getchild (&o, &keyslots, i) ||
	  grub_json_getuint64 (&bit, &o, NULL))
	return grub_error (GRUB_ERR_BAD_ARGUMENT, "Invalid keyslot");
      out->keyslots |= (1 << bit);
    }

  return GRUB_ERR_NONE;
}

static grub_err_t
luks2_get_keyslot (grub_luks2_keyslot_t *k, grub_luks2_digest_t *d, grub_luks2_segment_t *s,
		   const grub_json_t *root, grub_size_t keyslot_json_idx)
{
  grub_json_t keyslots, keyslot, digests, digest, segments, segment;
  grub_size_t json_idx, size;

  /* Get nth keyslot */
  if (grub_json_getvalue (&keyslots, root, "keyslots") ||
      grub_json_getchild (&keyslot, &keyslots, keyslot_json_idx) ||
      grub_json_getuint64 (&k->idx, &keyslot, NULL) ||
      grub_json_getchild (&keyslot, &keyslot, 0) ||
      luks2_parse_keyslot (k, &keyslot))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Could not parse keyslot index %" PRIuGRUB_SIZE, keyslot_json_idx);

  /* Get digest that matches the keyslot. */
  if (grub_json_getvalue (&digests, root, "digests") ||
      grub_json_getsize (&size, &digests))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Could not get digests");
  for (json_idx = 0; json_idx < size; json_idx++)
    {
      if (grub_json_getchild (&digest, &digests, json_idx) ||
	  grub_json_getuint64 (&d->idx, &digest, NULL) ||
	  grub_json_getchild (&digest, &digest, 0) ||
	  luks2_parse_digest (d, &digest))
	return grub_error (GRUB_ERR_BAD_ARGUMENT, "Could not parse digest index %" PRIuGRUB_SIZE, json_idx);

      if ((d->keyslots & (1 << k->idx)))
	break;
    }
  if (json_idx == size)
      return grub_error (GRUB_ERR_FILE_NOT_FOUND, "No digest for keyslot \"%" PRIuGRUB_UINT64_T "\"", k->idx);

  /* Get segment that matches the digest. */
  if (grub_json_getvalue (&segments, root, "segments") ||
      grub_json_getsize (&size, &segments))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Could not get segments");
  for (json_idx = 0; json_idx < size; json_idx++)
    {
      if (grub_json_getchild (&segment, &segments, json_idx) ||
	  grub_json_getuint64 (&s->idx, &segment, NULL) ||
	  grub_json_getchild (&segment, &segment, 0) ||
	  luks2_parse_segment (s, &segment))
	return grub_error (GRUB_ERR_BAD_ARGUMENT, "Could not parse segment index %" PRIuGRUB_SIZE, json_idx);

      if ((d->segments & (1 << s->idx)))
	break;
    }
  if (json_idx == size)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, "No segment for digest \"%" PRIuGRUB_UINT64_T "\"", d->idx);

  return GRUB_ERR_NONE;
}

/* Determine whether to use primary or secondary header */
static grub_err_t
luks2_read_header (grub_disk_t disk, grub_luks2_header_t *outhdr)
{
  grub_luks2_header_t primary, secondary, *header = &primary;
  grub_err_t ret;

  /* Read the primary LUKS header. */
  ret = grub_disk_read (disk, 0, 0, sizeof (primary), &primary);
  if (ret)
    return ret;

  /* Look for LUKS magic sequence.  */
  if (grub_memcmp (primary.magic, LUKS_MAGIC_1ST, sizeof (primary.magic)) ||
      grub_be_to_cpu16 (primary.version) != 2)
    return GRUB_ERR_BAD_SIGNATURE;

  /* Read the secondary header. */
  ret = grub_disk_read (disk, 0, grub_be_to_cpu64 (primary.hdr_size), sizeof (secondary), &secondary);
  if (ret)
    return ret;

  /* Look for LUKS magic sequence.  */
  if (grub_memcmp (secondary.magic, LUKS_MAGIC_2ND, sizeof (secondary.magic)) ||
      grub_be_to_cpu16 (secondary.version) != 2)
    return GRUB_ERR_BAD_SIGNATURE;

  if (grub_be_to_cpu64 (primary.seqid) < grub_be_to_cpu64 (secondary.seqid))
      header = &secondary;
  grub_memcpy (outhdr, header, sizeof (*header));

  return GRUB_ERR_NONE;
}

static grub_cryptodisk_t
luks2_scan (grub_disk_t disk, grub_cryptomount_args_t cargs)
{
  grub_cryptodisk_t cryptodisk;
  grub_luks2_header_t header;
  char uuid[sizeof (header.uuid) + 1];
  grub_size_t i, j;

  if (cargs->check_boot)
    return NULL;

  if (luks2_read_header (disk, &header))
    {
      grub_errno = GRUB_ERR_NONE;
      return NULL;
    }

  for (i = 0, j = 0; i < sizeof (header.uuid); i++)
    if (header.uuid[i] != '-')
      uuid[j++] = header.uuid[i];
  uuid[j] = '\0';

  if (cargs->search_uuid != NULL && grub_strcasecmp (cargs->search_uuid, uuid) != 0)
    {
      grub_dprintf ("luks2", "%s != %s\n", uuid, cargs->search_uuid);
      return NULL;
    }

  cryptodisk = grub_zalloc (sizeof (*cryptodisk));
  if (!cryptodisk)
    return NULL;

  COMPILE_TIME_ASSERT (sizeof (cryptodisk->uuid) >= sizeof (uuid));
  grub_memcpy (cryptodisk->uuid, uuid, sizeof (uuid));

  cryptodisk->modname = "luks2";
  return cryptodisk;
}

static grub_err_t
luks2_verify_key (grub_luks2_digest_t *d, grub_uint8_t *candidate_key,
		  grub_size_t candidate_key_len)
{
  grub_uint8_t candidate_digest[GRUB_CRYPTODISK_MAX_KEYLEN];
  grub_uint8_t digest[GRUB_CRYPTODISK_MAX_KEYLEN], salt[GRUB_CRYPTODISK_MAX_KEYLEN];
  grub_size_t saltlen = sizeof (salt), digestlen = sizeof (digest);
  const gcry_md_spec_t *hash;
  gcry_err_code_t gcry_ret;

  /* Decode both digest and salt */
  if (!base64_decode (d->digest, grub_strlen (d->digest), (char *)digest, &digestlen))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Invalid digest");
  if (!base64_decode (d->salt, grub_strlen (d->salt), (char *)salt, &saltlen))
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "Invalid digest salt");

  /* Configure the hash used for the digest. */
  hash = grub_crypto_lookup_md_by_name (d->hash);
  if (!hash)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND, "Couldn't load %s hash", d->hash);

  /* Calculate the candidate key's digest */
  gcry_ret = grub_crypto_pbkdf2 (hash,
				 candidate_key, candidate_key_len,
				 salt, saltlen,
				 d->iterations,
				 candidate_digest, digestlen);
  if (gcry_ret)
    return grub_crypto_gcry_error (gcry_ret);

  if (grub_memcmp (candidate_digest, digest, digestlen) != 0)
    return grub_error (GRUB_ERR_ACCESS_DENIED, "Mismatching digests");

  return GRUB_ERR_NONE;
}

static grub_err_t
luks2_decrypt_key (grub_uint8_t *out_key,
		   grub_disk_t source, grub_cryptodisk_t crypt,
		   grub_luks2_keyslot_t *k,
		   const grub_uint8_t *passphrase, grub_size_t passphraselen)
{
  grub_uint8_t area_key[GRUB_CRYPTODISK_MAX_KEYLEN];
  grub_uint8_t salt[GRUB_CRYPTODISK_MAX_KEYLEN];
  grub_uint8_t *split_key = NULL;
  grub_size_t saltlen = sizeof (salt);
  char cipher[32], *p;
  const gcry_md_spec_t *hash;
  gcry_err_code_t gcry_ret;
  grub_err_t ret;

  if (!base64_decode (k->kdf.salt, grub_strlen (k->kdf.salt),
		     (char *)salt, &saltlen))
    {
      ret = grub_error (GRUB_ERR_BAD_ARGUMENT, "Invalid keyslot salt");
      goto err;
    }

  /* Calculate the binary area key of the user supplied passphrase. */
  switch (k->kdf.type)
    {
      case LUKS2_KDF_TYPE_ARGON2I:
	ret = grub_error (GRUB_ERR_BAD_ARGUMENT, "Argon2 not supported");
	goto err;
      case LUKS2_KDF_TYPE_PBKDF2:
	hash = grub_crypto_lookup_md_by_name (k->kdf.u.pbkdf2.hash);
	if (!hash)
	  {
	    ret = grub_error (GRUB_ERR_FILE_NOT_FOUND, "Couldn't load %s hash",
			      k->kdf.u.pbkdf2.hash);
	    goto err;
	  }

	gcry_ret = grub_crypto_pbkdf2 (hash, (grub_uint8_t *) passphrase,
				       passphraselen,
				       salt, saltlen,
				       k->kdf.u.pbkdf2.iterations,
				       area_key, k->area.key_size);
	if (gcry_ret)
	  {
	    ret = grub_crypto_gcry_error (gcry_ret);
	    goto err;
	  }

	break;
    }

  /* Set up disk encryption parameters for the key area */
  grub_strncpy (cipher, k->area.encryption, sizeof (cipher));
  p = grub_memchr (cipher, '-', grub_strlen (cipher));
  if (!p)
      return grub_error (GRUB_ERR_BAD_ARGUMENT, "Invalid encryption");
  *p = '\0';

  ret = grub_cryptodisk_setcipher (crypt, cipher, p + 1);
  if (ret)
      return ret;

  gcry_ret = grub_cryptodisk_setkey (crypt, area_key, k->area.key_size);
  if (gcry_ret)
    {
      ret = grub_crypto_gcry_error (gcry_ret);
      goto err;
    }

 /* Read and decrypt the binary key area with the area key. */
  split_key = grub_malloc (k->area.size);
  if (!split_key)
    {
      ret = grub_errno;
      goto err;
    }

  grub_errno = GRUB_ERR_NONE;
  ret = grub_disk_read (source, 0, k->area.offset, k->area.size, split_key);
  if (ret)
    {
      grub_error (GRUB_ERR_IO, "Read error: %s\n", grub_errmsg);
      goto err;
    }

  /*
   * The key slots area is always encrypted in 512-byte sectors,
   * regardless of encrypted data sector size.
   */
  gcry_ret = grub_cryptodisk_decrypt (crypt, split_key, k->area.size, 0,
				      GRUB_LUKS1_LOG_SECTOR_SIZE);
  if (gcry_ret)
    {
      ret = grub_crypto_gcry_error (gcry_ret);
      goto err;
    }

  /* Configure the hash used for anti-forensic merging. */
  hash = grub_crypto_lookup_md_by_name (k->af.hash);
  if (!hash)
    {
      ret = grub_error (GRUB_ERR_FILE_NOT_FOUND, "Couldn't load %s hash",
			k->af.hash);
      goto err;
    }

  /* Merge the decrypted key material to get the candidate master key. */
  gcry_ret = AF_merge (hash, split_key, out_key, k->key_size, k->af.stripes);
  if (gcry_ret)
    {
      ret = grub_crypto_gcry_error (gcry_ret);
      goto err;
    }

  grub_dprintf ("luks2", "Candidate key recovered\n");

 err:
  grub_free (split_key);
  return ret;
}

static grub_err_t
luks2_recover_key (grub_disk_t source,
		   grub_cryptodisk_t crypt,
		   grub_cryptomount_args_t cargs)
{
  grub_uint8_t candidate_key[GRUB_CRYPTODISK_MAX_KEYLEN];
  char cipher[32], *json_header = NULL, *ptr;
  grub_size_t candidate_key_len = 0, json_idx, size;
  grub_luks2_header_t header;
  grub_luks2_keyslot_t keyslot;
  grub_luks2_digest_t digest;
  grub_luks2_segment_t segment;
  gcry_err_code_t gcry_ret;
  grub_json_t *json = NULL, keyslots;
  grub_err_t ret;

  if (cargs->key_data == NULL || cargs->key_len == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "no key data");

  ret = luks2_read_header (source, &header);
  if (ret)
    return ret;

  json_header = grub_zalloc (grub_be_to_cpu64 (header.hdr_size) - sizeof (header));
  if (!json_header)
      return GRUB_ERR_OUT_OF_MEMORY;

  /* Read the JSON area. */
  ret = grub_disk_read (source, 0, grub_be_to_cpu64 (header.hdr_offset) + sizeof (header),
			grub_be_to_cpu64 (header.hdr_size) - sizeof (header), json_header);
  if (ret)
      goto err;

  ptr = grub_memchr (json_header, 0, grub_be_to_cpu64 (header.hdr_size) - sizeof (header));
  if (!ptr)
    goto err;

  ret = grub_json_parse (&json, json_header, grub_be_to_cpu64 (header.hdr_size));
  if (ret)
    {
      ret = grub_error (GRUB_ERR_BAD_ARGUMENT, "Invalid LUKS2 JSON header");
      goto err;
    }

  if (grub_json_getvalue (&keyslots, json, "keyslots") ||
      grub_json_getsize (&size, &keyslots))
    {
      ret = grub_error (GRUB_ERR_BAD_ARGUMENT, "Could not get keyslots");
      goto err;
    }

  if (grub_disk_native_sectors (source) == GRUB_DISK_SIZE_UNKNOWN)
    {
      /* FIXME: Allow use of source disk, and maybe cause errors in read. */
      grub_dprintf ("luks2", "Source disk %s has an unknown size, "
			     "conservatively returning error\n", source->name);
      ret = grub_error (GRUB_ERR_BUG, "Unknown size of luks2 source device");
      goto err;
    }

  /* Try all keyslot */
  for (json_idx = 0; json_idx < size; json_idx++)
    {
      char indexstr[21]; /* log10(2^64) ~ 20, plus NUL character. */
      typeof (source->total_sectors) max_crypt_sectors = 0;

      grub_errno = GRUB_ERR_NONE;
      ret = luks2_get_keyslot (&keyslot, &digest, &segment, json, json_idx);
      if (ret)
	goto err;
      if (grub_errno != GRUB_ERR_NONE)
	  grub_dprintf ("luks2", "Ignoring unhandled error %d from luks2_get_keyslot\n", grub_errno);

      if (keyslot.priority == 0)
	{
	  grub_dprintf ("luks2", "Ignoring keyslot \"%" PRIuGRUB_UINT64_T "\" due to priority\n", keyslot.idx);
	  continue;
	}

      grub_dprintf ("luks2", "Trying keyslot \"%" PRIuGRUB_UINT64_T "\"\n", keyslot.idx);

      /* Sector size should be one of 512, 1024, 2048, or 4096. */
      if (!(segment.sector_size == 512 || segment.sector_size == 1024 ||
	    segment.sector_size == 2048 || segment.sector_size == 4096))
	{
	  grub_dprintf ("luks2", "Segment \"%" PRIuGRUB_UINT64_T "\" sector"
				 " size %" PRIuGRUB_UINT64_T " is not one of"
				 " 512, 1024, 2048, or 4096\n",
				 segment.idx, segment.sector_size);
	  continue;
	}

      /* Set up disk according to keyslot's segment. */
      crypt->offset_sectors = grub_divmod64 (segment.offset, segment.sector_size, NULL);
      crypt->log_sector_size = grub_log2ull (segment.sector_size);
      /* Set to the source disk/partition size, which is the maximum we allow. */
      max_crypt_sectors = grub_disk_native_sectors (source);
      max_crypt_sectors = grub_convert_sector (max_crypt_sectors, GRUB_DISK_SECTOR_BITS,
					       crypt->log_sector_size);

      if (max_crypt_sectors < crypt->offset_sectors)
	{
	  grub_dprintf ("luks2", "Segment \"%" PRIuGRUB_UINT64_T "\" has offset"
				 " %" PRIuGRUB_UINT64_T " which is greater than"
				 " source disk size %" PRIuGRUB_UINT64_T ","
				 " skipping\n", segment.idx, crypt->offset_sectors,
				 max_crypt_sectors);
	  continue;
	}

      if (grub_strcmp (segment.size, "dynamic") == 0)
	crypt->total_sectors = max_crypt_sectors - crypt->offset_sectors;
      else
	{
	  grub_errno = GRUB_ERR_NONE;

	  /* Convert segment.size to sectors, rounding up to nearest sector */
	  crypt->total_sectors = grub_strtoull (segment.size, NULL, 10);

	  if (grub_errno == GRUB_ERR_NONE)
	    {
	      crypt->total_sectors = ALIGN_UP (crypt->total_sectors,
					       1 << crypt->log_sector_size);
	      crypt->total_sectors >>= crypt->log_sector_size;
	    }
	  else if (grub_errno == GRUB_ERR_BAD_NUMBER)
	    {
	      grub_dprintf ("luks2", "Segment \"%" PRIuGRUB_UINT64_T "\" size"
				     " \"%s\" is not a parsable number,"
				     " skipping keyslot\n",
				     segment.idx, segment.size);
	      continue;
	    }
	  else if (grub_errno == GRUB_ERR_OUT_OF_RANGE)
	    {
	      /*
	       * There was an overflow in parsing segment.size, so disk must
	       * be very large or the string is incorrect.
	       *
	       * TODO: Allow reading of at least up max_crypt_sectors. Really,
	       * its very unlikely one would be booting from such a large drive
	       * anyway. Use another smaller LUKS2 boot device.
	       */
	      grub_dprintf ("luks2", "Segment \"%" PRIuGRUB_UINT64_T "\" size"
				     " %s overflowed 64-bit unsigned integer,"
				     " skipping keyslot\n", segment.idx, segment.size);
	      continue;
	    }
	}

      if (crypt->total_sectors == 0)
	{
	  grub_dprintf ("luks2", "Segment \"%" PRIuGRUB_UINT64_T "\" has zero"
				 " sectors, skipping\n", segment.idx);
	  continue;
	}
      else if (max_crypt_sectors < (crypt->offset_sectors + crypt->total_sectors))
	{
	  grub_dprintf ("luks2", "Segment \"%" PRIuGRUB_UINT64_T "\" has last"
				 " data position greater than source disk size,"
				 " the end of the crypto device will be"
				 " inaccessible\n", segment.idx);

	  /* Allow decryption up to the end of the source disk. */
	  crypt->total_sectors = max_crypt_sectors - crypt->offset_sectors;
	}

      ret = luks2_decrypt_key (candidate_key, source, crypt, &keyslot,
			       cargs->key_data, cargs->key_len);
      if (ret)
	{
	  grub_dprintf ("luks2", "Decryption with keyslot \"%" PRIuGRUB_UINT64_T "\" failed: %s\n",
			keyslot.idx, grub_errmsg);
	  continue;
	}

      ret = luks2_verify_key (&digest, candidate_key, keyslot.key_size);
      if (ret)
	{
	  grub_dprintf ("luks2", "Could not open keyslot \"%" PRIuGRUB_UINT64_T "\": %s\n",
			keyslot.idx, grub_errmsg);
	  continue;
	}

      grub_snprintf (indexstr, sizeof (indexstr) - 1, "%" PRIuGRUB_UINT64_T, keyslot.idx);
      /*
       * TRANSLATORS: It's a cryptographic key slot: one element of an array
       * where each element is either empty or holds a key.
       */
      grub_printf_ (N_("Slot \"%s\" opened\n"), indexstr);

      candidate_key_len = keyslot.key_size;
      break;
    }
  if (candidate_key_len == 0)
    {
      ret = grub_error (GRUB_ERR_ACCESS_DENIED, "Invalid passphrase");
      goto err;
    }

  /* Set up disk cipher. */
  grub_strncpy (cipher, segment.encryption, sizeof (cipher));
  ptr = grub_memchr (cipher, '-', grub_strlen (cipher));
  if (!ptr)
      return grub_error (GRUB_ERR_BAD_ARGUMENT, "Invalid encryption");
  *ptr = '\0';

  ret = grub_cryptodisk_setcipher (crypt, cipher, ptr + 1);
  if (ret)
      goto err;

  /* Set the master key. */
  gcry_ret = grub_cryptodisk_setkey (crypt, candidate_key, candidate_key_len);
  if (gcry_ret)
    {
      ret = grub_crypto_gcry_error (gcry_ret);
      goto err;
    }

 err:
  grub_free (json_header);
  grub_json_free (json);
  return ret;
}

static struct grub_cryptodisk_dev luks2_crypto = {
  .scan = luks2_scan,
  .recover_key = luks2_recover_key
};

GRUB_MOD_INIT (luks2)
{
  grub_cryptodisk_dev_register (&luks2_crypto);
}

GRUB_MOD_FINI (luks2)
{
  grub_cryptodisk_dev_unregister (&luks2_crypto);
}
