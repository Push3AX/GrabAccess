#ifndef GRUB_STARTUP_CPU_HEADER
#define GRUB_STARTUP_CPU_HEADER

struct grub_arm_startup_registers
{
  /* registers 0-11 */
  /* for U-boot r[1] is machine type */
  /* for U-boot r[2] is boot data */
  grub_uint32_t r[12];
  grub_uint32_t sp;
  grub_uint32_t lr;
};

extern struct grub_arm_startup_registers grub_arm_saved_registers;

#endif
