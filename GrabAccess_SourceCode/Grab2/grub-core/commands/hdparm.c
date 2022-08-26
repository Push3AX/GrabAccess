/* hdparm.c - command to get/set ATA disk parameters.  */
/*
 *  GRUB  --  GRand Unified Bootloader
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

#include <grub/ata.h>
#include <grub/scsi.h>
#include <grub/disk.h>
#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/lib/hexdump.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

static const struct grub_arg_option options[] = {
  {"apm",             'B', 0, N_("Set Advanced Power Management\n"
			      "(1=low, ..., 254=high, 255=off)."),
			      0, ARG_TYPE_INT},
  {"power",           'C', 0, N_("Display power mode."), 0, ARG_TYPE_NONE},
  {"security-freeze", 'F', 0, N_("Freeze ATA security settings until reset."),
			      0, ARG_TYPE_NONE},
  {"health",          'H', 0, N_("Display SMART health status."), 0, ARG_TYPE_NONE},
  {"aam",             'M', 0, N_("Set Automatic Acoustic Management\n"
			      "(0=off, 128=quiet, ..., 254=fast)."),
			      0, ARG_TYPE_INT},
  {"standby-timeout", 'S', 0, N_("Set standby timeout\n"
			      "(0=off, 1=5s, 2=10s, ..., 240=20m, 241=30m, ...)."),
			      0, ARG_TYPE_INT},
  {"standby",         'y', 0, N_("Set drive to standby mode."), 0, ARG_TYPE_NONE},
  {"sleep",           'Y', 0, N_("Set drive to sleep mode."), 0, ARG_TYPE_NONE},
  {"identify",        'i', 0, N_("Print drive identity and settings."),
			      0, ARG_TYPE_NONE},
  {"dumpid",          'I', 0, N_("Show raw contents of ATA IDENTIFY sector."),
			       0, ARG_TYPE_NONE},
  {"smart",            -1, 0, N_("Disable/enable SMART (0/1)."), 0, ARG_TYPE_INT},
  {"quiet",           'q', 0, N_("Do not print messages."), 0, ARG_TYPE_NONE},
  {0, 0, 0, 0, 0, 0}
};

enum grub_ata_smart_commands
  {
    GRUB_ATA_FEAT_SMART_ENABLE  = 0xd8,
    GRUB_ATA_FEAT_SMART_DISABLE = 0xd9,
    GRUB_ATA_FEAT_SMART_STATUS  = 0xda,
  };

static int quiet = 0;

static grub_err_t
grub_hdparm_do_ata_cmd (grub_ata_t ata, grub_uint8_t cmd,
			grub_uint8_t features, grub_uint8_t sectors,
			void * buffer, int size)
{
  struct grub_disk_ata_pass_through_parms apt;
  grub_memset (&apt, 0, sizeof (apt));

  apt.taskfile.cmd = cmd;
  apt.taskfile.features = features;
  apt.taskfile.sectors = sectors;
  apt.taskfile.disk = 0xE0;

  apt.buffer = buffer;
  apt.size = size;

  if (ata->dev->readwrite (ata, &apt, 0))
    return grub_errno;

  return GRUB_ERR_NONE;
}

static int
grub_hdparm_do_check_powermode_cmd (grub_ata_t ata)
{
  struct grub_disk_ata_pass_through_parms apt;
  grub_memset (&apt, 0, sizeof (apt));

  apt.taskfile.cmd = GRUB_ATA_CMD_CHECK_POWER_MODE;
  apt.taskfile.disk = 0xE0;

  if (ata->dev->readwrite (ata, &apt, 0))
    return -1;

  return apt.taskfile.sectors;
}

static int
grub_hdparm_do_smart_cmd (grub_ata_t ata, grub_uint8_t features)
{
  struct grub_disk_ata_pass_through_parms apt;
  grub_memset (&apt, 0, sizeof (apt));

  apt.taskfile.cmd = GRUB_ATA_CMD_SMART;
  apt.taskfile.features = features;
  apt.taskfile.lba_mid  = 0x4f;
  apt.taskfile.lba_high = 0xc2;
  apt.taskfile.disk = 0xE0;

  if (ata->dev->readwrite (ata, &apt, 0))
    return -1;

  if (features == GRUB_ATA_FEAT_SMART_STATUS)
    {
      if (   apt.taskfile.lba_mid  == 0x4f
          && apt.taskfile.lba_high == 0xc2)
	return 0; /* Good SMART status.  */
      else if (   apt.taskfile.lba_mid  == 0xf4
	       && apt.taskfile.lba_high == 0x2c)
	return 1; /* Bad SMART status.  */
      else
	return -1;
    }
  return 0;
}

static grub_err_t
grub_hdparm_simple_cmd (const char * msg,
			grub_ata_t ata, grub_uint8_t cmd)
{
  if (! quiet && msg)
    grub_printf ("%s", msg);

  grub_err_t err = grub_hdparm_do_ata_cmd (ata, cmd, 0, 0, NULL, 0);

  if (! quiet && msg)
    grub_printf ("%s\n", ! err ? "" : ": not supported");
  return err;
}

static grub_err_t
grub_hdparm_set_val_cmd (const char * msg, int val,
			 grub_ata_t ata, grub_uint8_t cmd,
			 grub_uint8_t features, grub_uint8_t sectors)
{
  if (! quiet && msg && *msg)
    {
      if (val >= 0)
	grub_printf ("Set %s to %d", msg, val);
      else
	grub_printf ("Disable %s", msg);
    }

  grub_err_t err = grub_hdparm_do_ata_cmd (ata, cmd, features, sectors,
					   NULL, 0);

  if (! quiet && msg)
    grub_printf ("%s\n", ! err ? "" : ": not supported");
  return err;
}

static const char *
le16_to_char (grub_uint16_t *dest, const grub_uint16_t * src16, unsigned bytes)
{
  unsigned i;
  for (i = 0; i < bytes / 2; i++)
    dest[i] = grub_swap_bytes16 (src16[i]);
  dest[i] = 0;
  return (char *) dest;
}

static void
grub_hdparm_print_identify (const grub_uint16_t * idw)
{
  /* Print identity strings.  */
  grub_uint16_t tmp[21];
  grub_printf ("Model:    \"%.40s\"\n", le16_to_char (tmp, &idw[27], 40));
  grub_printf ("Firmware: \"%.8s\"\n",  le16_to_char (tmp, &idw[23], 8));
  grub_printf ("Serial:   \"%.20s\"\n", le16_to_char (tmp, &idw[10], 20));

  /* Print AAM, APM and SMART settings.  */
  grub_uint16_t features1 = grub_le_to_cpu16 (idw[82]);
  grub_uint16_t features2 = grub_le_to_cpu16 (idw[83]);
  grub_uint16_t enabled1  = grub_le_to_cpu16 (idw[85]);
  grub_uint16_t enabled2  = grub_le_to_cpu16 (idw[86]);

  grub_printf ("Automatic Acoustic Management: ");
  if (features2 & 0x0200)
    {
      if (enabled2 & 0x0200)
	{
	  grub_uint16_t aam = grub_le_to_cpu16 (idw[94]);
	  grub_printf ("%u (128=quiet, ..., 254=fast, recommended=%u)\n",
		       aam & 0xff, (aam >> 8) & 0xff);
	}
      else
	grub_printf ("disabled\n");
    }
  else
    grub_printf ("not supported\n");

  grub_printf ("Advanced Power Management: ");
  if (features2 & 0x0008)
    {
      if (enabled2 & 0x0008)
	grub_printf ("%u (1=low, ..., 254=high)\n",
		     grub_le_to_cpu16 (idw[91]) & 0xff);
      else
	grub_printf ("disabled\n");
    }
  else
    grub_printf ("not supported\n");

  grub_printf ("SMART Feature Set: ");
  if (features1 & 0x0001)
    grub_printf ("%sabled\n", (enabled1 & 0x0001 ? "en" : "dis"));
  else
    grub_printf ("not supported\n");

  /* Print security settings.  */
  grub_uint16_t security = grub_le_to_cpu16 (idw[128]);

  grub_printf ("ATA Security: ");
  if (security & 0x0001)
    grub_printf ("%s, %s, %s, %s\n",
		 (security & 0x0002 ? "ENABLED" : "disabled"),
		 (security & 0x0004 ? "**LOCKED**"  : "not locked"),
		 (security & 0x0008 ? "frozen" : "NOT FROZEN"),
		 (security & 0x0010 ? "COUNT EXPIRED" : "count not expired"));
  else
    grub_printf ("not supported\n");
}

static void
grub_hdparm_print_standby_tout (int timeout)
{
  if (timeout == 0)
    grub_printf ("off");
  else if (timeout <= 252 || timeout == 255)
    {
      int h = 0, m = 0 , s = 0;
      if (timeout == 255)
	{
	  m = 21;
	  s = 15;
	}
      else if (timeout == 252)
	m = 21;
      else if (timeout <= 240)
	{
	  s = timeout * 5;
	  m = s / 60;
	  s %= 60;
	}
      else
	{
	  m = (timeout - 240) * 30;
	  h  = m / 60;
	  m %= 60;
	}
      grub_printf ("%02d:%02d:%02d", h, m, s);
    }
  else
    grub_printf ("invalid or vendor-specific");
}

static int get_int_arg (const struct grub_arg_list *state)
{
  return (state->set ? (int)grub_strtoul (state->arg, 0, 0) : -1);
}

static grub_err_t
grub_cmd_hdparm (grub_extcmd_context_t ctxt, int argc, char **args)
{
  struct grub_arg_list *state = ctxt->state;
  struct grub_ata *ata;
  const char *diskname;

  /* Check command line.  */
  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("one argument expected"));

  if (args[0][0] == '(')
    {
      grub_size_t len = grub_strlen (args[0]);
      if (args[0][len - 1] == ')')
	args[0][len - 1] = 0;
      diskname = &args[0][1];
    }
  else
    diskname = &args[0][0];

  int i = 0;
  int apm          = get_int_arg (&state[i++]);
  int power        = state[i++].set;
  int sec_freeze   = state[i++].set;
  int health       = state[i++].set;
  int aam          = get_int_arg (&state[i++]);
  int standby_tout = get_int_arg (&state[i++]);
  int standby_now  = state[i++].set;
  int sleep_now    = state[i++].set;
  int ident        = state[i++].set;
  int dumpid       = state[i++].set;
  int enable_smart = get_int_arg (&state[i++]);
  quiet            = state[i++].set;

  /* Open disk.  */
  grub_disk_t disk = grub_disk_open (diskname);
  if (! disk)
    return grub_errno;

  switch (disk->dev->id)
    {
    case GRUB_DISK_DEVICE_ATA_ID:
      ata = disk->data;
      break;
    case GRUB_DISK_DEVICE_SCSI_ID:
      if (((disk->id >> GRUB_SCSI_ID_SUBSYSTEM_SHIFT) & 0xFF)
	  == GRUB_SCSI_SUBSYSTEM_PATA
	  || (((disk->id >> GRUB_SCSI_ID_SUBSYSTEM_SHIFT) & 0xFF)
	      == GRUB_SCSI_SUBSYSTEM_AHCI))
	{
	  ata = ((struct grub_scsi *) disk->data)->data;
	  break;
	}
      /* FALLTHROUGH */
    default:
      grub_disk_close (disk);
      return grub_error (GRUB_ERR_IO, "not an ATA device");
    }
    

  /* Change settings.  */
  if (aam >= 0)
    grub_hdparm_set_val_cmd ("Automatic Acoustic Management", (aam ? aam : -1),
			     ata, GRUB_ATA_CMD_SET_FEATURES,
			     (aam ? 0x42 : 0xc2), aam);

  if (apm >= 0)
    grub_hdparm_set_val_cmd ("Advanced Power Management",
			     (apm != 255 ? apm : -1), ata,
			     GRUB_ATA_CMD_SET_FEATURES,
			     (apm != 255 ? 0x05 : 0x85),
			     (apm != 255 ? apm : 0));

  if (standby_tout >= 0)
    {
      if (! quiet)
	{
	  grub_printf ("Set standby timeout to %d (", standby_tout);
	  grub_hdparm_print_standby_tout (standby_tout);
	  grub_printf (")");
	}
      /* The IDLE cmd sets disk to idle mode and configures standby timer.  */
      grub_hdparm_set_val_cmd ("", -1, ata, GRUB_ATA_CMD_IDLE, 0, standby_tout);
    }

  if (enable_smart >= 0)
    {
      if (! quiet)
	grub_printf ("%sable SMART operations", (enable_smart ? "En" : "Dis"));
      int err = grub_hdparm_do_smart_cmd (ata, (enable_smart ?
	          GRUB_ATA_FEAT_SMART_ENABLE : GRUB_ATA_FEAT_SMART_DISABLE));
      if (! quiet)
	grub_printf ("%s\n", err ? ": not supported" : "");
    }

  if (sec_freeze)
    grub_hdparm_simple_cmd ("Freeze security settings", ata,
                            GRUB_ATA_CMD_SECURITY_FREEZE_LOCK);

  /* Print/dump IDENTIFY.  */
  if (ident || dumpid)
    {
      grub_uint16_t buf[GRUB_DISK_SECTOR_SIZE / 2];
      if (grub_hdparm_do_ata_cmd (ata, GRUB_ATA_CMD_IDENTIFY_DEVICE,
          0, 0, buf, sizeof (buf)))
	grub_printf ("Cannot read ATA IDENTIFY data\n");
      else
	{
	  if (ident)
	    grub_hdparm_print_identify (buf);
	  if (dumpid)
	    hexdump (0, (char *) buf, sizeof (buf));
	}
    }

  /* Check power mode.  */
  if (power)
    {
      grub_printf ("Disk power mode is: ");
      int mode = grub_hdparm_do_check_powermode_cmd (ata);
      if (mode < 0)
        grub_printf ("unknown\n");
      else
	grub_printf ("%s (0x%02x)\n",
		     (mode == 0xff ? "active/idle" :
		      mode == 0x80 ? "idle" :
		      mode == 0x00 ? "standby" : "unknown"), mode);
    }

  /* Check health.  */
  int status = 0;
  if (health)
    {
      if (! quiet)
	grub_printf ("SMART status is: ");
      int err = grub_hdparm_do_smart_cmd (ata, GRUB_ATA_FEAT_SMART_STATUS);
      if (! quiet)
	grub_printf ("%s\n", (err  < 0 ? "unknown" :
	                      err == 0 ? "OK" : "*BAD*"));
      status = (err > 0);
    }

  /* Change power mode.  */
  if (standby_now)
    grub_hdparm_simple_cmd ("Set disk to standby mode", ata,
			    GRUB_ATA_CMD_STANDBY_IMMEDIATE);

  if (sleep_now)
    grub_hdparm_simple_cmd ("Set disk to sleep mode", ata,
			    GRUB_ATA_CMD_SLEEP);

  grub_disk_close (disk);

  grub_errno = GRUB_ERR_NONE;
  return status;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(hdparm)
{
  cmd = grub_register_extcmd ("hdparm", grub_cmd_hdparm, 0,
			      N_("[OPTIONS] DISK"),
			      N_("Get/set ATA disk parameters."), options);
}

GRUB_MOD_FINI(hdparm)
{
  grub_unregister_extcmd (cmd);
}
