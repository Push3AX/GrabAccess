/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2020  Free Software Foundation, Inc.
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

/*
 * Copyright (c) Mark Harmstone 2020
 *
 * This file is part of Quibble.
 *
 * Quibble is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public Licence as published by
 * the Free Software Foundation, either version 3 of the Licence, or
 * (at your option) any later version.
 *
 * Quibble is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public Licence for more details.
 *
 * You should have received a copy of the GNU Lesser General Public Licence
 * along with Quibble.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/types.h>
#include <grub/file.h>
#include <reg.h>

#define _CR(RECORD, TYPE, FIELD) \
    ((TYPE *) ((char *) (RECORD) - (char *) &(((TYPE *) 0)->FIELD)))

#define _offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)NULL)->MEMBER)

#pragma GCC diagnostic ignored "-Wcast-align"

enum reg_bool
{
  false = 0,
  true  = 1,
};

static size_t
reg_wcslen (const uint16_t *s)
{
  size_t i = 0;
  while (s[i] != 0)
    i++;
  return i;
}

static enum reg_bool check_header(grub_hive_t *h)
{
  HBASE_BLOCK* base_block = (HBASE_BLOCK*)h->data;
  uint32_t csum;
  enum reg_bool dirty = false;

  if (base_block->Signature != HV_HBLOCK_SIGNATURE)
  {
    printf ("Invalid signature.\n");
    return false;
  }

  if (base_block->Major != HSYS_MAJOR)
  {
    printf ("Invalid major value.\n");
    return false;
  }

  if (base_block->Minor < HSYS_MINOR)
  {
    printf ("Invalid minor value.\n");
    return false;
  }

  if (base_block->Type != HFILE_TYPE_PRIMARY)
  {
    printf ("Type was not HFILE_TYPE_PRIMARY.\n");
    return false;
  }

  if (base_block->Format != HBASE_FORMAT_MEMORY)
  {
    printf ("Format was not HBASE_FORMAT_MEMORY.\n");
    return false;
  }

  if (base_block->Cluster != 1)
  {
    printf ("Cluster was not 1.\n");
    return false;
  }

  // FIXME - should apply LOG file if sequences don't match
  if (base_block->Sequence1 != base_block->Sequence2)
  {
    printf ("Sequence1 != Sequence2.\n");
    base_block->Sequence2 = base_block->Sequence1;
    dirty = true;
  }

  // check checksum
  csum = 0;
  for (unsigned int i = 0; i < 127; i++)
  {
    csum ^= ((uint32_t*)h->data)[i];
  }
  if (csum == 0xffffffff)
    csum = 0xfffffffe;
  else if (csum == 0)
    csum = 1;

  if (csum != base_block->CheckSum)
  {
    printf ("Invalid checksum.\n");
    base_block->CheckSum = csum;
    dirty = true;
  }

  if (dirty)
  {
    printf ("Hive is dirty.\n");

    // FIXME - recover by processing LOG files (old style, < Windows 8.1)
    // FIXME - recover by processing LOG files (new style, >= Windows 8.1)
  }

  return true;
}

static void close_hive (grub_reg_hive_t *this)
{
  grub_hive_t *h = _CR(this, grub_hive_t, public);

  if (h->data)
    grub_free (h->data);

  grub_free (h);
}

static void find_root (grub_reg_hive_t *this, HKEY* key)
{
  grub_hive_t *h = _CR(this, grub_hive_t, public);
  HBASE_BLOCK *base_block = (HBASE_BLOCK *)h->data;

  *key = 0x1000 + base_block->RootCell;
}

static grub_err_t
enum_keys (grub_reg_hive_t *this, HKEY key, uint32_t index,
           uint16_t *name, uint32_t name_len)
{
  grub_hive_t *h = _CR(this, grub_hive_t, public);
  int32_t size;
  CM_KEY_NODE* nk;
  CM_KEY_FAST_INDEX* lh;
  CM_KEY_NODE* nk2;
  enum reg_bool overflow = false;

  // FIXME - make sure no buffer overruns (here and elsewhere)
  // find parent key node

  size = -*(int32_t*)((uint8_t*)h->data + key);

  if (size < 0)
    return GRUB_ERR_FILE_NOT_FOUND;

  if ((uint32_t)size < sizeof(int32_t) + _offsetof(CM_KEY_NODE, Name[0]))
    return GRUB_ERR_BAD_ARGUMENT;

  nk = (CM_KEY_NODE*)((uint8_t*)h->data + key + sizeof(int32_t));

  if (nk->Signature != CM_KEY_NODE_SIGNATURE)
    return GRUB_ERR_BAD_ARGUMENT;

  if ((uint32_t)size < sizeof(int32_t)
      + _offsetof(CM_KEY_NODE, Name[0]) + nk->NameLength)
    return GRUB_ERR_BAD_ARGUMENT;

  // FIXME - volatile keys?

  if (index >= nk->SubKeyCount || nk->SubKeyList == 0xffffffff)
    return GRUB_ERR_FILE_NOT_FOUND;

  // go to key index

  size = -*(int32_t*)((uint8_t*)h->data + 0x1000 + nk->SubKeyList);

  if (size < 0)
    return GRUB_ERR_FILE_NOT_FOUND;

  if ((uint32_t)size < sizeof(int32_t) + _offsetof(CM_KEY_FAST_INDEX, List[0]))
    return GRUB_ERR_BAD_ARGUMENT;

  lh = (CM_KEY_FAST_INDEX*)((uint8_t*)h->data + 0x1000
                            + nk->SubKeyList + sizeof(int32_t));

  if (lh->Signature != CM_KEY_HASH_LEAF && lh->Signature != CM_KEY_FAST_LEAF)
    return GRUB_ERR_BAD_ARGUMENT;

  if ((uint32_t)size < sizeof(int32_t) + _offsetof(CM_KEY_FAST_INDEX, List[0])
      + (lh->Count * sizeof(CM_INDEX)))
    return GRUB_ERR_BAD_ARGUMENT;

  if (index >= lh->Count)
    return GRUB_ERR_BAD_ARGUMENT;

  // find child key node

  size = -*(int32_t*)((uint8_t*)h->data + 0x1000 + lh->List[index].Cell);

  if (size < 0)
    return GRUB_ERR_FILE_NOT_FOUND;

  if ((uint32_t)size < sizeof(int32_t) + _offsetof(CM_KEY_NODE, Name[0]))
    return GRUB_ERR_BAD_ARGUMENT;

  nk2 = (CM_KEY_NODE*)((uint8_t*)h->data + 0x1000
                       + lh->List[index].Cell + sizeof(int32_t));

  if (nk2->Signature != CM_KEY_NODE_SIGNATURE)
    return GRUB_ERR_BAD_ARGUMENT;

  if ((uint32_t)size < sizeof(int32_t) 
      + _offsetof(CM_KEY_NODE, Name[0]) + nk2->NameLength)
    return GRUB_ERR_BAD_ARGUMENT;

  if (nk2->Flags & KEY_COMP_NAME)
  {
    unsigned int i = 0;
    char* nkname = (char*)nk2->Name;
    for (i = 0; i < nk2->NameLength; i++)
    {
      if (i >= name_len)
      {
        overflow = true;
        break;
      }
      name[i] = nkname[i];
    }
    name[i] = 0;
  }
  else
  {
    unsigned int i = 0;
    for (i = 0; i < nk2->NameLength / sizeof(uint16_t); i++)
    {
      if (i >= name_len)
      {
        overflow = true;
        break;
      }
      name[i] = nk2->Name[i];
    }
    name[i] = 0;
  }

  return overflow ? GRUB_ERR_OUT_OF_MEMORY : GRUB_ERR_NONE;
}

static grub_err_t
find_child_key (grub_hive_t* h, HKEY parent,
                const uint16_t* namebit, size_t nblen, HKEY* key)
{
  int32_t size;
  CM_KEY_NODE* nk;
  CM_KEY_FAST_INDEX* lh;

  // find parent key node
  size = -*(int32_t*)((uint8_t*)h->data + parent);

  if (size < 0)
    return GRUB_ERR_FILE_NOT_FOUND;

  if ((uint32_t)size < sizeof(int32_t) + _offsetof(CM_KEY_NODE, Name[0]))
    return GRUB_ERR_BAD_ARGUMENT;

  nk = (CM_KEY_NODE*)((uint8_t*)h->data + parent + sizeof(int32_t));

  if (nk->Signature != CM_KEY_NODE_SIGNATURE)
    return GRUB_ERR_BAD_ARGUMENT;

  if ((uint32_t)size < sizeof(int32_t)
      + _offsetof(CM_KEY_NODE, Name[0]) + nk->NameLength)
    return GRUB_ERR_BAD_ARGUMENT;

  if (nk->SubKeyCount == 0 || nk->SubKeyList == 0xffffffff)
    return GRUB_ERR_FILE_NOT_FOUND;

  // go to key index

  size = -*(int32_t*)((uint8_t*)h->data + 0x1000 + nk->SubKeyList);

  if (size < 0)
    return GRUB_ERR_FILE_NOT_FOUND;

  if ((uint32_t)size < sizeof(int32_t) + _offsetof(CM_KEY_FAST_INDEX, List[0]))
    return GRUB_ERR_BAD_ARGUMENT;

  lh = (CM_KEY_FAST_INDEX*)((uint8_t*)h->data + 0x1000
                            + nk->SubKeyList + sizeof(int32_t));

  if (lh->Signature != CM_KEY_HASH_LEAF && lh->Signature != CM_KEY_FAST_LEAF)
    return GRUB_ERR_BAD_ARGUMENT;

  if ((uint32_t)size < sizeof(int32_t)
      + _offsetof(CM_KEY_FAST_INDEX, List[0]) + (lh->Count * sizeof(CM_INDEX)))
    return GRUB_ERR_BAD_ARGUMENT;

  // FIXME - check against hashes if CM_KEY_HASH_LEAF

  for (unsigned int i = 0; i < lh->Count; i++)
  {
    CM_KEY_NODE* nk2;
    size = -*(int32_t*)((uint8_t*)h->data + 0x1000 + lh->List[i].Cell);
    if (size < 0)
      continue;

    if ((uint32_t)size < sizeof(int32_t) + _offsetof(CM_KEY_NODE, Name[0]))
      continue;

    nk2 = (CM_KEY_NODE*)((uint8_t*)h->data
                         + 0x1000 + lh->List[i].Cell + sizeof(int32_t));

    if (nk2->Signature != CM_KEY_NODE_SIGNATURE)
      continue;

    if ((uint32_t)size < sizeof(int32_t)
        + _offsetof(CM_KEY_NODE, Name[0]) + nk2->NameLength)
      continue;

    // FIXME - use string protocol here to do comparison properly?
    if (nk2->Flags & KEY_COMP_NAME)
    {
      unsigned int j;
      char* name = (char*)nk2->Name;

      if (nk2->NameLength != nblen)
        continue;

      for (j = 0; j < nk2->NameLength; j++)
      {
        uint16_t c1 = name[j];
        uint16_t c2 = namebit[j];
        if (c1 >= 'A' && c1 <= 'Z')
          c1 = c1 - 'A' + 'a';
        if (c2 >= 'A' && c2 <= 'Z')
          c2 = c2 - 'A' + 'a';
        if (c1 != c2)
          break;
      }

      if (j != nk2->NameLength)
        continue;

      *key = 0x1000 + lh->List[i].Cell;

      return GRUB_ERR_NONE;
    }
    else
    {
      unsigned int j;
      if (nk2->NameLength / sizeof(uint16_t) != nblen)
        continue;

      for (j = 0; j < nk2->NameLength / sizeof(uint16_t); j++)
      {
        uint16_t c1 = nk2->Name[j];
        uint16_t c2 = namebit[j];
        if (c1 >= 'A' && c1 <= 'Z')
          c1 = c1 - 'A' + 'a';
        if (c2 >= 'A' && c2 <= 'Z')
          c2 = c2 - 'A' + 'a';
        if (c1 != c2)
          break;
      }
      if (j != nk2->NameLength / sizeof(uint16_t))
        continue;
      *key = 0x1000 + lh->List[i].Cell;
      return GRUB_ERR_NONE;
    }
  }
  return GRUB_ERR_FILE_NOT_FOUND;
}

static grub_err_t
find_key (grub_reg_hive_t* this, HKEY parent, const uint16_t* path, HKEY* key)
{
  grub_err_t errno;
  grub_hive_t* h = _CR(this, grub_hive_t, public);
  size_t nblen;
  HKEY k;

  do
  {
    nblen = 0;
    while (path[nblen] != '\\' && path[nblen] != 0)
    {
      nblen++;
    }

    errno = find_child_key (h, parent, path, nblen, &k);
    if (errno)
      return errno;

    if (path[nblen] == 0 || (path[nblen] == '\\' && path[nblen + 1] == 0))
    {
      *key = k;
      return errno;
    }

    parent = k;
    path = &path[nblen + 1];
  }
  while (1);
}

static grub_err_t
enum_values (grub_reg_hive_t *this, HKEY key,
             uint32_t index, uint16_t* name, uint32_t name_len, uint32_t* type)
{
  grub_hive_t* h = _CR(this, grub_hive_t, public);
  int32_t size;
  CM_KEY_NODE* nk;
  uint32_t* list;
  CM_KEY_VALUE* vk;
  enum reg_bool overflow = false;

  // find key node
  size = -*(int32_t*)((uint8_t*)h->data + key);

  if (size < 0)
    return GRUB_ERR_FILE_NOT_FOUND;

  if ((uint32_t)size < sizeof(int32_t) + _offsetof(CM_KEY_NODE, Name[0]))
    return GRUB_ERR_BAD_ARGUMENT;

  nk = (CM_KEY_NODE*)((uint8_t*)h->data + key + sizeof(int32_t));

  if (nk->Signature != CM_KEY_NODE_SIGNATURE)
    return GRUB_ERR_BAD_ARGUMENT;

  if ((uint32_t)size < sizeof(int32_t) 
      + _offsetof(CM_KEY_NODE, Name[0]) + nk->NameLength)
    return GRUB_ERR_BAD_ARGUMENT;

  if (index >= nk->ValuesCount || nk->Values == 0xffffffff)
    return GRUB_ERR_FILE_NOT_FOUND;

  // go to key index
  size = -*(int32_t*)((uint8_t*)h->data + 0x1000 + nk->Values);

  if (size < 0)
    return GRUB_ERR_FILE_NOT_FOUND;

  if ((uint32_t)size < sizeof(int32_t) + (sizeof(uint32_t) * nk->ValuesCount))
    return GRUB_ERR_BAD_ARGUMENT;

  list = (uint32_t*)((uint8_t*)h->data + 0x1000 + nk->Values + sizeof(int32_t));

  // find value node
  size = -*(int32_t*)((uint8_t*)h->data + 0x1000 + list[index]);

  if (size < 0)
    return GRUB_ERR_FILE_NOT_FOUND;

  if ((uint32_t)size < sizeof(int32_t) + _offsetof(CM_KEY_VALUE, Name[0]))
    return GRUB_ERR_BAD_ARGUMENT;

  vk = (CM_KEY_VALUE*)((uint8_t*)h->data + 0x1000 + list[index] + sizeof(int32_t));

  if (vk->Signature != CM_KEY_VALUE_SIGNATURE)
    return GRUB_ERR_BAD_ARGUMENT;

  if ((uint32_t)size < sizeof(int32_t)
      + _offsetof(CM_KEY_VALUE, Name[0]) + vk->NameLength)
    return GRUB_ERR_BAD_ARGUMENT;

  if (vk->Flags & VALUE_COMP_NAME)
  {
    unsigned int i = 0;
    char* nkname = (char*)vk->Name;
    for (i = 0; i < vk->NameLength; i++)
    {
      if (i >= name_len)
      {
        overflow = true;
        break;
      }
      name[i] = nkname[i];
    }
    name[i] = 0;
  }
  else
  {
    unsigned int i = 0;
    for (i = 0; i < vk->NameLength / sizeof(uint16_t); i++)
    {
      if (i >= name_len)
      {
        overflow = true;
        break;
      }
      name[i] = vk->Name[i];
    }
    name[i] = 0;
  }
  *type = vk->Type;
  return overflow ? GRUB_ERR_OUT_OF_MEMORY : GRUB_ERR_NONE;
}

static grub_err_t
query_value_no_copy (grub_reg_hive_t *this, HKEY key,
                     const uint16_t* name, void** data,
                     uint32_t* data_len, uint32_t* type)
{
  grub_hive_t* h = _CR(this, grub_hive_t, public);
  int32_t size;
  CM_KEY_NODE* nk;
  uint32_t* list;
  unsigned int namelen = reg_wcslen(name);

  // find key node
  size = -*(int32_t*)((uint8_t*)h->data + key);

  if (size < 0)
    return GRUB_ERR_FILE_NOT_FOUND;

  if ((uint32_t)size < sizeof(int32_t) + _offsetof(CM_KEY_NODE, Name[0]))
    return GRUB_ERR_BAD_ARGUMENT;

  nk = (CM_KEY_NODE*)((uint8_t*)h->data + key + sizeof(int32_t));

  if (nk->Signature != CM_KEY_NODE_SIGNATURE)
    return GRUB_ERR_BAD_ARGUMENT;

  if ((uint32_t)size < sizeof(int32_t)
      + _offsetof(CM_KEY_NODE, Name[0]) + nk->NameLength)
    return GRUB_ERR_BAD_ARGUMENT;

  if (nk->ValuesCount == 0 || nk->Values == 0xffffffff)
    return GRUB_ERR_FILE_NOT_FOUND;

  // go to key index

  size = -*(int32_t*)((uint8_t*)h->data + 0x1000 + nk->Values);

  if (size < 0)
    return GRUB_ERR_FILE_NOT_FOUND;

  if ((uint32_t)size < sizeof(int32_t) + (sizeof(uint32_t) * nk->ValuesCount))
    return GRUB_ERR_BAD_ARGUMENT;

  list = (uint32_t*)((uint8_t*)h->data + 0x1000 + nk->Values + sizeof(int32_t));

  // find value node
  for (unsigned int i = 0; i < nk->ValuesCount; i++)
  {
    CM_KEY_VALUE* vk;
    size = -*(int32_t*)((uint8_t*)h->data + 0x1000 + list[i]);

    if (size < 0)
      continue;

    if ((uint32_t)size < sizeof(int32_t) + _offsetof(CM_KEY_VALUE, Name[0]))
      continue;

    vk = (CM_KEY_VALUE*)((uint8_t*)h->data + 0x1000 + list[i] + sizeof(int32_t));
    if (vk->Signature != CM_KEY_VALUE_SIGNATURE)
      continue;

    if ((uint32_t)size < sizeof(int32_t)
        + _offsetof(CM_KEY_VALUE, Name[0]) + vk->NameLength)
      continue;

    if (vk->Flags & VALUE_COMP_NAME)
    {
      unsigned int j;
      char* valname = (char*)vk->Name;

      if (vk->NameLength != namelen)
        continue;

      for (j = 0; j < vk->NameLength; j++)
      {
        uint16_t c1 = valname[j];
        uint16_t c2 = name[j];
        if (c1 >= 'A' && c1 <= 'Z')
          c1 = c1 - 'A' + 'a';
        if (c2 >= 'A' && c2 <= 'Z')
          c2 = c2 - 'A' + 'a';
        if (c1 != c2)
          break;
      }
      if (j != vk->NameLength)
        continue;
    }
    else
    {
      unsigned int j;
      if (vk->NameLength / sizeof(uint16_t) != namelen)
        continue;
      for (j = 0; j < vk->NameLength / sizeof(uint16_t); j++)
      {
        uint16_t c1 = vk->Name[j];
        uint16_t c2 = name[j];
        if (c1 >= 'A' && c1 <= 'Z')
          c1 = c1 - 'A' + 'a';
        if (c2 >= 'A' && c2 <= 'Z')
          c2 = c2 - 'A' + 'a';
        if (c1 != c2)
          break;
      }
      if (j != vk->NameLength / sizeof(uint16_t))
        continue;
    }

    if (vk->DataLength & CM_KEY_VALUE_SPECIAL_SIZE)
    { // data stored as data offset
      size_t datalen = vk->DataLength & ~CM_KEY_VALUE_SPECIAL_SIZE;
      uint8_t *ptr = NULL;

      if (datalen == 4 || datalen == 2 || datalen == 1)
        ptr = (uint8_t*)&vk->Data;
      else if (datalen != 0)
        return GRUB_ERR_BAD_ARGUMENT;

      *data = ptr;
    }
    else
    {
      size = -*(int32_t*)((uint8_t*)h->data + 0x1000 + vk->Data);

      if ((uint32_t)size < vk->DataLength)
        return GRUB_ERR_BAD_ARGUMENT;

      *data = (uint8_t*)h->data + 0x1000 + vk->Data + sizeof(int32_t);
    }

    // FIXME - handle long "data block" values
    *data_len = vk->DataLength & ~CM_KEY_VALUE_SPECIAL_SIZE;
    *type = vk->Type;

    return GRUB_ERR_NONE;
  }

  return GRUB_ERR_FILE_NOT_FOUND;
}

static grub_err_t
query_value (grub_reg_hive_t *this, HKEY key,
             const uint16_t* name, void* data,
             uint32_t* data_len, uint32_t* type)
{
  grub_err_t errno;
  void* out;
  uint32_t len;
  errno = query_value_no_copy (this, key, name, &out, &len, type);
  if (errno)
    return errno;
  if (len > *data_len)
  {
    memcpy(data, out, *data_len);
    *data_len = len;
    return GRUB_ERR_OUT_OF_MEMORY;
  }
  memcpy(data, out, len);
  *data_len = len;
  return GRUB_ERR_NONE;
}

static void
steal_data (grub_reg_hive_t *this, void** data, uint32_t* size)
{
  grub_hive_t* h = _CR(this, grub_hive_t, public);
  *data = h->data;
  *size = h->size;
  h->data = NULL;
  h->size = 0;
}

static void clear_volatile (grub_hive_t* h, HKEY key)
{
  int32_t size;
  CM_KEY_NODE* nk;
  uint16_t sig;

  size = -*(int32_t*)((uint8_t*)h->data + key);

  if (size < 0)
    return;

  if ((uint32_t)size < sizeof(int32_t) + _offsetof(CM_KEY_NODE, Name[0]))
    return;

  nk = (CM_KEY_NODE*)((uint8_t*)h->data + key + sizeof(int32_t));

  if (nk->Signature != CM_KEY_NODE_SIGNATURE)
    return;

  nk->VolatileSubKeyList = 0xbaadf00d;
  nk->VolatileSubKeyCount = 0;

  if (nk->SubKeyCount == 0 || nk->SubKeyList == 0xffffffff)
    return;

  size = -*(int32_t*)((uint8_t*)h->data + 0x1000 + nk->SubKeyList);

  sig = *(uint16_t*)((uint8_t*)h->data + 0x1000 + nk->SubKeyList + sizeof(int32_t));

  if (sig == CM_KEY_HASH_LEAF || sig == CM_KEY_FAST_LEAF)
  {
    CM_KEY_FAST_INDEX* lh =
        (CM_KEY_FAST_INDEX*)((uint8_t*)h->data + 0x1000
                             + nk->SubKeyList + sizeof(int32_t));

    for (unsigned int i = 0; i < lh->Count; i++)
    {
      clear_volatile(h, 0x1000 + lh->List[i].Cell);
    }
  }
  else if (sig == CM_KEY_INDEX_ROOT)
  {
    CM_KEY_INDEX* ri = (CM_KEY_INDEX*)((uint8_t*)h->data + 0x1000
                                       + nk->SubKeyList + sizeof(int32_t));

    for (unsigned int i = 0; i < ri->Count; i++)
    {
      clear_volatile(h, 0x1000 + ri->List[i]);
    }
  }
  else
  {
    printf ("Unhandled registry signature 0x%x.\n", sig);
  }
}

grub_err_t
grub_open_hive (grub_file_t file, grub_reg_hive_t **hive)
{
  grub_hive_t* h = NULL;

  h = grub_zalloc (sizeof (grub_hive_t));
  if (!h)
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory.");
  h->size = grub_file_size (file);
  h->data = malloc (h->size);
  if (!h->data)
  {
    grub_free (h);
    return grub_error (GRUB_ERR_OUT_OF_MEMORY, "out of memory.");
  }
  grub_file_read (file, h->data, h->size);

  if (!check_header(h))
  {
    printf ("Header check failed.\n");
    grub_free (h->data);
    grub_free (h);
    return GRUB_ERR_BAD_ARGUMENT;
  }

  clear_volatile (h, 0x1000 + ((HBASE_BLOCK*)h->data)->RootCell);

  h->public.close = close_hive;
  h->public.find_root = find_root;
  h->public.enum_keys = enum_keys;
  h->public.find_key = find_key;
  h->public.enum_values = enum_values;
  h->public.query_value = query_value;
  h->public.steal_data = steal_data;
  h->public.query_value_no_copy = query_value_no_copy;

  *hive = &h->public;

  return GRUB_ERR_NONE;
}

#pragma GCC diagnostic error "-Wcast-align"
