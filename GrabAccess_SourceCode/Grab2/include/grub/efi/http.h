/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2008  Free Software Foundation, Inc.
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

#ifndef GRUB_EFI_HTTP_HEADER
#define GRUB_EFI_HTTP_HEADER	1

#include <grub/symbol.h>
#include <grub/net.h>
#include <grub/efi/api.h>

#define GRUB_EFI_HTTP_SERVICE_BINDING_PROTOCOL_GUID \
  { 0xbdc8e6af, 0xd9bc, 0x4379, \
      { 0xa7, 0x2a, 0xe0, 0xc4, 0xe7, 0x5d, 0xae, 0x1c } \
  }

#define GRUB_EFI_HTTP_PROTOCOL_GUID \
  { 0x7A59B29B, 0x910B, 0x4171, \
      { 0x82, 0x42, 0xA8, 0x5A, 0x0D, 0xF2, 0x5B, 0x5B } \
  }

#define EFIHTTP_WAIT_TIME 10000 // 10000ms = 10s
#define EFIHTTP_RX_BUF_LEN 10240

//******************************************
// Protocol Interface Structure
//******************************************
struct grub_efi_http;

//******************************************
// EFI_HTTP_VERSION
//******************************************
typedef enum {
  GRUB_EFI_HTTPVERSION10,
  GRUB_EFI_HTTPVERSION11,
  GRUB_EFI_HTTPVERSIONUNSUPPORTED
} grub_efi_http_version_t;

//******************************************
// EFI_HTTPv4_ACCESS_POINT
//******************************************
typedef struct {
  grub_efi_boolean_t use_default_address;
  grub_efi_ipv4_address_t local_address;
  grub_efi_ipv4_address_t local_subnet;
  grub_efi_uint16_t local_port;
} grub_efi_httpv4_access_point_t;

//******************************************
// EFI_HTTPv6_ACCESS_POINT
//******************************************
typedef struct {
  grub_efi_ipv6_address_t local_address;
  grub_efi_uint16_t local_port;
} grub_efi_httpv6_access_point_t;

//******************************************
// EFI_HTTP_CONFIG_DATA
//******************************************
typedef struct {
  grub_efi_http_version_t http_version;
  grub_efi_uint32_t timeout_millisec;
  grub_efi_boolean_t local_address_is_ipv6;
  union {
    grub_efi_httpv4_access_point_t *ipv4_node;
    grub_efi_httpv6_access_point_t *ipv6_node;
  } access_point;
} grub_efi_http_config_data_t;

//******************************************
// EFI_HTTP_METHOD
//******************************************
typedef enum {
  GRUB_EFI_HTTPMETHODGET,
  GRUB_EFI_HTTPMETHODPOST,
  GRUB_EFI_HTTPMETHODPATCH,
  GRUB_EFI_HTTPMETHODOPTIONS,
  GRUB_EFI_HTTPMETHODCONNECT,
  GRUB_EFI_HTTPMETHODHEAD,
  GRUB_EFI_HTTPMETHODPUT,
  GRUB_EFI_HTTPMETHODDELETE,
  GRUB_EFI_HTTPMETHODTRACE,
} grub_efi_http_method_t;

//******************************************
// EFI_HTTP_REQUEST_DATA
//******************************************
typedef struct {
  grub_efi_http_method_t method;
  grub_efi_char16_t *url;
} grub_efi_http_request_data_t;

typedef enum {
  GRUB_EFI_HTTP_STATUS_UNSUPPORTED_STATUS = 0,
  GRUB_EFI_HTTP_STATUS_100_CONTINUE,
  GRUB_EFI_HTTP_STATUS_101_SWITCHING_PROTOCOLS,
  GRUB_EFI_HTTP_STATUS_200_OK,
  GRUB_EFI_HTTP_STATUS_201_CREATED,
  GRUB_EFI_HTTP_STATUS_202_ACCEPTED,
  GRUB_EFI_HTTP_STATUS_203_NON_AUTHORITATIVE_INFORMATION,
  GRUB_EFI_HTTP_STATUS_204_NO_CONTENT,
  GRUB_EFI_HTTP_STATUS_205_RESET_CONTENT,
  GRUB_EFI_HTTP_STATUS_206_PARTIAL_CONTENT,
  GRUB_EFI_HTTP_STATUS_300_MULTIPLE_CHIOCES,
  GRUB_EFI_HTTP_STATUS_301_MOVED_PERMANENTLY,
  GRUB_EFI_HTTP_STATUS_302_FOUND,
  GRUB_EFI_HTTP_STATUS_303_SEE_OTHER,
  GRUB_EFI_HTTP_STATUS_304_NOT_MODIFIED,
  GRUB_EFI_HTTP_STATUS_305_USE_PROXY,
  GRUB_EFI_HTTP_STATUS_307_TEMPORARY_REDIRECT,
  GRUB_EFI_HTTP_STATUS_400_BAD_REQUEST,
  GRUB_EFI_HTTP_STATUS_401_UNAUTHORIZED,
  GRUB_EFI_HTTP_STATUS_402_PAYMENT_REQUIRED,
  GRUB_EFI_HTTP_STATUS_403_FORBIDDEN,
  GRUB_EFI_HTTP_STATUS_404_NOT_FOUND,
  GRUB_EFI_HTTP_STATUS_405_METHOD_NOT_ALLOWED,
  GRUB_EFI_HTTP_STATUS_406_NOT_ACCEPTABLE,
  GRUB_EFI_HTTP_STATUS_407_PROXY_AUTHENTICATION_REQUIRED,
  GRUB_EFI_HTTP_STATUS_408_REQUEST_TIME_OUT,
  GRUB_EFI_HTTP_STATUS_409_CONFLICT,
  GRUB_EFI_HTTP_STATUS_410_GONE,
  GRUB_EFI_HTTP_STATUS_411_LENGTH_REQUIRED,
  GRUB_EFI_HTTP_STATUS_412_PRECONDITION_FAILED,
  GRUB_EFI_HTTP_STATUS_413_REQUEST_ENTITY_TOO_LARGE,
  GRUB_EFI_HTTP_STATUS_414_REQUEST_URI_TOO_LARGE,
  GRUB_EFI_HTTP_STATUS_415_UNSUPPORTED_MEDIA_TYPE,
  GRUB_EFI_HTTP_STATUS_416_REQUESTED_RANGE_NOT_SATISFIED,
  GRUB_EFI_HTTP_STATUS_417_EXPECTATION_FAILED,
  GRUB_EFI_HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
  GRUB_EFI_HTTP_STATUS_501_NOT_IMPLEMENTED,
  GRUB_EFI_HTTP_STATUS_502_BAD_GATEWAY,
  GRUB_EFI_HTTP_STATUS_503_SERVICE_UNAVAILABLE,
  GRUB_EFI_HTTP_STATUS_504_GATEWAY_TIME_OUT,
  GRUB_EFI_HTTP_STATUS_505_HTTP_VERSION_NOT_SUPPORTED
} grub_efi_http_status_code_t;

//******************************************
// EFI_HTTP_RESPONSE_DATA
//******************************************
typedef struct {
  grub_efi_http_status_code_t status_code;
} grub_efi_http_response_data_t;

//******************************************
// EFI_HTTP_HEADER
//******************************************
typedef struct {
  grub_efi_char8_t *field_name;
  grub_efi_char8_t *field_value;
} grub_efi_http_header_t;

//******************************************
// EFI_HTTP_MESSAGE
//******************************************
typedef struct {
  union {
    grub_efi_http_request_data_t *request;
    grub_efi_http_response_data_t *response;
  } data;
  grub_efi_uint32_t header_count;
  grub_efi_http_header_t *headers;
  grub_efi_uint32_t body_length;
  void *body;
} grub_efi_http_message_t;

//******************************************
// EFI_HTTP_TOKEN
//******************************************
typedef struct {
  grub_efi_event_t event;
  grub_efi_status_t status;
  grub_efi_http_message_t *message;
} grub_efi_http_token_t;

struct grub_efi_http {
  grub_efi_status_t
  (*get_mode_data) (struct grub_efi_http *this,
                    grub_efi_http_config_data_t *http_config_data);

  grub_efi_status_t
  (*configure) (struct grub_efi_http *this,
                grub_efi_http_config_data_t *http_config_data);

  grub_efi_status_t
  (*request) (struct grub_efi_http *this,
              grub_efi_http_token_t *token);

  grub_efi_status_t
  (*cancel) (struct grub_efi_http *this,
             grub_efi_http_token_t *token);

  grub_efi_status_t
  (*response) (struct grub_efi_http *this,
               grub_efi_http_token_t *token);

  grub_efi_status_t
  (*poll) (struct grub_efi_http *this);
};
typedef struct grub_efi_http grub_efi_http_t;

#endif /* !GRUB_EFI_HTTP_HEADER */
