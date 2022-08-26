/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013  Free Software Foundation, Inc.
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

#ifndef GRUB_UDF_H
#define GRUB_UDF_H	1

#include <grub/types.h>
#include <grub/file.h>

#define GRUB_UDF_MAX_PDS		2
#define GRUB_UDF_MAX_PMS		6

#define U16				grub_le_to_cpu16
#define U32				grub_le_to_cpu32
#define U64				grub_le_to_cpu64

#define GRUB_UDF_TAG_IDENT_PVD		0x0001
#define GRUB_UDF_TAG_IDENT_AVDP		0x0002
#define GRUB_UDF_TAG_IDENT_VDP		0x0003
#define GRUB_UDF_TAG_IDENT_IUVD		0x0004
#define GRUB_UDF_TAG_IDENT_PD		0x0005
#define GRUB_UDF_TAG_IDENT_LVD		0x0006
#define GRUB_UDF_TAG_IDENT_USD		0x0007
#define GRUB_UDF_TAG_IDENT_TD		0x0008
#define GRUB_UDF_TAG_IDENT_LVID		0x0009

#define GRUB_UDF_TAG_IDENT_FSD		0x0100
#define GRUB_UDF_TAG_IDENT_FID		0x0101
#define GRUB_UDF_TAG_IDENT_AED		0x0102
#define GRUB_UDF_TAG_IDENT_IE		0x0103
#define GRUB_UDF_TAG_IDENT_TE		0x0104
#define GRUB_UDF_TAG_IDENT_FE		0x0105
#define GRUB_UDF_TAG_IDENT_EAHD		0x0106
#define GRUB_UDF_TAG_IDENT_USE		0x0107
#define GRUB_UDF_TAG_IDENT_SBD		0x0108
#define GRUB_UDF_TAG_IDENT_PIE		0x0109
#define GRUB_UDF_TAG_IDENT_EFE		0x010A

#define GRUB_UDF_ICBTAG_TYPE_UNDEF	0x00
#define GRUB_UDF_ICBTAG_TYPE_USE	0x01
#define GRUB_UDF_ICBTAG_TYPE_PIE	0x02
#define GRUB_UDF_ICBTAG_TYPE_IE		0x03
#define GRUB_UDF_ICBTAG_TYPE_DIRECTORY	0x04
#define GRUB_UDF_ICBTAG_TYPE_REGULAR	0x05
#define GRUB_UDF_ICBTAG_TYPE_BLOCK	0x06
#define GRUB_UDF_ICBTAG_TYPE_CHAR	0x07
#define GRUB_UDF_ICBTAG_TYPE_EA		0x08
#define GRUB_UDF_ICBTAG_TYPE_FIFO	0x09
#define GRUB_UDF_ICBTAG_TYPE_SOCKET	0x0A
#define GRUB_UDF_ICBTAG_TYPE_TE		0x0B
#define GRUB_UDF_ICBTAG_TYPE_SYMLINK	0x0C
#define GRUB_UDF_ICBTAG_TYPE_STREAMDIR	0x0D

#define GRUB_UDF_ICBTAG_FLAG_AD_MASK	0x0007
#define GRUB_UDF_ICBTAG_FLAG_AD_SHORT	0x0000
#define GRUB_UDF_ICBTAG_FLAG_AD_LONG	0x0001
#define GRUB_UDF_ICBTAG_FLAG_AD_EXT	0x0002
#define GRUB_UDF_ICBTAG_FLAG_AD_IN_ICB	0x0003

#define GRUB_UDF_EXT_NORMAL		0x00000000
#define GRUB_UDF_EXT_NREC_ALLOC		0x40000000
#define GRUB_UDF_EXT_NREC_NALLOC	0x80000000
#define GRUB_UDF_EXT_MASK		0xC0000000

#define GRUB_UDF_FID_CHAR_HIDDEN	0x01
#define GRUB_UDF_FID_CHAR_DIRECTORY	0x02
#define GRUB_UDF_FID_CHAR_DELETED	0x04
#define GRUB_UDF_FID_CHAR_PARENT	0x08
#define GRUB_UDF_FID_CHAR_METADATA	0x10

#define GRUB_UDF_STD_IDENT_BEA01	"BEA01"
#define GRUB_UDF_STD_IDENT_BOOT2	"BOOT2"
#define GRUB_UDF_STD_IDENT_CD001	"CD001"
#define GRUB_UDF_STD_IDENT_CDW02	"CDW02"
#define GRUB_UDF_STD_IDENT_NSR02	"NSR02"
#define GRUB_UDF_STD_IDENT_NSR03	"NSR03"
#define GRUB_UDF_STD_IDENT_TEA01	"TEA01"

#define GRUB_UDF_CHARSPEC_TYPE_CS0	0x00
#define GRUB_UDF_CHARSPEC_TYPE_CS1	0x01
#define GRUB_UDF_CHARSPEC_TYPE_CS2	0x02
#define GRUB_UDF_CHARSPEC_TYPE_CS3	0x03
#define GRUB_UDF_CHARSPEC_TYPE_CS4	0x04
#define GRUB_UDF_CHARSPEC_TYPE_CS5	0x05
#define GRUB_UDF_CHARSPEC_TYPE_CS6	0x06
#define GRUB_UDF_CHARSPEC_TYPE_CS7	0x07
#define GRUB_UDF_CHARSPEC_TYPE_CS8	0x08

#define GRUB_UDF_PARTMAP_TYPE_1		1
#define GRUB_UDF_PARTMAP_TYPE_2		2

struct grub_udf_lb_addr
{
  grub_uint32_t block_num;
  grub_uint16_t part_ref;
} GRUB_PACKED;

struct grub_udf_short_ad
{
  grub_uint32_t length;
  grub_uint32_t position;
} GRUB_PACKED;

struct grub_udf_long_ad
{
  grub_uint32_t length;
  struct grub_udf_lb_addr block;
  grub_uint8_t imp_use[6];
} GRUB_PACKED;

struct grub_udf_extent_ad
{
  grub_uint32_t length;
  grub_uint32_t start;
} GRUB_PACKED;

struct grub_udf_charspec
{
  grub_uint8_t charset_type;
  grub_uint8_t charset_info[63];
} GRUB_PACKED;

struct grub_udf_timestamp
{
  grub_uint16_t type_and_timezone;
  grub_uint16_t year;
  grub_uint8_t month;
  grub_uint8_t day;
  grub_uint8_t hour;
  grub_uint8_t minute;
  grub_uint8_t second;
  grub_uint8_t centi_seconds;
  grub_uint8_t hundreds_of_micro_seconds;
  grub_uint8_t micro_seconds;
} GRUB_PACKED;

struct grub_udf_regid
{
  grub_uint8_t flags;
  grub_uint8_t ident[23];
  grub_uint8_t ident_suffix[8];
} GRUB_PACKED;

struct grub_udf_tag
{
  grub_uint16_t tag_ident;
  grub_uint16_t desc_version;
  grub_uint8_t tag_checksum;
  grub_uint8_t reserved;
  grub_uint16_t tag_serial_number;
  grub_uint16_t desc_crc;
  grub_uint16_t desc_crc_length;
  grub_uint32_t tag_location;
} GRUB_PACKED;

struct grub_udf_fileset
{
  struct grub_udf_tag tag;
  struct grub_udf_timestamp datetime;
  grub_uint16_t interchange_level;
  grub_uint16_t max_interchange_level;
  grub_uint32_t charset_list;
  grub_uint32_t max_charset_list;
  grub_uint32_t fileset_num;
  grub_uint32_t fileset_desc_num;
  struct grub_udf_charspec vol_charset;
  grub_uint8_t vol_ident[128];
  struct grub_udf_charspec fileset_charset;
  grub_uint8_t fileset_ident[32];
  grub_uint8_t copyright_file_ident[32];
  grub_uint8_t abstract_file_ident[32];
  struct grub_udf_long_ad root_icb;
  struct grub_udf_regid domain_ident;
  struct grub_udf_long_ad next_ext;
  struct grub_udf_long_ad streamdir_icb;
} GRUB_PACKED;

struct grub_udf_icbtag
{
  grub_uint32_t prior_recorded_num_direct_entries;
  grub_uint16_t strategy_type;
  grub_uint16_t strategy_parameter;
  grub_uint16_t num_entries;
  grub_uint8_t reserved;
  grub_uint8_t file_type;
  struct grub_udf_lb_addr parent_idb;
  grub_uint16_t flags;
} GRUB_PACKED;

struct grub_udf_file_ident
{
  struct grub_udf_tag tag;
  grub_uint16_t version_num;
  grub_uint8_t characteristics;
  grub_uint8_t file_ident_length;
  struct grub_udf_long_ad icb;
  grub_uint16_t imp_use_length;
} GRUB_PACKED;

struct grub_udf_file_entry
{
  struct grub_udf_tag tag;
  struct grub_udf_icbtag icbtag;
  grub_uint32_t uid;
  grub_uint32_t gid;
  grub_uint32_t permissions;
  grub_uint16_t link_count;
  grub_uint8_t record_format;
  grub_uint8_t record_display_attr;
  grub_uint32_t record_length;
  grub_uint64_t file_size;
  grub_uint64_t blocks_recorded;
  struct grub_udf_timestamp access_time;
  struct grub_udf_timestamp modification_time;
  struct grub_udf_timestamp attr_time;
  grub_uint32_t checkpoint;
  struct grub_udf_long_ad extended_attr_idb;
  struct grub_udf_regid imp_ident;
  grub_uint64_t unique_id;
  grub_uint32_t ext_attr_length;
  grub_uint32_t alloc_descs_length;
  grub_uint8_t ext_attr[0];
} GRUB_PACKED;

struct grub_udf_extended_file_entry
{
  struct grub_udf_tag tag;
  struct grub_udf_icbtag icbtag;
  grub_uint32_t uid;
  grub_uint32_t gid;
  grub_uint32_t permissions;
  grub_uint16_t link_count;
  grub_uint8_t record_format;
  grub_uint8_t record_display_attr;
  grub_uint32_t record_length;
  grub_uint64_t file_size;
  grub_uint64_t object_size;
  grub_uint64_t blocks_recorded;
  struct grub_udf_timestamp access_time;
  struct grub_udf_timestamp modification_time;
  struct grub_udf_timestamp create_time;
  struct grub_udf_timestamp attr_time;
  grub_uint32_t checkpoint;
  grub_uint32_t reserved;
  struct grub_udf_long_ad extended_attr_icb;
  struct grub_udf_long_ad streamdir_icb;
  struct grub_udf_regid imp_ident;
  grub_uint64_t unique_id;
  grub_uint32_t ext_attr_length;
  grub_uint32_t alloc_descs_length;
  grub_uint8_t ext_attr[0];
} GRUB_PACKED;

struct grub_udf_vrs
{
  grub_uint8_t type;
  grub_uint8_t magic[5];
  grub_uint8_t version;
} GRUB_PACKED;

struct grub_udf_avdp
{
  struct grub_udf_tag tag;
  struct grub_udf_extent_ad vds;
} GRUB_PACKED;

struct grub_udf_pd
{
  struct grub_udf_tag tag;
  grub_uint32_t seq_num;
  grub_uint16_t flags;
  grub_uint16_t part_num;
  struct grub_udf_regid contents;
  grub_uint8_t contents_use[128];
  grub_uint32_t access_type;
  grub_uint32_t start;
  grub_uint32_t length;
} GRUB_PACKED;

struct grub_udf_partmap
{
  grub_uint8_t type;
  grub_uint8_t length;
  union
  {
    struct
    {
      grub_uint16_t seq_num;
      grub_uint16_t part_num;
    } type1;

    struct
    {
      grub_uint8_t ident[62];
    } type2;
  };
} GRUB_PACKED;

struct grub_udf_pvd
{
  struct grub_udf_tag tag;
  grub_uint32_t seq_num;
  grub_uint32_t pvd_num;
  grub_uint8_t ident[32];
  grub_uint16_t vol_seq_num;
  grub_uint16_t max_vol_seq_num;
  grub_uint16_t interchange_level;
  grub_uint16_t max_interchange_level;
  grub_uint32_t charset_list;
  grub_uint32_t max_charset_list;
  grub_uint8_t volset_ident[128];
  struct grub_udf_charspec desc_charset;
  struct grub_udf_charspec expl_charset;
  struct grub_udf_extent_ad vol_abstract;
  struct grub_udf_extent_ad vol_copyright;
  struct grub_udf_regid app_ident;
  struct grub_udf_timestamp recording_time;
  struct grub_udf_regid imp_ident;
  grub_uint8_t imp_use[64];
  grub_uint32_t pred_vds_loc;
  grub_uint16_t flags;
  grub_uint8_t reserved[22];
} GRUB_PACKED;

struct grub_udf_lvd
{
  struct grub_udf_tag tag;
  grub_uint32_t seq_num;
  struct grub_udf_charspec charset;
  grub_uint8_t ident[128];
  grub_uint32_t bsize;
  struct grub_udf_regid domain_ident;
  struct grub_udf_long_ad root_fileset;
  grub_uint32_t map_table_length;
  grub_uint32_t num_part_maps;
  struct grub_udf_regid imp_ident;
  grub_uint8_t imp_use[128];
  struct grub_udf_extent_ad integrity_seq_ext;
  grub_uint8_t part_maps[1608];
} GRUB_PACKED;

struct grub_udf_aed
{
  struct grub_udf_tag tag;
  grub_uint32_t prev_ae;
  grub_uint32_t ae_len;
} GRUB_PACKED;

grub_uint64_t
grub_udf_get_file_offset(grub_file_t file);

grub_uint64_t
grub_udf_get_last_pd_size_offset(void);

grub_uint64_t
grub_udf_get_last_file_attr_offset (grub_file_t file,
                                    grub_uint32_t *startBlock,
                                    grub_uint64_t *fe_entry_size_offset);

#ifdef GRUB_UTIL
#include <grub/disk.h>

grub_disk_addr_t
grub_udf_get_cluster_sector (grub_disk_t disk, grub_uint64_t *sec_per_lcn);
#endif
#endif
