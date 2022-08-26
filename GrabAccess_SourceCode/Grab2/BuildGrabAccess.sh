#!/bin/sh
./bootstrap
./configure --target=x86_64 --with-platform=efi
make clean
make -j4
./grub-mkimage -d ./grub-core -o bootx64.efi -O x86_64-efi -c ./x86_64-efi.cfg -p /EFI/boot part_gpt part_msdos disk fat exfat ext2 ntfs xfs appleldr hfs normal search_fs_file configfile linux linux16 chain loopback echo efi_gop efi_uga file halt reboot ls true wpbt
