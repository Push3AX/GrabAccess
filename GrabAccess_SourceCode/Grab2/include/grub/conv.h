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

#ifndef GRUB_STRCONV_HEADER
#define GRUB_STRCONV_HEADER 1

void
str_normalize_init (void);
void
str_normalize_utf8 (char *text, unsigned options);

int
gbk_to_utf8 (const char *from, unsigned int from_len,
             char **to, unsigned int *to_len);
int
utf8_to_gbk (const char *from, unsigned int from_len,
             char **to, unsigned int *to_len);

#endif
