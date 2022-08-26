/*
 * Copyright (C) 2014 Michael Brown <mbrown@fensystems.co.uk>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * @file
 *
 * WIM images
 *
 */

#include <grub/misc.h>
#include <grub/file.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lzx.h>
#include <xpress.h>
#include <wim.h>
#include <misc.h>
#include <grub/wimtools.h>

#pragma GCC diagnostic ignored "-Wcast-align"

/**
 * Get WIM header
 *
 * @v file    Virtual file
 * @v header    WIM header to fill in
 * @ret rc    Return status code
 */
static int grub_wim_header (grub_file_t file, struct wim_header *header)
{
  /* Sanity check */
  if (sizeof (*header) > file->size)
    return -1;
  /* Read WIM header */
  file_read (file, header, sizeof (*header), 0);
  return 0;
}

/**
 * Get compressed chunk offset
 *
 * @v file    Virtual file
 * @v resource    Resource
 * @v chunk    Chunk number
 * @v offset    Offset to fill in
 * @ret rc    Return status code
 */
static int
grub_wim_chunk_offset (grub_file_t file, struct wim_resource_header *resource,
                       unsigned int chunk, size_t *offset)
{
  size_t zlen = (resource->zlen__flags & WIM_RESHDR_ZLEN_MASK);
  unsigned int chunks;
  size_t offset_offset;
  size_t offset_len;
  size_t chunks_len;
  union
  {
    uint32_t offset_32;
    uint64_t offset_64;
  } u;

  /* Special case: zero-length files have no chunks */
  if (! resource->len)
  {
    *offset = 0;
    return 0;
  }

  /* Calculate chunk parameters */
  chunks = ((resource->len + WIM_CHUNK_LEN - 1) / WIM_CHUNK_LEN);
  offset_len = ((resource->len > 0xffffffffULL) ?
           sizeof (u.offset_64) : sizeof (u.offset_32));
  chunks_len = ((chunks - 1) * offset_len);

  /* Sanity check */
  if (chunks_len > zlen)
    return -1;

  /* Special case: chunk 0 has no offset field */
  if (! chunk)
  {
    *offset = chunks_len;
    return 0;
  }

  /* Treat out-of-range chunks as being at the end of the
   * resource, to allow for length calculation on the final
   * chunk.
   */
  if (chunk >= chunks)
  {
    *offset = zlen;
    return 0;
  }

  /* Otherwise, read the chunk offset */
  offset_offset = ((chunk - 1) * offset_len);
  file_read (file, &u, offset_len, resource->offset + offset_offset);
  *offset = (chunks_len + ((offset_len == sizeof (u.offset_64)) ?
           u.offset_64 : u.offset_32));
  if (*offset > zlen)
    return -1;
  return 0;
}

/**
 * Read chunk from a compressed resource
 *
 * @v file    Virtual file
 * @v header    WIM header
 * @v resource    Resource
 * @v chunk    Chunk number
 * @v buf    Chunk buffer
 * @ret rc    Return status code
 */
static int
grub_wim_chunk (grub_file_t file, struct wim_header *header,
                struct wim_resource_header *resource,
                unsigned int chunk, struct wim_chunk_buffer *buf)
{
  ssize_t (* decompress) (const void *data, size_t len, void *buf);
  unsigned int chunks;
  size_t offset;
  size_t next_offset;
  size_t len;
  size_t expected_out_len;
  ssize_t out_len;
  int rc;

  /* Get chunk compressed data offset and length */
  if ((rc = grub_wim_chunk_offset (file, resource, chunk,
               &offset)) != 0)
    return rc;
  if ((rc = grub_wim_chunk_offset (file, resource, (chunk + 1),
               &next_offset)) != 0)
    return rc;
  len = (next_offset - offset);

  /* Calculate uncompressed length */
  chunks = ((resource->len + WIM_CHUNK_LEN - 1) / WIM_CHUNK_LEN);
  expected_out_len = ((chunk >= (chunks - 1)) ?
           (resource->len % WIM_CHUNK_LEN) : WIM_CHUNK_LEN);

  /* Read possibly-compressed data */
  if (len == expected_out_len)
  {
    /* Chunk did not compress; read raw data */
    file_read (file, buf->data, len, resource->offset + offset);
  }
  else
  {
    uint8_t zbuf[len];
    /* Read compressed data into a temporary buffer */
    file_read (file, zbuf, len, resource->offset + offset);

    /* Identify decompressor */
    if (header->flags & WIM_HDR_LZX)
      decompress = lzx_decompress;
    else if (header->flags & WIM_HDR_XPRESS)
      decompress = xca_decompress;
    else
      return -1;

    /* Decompress data */
    out_len = decompress (zbuf, len, NULL);
    if (out_len < 0)
      return out_len;
    if (((size_t) out_len) != expected_out_len)
      return -1;

    decompress (zbuf, len, buf->data);
  }
  return 0;
}

/**
 * Read from a (possibly compressed) resource
 *
 * @v file    Virtual file
 * @v header    WIM header
 * @v resource    Resource
 * @v data    Data buffer
 * @v offset    Starting offset
 * @v len    Length
 * @ret rc    Return status code
 */
static int
grub_wim_read (grub_file_t file, struct wim_header *header,
               struct wim_resource_header *resource, void *data,
               size_t offset, size_t len)
{
  static struct wim_chunk_buffer grub_wim_chunk_buffer;
  static grub_file_t cached_file;
  static size_t cached_resource_offset;
  static unsigned int cached_chunk;
  size_t zlen = (resource->zlen__flags & WIM_RESHDR_ZLEN_MASK);
  unsigned int chunk;
  size_t skip_len;
  size_t frag_len;
  int rc;

  /* Sanity checks */
  if ((offset + len) > resource->len)
    return -1;

  if ((resource->offset + zlen) > file->size)
    return -1;

  /* If resource is uncompressed, just read the raw data */
  if (! (resource->zlen__flags & (WIM_RESHDR_COMPRESSED |
             WIM_RESHDR_PACKED_STREAMS)))
  {
    file_read (file, data, len, resource->offset + offset);
    return 0;
  }

  /* Read from each chunk overlapping the target region */
  while (len)
  {
    /* Calculate chunk number */
    chunk = offset / WIM_CHUNK_LEN;

    /* Read chunk, if not already cached */
    if ((file != cached_file) ||
         (resource->offset != cached_resource_offset) ||
         (chunk != cached_chunk))
    {
      /* Read chunk */
      if ((rc = grub_wim_chunk (file, header, resource, chunk,
            &grub_wim_chunk_buffer)) != 0)
        return rc;

      /* Update cache */
      cached_file = file;
      cached_resource_offset = resource->offset;
      cached_chunk = chunk;
    }

    /* Copy fragment from this chunk */
    skip_len = (offset % WIM_CHUNK_LEN);
    frag_len = (WIM_CHUNK_LEN - skip_len);
    if (frag_len > len)
      frag_len = len;
    memcpy (data, (grub_wim_chunk_buffer.data + skip_len), frag_len);

    /* Move to next chunk */
    data = (char *)data + frag_len;
    offset += frag_len;
    len -= frag_len;
  }

  return 0;
}

/**
 * Get WIM image metadata
 *
 * @v file    Virtual file
 * @v header    WIM header
 * @v index    Image index, or 0 to use boot image
 * @v meta    Metadata to fill in
 * @ret rc    Return status code
 */
static int
grub_wim_metadata (grub_file_t file, struct wim_header *header,
                   unsigned int index, struct wim_resource_header *meta)
{
  struct wim_lookup_entry entry;
  size_t offset;
  unsigned int found = 0;
  int rc;

  /* If no image index is specified, just use the boot metadata */
  if (index == 0)
  {
    memcpy (meta, &header->boot, sizeof (*meta));
    return 0;
  }

  /* Look for metadata entry */
  for (offset = 0 ; (offset + sizeof (entry)) <= header->lookup.len ;
        offset += sizeof (entry))
 {

    /* Read entry */
    if ((rc = grub_wim_read (file, header, &header->lookup, &entry,
               offset, sizeof (entry))) != 0)
      return rc;

    /* Look for our target entry */
    if (entry.resource.zlen__flags & WIM_RESHDR_METADATA)
    {
      found++;
      if (found == index)
      {
        memcpy (meta, &entry.resource, sizeof (*meta));
        return 0;
      }
    }
  }

  return -1;
}

/**
 * Get directory entry
 *
 * @v file    Virtual file
 * @v header    WIM header
 * @v meta    Metadata
 * @v name    Name
 * @v offset    Directory offset (will be updated)
 * @v direntry    Directory entry to fill in
 * @ret rc    Return status code
 */
static int
grub_wim_direntry (grub_file_t file, struct wim_header *header,
                   struct wim_resource_header *meta,
                   const wchar_t *name, size_t *offset,
                   struct wim_directory_entry *direntry)
{
  size_t name_len = wcslen (name) + 1;
  wchar_t name_buf[name_len];
  int rc;

  /* Search directory */
  for (; ; *offset += direntry->len)
  {
    /* Read length field */
    if ((rc = grub_wim_read (file, header, meta, direntry, *offset,
               sizeof (direntry->len))) != 0)
      return rc;

    /* Check for end of this directory */
    if (! direntry->len)
      return -1;

    /* Read fixed-length portion of directory entry */
    if ((rc = grub_wim_read (file, header, meta, direntry, *offset,
                             sizeof (*direntry))) != 0)
      return rc;

    /* Check name length */
    if (direntry->name_len > name_len * sizeof (wchar_t))
      continue;

    /* Read name */
    if ((rc = grub_wim_read (file, header, meta, &name_buf,
               (*offset + sizeof (*direntry)),
               name_len * sizeof (wchar_t))) != 0)
      return rc;

    /* Check name */
    if (wcscasecmp (name, name_buf) != 0)
      continue;

    return 0;
  }
}

/**
 * Get directory entry for a path
 *
 * @v file    Virtual file
 * @v header    WIM header
 * @v meta    Metadata
 * @v path    Path to file/directory
 * @v offset    Directory entry offset to fill in
 * @v direntry    Directory entry to fill in
 * @ret rc    Return status code
 */
static int
grub_wim_path (grub_file_t file, struct wim_header *header,
          struct wim_resource_header *meta, const wchar_t *path,
          size_t *offset, struct wim_directory_entry *direntry)
{
  wchar_t path_copy[wcslen (path) + 1];
  struct wim_security_header security;
  wchar_t *name;
  wchar_t *next;
  int rc;

  /* Read security data header */
  if ((rc = grub_wim_read (file, header, meta, &security, 0,
                           sizeof (security))) != 0)
    return rc;

  /* Get root directory offset */
  direntry->subdir = ((security.len + sizeof (uint64_t) - 1) &
           ~(sizeof (uint64_t) - 1));

  /* Find directory entry */
  name = memcpy (path_copy, path, sizeof (path_copy));
  do
  {
    next = wcschr (name, L'\\');
    if (next)
      *next = L'\0';
    *offset = direntry->subdir;
    if ((rc = grub_wim_direntry (file, header, meta, name, offset, direntry)) != 0)
      return rc;
    name = (next + 1);
  }
  while (next);

  return 0;
}

/**
 * Get file resource
 *
 * @v file    Virtual file
 * @v header    WIM header
 * @v meta    Metadata
 * @v path    Path to file
 * @v resource    File resource to fill in
 * @ret rc    Return status code
 */
static int
grub_wim_file (grub_file_t file, struct wim_header *header,
               struct wim_resource_header *meta, const wchar_t *path,
               struct wim_resource_header *resource)
{
  struct wim_directory_entry direntry;
  struct wim_lookup_entry entry;
  size_t offset;
  int rc;

  /* Find directory entry */
  if ((rc = grub_wim_path (file, header, meta, path, &offset, &direntry)) != 0)
    return rc;

  /* File matching file entry */
  for (offset = 0 ; (offset + sizeof (entry)) <= header->lookup.len ;
        offset += sizeof (entry))
  {

    /* Read entry */
    if ((rc = grub_wim_read (file, header, &header->lookup, &entry,
               offset, sizeof (entry))) != 0)
      return rc;

    /* Look for our target entry */
    if (memcmp (&entry.hash, &direntry.hash, sizeof (entry.hash)) == 0)
    {
      memcpy (resource, &entry.resource, sizeof (*resource));
      return 0;
    }
  }
  return -1;
}

static int
grub_wim_ispe64 (grub_uint8_t *buffer)
{
  grub_uint32_t pe_off;
  if (buffer[0] != 'M' || buffer[1] != 'Z')
    return 0;
  pe_off = *(grub_uint32_t *)(buffer + 60);
  if (buffer[pe_off] != 'P' || buffer[pe_off + 1] != 'E')
    return 0;
  if (*(grub_uint16_t *)(buffer + pe_off + 24) == 0x020b)
    return 1;
  return 0;
}

int
grub_wim_file_exist (grub_file_t file, unsigned int index, const char *path)
{
  int ret;
  wchar_t *wpath = NULL;
  /** WIM header */
  struct wim_header header;
  /** Resource */
  struct wim_resource_header resource;
  struct wim_resource_header meta;

  /* Get WIM header */
  if (grub_wim_header (file, &header))
    return 0;

  /* Get image metadata */
  if (grub_wim_metadata (file, &header, index, &meta))
    return 0;

  wpath = malloc (2 * (strlen (path) + 1));
  if (!wpath)
    return 0;
  mbstowcs (wpath, path, strlen (path) + 1);

  /* Get file resource */
  if (grub_wim_file (file, &header, &meta, wpath, &resource))
    ret = 0;
  else
    ret = 1;
  grub_free (wpath);
  return ret;
}

int
grub_wim_is64 (grub_file_t file, unsigned int index)
{
  int ret;
  uint8_t *exe_data = NULL;
  uint32_t exe_len = 0;
  struct wim_header header;
  struct wim_resource_header resource;
  struct wim_resource_header meta;
  const wchar_t winload[] = L"\\Windows\\System32\\Boot\\winload.exe";

  if (grub_wim_header (file, &header))
    return 0;

  /* Get image metadata */
  if (grub_wim_metadata (file, &header, index, &meta))
    return 0;

  /* Get file resource */
  if (grub_wim_file (file, &header, &meta, winload, &resource))
    return 0;

  exe_len = (uint32_t) resource.len;
  exe_data = grub_zalloc (exe_len);
  if (!exe_data)
    return 0;
  grub_wim_read (file, &header, &resource, exe_data, 0, exe_len);
  ret = grub_wim_ispe64 (exe_data);
  grub_free (exe_data);
  return ret;
}

grub_uint32_t
grub_wim_image_count (grub_file_t file)
{
  struct wim_header header;
  if (grub_wim_header (file, &header))
    return 0;
  return header.images;
}

grub_uint32_t
grub_wim_boot_index (grub_file_t file)
{
  struct wim_header header;
  if (grub_wim_header (file, &header))
    return 0;
  return header.boot_index;
}
