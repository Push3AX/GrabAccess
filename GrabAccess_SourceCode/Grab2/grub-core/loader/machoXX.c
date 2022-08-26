
#include <grub/file.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/i18n.h>

#define min(a,b) (((a) < (b)) ? (a) : (b))

static int
SUFFIX (grub_macho_contains_macho) (grub_macho_t macho)
{
  return macho->offsetXX != -1;
}

void
SUFFIX (grub_macho_parse) (grub_macho_t macho, const char *filename)
{
  union {
    struct grub_macho_lzss_header lzss;
    grub_macho_header_t macho;
  } head;

  /* Is there any candidate at all? */
  if (macho->offsetXX == -1)
    return;

  /* Read header and check magic.  */
  if (grub_file_seek (macho->file, macho->offsetXX) == (grub_off_t) -1
      || grub_file_read (macho->file, &head, sizeof (head))
      != sizeof (head))
    {
      if (!grub_errno)
	grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
		    filename);
      macho->offsetXX = -1;
      return;
    }
  if (grub_memcmp (head.lzss.magic, GRUB_MACHO_LZSS_MAGIC,
		   sizeof (head.lzss.magic)) == 0)
    {
      macho->compressed_sizeXX = grub_be_to_cpu32 (head.lzss.compressed_size);
      macho->uncompressed_sizeXX
	= grub_be_to_cpu32 (head.lzss.uncompressed_size);
      if (macho->uncompressed_sizeXX < sizeof (head.macho))
	{
	  grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
		      filename);
	  macho->offsetXX = -1;
	  return;
	}
      /* Skip header check.  */
      macho->compressedXX = 1;
      return;
    }

  if (head.macho.magic != GRUB_MACHO_MAGIC)
    {
      grub_error (GRUB_ERR_BAD_OS, "invalid Mach-O  header");
      macho->offsetXX = -1;
      return;
    }

  /* Read commands. */
  macho->ncmdsXX = head.macho.ncmds;
  macho->cmdsizeXX = head.macho.sizeofcmds;
  macho->cmdsXX = grub_malloc (macho->cmdsizeXX);
  if (! macho->cmdsXX)
    return;
  if (grub_file_seek (macho->file, macho->offsetXX
		      + sizeof (grub_macho_header_t)) == (grub_off_t) -1
      || grub_file_read (macho->file, macho->cmdsXX,
		      (grub_size_t) macho->cmdsizeXX)
      != (grub_ssize_t) macho->cmdsizeXX)
    {
      if (!grub_errno)
	grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
		    filename);
      macho->offsetXX = -1;
    }
}

typedef int (*grub_macho_iter_hook_t)
(grub_macho_t , struct grub_macho_cmd *,
	       void *);

static grub_err_t
grub_macho_cmds_iterate (grub_macho_t macho,
			 grub_macho_iter_hook_t hook,
			 void *hook_arg,
			 const char *filename)
{
  grub_uint8_t *hdrs;
  int i;

  if (macho->compressedXX && !macho->uncompressedXX)
    {
      grub_uint8_t *tmp;
      grub_macho_header_t *head;
      macho->uncompressedXX = grub_malloc (macho->uncompressed_sizeXX);
      if (!macho->uncompressedXX)
	return grub_errno;
      tmp = grub_malloc (macho->compressed_sizeXX);
      if (!tmp)
	{
	  grub_free (macho->uncompressedXX);
	  macho->uncompressedXX = 0;
	  return grub_errno;
	}
      if (grub_file_seek (macho->file, macho->offsetXX
			  + GRUB_MACHO_LZSS_OFFSET) == (grub_off_t) -1
	  || grub_file_read (macho->file, tmp,
			     (grub_size_t) macho->compressed_sizeXX)
	  != (grub_ssize_t) macho->compressed_sizeXX)
	{
	  if (!grub_errno)
	    grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
			filename);
	  grub_free (tmp);
	  grub_free (macho->uncompressedXX);
	  macho->uncompressedXX = 0;
	  macho->offsetXX = -1;
	  return grub_errno;
	}
      if (grub_decompress_lzss (macho->uncompressedXX,
				macho->uncompressedXX
				+ macho->uncompressed_sizeXX,
				tmp, tmp + macho->compressed_sizeXX)
	  != macho->uncompressed_sizeXX)
	{
	  if (!grub_errno)
	    grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
			filename);
	  grub_free (tmp);
	  grub_free (macho->uncompressedXX);
	  macho->uncompressedXX = 0;
	  macho->offsetXX = -1;
	  return grub_errno;
	}
      grub_free (tmp);
      head = (grub_macho_header_t *) macho->uncompressedXX;
      macho->ncmdsXX = head->ncmds;
      macho->cmdsizeXX = head->sizeofcmds;
      macho->cmdsXX = macho->uncompressedXX + sizeof (grub_macho_header_t);
      if (sizeof (grub_macho_header_t) + macho->cmdsizeXX
	  >= macho->uncompressed_sizeXX)
	{
	  grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
		      filename);
	  grub_free (macho->uncompressedXX);
	  macho->uncompressedXX = 0;
	  macho->offsetXX = -1;
	  return grub_errno;
	}
    }

  if (! macho->cmdsXX)
    return grub_error (GRUB_ERR_BAD_OS, "couldn't find Mach-O commands");
  hdrs = macho->cmdsXX;
  for (i = 0; i < macho->ncmdsXX; i++)
    {
      struct grub_macho_cmd *hdr = (struct grub_macho_cmd *) hdrs;
      if (hook (macho, hdr, hook_arg))
	break;
      hdrs += hdr->cmdsize;
    }

  return grub_errno;
}

grub_size_t
SUFFIX (grub_macho_filesize) (grub_macho_t macho)
{
  if (SUFFIX (grub_macho_contains_macho) (macho))
    return macho->endXX - macho->offsetXX;
  return 0;
}

grub_err_t
SUFFIX (grub_macho_readfile) (grub_macho_t macho,
			      const char *filename,
			      void *dest)
{
  grub_ssize_t read;
  if (! SUFFIX (grub_macho_contains_macho) (macho))
    return grub_error (GRUB_ERR_BAD_OS,
		       "couldn't read architecture-specific part");

  if (grub_file_seek (macho->file, macho->offsetXX) == (grub_off_t) -1)
    return grub_errno;

  read = grub_file_read (macho->file, dest,
			 macho->endXX - macho->offsetXX);
  if (read != (grub_ssize_t) (macho->endXX - macho->offsetXX))
    {
      if (!grub_errno)
	grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
		    filename);
      return grub_errno;
    }
  return GRUB_ERR_NONE;
}

struct calcsize_ctx
{
  int flags;
  int nr_phdrs;
  grub_macho_addr_t *segments_start;
  grub_macho_addr_t *segments_end;
};

/* Run through the program headers to calculate the total memory size we
   should claim.  */
static int
calcsize (grub_macho_t _macho __attribute__ ((unused)),
	  struct grub_macho_cmd *hdr0,
	  void *_arg)
{
  grub_macho_segment_t *hdr = (grub_macho_segment_t *) hdr0;
  struct calcsize_ctx *ctx = _arg;
  if (hdr->cmd != GRUB_MACHO_CMD_SEGMENT)
    return 0;

  if (! hdr->vmsize)
    return 0;

  if (! hdr->filesize && (ctx->flags & GRUB_MACHO_NOBSS))
    return 0;

  ctx->nr_phdrs++;
  if (hdr->vmaddr < *ctx->segments_start)
    *ctx->segments_start = hdr->vmaddr;
  if (hdr->vmaddr + hdr->vmsize > *ctx->segments_end)
    *ctx->segments_end = hdr->vmaddr + hdr->vmsize;
  return 0;
}

/* Calculate the amount of memory spanned by the segments. */
grub_err_t
SUFFIX (grub_macho_size) (grub_macho_t macho, grub_macho_addr_t *segments_start,
			  grub_macho_addr_t *segments_end, int flags,
			  const char *filename)
{
  struct calcsize_ctx ctx = {
    .flags = flags,
    .nr_phdrs = 0,
    .segments_start = segments_start,
    .segments_end = segments_end,
  };

  *segments_start = (grub_macho_addr_t) -1;
  *segments_end = 0;

  grub_macho_cmds_iterate (macho, calcsize, &ctx, filename);

  if (ctx.nr_phdrs == 0)
    return grub_error (GRUB_ERR_BAD_OS, "no program headers present");

  if (*segments_end < *segments_start)
    /* Very bad addresses.  */
    return grub_error (GRUB_ERR_BAD_OS, "bad program header load addresses");

  return GRUB_ERR_NONE;
}

struct do_load_ctx
{
  int flags;
  char *offset;
  const char *filename;
  int *darwin_version;
};

static int
do_load(grub_macho_t _macho,
	struct grub_macho_cmd *hdr0,
	void *_arg)
{
  grub_macho_segment_t *hdr = (grub_macho_segment_t *) hdr0;
  struct do_load_ctx *ctx = _arg;

  if (hdr->cmd != GRUB_MACHO_CMD_SEGMENT)
    return 0;

  if (! hdr->filesize && (ctx->flags & GRUB_MACHO_NOBSS))
    return 0;
  if (! hdr->vmsize)
    return 0;

  if (hdr->filesize)
    {
      grub_ssize_t read, toread = min (hdr->filesize, hdr->vmsize);
      if (_macho->uncompressedXX)
	{
	  if (hdr->fileoff + (grub_size_t) toread
	      > _macho->uncompressed_sizeXX)
	    read = -1;
	  else
	    {
	      read = toread;
	      grub_memcpy (ctx->offset + hdr->vmaddr,
			   _macho->uncompressedXX + hdr->fileoff, read);
	    }
	}
      else
	{
	  if (grub_file_seek (_macho->file, hdr->fileoff
			      + _macho->offsetXX) == (grub_off_t) -1)
	    return 1;
	  read = grub_file_read (_macho->file, ctx->offset + hdr->vmaddr,
				 toread);
	}

      if (read != toread)
	{
	  /* XXX How can we free memory from `load_hook'? */
	  if (!grub_errno)
	    grub_error (GRUB_ERR_BAD_OS, N_("premature end of file %s"),
			ctx->filename);

	  return 1;
	}
      if (ctx->darwin_version)
	{
	  const char *ptr = ctx->offset + hdr->vmaddr;
	  const char *end = ptr + min (hdr->filesize, hdr->vmsize)
	    - (sizeof ("Darwin Kernel Version ") - 1);
	  for (; ptr < end; ptr++)
	    if (grub_memcmp (ptr, "Darwin Kernel Version ",
			     sizeof ("Darwin Kernel Version ") - 1) == 0)
	      {
		ptr += sizeof ("Darwin Kernel Version ") - 1;
		*ctx->darwin_version = 0;
		end += (sizeof ("Darwin Kernel Version ") - 1);
		while (ptr < end && grub_isdigit (*ptr))
		  *ctx->darwin_version = (*ptr++ - '0') + *ctx->darwin_version * 10;
		break;
	      }
	}
    }

  if (hdr->filesize < hdr->vmsize)
    grub_memset (ctx->offset + hdr->vmaddr + hdr->filesize,
		 0, hdr->vmsize - hdr->filesize);
  return 0;
}

/* Load every loadable segment into memory specified by `_load_hook'.  */
grub_err_t
SUFFIX (grub_macho_load) (grub_macho_t macho, const char *filename,
			  char *offset, int flags, int *darwin_version)
{
  struct do_load_ctx ctx = {
    .flags = flags,
    .offset = offset,
    .filename = filename,
    .darwin_version = darwin_version
  };

  if (darwin_version)
    *darwin_version = 0;

  grub_macho_cmds_iterate (macho, do_load, &ctx, filename);

  return grub_errno;
}

static int
find_entry_point (grub_macho_t _macho __attribute__ ((unused)),
			    struct grub_macho_cmd *hdr,
			    void *_arg)
{
  grub_macho_addr_t *entry_point = _arg;
  if (hdr->cmd == GRUB_MACHO_CMD_THREAD)
    *entry_point = ((grub_macho_thread_t *) hdr)->entry_point;
  return 0;
}

grub_macho_addr_t
SUFFIX (grub_macho_get_entry_point) (grub_macho_t macho, const char *filename)
{
  grub_macho_addr_t entry_point = 0;
  grub_macho_cmds_iterate (macho, find_entry_point, &entry_point, filename);
  return entry_point;
}
