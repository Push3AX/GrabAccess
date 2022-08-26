/* vboot.c - dynamic vboot module, support booting an OS from a VHD file */
/*
 *  Copyright 2010, VMLite, Inc.
 *  http://www.vmlite.com
 */

#include <grub/loader.h>
#include <grub/file.h>
#include <grub/err.h>
#include <grub/device.h>
#include <grub/disk.h>
#include <grub/misc.h>
#include <grub/types.h>
#include <grub/video.h>
#include <grub/term.h>
#include <grub/partition.h>
#include <grub/i386/pc/memory.h>
#include <grub/dl.h>
#include <grub/env.h>
#include <grub/command.h>
#include <grub/extcmd.h>
#include <grub/i386/pc/biosnum.h>
#include <grub/mm.h>
#include <grub/i386/relocator.h>

//#include <grub/term.h>

#include "vdl.h"

GRUB_MOD_LICENSE ("GPLv3+");

/* we should use a .h file, so it's same as our bootloader side */
#define VBOOT_MEMORY_REQUIRED   63U /* required memory in KiB */
//#define VBOOT_LOADER_SEG		0x8000U
//#define VBOOT_LOADER_SEG		0x9000U
// 639k - 64k = 8FC00
#define VBOOT_LOADER_SEG		0x8FC0U
// windows seems not to use 0x8100 - 0x9100
//#define VBOOT_LOADER_SEG		0x8100U
#define VBOOT_EXECUTABLE_OFFSET 0x200
#define VBOOT_LOADER_ADDR		((VBOOT_LOADER_SEG << 4) + VBOOT_EXECUTABLE_OFFSET)

static grub_dl_t my_mod;
static int boot_drive;
static grub_addr_t boot_part_addr;

// where to load vbootcore.mod module
char *vboot_core_addr = NULL; //(char *)0x6400000; /* 100 MB */

// local helper
int test_vhd_file(const char *pszFilename);
static struct grub_relocator *rel;

static grub_err_t
grub_vboot_boot (void)
{
  struct grub_relocator16_state state = { 
    .edx = boot_drive,
    .esi = boot_part_addr,
    .ds = 0,
    .es = 0,
    .fs = 0,
    .gs = 0,
    .ss = 0,
    .cs = 0,
    .sp = GRUB_MEMORY_MACHINE_BOOT_LOADER_ADDR,
    .ip = GRUB_MEMORY_MACHINE_BOOT_LOADER_ADDR,
    .a20 = 0
  };
  grub_video_set_mode ("text", 0, 0);

  return grub_relocator16_boot (rel, state);
}

static grub_err_t
grub_vboot_unload (void)
{
  grub_relocator_unload (rel);
  rel = NULL;
  grub_dl_unref (my_mod);
  return GRUB_ERR_NONE;
}

/* read 1st sector to 0x7c00, and the rest to 0x90000:0100, make sure to sync with our boot loader */
static void
grub_vboot_cmd (const char *filename, const char * vhd_filename __attribute__ ((unused)))
{
	grub_file_t file = 0;
	grub_uint16_t signature;
	grub_size_t boot_loader_size;

	grub_printf("grub_vboot_cmd(vhd_filename=%s)\n", vhd_filename);

	grub_dl_ref (my_mod);

	file = grub_file_open (filename, GRUB_FILE_TYPE_GRUB_MODULE);
	if (! file)
		goto fail;

	/* Read the first block to 0x7c00, the bios start address.  */
	if (grub_file_read (file, (void *) 0x7C00, GRUB_DISK_SECTOR_SIZE) != GRUB_DISK_SECTOR_SIZE)
	{
		if (grub_errno == GRUB_ERR_NONE)
			grub_error (GRUB_ERR_BAD_OS, "too small");

		goto fail;
	}

	grub_printf("grub_vboot_cmd(grub_file_read())\n");

	/* Check the signature.  */
	signature = *((grub_uint16_t *) (0x7C00 + GRUB_DISK_SECTOR_SIZE - 2));
	if (signature != grub_le_to_cpu16 (0xaa55))
	{
		grub_error (GRUB_ERR_BAD_OS, "invalid signature");
		goto fail;
	}

	// Clear BSS section
	grub_memset((void *)(VBOOT_LOADER_SEG <<4), 0, VBOOT_EXECUTABLE_OFFSET);
	grub_memset((void *)VBOOT_LOADER_ADDR, 0, VBOOT_MEMORY_REQUIRED * 1024 - VBOOT_EXECUTABLE_OFFSET - 1);	

	// load the rest to 0x9000:0200, make sure to sync with our boot loader
	boot_loader_size = grub_file_size (file) - GRUB_DISK_SECTOR_SIZE;	
	if (grub_file_read (file, (void *)VBOOT_LOADER_ADDR, boot_loader_size) != (grub_ssize_t)boot_loader_size)
	{
		grub_error (GRUB_ERR_FILE_READ_ERROR, "Couldn't read file");
		goto fail;
	}

	grub_printf("grub_vboot_cmd(boot_loader_size=0x%x)\n", boot_loader_size);

	// set the segment value so our 16-bit knows where its own memory location
	// offset 6 from the beginning of 7C00
	*(grub_uint16_t *)0x7C06 = VBOOT_LOADER_SEG;

	grub_file_close (file);

	// test vhd opening
	if (vhd_filename)
	{
		int rc = test_vhd_file(vhd_filename);
		grub_printf("test_vhd_file(%s), rc=%d\n", vhd_filename, rc);
	}

	// copy vhd file to 0x9000:0000, max length is 512 bytes
	if (vhd_filename && grub_strlen(vhd_filename) < 0x200)
	{
		grub_memcpy((void *)(VBOOT_LOADER_SEG <<4), vhd_filename, grub_strlen(vhd_filename));
	}
	else
	{
		grub_error (GRUB_ERR_BAD_ARGUMENT, "the length of vhd file name must be less than 512 bytes");
		goto fail;
	}

	/* Ignore errors. Perhaps it's not fatal.  */
	grub_errno = GRUB_ERR_NONE;

	boot_drive = 0x80;
	boot_part_addr = 0;

	grub_loader_set (grub_vboot_boot, grub_vboot_unload, 1);
	return;

fail:

	if (file)
		grub_file_close (file);

	grub_dl_unref (my_mod);
}

static grub_err_t
grub_cmd_vboot16 (grub_extcmd_context_t ctxt __attribute__ ((unused)),
		      int argc, char *argv[])
{	
	char *vhd_filename = 0;

	if (argc > 1 /*&& grub_strcmp (argv[0], "--vhd") == 0*/)
	{
		vhd_filename = argv[1];
		//argc--;
		//argv++;
	}

	if (argc == 0)
		return grub_error (GRUB_ERR_BAD_ARGUMENT, "no file specified");
	else
		grub_vboot_cmd (argv[0], vhd_filename);

	return grub_errno;
}

/* insmod MODULE */
static grub_err_t
vboot_cmd_insmod (grub_extcmd_context_t ctxt __attribute__ ((unused)),
		      int argc, char *argv[])
{
  char *p;
  grub_dl_t mod;

  if (argc == 0)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "no module specified");

  p = grub_strchr (argv[0], '/');
  if (! p)
    mod = vboot_dl_load (argv[0]);
  else
    mod = vboot_dl_load_file (argv[0]);

  if (mod)
  {
    vboot_dl_ref (mod);

	grub_printf ("module name: %s\n", mod->name);
	grub_printf ("text section addr: %p\n", p_text_section_addr);
	grub_printf ("init function: %p\n", mod->init);	
  }

  return 0;
}

/* retrieve the memory range the module occupies, from start of lowest segment to end of the highest segment
 * return the address, with the size
 */
static void*
get_module_memory_span (grub_dl_t mod, grub_uint32_t *pOutSize)
{
	grub_dl_segment_t seg;
	char *addr = 0;
	char *end = 0;	

	for (seg = mod->segment; seg; seg = seg->next) 
	{
		if (seg->size == 0)
			continue;
		if (addr == 0)
			addr = seg->addr;
		else if ((char *)seg->addr < addr)
			addr = (char *)seg->addr;

		if (end < (char *)seg->addr + seg->size)
			end = (char *)seg->addr + seg->size;
	}

	*pOutSize = end - addr;
	return addr;
}
#if 0
#define StartsWith(s1, s2) ((grub_strlen(s1) >= grub_strlen(s2)) && !grub_strncmp(s1, s2, grub_strlen(s2)))

// add double quotes to strings, e.g., new_menu_entry=Snapshot 1 => new_menu_entry="Snapshot 1"
static char *quote_string_pattern(char *str, const char *pattern)
{
	char *new_str;
	char *p;

	if (StartsWith(str, pattern))
	{
		p = str + grub_strlen(pattern);
		if (*p != '"')
		{
			new_str = grub_malloc(grub_strlen(str) + 2);
			if (new_str)
			{
				grub_sprintf(new_str, "%s\"%s\"", pattern, p);
				return new_str;
			}
		}
	}

	return NULL;
}

static char *quote_string(char *str)
{
	char *new_str;

	new_str = quote_string_pattern(str, "new_menu_entry=");
	if (new_str)
		return new_str;

	new_str = quote_string_pattern(str, "harddisk=");
	if (new_str)
		return new_str;

	new_str = quote_string_pattern(str, "cdrom=");
	if (new_str)
		return new_str;

	new_str = quote_string_pattern(str, "floppy=");
	if (new_str)
		return new_str;

	new_str = quote_string_pattern(str, "floppy_b=");
	if (new_str)
		return new_str;

	new_str = quote_string_pattern(str, "take_snapshot=");
	if (new_str)
		return new_str;
	
	new_str = quote_string_pattern(str, "config_file=");
	if (new_str)
		return new_str;

	return str;
}
#endif
/* insmod MODULE */
static grub_err_t
grub_cmd_vboot (grub_extcmd_context_t ctxt __attribute__ ((unused)),
		      int argc __attribute__ ((unused)), char *argv[] __attribute__ ((unused)))
{
  grub_dl_t mod;
  const char *root = grub_env_get("root");
  const char *p = grub_env_get("vbootcore_addr");;
  char *vbootloader = (char *)grub_env_get("vbootloader");

  //if (argc == 0)
  //  return grub_error (GRUB_ERR_BAD_ARGUMENT, "no module specified");
 
  if (! vbootloader)
  {
    if (root)
	  grub_sprintf(vbootloader, "(%s)/vboot/vboot", root);
    else
	  grub_strcpy(vbootloader, "/vboot/vboot");
  }
  grub_file_t file = grub_file_open(vbootloader, GRUB_FILE_TYPE_LINUX_KERNEL);
  if (file == 0)
  {
	  grub_printf("The critical vboot loader file does not exist: %s\n", vbootloader);
	  return grub_errno;
  }

  if (p)
  {
	  vboot_core_addr = (char *)grub_strtoul(p, 0, 16);
  }

  grub_printf ("vboot_core_addr: %p\n", vboot_core_addr);
  mod = vboot_dl_load ("vbootcore");
 
  if (mod)
  {
	  void *mod_addr;
	  grub_uint32_t size;
    int i;
	int num_args;
	char **linux16_args;
	char code[48]; 
	char init[48];
	char addr[48];
	char size_buf[48];
	//char *initrd16_args[2];

    vboot_dl_ref (mod);
	mod_addr = get_module_memory_span(mod, &size);

	grub_printf ("module name: %s\n", mod->name);
	grub_printf ("module addr: %p, size: 0x%x\n", mod_addr, size);
	grub_printf ("text section addr: %p\n", p_text_section_addr);
	grub_printf ("init function: %p\n", mod->init);

	/* linux16 /vboot/memdisk code=12356 init=78980 config_file=(hd0,1)/vboot/grub/grub.cfg */
	grub_sprintf(code, "code=%x", (grub_uint32_t)p_text_section_addr);
	grub_sprintf(init, "init=%x", (grub_uint32_t)mod->init);
	grub_sprintf(addr, "addr=%x", (grub_uint32_t)mod_addr);
	grub_sprintf(size_buf, "size=%x", size);

	

	linux16_args = (char **)grub_malloc((argc + 6) * sizeof(char **));
	linux16_args[0] = vbootloader;	
	linux16_args[1] = code;
	linux16_args[2] = init;
	linux16_args[3] = addr;
	linux16_args[4] = size_buf;
    num_args = 5;

	for (i=0; i<argc; i++)
	{
		linux16_args[num_args+i] = argv[i];
        grub_printf("arg %d: %s\n", i, linux16_args[num_args+i]);
	}
	linux16_args[i+num_args] = 0;
    
    //grub_printf("press any key to continue...");
	//grub_getkey();

	grub_command_execute("linux16", argc + num_args, linux16_args);	

	/* initrd16 /vboot/622C.IMG */
	/*
    initrd16_args[0] = "/vboot/622C.IMG";	
	initrd16_args[1] = 0;
	grub_command_execute("initrd16", 1, initrd16_args);
	*/

	grub_free(linux16_args);
  }

  return 0;
}

int test_vhd_file(const char *pszFilename)
{
	int rc = GRUB_ERR_NONE;
	grub_uint64_t off;
	//grub_ssize_t bytesRead;
	char buf[512];
    
    grub_file_t file = grub_file_open(pszFilename, GRUB_FILE_TYPE_LOOPBACK);
	if (file == 0)
		rc = grub_errno;

	// read last sector, the VHD header
	off = file->size - 512;
	grub_file_seek(file, off);

    //bytesRead = 
    grub_file_read(file, buf, 512);

	if (grub_memcmp(buf, "conectix", 8) != 0)
	{
		grub_printf("test_vhd_file(): VERR_VD_VHD_INVALID_HEADER\n");
		rc = 31;
	}

	grub_file_close(file);

	return rc;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(vboot)
{
	cmd = grub_register_extcmd ("vboot16", grub_cmd_vboot16, 0, "vboot [OPTIONS...]", "vhd native boot", 0);
	cmd = grub_register_extcmd ("vbootinsmod", vboot_cmd_insmod, 0, "vboot [OPTIONS...]", "vhd native boot", 0);
	cmd = grub_register_extcmd ("vboot", grub_cmd_vboot, 0, "vboot [OPTIONS...]", "vhd native boot", 0);
	my_mod = mod;
}

GRUB_MOD_FINI(vboot)
{
	grub_unregister_extcmd (cmd);
}
