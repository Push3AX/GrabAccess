#!/usr/bin/env sh

echo "i386-efi"
modules=$(cat arch/ia32/builtin.txt)
grub-mkimage -d i386-efi -p "/boot/grub" -o grubia32.efi -O i386-efi $modules

echo "x86_64-efi"
modules=$(cat arch/x64/builtin.txt)
grub-mkimage -d x86_64-efi -p "/boot/grub" -o grubx64.efi -O x86_64-efi $modules

echo "i386-pc"
modules=$(cat arch/legacy/builtin.txt) 
grub-mkimage -d i386-pc -p "/boot/grub" -o core.img -O i386-pc $modules
