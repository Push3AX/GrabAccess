/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"      /* Obtains integer types */
#include "diskio.h"    /* Declarations of disk functions */
#include <grub/types.h>
#include <grub/disk.h>
#include <grub/err.h>
#include <grub/datetime.h>

STAT fat_stat[MAX_DRIVES];

/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (BYTE pdrv)
{
  if (pdrv >= MAX_DRIVES)
    return STA_NOINIT;

  if (!fat_stat[pdrv].present)
    return STA_NOINIT;
  if (!fat_stat[pdrv].disk)
    return STA_NOINIT;

  return 0;
}

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (BYTE pdrv)
{
  if (pdrv >= MAX_DRIVES)
    return STA_NOINIT;

  if (!fat_stat[pdrv].present)
    return STA_NOINIT;
  if (!fat_stat[pdrv].disk)
    return STA_NOINIT;

  return 0;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
  grub_err_t errno;
  grub_size_t size = 0;

  size = count << GRUB_DISK_SECTOR_BITS;

  if (pdrv >= MAX_DRIVES)
    return RES_PARERR;
  if (!fat_stat[pdrv].present)
    return RES_NOTRDY;
  if (!fat_stat[pdrv].disk)
    return RES_NOTRDY;
  if (sector > fat_stat[pdrv].total_sectors)
    return RES_ERROR;

  errno = grub_disk_read (fat_stat[pdrv].disk, sector, 0, size, buff);

  if (errno == GRUB_ERR_NONE)
    return RES_OK;
  return RES_ERROR;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

DRESULT disk_write (BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
  grub_err_t errno;
  grub_size_t size = 0;

  size = count << GRUB_DISK_SECTOR_BITS;

  if (pdrv >= MAX_DRIVES)
    return RES_PARERR;
  if (!fat_stat[pdrv].present)
    return RES_ERROR;
  if (!fat_stat[pdrv].disk)
    return RES_ERROR;
  if (sector > fat_stat[pdrv].total_sectors)
    return RES_ERROR;

  errno = grub_disk_write (fat_stat[pdrv].disk, sector, 0, size, buff);

  if (errno == GRUB_ERR_NONE)
    return RES_OK;

  return RES_ERROR;
}

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (BYTE pdrv, BYTE cmd, void *buff)
{
  DRESULT res;
  if (pdrv >= MAX_DRIVES)
    return RES_PARERR;
  if (!fat_stat[pdrv].present)
    return RES_ERROR;
  if (!fat_stat[pdrv].disk)
    return RES_ERROR;

  switch (cmd)
  {
    case CTRL_SYNC:
      res = RES_OK;
      break;
    case GET_SECTOR_COUNT:
      *(LBA_t*)buff = fat_stat[pdrv].total_sectors;
      res = RES_OK;
      break;
    case GET_SECTOR_SIZE:
      *(WORD*)buff = GRUB_DISK_SECTOR_SIZE;
      res = RES_OK;
      break;
    case GET_BLOCK_SIZE:
      *(DWORD*)buff = 1;
      res = RES_OK;
      break;
    default:
      res = RES_PARERR;
  }
  return res;
}

DWORD get_fattime (void)
{
  struct grub_datetime tm;

  if (grub_get_datetime (&tm))
    return 0;

  /* Pack date and time into a DWORD variable */
  return ((DWORD)(tm.year - 1980) << 25)
         | ((DWORD)tm.month << 21)
         | ((DWORD)tm.day << 16)
         | (WORD)(tm.hour << 11)
         | (WORD)(tm.minute << 5)
         | (WORD)(tm.second >> 1);
}
