#ifndef GRUB_EFI_DHCP_HEADER
#define GRUB_EFI_DHCP_HEADER	1

#define GRUB_EFI_DHCP4_SERVICE_BINDING_PROTOCOL_GUID \
  { 0x9d9a39d8, 0xbd42, 0x4a73, \
    { 0xa4, 0xd5, 0x8e, 0xe9, 0x4b, 0xe1, 0x13, 0x80 } \
  }

#define GRUB_EFI_DHCP4_PROTOCOL_GUID \
  { 0x8a219718, 0x4ef5, 0x4761, \
    { 0x91, 0xc8, 0xc0, 0xf0, 0x4b, 0xda, 0x9e, 0x56 } \
  }

#define GRUB_EFI_DHCP6_SERVICE_BINDING_PROTOCOL_GUID \
  { 0x9fb9a8a1, 0x2f4a, 0x43a6, \
    { 0x88, 0x9c, 0xd0, 0xf7, 0xb6, 0xc4 ,0x7a, 0xd5 } \
  }

#define GRUB_EFI_DHCP6_PROTOCOL_GUID \
  { 0x87c8bad7, 0x595, 0x4053, \
    { 0x82, 0x97, 0xde, 0xde, 0x39, 0x5f, 0x5d, 0x5b } \
  }

typedef struct grub_efi_dhcp4_protocol grub_efi_dhcp4_protocol_t;

enum grub_efi_dhcp4_state {
  GRUB_EFI_DHCP4_STOPPED,
  GRUB_EFI_DHCP4_INIT,
  GRUB_EFI_DHCP4_SELECTING,
  GRUB_EFI_DHCP4_REQUESTING,
  GRUB_EFI_DHCP4_BOUND,
  GRUB_EFI_DHCP4_RENEWING,
  GRUB_EFI_DHCP4_REBINDING,
  GRUB_EFI_DHCP4_INIT_REBOOT,
  GRUB_EFI_DHCP4_REBOOTING
};

typedef enum grub_efi_dhcp4_state grub_efi_dhcp4_state_t;

struct grub_efi_dhcp4_header {
  grub_efi_uint8_t op_code;
  grub_efi_uint8_t hw_type;
  grub_efi_uint8_t hw_addr_len;
  grub_efi_uint8_t hops;
  grub_efi_uint32_t xid;
  grub_efi_uint16_t seconds;
  grub_efi_uint16_t reserved;
  grub_efi_ipv4_address_t client_addr;
  grub_efi_ipv4_address_t your_addr;
  grub_efi_ipv4_address_t server_addr;
  grub_efi_ipv4_address_t gateway_addr;
  grub_efi_uint8_t client_hw_addr[16];
  grub_efi_char8_t server_name[64];
  grub_efi_char8_t boot_file_name[128];
} GRUB_PACKED;

typedef struct grub_efi_dhcp4_header grub_efi_dhcp4_header_t;

struct grub_efi_dhcp4_packet {
  grub_efi_uint32_t size;
  grub_efi_uint32_t length;
  struct {
    grub_efi_dhcp4_header_t header;
    grub_efi_uint32_t magik;
    grub_efi_uint8_t option[1];
  } dhcp4;
} GRUB_PACKED;

typedef struct grub_efi_dhcp4_packet grub_efi_dhcp4_packet_t;

struct grub_efi_dhcp4_listen_point {
  grub_efi_ipv4_address_t listen_address;
  grub_efi_ipv4_address_t subnet_mask;
  grub_efi_uint16_t listen_port;
};

typedef struct grub_efi_dhcp4_listen_point grub_efi_dhcp4_listen_point_t;

struct grub_efi_dhcp4_transmit_receive_token {
  grub_efi_status_t status;
  grub_efi_event_t completion_event;
  grub_efi_ipv4_address_t remote_address;
  grub_efi_uint16_t remote_port;
  grub_efi_ipv4_address_t gateway_address;
  grub_efi_uint32_t listen_point_count;
  grub_efi_dhcp4_listen_point_t *listen_points;
  grub_efi_uint32_t timeout_value;
  grub_efi_dhcp4_packet_t *packet;
  grub_efi_uint32_t response_count;
  grub_efi_dhcp4_packet_t *response_list;
};

typedef struct grub_efi_dhcp4_transmit_receive_token grub_efi_dhcp4_transmit_receive_token_t;

enum grub_efi_dhcp4_event {
  GRUB_EFI_DHCP4_SEND_DISCOVER = 0X01,
  GRUB_EFI_DHCP4_RCVD_OFFER,
  GRUB_EFI_DHCP4_SELECT_OFFER,
  GRUB_EFI_DHCP4_SEND_REQUEST,
  GRUB_EFI_DHCP4_RCVD_ACK,
  GRUB_EFI_DHCP4_RCVD_NAK,
  GRUB_EFI_DHCP4_SEND_DECLINE,
  GRUB_EFI_DHCP4_BOUND_COMPLETED,
  GRUB_EFI_DHCP4_ENTER_RENEWING,
  GRUB_EFI_DHCP4_ENTER_REBINDING,
  GRUB_EFI_DHCP4_ADDRESS_LOST,
  GRUB_EFI_DHCP4_FAIL
};

typedef enum grub_efi_dhcp4_event grub_efi_dhcp4_event_t;

struct grub_efi_dhcp4_packet_option {
  grub_efi_uint8_t op_code;
  grub_efi_uint8_t length;
  grub_efi_uint8_t data[1];
} GRUB_PACKED;

typedef struct grub_efi_dhcp4_packet_option grub_efi_dhcp4_packet_option_t;

struct grub_efi_dhcp4_config_data {
  grub_efi_uint32_t discover_try_count;
  grub_efi_uint32_t *discover_timeout;
  grub_efi_uint32_t request_try_count;
  grub_efi_uint32_t *request_timeout;
  grub_efi_ipv4_address_t client_address;
  grub_efi_status_t (*dhcp4_callback) (
    grub_efi_dhcp4_protocol_t *this,
    void *context,
    grub_efi_dhcp4_state_t current_state,
    grub_efi_dhcp4_event_t dhcp4_event,
    grub_efi_dhcp4_packet_t *packet,
    grub_efi_dhcp4_packet_t **new_packet
  );
  void *callback_context;
  grub_efi_uint32_t option_count;
  grub_efi_dhcp4_packet_option_t **option_list;
};

typedef struct grub_efi_dhcp4_config_data grub_efi_dhcp4_config_data_t;

struct grub_efi_dhcp4_mode_data {
  grub_efi_dhcp4_state_t state;
  grub_efi_dhcp4_config_data_t config_data;
  grub_efi_ipv4_address_t client_address;
  grub_efi_mac_address_t client_mac_address;
  grub_efi_ipv4_address_t server_address;
  grub_efi_ipv4_address_t router_address;
  grub_efi_ipv4_address_t subnet_mask;
  grub_efi_uint32_t lease_time;
  grub_efi_dhcp4_packet_t *reply_packet;
};

typedef struct grub_efi_dhcp4_mode_data grub_efi_dhcp4_mode_data_t;

struct grub_efi_dhcp4_protocol {
  grub_efi_status_t (*get_mode_data) (grub_efi_dhcp4_protocol_t *this,
	      grub_efi_dhcp4_mode_data_t *dhcp4_mode_data);
  grub_efi_status_t (*configure) (grub_efi_dhcp4_protocol_t *this,
	      grub_efi_dhcp4_config_data_t *dhcp4_cfg_data);
  grub_efi_status_t (*start) (grub_efi_dhcp4_protocol_t *this,
	      grub_efi_event_t completion_event);
  grub_efi_status_t (*renew_rebind) (grub_efi_dhcp4_protocol_t *this,
	      grub_efi_boolean_t rebind_request,
	      grub_efi_event_t completion_event);
  grub_efi_status_t (*release) (grub_efi_dhcp4_protocol_t *this);
  grub_efi_status_t (*stop) (grub_efi_dhcp4_protocol_t *this);
  grub_efi_status_t (*build) (grub_efi_dhcp4_protocol_t *this,
	      grub_efi_dhcp4_packet_t *seed_packet,
	      grub_efi_uint32_t delete_count,
	      grub_efi_uint8_t *delete_list,
	      grub_efi_uint32_t append_count,
	      grub_efi_dhcp4_packet_option_t *append_list[],
	      grub_efi_dhcp4_packet_t **new_packet);
  grub_efi_status_t (*transmit_receive) (grub_efi_dhcp4_protocol_t *this,
	      grub_efi_dhcp4_transmit_receive_token_t *token);
  grub_efi_status_t (*parse) (grub_efi_dhcp4_protocol_t *this,
	      grub_efi_dhcp4_packet_t *packet,
	      grub_efi_uint32_t *option_count,
	      grub_efi_dhcp4_packet_option_t *packet_option_list[]);
};

typedef struct grub_efi_dhcp6_protocol grub_efi_dhcp6_protocol_t;

struct grub_efi_dhcp6_retransmission {
  grub_efi_uint32_t irt;
  grub_efi_uint32_t mrc;
  grub_efi_uint32_t mrt;
  grub_efi_uint32_t mrd;
};

typedef struct grub_efi_dhcp6_retransmission grub_efi_dhcp6_retransmission_t;

enum grub_efi_dhcp6_event {
  GRUB_EFI_DHCP6_SEND_SOLICIT,
  GRUB_EFI_DHCP6_RCVD_ADVERTISE,
  GRUB_EFI_DHCP6_SELECT_ADVERTISE,
  GRUB_EFI_DHCP6_SEND_REQUEST,
  GRUB_EFI_DHCP6_RCVD_REPLY,
  GRUB_EFI_DHCP6_RCVD_RECONFIGURE,
  GRUB_EFI_DHCP6_SEND_DECLINE,
  GRUB_EFI_DHCP6_SEND_CONFIRM,
  GRUB_EFI_DHCP6_SEND_RELEASE,
  GRUB_EFI_DHCP6_SEND_RENEW,
  GRUB_EFI_DHCP6_SEND_REBIND
};

typedef enum grub_efi_dhcp6_event grub_efi_dhcp6_event_t;

struct grub_efi_dhcp6_packet_option {
  grub_efi_uint16_t op_code;
  grub_efi_uint16_t op_len;
  grub_efi_uint8_t data[1];
} GRUB_PACKED;

typedef struct grub_efi_dhcp6_packet_option grub_efi_dhcp6_packet_option_t;

struct grub_efi_dhcp6_header {
  grub_efi_uint32_t transaction_id:24;
  grub_efi_uint32_t message_type:8;
} GRUB_PACKED;

typedef struct grub_efi_dhcp6_header grub_efi_dhcp6_header_t;

struct grub_efi_dhcp6_packet {
  grub_efi_uint32_t size;
  grub_efi_uint32_t length;
  struct {
    grub_efi_dhcp6_header_t header;
    grub_efi_uint8_t option[1];
  } dhcp6;
} GRUB_PACKED;

typedef struct grub_efi_dhcp6_packet grub_efi_dhcp6_packet_t;

struct grub_efi_dhcp6_ia_address {
  grub_efi_ipv6_address_t ip_address;
  grub_efi_uint32_t preferred_lifetime;
  grub_efi_uint32_t valid_lifetime;
};

typedef struct grub_efi_dhcp6_ia_address grub_efi_dhcp6_ia_address_t;

enum grub_efi_dhcp6_state {
  GRUB_EFI_DHCP6_INIT,
  GRUB_EFI_DHCP6_SELECTING,
  GRUB_EFI_DHCP6_REQUESTING,
  GRUB_EFI_DHCP6_DECLINING,
  GRUB_EFI_DHCP6_CONFIRMING,
  GRUB_EFI_DHCP6_RELEASING,
  GRUB_EFI_DHCP6_BOUND,
  GRUB_EFI_DHCP6_RENEWING,
  GRUB_EFI_DHCP6_REBINDING
};

typedef enum grub_efi_dhcp6_state grub_efi_dhcp6_state_t;

#define GRUB_EFI_DHCP6_IA_TYPE_NA 3
#define GRUB_EFI_DHCP6_IA_TYPE_TA 4

struct grub_efi_dhcp6_ia_descriptor {
  grub_efi_uint16_t type;
  grub_efi_uint32_t ia_id;
};

typedef struct grub_efi_dhcp6_ia_descriptor grub_efi_dhcp6_ia_descriptor_t;

struct grub_efi_dhcp6_ia {
  grub_efi_dhcp6_ia_descriptor_t descriptor;
  grub_efi_dhcp6_state_t state;
  grub_efi_dhcp6_packet_t *reply_packet;
  grub_efi_uint32_t ia_address_count;
  grub_efi_dhcp6_ia_address_t ia_address[1];
};

typedef struct grub_efi_dhcp6_ia grub_efi_dhcp6_ia_t;

struct grub_efi_dhcp6_duid {
  grub_efi_uint16_t length;
  grub_efi_uint8_t duid[1];
};

typedef struct grub_efi_dhcp6_duid grub_efi_dhcp6_duid_t;

struct grub_efi_dhcp6_mode_data {
  grub_efi_dhcp6_duid_t *client_id;
  grub_efi_dhcp6_ia_t *ia;
};

typedef struct grub_efi_dhcp6_mode_data grub_efi_dhcp6_mode_data_t;

struct grub_efi_dhcp6_config_data {
  grub_efi_status_t (*dhcp6_callback) (grub_efi_dhcp6_protocol_t this,
		void *context,
		grub_efi_dhcp6_state_t current_state,
		grub_efi_dhcp6_event_t dhcp6_event,
		grub_efi_dhcp6_packet_t *packet,
		grub_efi_dhcp6_packet_t **new_packet);
  void *callback_context;
  grub_efi_uint32_t option_count;
  grub_efi_dhcp6_packet_option_t **option_list;
  grub_efi_dhcp6_ia_descriptor_t ia_descriptor;
  grub_efi_event_t ia_info_event;
  grub_efi_boolean_t reconfigure_accept;
  grub_efi_boolean_t rapid_commit;
  grub_efi_dhcp6_retransmission_t *solicit_retransmission;
};

typedef struct grub_efi_dhcp6_config_data grub_efi_dhcp6_config_data_t;

struct grub_efi_dhcp6_protocol {
  grub_efi_status_t (*get_mode_data) (grub_efi_dhcp6_protocol_t *this,
	    grub_efi_dhcp6_mode_data_t *dhcp6_mode_data,
	    grub_efi_dhcp6_config_data_t *dhcp6_config_data);
  grub_efi_status_t (*configure) (grub_efi_dhcp6_protocol_t *this,
	    grub_efi_dhcp6_config_data_t *dhcp6_cfg_data);
  grub_efi_status_t (*start) (grub_efi_dhcp6_protocol_t *this);
  grub_efi_status_t (*info_request) (grub_efi_dhcp6_protocol_t *this,
	    grub_efi_boolean_t send_client_id,
	    grub_efi_dhcp6_packet_option_t *option_request,
	    grub_efi_uint32_t option_count,
	    grub_efi_dhcp6_packet_option_t *option_list[],
	    grub_efi_dhcp6_retransmission_t *retransmission,
	    grub_efi_event_t timeout_event,
	    grub_efi_status_t (*reply_callback) (grub_efi_dhcp6_protocol_t *this,
		    void *context,
		    grub_efi_dhcp6_packet_t *packet),
	    void *callback_context);
  grub_efi_status_t (*renew_rebind) (grub_efi_dhcp6_protocol_t *this,
	    grub_efi_boolean_t rebind_request);
  grub_efi_status_t (*decline) (grub_efi_dhcp6_protocol_t *this,
	    grub_efi_uint32_t address_count,
	    grub_efi_ipv6_address_t *addresses);
  grub_efi_status_t (*release) (grub_efi_dhcp6_protocol_t *this,
	    grub_efi_uint32_t address_count,
	    grub_efi_ipv6_address_t *addresses);
  grub_efi_status_t (*stop) (grub_efi_dhcp6_protocol_t *this);
  grub_efi_status_t (*parse) (grub_efi_dhcp6_protocol_t *this,
	    grub_efi_dhcp6_packet_t *packet,
	    grub_efi_uint32_t *option_count,
	    grub_efi_dhcp6_packet_option_t *packet_option_list[]);
};

#endif /* ! GRUB_EFI_DHCP_HEADER */
