#define MODE_MDA 1
#include "vga_text.c"

GRUB_MOD_INIT(mda_text)
{
  grub_term_register_output ("mda_text", &grub_vga_text_term);
}

GRUB_MOD_FINI(mda_text)
{
  grub_term_unregister_output (&grub_vga_text_term);
}

