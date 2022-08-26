/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2018  Free Software Foundation, Inc.
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
 *
 *  EFI TPM support code.
 */

#include <grub/err.h>
#include <grub/i18n.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/tpm.h>
#include <grub/mm.h>
#include <grub/tpm.h>
#include <grub/term.h>

typedef TCG_PCR_EVENT grub_tpm_event_t;

static grub_efi_guid_t tpm_guid = EFI_TPM_GUID;
static grub_efi_guid_t tpm2_guid = EFI_TPM2_GUID;

static grub_efi_handle_t *grub_tpm_handle;
static grub_uint8_t grub_tpm_version;

static grub_int8_t tpm1_present = -1;
static grub_int8_t tpm2_present = -1;

static grub_efi_boolean_t
grub_tpm1_present (grub_efi_tpm_protocol_t *tpm)
{
  grub_efi_status_t status;
  TCG_EFI_BOOT_SERVICE_CAPABILITY caps;
  grub_uint32_t flags;
  grub_efi_physical_address_t eventlog, lastevent;

  if (tpm1_present != -1)
    return (grub_efi_boolean_t) tpm1_present;

  caps.Size = (grub_uint8_t) sizeof (caps);

  status = efi_call_5 (tpm->status_check, tpm, &caps, &flags, &eventlog,
		       &lastevent);

  if (status != GRUB_EFI_SUCCESS || caps.TPMDeactivatedFlag
      || !caps.TPMPresentFlag)
    tpm1_present = 0;
  else
    tpm1_present = 1;

  grub_dprintf ("tpm", "tpm1%s present\n", tpm1_present ? "" : " NOT");

  return (grub_efi_boolean_t) tpm1_present;
}

static grub_efi_boolean_t
grub_tpm2_present (grub_efi_tpm2_protocol_t *tpm)
{
  grub_efi_status_t status;
  EFI_TCG2_BOOT_SERVICE_CAPABILITY caps;

  caps.Size = (grub_uint8_t) sizeof (caps);

  if (tpm2_present != -1)
    return (grub_efi_boolean_t) tpm2_present;

  status = efi_call_2 (tpm->get_capability, tpm, &caps);

  if (status != GRUB_EFI_SUCCESS || !caps.TPMPresentFlag)
    tpm2_present = 0;
  else
    tpm2_present = 1;

  grub_dprintf ("tpm", "tpm2%s present\n", tpm2_present ? "" : " NOT");

  return (grub_efi_boolean_t) tpm2_present;
}

static grub_efi_boolean_t
grub_tpm_handle_find (grub_efi_handle_t *tpm_handle,
		      grub_efi_uint8_t *protocol_version)
{
  grub_efi_handle_t *handles;
  grub_efi_uintn_t num_handles;

  if (grub_tpm_handle != NULL)
    {
      *tpm_handle = grub_tpm_handle;
      *protocol_version = grub_tpm_version;
      return 1;
    }

  handles = grub_efi_locate_handle (GRUB_EFI_BY_PROTOCOL, &tpm_guid, NULL,
				    &num_handles);
  if (handles && num_handles > 0)
    {
      grub_tpm_handle = handles[0];
      *tpm_handle = handles[0];
      grub_tpm_version = 1;
      *protocol_version = 1;
      grub_dprintf ("tpm", "TPM handle Found, version: 1\n");
      return 1;
    }

  handles = grub_efi_locate_handle (GRUB_EFI_BY_PROTOCOL, &tpm2_guid, NULL,
				    &num_handles);
  if (handles && num_handles > 0)
    {
      grub_tpm_handle = handles[0];
      *tpm_handle = handles[0];
      grub_tpm_version = 2;
      *protocol_version = 2;
      grub_dprintf ("tpm", "TPM handle Found, version: 2\n");
      return 1;
    }

  return 0;
}

static grub_err_t
grub_efi_log_event_status (grub_efi_status_t status)
{
  switch (status)
    {
    case GRUB_EFI_SUCCESS:
      return 0;
    case GRUB_EFI_DEVICE_ERROR:
      return grub_error (GRUB_ERR_IO, N_("Command failed"));
    case GRUB_EFI_INVALID_PARAMETER:
      return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("Invalid parameter"));
    case GRUB_EFI_BUFFER_TOO_SMALL:
      return grub_error (GRUB_ERR_BAD_ARGUMENT, N_("Output buffer too small"));
    case GRUB_EFI_NOT_FOUND:
      return grub_error (GRUB_ERR_UNKNOWN_DEVICE, N_("TPM unavailable"));
    default:
      return grub_error (GRUB_ERR_UNKNOWN_DEVICE, N_("Unknown TPM error"));
    }
}

static grub_err_t
grub_tpm1_log_event (grub_efi_handle_t tpm_handle, unsigned char *buf,
		     grub_size_t size, grub_uint8_t pcr,
		     const char *description)
{
  grub_tpm_event_t *event;
  grub_efi_status_t status;
  grub_efi_tpm_protocol_t *tpm;
  grub_efi_physical_address_t lastevent;
  grub_uint32_t algorithm;
  grub_uint32_t eventnum = 0;

  tpm = grub_efi_open_protocol (tpm_handle, &tpm_guid,
				GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);

  if (!grub_tpm1_present (tpm))
    return 0;

  event = grub_zalloc (sizeof (*event) + grub_strlen (description) + 1);
  if (!event)
    return grub_error (GRUB_ERR_OUT_OF_MEMORY,
		       N_("cannot allocate TPM event buffer"));

  event->PCRIndex = pcr;
  event->EventType = EV_IPL;
  event->EventSize = grub_strlen (description) + 1;
  grub_memcpy (event->Event, description, event->EventSize);

  algorithm = TCG_ALG_SHA;
  status = efi_call_7 (tpm->log_extend_event, tpm, (grub_addr_t) buf, (grub_uint64_t) size,
		       algorithm, event, &eventnum, &lastevent);
  grub_free (event);

  return grub_efi_log_event_status (status);
}

static grub_err_t
grub_tpm2_log_event (grub_efi_handle_t tpm_handle, unsigned char *buf,
		     grub_size_t size, grub_uint8_t pcr,
		     const char *description)
{
  EFI_TCG2_EVENT *event;
  grub_efi_status_t status;
  grub_efi_tpm2_protocol_t *tpm;

  tpm = grub_efi_open_protocol (tpm_handle, &tpm2_guid,
				GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);

  if (!grub_tpm2_present (tpm))
    return 0;

  event =
    grub_zalloc (sizeof (EFI_TCG2_EVENT) + grub_strlen (description) + 1);
  if (!event)
    return grub_error (GRUB_ERR_OUT_OF_MEMORY,
		       N_("cannot allocate TPM event buffer"));

  event->Header.HeaderSize = sizeof (EFI_TCG2_EVENT_HEADER);
  event->Header.HeaderVersion = 1;
  event->Header.PCRIndex = pcr;
  event->Header.EventType = EV_IPL;
  event->Size =
    sizeof (*event) - sizeof (event->Event) + grub_strlen (description) + 1;
  grub_memcpy (event->Event, description, grub_strlen (description) + 1);

  status = efi_call_5 (tpm->hash_log_extend_event, tpm, 0, (grub_addr_t) buf,
		       (grub_uint64_t) size, event);
  grub_free (event);

  return grub_efi_log_event_status (status);
}

grub_err_t
grub_tpm_measure (unsigned char *buf, grub_size_t size, grub_uint8_t pcr,
		    const char *description)
{
  grub_efi_handle_t tpm_handle;
  grub_efi_uint8_t protocol_version;

  if (!grub_tpm_handle_find (&tpm_handle, &protocol_version))
    return 0;

  grub_dprintf ("tpm", "log_event, pcr = %d, size = 0x%" PRIxGRUB_SIZE ", %s\n",
                pcr, size, description);

  if (protocol_version == 1)
    return grub_tpm1_log_event (tpm_handle, buf, size, pcr, description);
  else
    return grub_tpm2_log_event (tpm_handle, buf, size, pcr, description);
}
