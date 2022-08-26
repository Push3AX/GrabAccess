#ifndef GRUB_ARM_CROS_EC_H
#define GRUB_ARM_CROS_EC_H 1

#include <grub/types.h>
#include <grub/fdtbus.h>

#define GRUB_CROS_EC_KEYSCAN_COLS 13
#define GRUB_CROS_EC_KEYSCAN_ROWS 8

struct grub_cros_ec_keyscan {
  grub_uint8_t data[GRUB_CROS_EC_KEYSCAN_COLS];
};

int
grub_cros_ec_scan_keyboard (const struct grub_fdtbus_dev *dev,
			    struct grub_cros_ec_keyscan *scan);

int
grub_cros_ec_validate (const struct grub_fdtbus_dev *dev);

#endif
