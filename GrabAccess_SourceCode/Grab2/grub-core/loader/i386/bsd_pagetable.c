/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (c) 1998  Michael Smith <msmith@freebsd.org>
 *  Copyright (C) 2009  Free Software Foundation, Inc.
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

/* Based on the code from FreeBSD originally distributed under the
   following terms: */

/*-
 * Copyright (c) 1998  Michael Smith <msmith@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */


static void
fill_bsd64_pagetable (grub_uint8_t *src, grub_addr_t target)
{
  grub_uint64_t *pt2, *pt3, *pt4;
  grub_addr_t pt2t, pt3t;
  int i;

#define PG_V		0x001
#define PG_RW		0x002
#define PG_U		0x004
#define PG_PS		0x080

  pt4 = (grub_uint64_t *) src;
  pt3 = (grub_uint64_t *) (src + 4096);
  pt2 = (grub_uint64_t *) (src + 8192);

  pt3t = target + 4096;
  pt2t = target + 8192;

  grub_memset (src, 0, 4096 * 3);

  /*
   * This is kinda brutal, but every single 1GB VM memory segment points to
   * the same first 1GB of physical memory.  But it is how BSD expects
   * it to be.
   */
  for (i = 0; i < 512; i++)
    {
      /* Each slot of the level 4 pages points to the same level 3 page */
      pt4[i] = (grub_addr_t) pt3t;
      pt4[i] |= PG_V | PG_RW | PG_U;

      /* Each slot of the level 3 pages points to the same level 2 page */
      pt3[i] = (grub_addr_t) pt2t;
      pt3[i] |= PG_V | PG_RW | PG_U;

      /* The level 2 page slots are mapped with 2MB pages for 1GB. */
      pt2[i] = i * (2 * 1024 * 1024);
      pt2[i] |= PG_V | PG_RW | PG_PS | PG_U;
    }
}
