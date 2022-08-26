/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013 Free Software Foundation, Inc.
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

#include <grub/test.h>
#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/crypto.h>

GRUB_MOD_LICENSE ("GPLv3+");

static struct
{
  const char *P;
  grub_size_t Plen;
  const char *S;
  grub_size_t Slen;
  unsigned int c;
  grub_size_t dkLen;
  const char *DK;
} vectors[] = {
  /* RFC6070. */
  {
    "password", 8,
    "salt", 4,
    1, 20,
    "\x0c\x60\xc8\x0f\x96\x1f\x0e\x71\xf3\xa9\xb5\x24\xaf\x60\x12"
    "\x06\x2f\xe0\x37\xa6"
  },
  {
    "password", 8,
    "salt", 4,
    2, 20,
    "\xea\x6c\x01\x4d\xc7\x2d\x6f\x8c"
    "\xcd\x1e\xd9\x2a\xce\x1d\x41\xf0"
    "\xd8\xde\x89\x57"
  },
  {
    "password", 8,
    "salt", 4,
    4096, 20,
    "\x4b\x00\x79\x01\xb7\x65\x48\x9a\xbe\xad\x49\xd9\x26\xf7"
    "\x21\xd0\x65\xa4\x29\xc1"
  },
  {
    "passwordPASSWORDpassword", 24,
    "saltSALTsaltSALTsaltSALTsaltSALTsalt", 36,
    4096, 25,
    "\x3d\x2e\xec\x4f\xe4\x1c\x84\x9b\x80\xc8\xd8\x36\x62\xc0"
    "\xe4\x4a\x8b\x29\x1a\x96\x4c\xf2\xf0\x70\x38"
  },
  {
    "pass\0word", 9,
    "sa\0lt", 5,
    4096, 16,
    "\x56\xfa\x6a\xa7\x55\x48\x09\x9d\xcc\x37\xd7\xf0\x34\x25\xe0\xc3"
  }
};

static void
pbkdf2_test (void)
{
  grub_size_t i;

  for (i = 0; i < ARRAY_SIZE (vectors); i++)
    {
      gcry_err_code_t err;
      grub_uint8_t DK[32];
      err = grub_crypto_pbkdf2 (GRUB_MD_SHA1,
				(const grub_uint8_t *) vectors[i].P,
				vectors[i].Plen,
				(const grub_uint8_t *) vectors[i].S,
				vectors[i].Slen,
				vectors[i].c,
				DK, vectors[i].dkLen);
      grub_test_assert (err == 0, "gcry error %d", err);
      grub_test_assert (grub_memcmp (DK, vectors[i].DK, vectors[i].dkLen) == 0,
			"PBKDF2 mismatch");
    }
}

/* Register example_test method as a functional test.  */
GRUB_FUNCTIONAL_TEST (pbkdf2_test, pbkdf2_test);
