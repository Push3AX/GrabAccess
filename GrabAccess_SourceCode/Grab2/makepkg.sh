#!/bin/sh
mkdir -p PKG/lib/grub/fonts
cp -r makepkg/* PKG/lib/grub/
cp PKG/share/grub/*.pf2 PKG/lib/grub/fonts/
cp PKG/bin/*.exe PKG/lib/grub/
cp PKG/sbin/*.exe PKG/lib/grub/
cp PKG/bin/grub-mkimage PKG/lib/grub/
cd PKG/lib/

for x in $(ls grub/locale/*.po)
do
    po=$(echo "$x" | sed 's/\.[^.]*$//')
    msgfmt "$x" -o "$po.mo"
done

rm -rf grub/i386-none
rm -f grub/locale/*.po
rm -f grub/*/*.module
rm -f grub/*/*.image
rm -f grub/*/*.exec
rm -f grub/*/config.h

tar -zcvf grub.tar.gz grub
mv grub.tar.gz ../../grub2-latest.tar.gz
