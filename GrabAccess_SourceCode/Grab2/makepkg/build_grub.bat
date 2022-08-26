@echo off
cd /d "%~dp0"

echo i386-efi
set /p modules= < arch\ia32\builtin.txt
grub-mkimage.exe -d i386-efi -p /boot/grub -o grubia32.efi -O i386-efi %modules%

echo x86_64-efi
set /p modules= < arch\x64\builtin.txt
grub-mkimage.exe -d x86_64-efi -p /boot/grub -o grubx64.efi -O x86_64-efi %modules%

echo i386-pc
set /p modules= < arch\legacy\builtin.txt
grub-mkimage.exe -d i386-pc -p /boot/grub -o core.img -O i386-pc %modules%
