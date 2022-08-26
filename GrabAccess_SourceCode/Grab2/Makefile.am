AUTOMAKE_OPTIONS = subdir-objects -Wno-portability

DEPDIR = .deps-util
SUBDIRS = grub-core/lib/gnulib .
if COND_real_platform
SUBDIRS += grub-core
endif
SUBDIRS += po docs util/bash-completion.d

include $(top_srcdir)/conf/Makefile.common
include $(top_srcdir)/conf/Makefile.extra-dist

AM_CFLAGS = $(HOST_CFLAGS)
AM_LDFLAGS = $(HOST_LDFLAGS)
AM_CPPFLAGS = $(HOST_CPPFLAGS) $(CPPFLAGS_DEFAULT)
AM_CCASFLAGS = $(HOST_CCASFLAGS) $(CCASFLAGS_DEFAULT)

ACLOCAL_AMFLAGS = -I m4

CFLAGS_PROGRAM += $(CFLAGS_GNULIB)
LDFLAGS_PROGRAM += $(LDFLAGS_GNULIB)
CPPFLAGS_PROGRAM += $(CPPFLAGS_GNULIB)
CCASFLAGS_PROGRAM += $(CCASFLAGS_GNULIB)

include $(srcdir)/Makefile.util.am

check_SCRIPTS = $(check_SCRIPTS_native) $(check_SCRIPTS_nonnative)
check_PROGRAMS = $(check_PROGRAMS_native) $(check_PROGRAMS_nonnative)
TESTS = $(check_SCRIPTS) $(check_PROGRAMS)

check-native:
	$(MAKE) TESTS="$(check_PROGRAMS_native) $(check_SCRIPTS_native)" check
check-nonnative:
	$(MAKE) TESTS="$(check_PROGRAMS_nonnative) $(check_SCRIPTS_nonnative)" check

# XXX Use Automake's LEX & YACC support
grub_script.tab.h: $(top_srcdir)/grub-core/script/parser.y
	$(YACC) -d -p grub_script_yy -b grub_script $(top_srcdir)/grub-core/script/parser.y
grub_script.tab.c: grub_script.tab.h
CLEANFILES += grub_script.tab.c grub_script.tab.h

# For the lexer.
grub_script.yy.h: $(top_srcdir)/grub-core/script/yylex.l
	$(LEX) -o grub_script.yy.c --header-file=grub_script.yy.h $(top_srcdir)/grub-core/script/yylex.l
grub_script.yy.c: grub_script.yy.h
CLEANFILES += grub_script.yy.c grub_script.yy.h

# For libgrub.a
libgrub.pp: config-util.h grub_script.tab.h grub_script.yy.h $(libgrubmods_a_SOURCES) $(libgrubkern_a_SOURCES)
	$(CPP) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(libgrubmods_a_CPPFLAGS) $(libgrubkern_a_CPPFLAGS) $(CPPFLAGS) \
	  -D'GRUB_MOD_INIT(x)=@MARKER@x@' $^ > $@ || (rm -f $@; exit 1)
CLEANFILES += libgrub.pp

libgrub_a_init.lst: libgrub.pp
	cat $< | grep '^@MARKER@' | sed 's/@MARKER@\(.*\)@/\1/g' | sort -u > $@ || (rm -f $@; exit 1)
CLEANFILES += libgrub_a_init.lst

libgrub_a_init.c: libgrub_a_init.lst $(top_srcdir)/geninit.sh
	sh $(top_srcdir)/geninit.sh `cat $<` > $@ || (rm -f $@; exit 1)
CLEANFILES += libgrub_a_init.c

# For grub-fstest
grub_fstest.pp: $(grub_fstest_SOURCES)
	$(CPP) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(grub_fstest_CPPFLAGS) $(CPPFLAGS) \
	  -D'GRUB_MOD_INIT(x)=@MARKER@x@' $^ > $@ || (rm -f $@; exit 1)
CLEANFILES += grub_fstest.pp

grub_fstest_init.lst: libgrub.pp grub_fstest.pp
	cat $^ | grep '^@MARKER@' | sed 's/@MARKER@\(.*\)@/\1/g' | sort -u > $@ || (rm -f $@; exit 1)
CLEANFILES += grub_fstest_init.lst

grub_fstest_init.c: grub_fstest_init.lst $(top_srcdir)/geninit.sh
	sh $(top_srcdir)/geninit.sh `cat $<` > $@ || (rm -f $@; exit 1)
CLEANFILES += grub_fstest_init.c

if COND_HAVE_FONT_SOURCE
pkgdata_DATA += unicode.pf2 ascii.pf2 euro.pf2 ascii.h widthspec.h
endif

starfield_theme_files = $(srcdir)/themes/starfield/blob_w.png $(srcdir)/themes/starfield/boot_menu_c.png $(srcdir)/themes/starfield/boot_menu_e.png $(srcdir)/themes/starfield/boot_menu_ne.png $(srcdir)/themes/starfield/boot_menu_n.png $(srcdir)/themes/starfield/boot_menu_nw.png $(srcdir)/themes/starfield/boot_menu_se.png $(srcdir)/themes/starfield/boot_menu_s.png $(srcdir)/themes/starfield/boot_menu_sw.png $(srcdir)/themes/starfield/boot_menu_w.png $(srcdir)/themes/starfield/slider_c.png $(srcdir)/themes/starfield/slider_n.png $(srcdir)/themes/starfield/slider_s.png $(srcdir)/themes/starfield/starfield.png $(srcdir)/themes/starfield/terminal_box_c.png $(srcdir)/themes/starfield/terminal_box_e.png $(srcdir)/themes/starfield/terminal_box_ne.png $(srcdir)/themes/starfield/terminal_box_n.png $(srcdir)/themes/starfield/terminal_box_nw.png $(srcdir)/themes/starfield/terminal_box_se.png $(srcdir)/themes/starfield/terminal_box_s.png $(srcdir)/themes/starfield/terminal_box_sw.png $(srcdir)/themes/starfield/terminal_box_w.png $(srcdir)/themes/starfield/theme.txt $(srcdir)/themes/starfield/README $(srcdir)/themes/starfield/COPYING.CC-BY-SA-3.0

build-grub-mkfont$(BUILD_EXEEXT): util/grub-mkfont.c grub-core/unidata.c grub-core/kern/emu/misc.c util/misc.c
	$(BUILD_CC) -o $@ -I$(top_srcdir)/include $(BUILD_CFLAGS) $(BUILD_CPPFLAGS) $(BUILD_LDFLAGS) -DGRUB_MKFONT=1 -DGRUB_BUILD=1 -DGRUB_UTIL=1 -DGRUB_BUILD_PROGRAM_NAME=\"build-grub-mkfont\" $^ $(BUILD_FREETYPE_CFLAGS) $(BUILD_FREETYPE_LIBS)
CLEANFILES += build-grub-mkfont$(BUILD_EXEEXT)

garbage-gen$(BUILD_EXEEXT): util/garbage-gen.c
	$(BUILD_CC) -o $@ $(BUILD_CFLAGS) $(BUILD_CPPFLAGS) $(BUILD_LDFLAGS)  $^
CLEANFILES += garbage-gen$(BUILD_EXEEXT)
EXTRA_DIST += util/garbage-gen.c

build-grub-gen-asciih$(BUILD_EXEEXT): util/grub-gen-asciih.c
	$(BUILD_CC) -o $@ -I$(top_srcdir)/include $(BUILD_CFLAGS) $(BUILD_CPPFLAGS) $(BUILD_LDFLAGS) -DGRUB_MKFONT=1 -DGRUB_BUILD=1 -DGRUB_UTIL=1 $^ $(BUILD_FREETYPE_CFLAGS) $(BUILD_FREETYPE_LIBS) -Wall -Werror
CLEANFILES += build-grub-gen-asciih$(BUILD_EXEEXT)

build-grub-gen-widthspec$(BUILD_EXEEXT): util/grub-gen-widthspec.c
	$(BUILD_CC) -o $@ -I$(top_srcdir)/include $(BUILD_CFLAGS) $(BUILD_CPPFLAGS) $(BUILD_LDFLAGS) -DGRUB_MKFONT=1 -DGRUB_BUILD=1 -DGRUB_UTIL=1 $^ $(BUILD_FREETYPE_CFLAGS) $(BUILD_FREETYPE_LIBS) -Wall -Werror
CLEANFILES += build-grub-gen-widthspec$(BUILD_EXEEXT)

if COND_STARFIELD
starfield_DATA = dejavu_10.pf2 dejavu_12.pf2 dejavu_bold_14.pf2 dejavu_14.pf2 dejavu_16.pf2 $(starfield_theme_files)
dejavu_10.pf2: $(DJVU_FONT_SOURCE) build-grub-mkfont$(BUILD_EXEEXT)
	./build-grub-mkfont$(BUILD_EXEEXT) -s 10 -o $@ $(DJVU_FONT_SOURCE)
CLEANFILES += dejavu_10.pf2
dejavu_12.pf2: $(DJVU_FONT_SOURCE) build-grub-mkfont$(BUILD_EXEEXT)
	./build-grub-mkfont$(BUILD_EXEEXT) -s 12 -o $@ $(DJVU_FONT_SOURCE)
CLEANFILES += dejavu_12.pf2
dejavu_14.pf2: $(DJVU_FONT_SOURCE) build-grub-mkfont$(BUILD_EXEEXT)
	./build-grub-mkfont$(BUILD_EXEEXT) -s 14 -o $@ $(DJVU_FONT_SOURCE)
CLEANFILES += dejavu_14.pf2
dejavu_bold_14.pf2: $(DJVU_FONT_SOURCE) build-grub-mkfont$(BUILD_EXEEXT)
	./build-grub-mkfont$(BUILD_EXEEXT) -b -s 14 -o $@ $(DJVU_FONT_SOURCE)
CLEANFILES += dejavu_bold_14.pf2
dejavu_16.pf2: $(DJVU_FONT_SOURCE) build-grub-mkfont$(BUILD_EXEEXT)
	./build-grub-mkfont$(BUILD_EXEEXT) -s 16 -o $@ $(DJVU_FONT_SOURCE)
CLEANFILES += dejavu_16.pf2
else
starfield_DATA =
endif

EXTRA_DIST += $(starfield_theme_files)
EXTRA_DIST += $(srcdir)/themes/starfield/src/slider_s.xcf $(srcdir)/themes/starfield/src/slider_n.xcf $(srcdir)/themes/starfield/src/slider_c.xcf $(srcdir)/themes/starfield/src/blob_nw.xcf $(srcdir)/themes/starfield/src/bootmenu/center.xcf $(srcdir)/themes/starfield/src/bootmenu/corner.xcf $(srcdir)/themes/starfield/src/bootmenu/side.xcf $(srcdir)/themes/starfield/src/terminalbox/side.xcf $(srcdir)/themes/starfield/src/terminalbox/corner.xcf $(srcdir)/themes/starfield/src/terminalbox/center.xcf

unicode.pf2: $(FONT_SOURCE) build-grub-mkfont$(BUILD_EXEEXT)
	./build-grub-mkfont$(BUILD_EXEEXT) -o $@ $(FONT_SOURCE) || (rm -f $@; exit 1)
CLEANFILES += unicode.pf2

# Arrows and lines are needed to draw the menu, so always include them
UNICODE_ARROWS=0x2190-0x2193
UNICODE_LINES=0x2501-0x251B

ascii.pf2: $(FONT_SOURCE) build-grub-mkfont$(BUILD_EXEEXT)
	./build-grub-mkfont$(BUILD_EXEEXT) -o $@ $(FONT_SOURCE) -r 0x0-0x7f,$(UNICODE_ARROWS),$(UNICODE_LINES) || (rm -f $@; exit 1)
CLEANFILES += ascii.pf2

euro.pf2: $(FONT_SOURCE) build-grub-mkfont$(BUILD_EXEEXT)
	./build-grub-mkfont$(BUILD_EXEEXT) -o $@ $(FONT_SOURCE) -r 0x0-0x4ff,0x1e00-0x1fff,$(UNICODE_ARROWS),$(UNICODE_LINES) || (rm -f $@; exit 1)
CLEANFILES += euro.pf2

ascii.h: $(FONT_SOURCE) build-grub-gen-asciih$(BUILD_EXEEXT)
	./build-grub-gen-asciih$(BUILD_EXEEXT) $(FONT_SOURCE) $@ || (rm -f $@; exit 1)
CLEANFILES += ascii.h

widthspec.h: $(FONT_SOURCE) build-grub-gen-widthspec$(BUILD_EXEEXT)
	./build-grub-gen-widthspec$(BUILD_EXEEXT) $(FONT_SOURCE) $@ || (rm -f $@; exit 1)
CLEANFILES += widthspec.h

# Install config.h into platformdir
nodist_platform_HEADERS = config.h

pkgdata_DATA += grub-mkconfig_lib


if COND_real_platform

if COND_i386_coreboot
QEMU32=qemu-system-i386
MINIMUM_CPU_LINUX=pentium2
endif

if COND_i386_multiboot
QEMU32=qemu-system-i386
MINIMUM_CPU_LINUX=pentium2
endif

if COND_i386_ieee1275
QEMU32=qemu-system-i386
MINIMUM_CPU_LINUX=pentium2
endif

if COND_i386_qemu
QEMU32=qemu-system-i386
MINIMUM_CPU_LINUX=pentium2
endif

if COND_i386_pc
QEMU32=qemu-system-i386
MINIMUM_CPU_LINUX=pentium2
endif

if COND_i386_efi
QEMU32=qemu-system-i386
MINIMUM_CPU_LINUX=pentium2
endif

if COND_x86_64_efi
QEMU32=qemu-system-x86_64
MINIMUM_CPU_LINUX=core2duo
endif

linux.init.x86_64: $(srcdir)/grub-core/tests/boot/linux.init-x86_64.S $(srcdir)/grub-core/tests/boot/qemu-shutdown-x86.S
	$(TARGET_CC) -o $@ $< -static -m64 -nostdlib -nostdinc -DSUCCESSFUL_BOOT_STRING=\"$(SUCCESSFUL_BOOT_STRING)\"

linux.init.i386: $(srcdir)/grub-core/tests/boot/linux.init-i386.S $(srcdir)/grub-core/tests/boot/qemu-shutdown-x86.S
	$(TARGET_CC) -o $@ $< -static -m32 -nostdlib -nostdinc -DSUCCESSFUL_BOOT_STRING=\"$(SUCCESSFUL_BOOT_STRING)\"

linux.init.mips: $(srcdir)/grub-core/tests/boot/linux.init-mips.S
	$(TARGET_CC) -o $@ $< -static -nostdlib -nostdinc -DSUCCESSFUL_BOOT_STRING=\"$(SUCCESSFUL_BOOT_STRING)\"

linux.init.ppc: $(srcdir)/grub-core/tests/boot/linux.init-ppc.S
	$(TARGET_CC) -o $@ $< -static -nostdlib -nostdinc -DSUCCESSFUL_BOOT_STRING=\"$(SUCCESSFUL_BOOT_STRING)\"

linux.init.mipsel: $(srcdir)/grub-core/tests/boot/linux.init-mips.S
	$(TARGET_CC) -o $@ $< -static -nostdlib -nostdinc -DSUCCESSFUL_BOOT_STRING=\"$(SUCCESSFUL_BOOT_STRING)\"

linux.init.loongson: $(srcdir)/grub-core/tests/boot/linux.init-mips.S
	$(TARGET_CC) -o $@ $< -static -nostdlib -nostdinc -DSUCCESSFUL_BOOT_STRING=\"$(SUCCESSFUL_BOOT_STRING)\" -DREBOOT=1

multiboot.elf: $(srcdir)/grub-core/tests/boot/kernel-i386.S $(srcdir)/grub-core/tests/boot/qemu-shutdown-x86.S
	$(TARGET_CC) -o $@ $< -DSUCCESSFUL_BOOT_STRING=\"$(SUCCESSFUL_BOOT_STRING)\" -static -ffreestanding -nostdlib -nostdinc -DTARGET_MULTIBOOT=1 -Wl,--build-id=none -Wl,-N -Wl,-Ttext,0x100000 -m32 -I$(srcdir)/include

kfreebsd.elf: $(srcdir)/grub-core/tests/boot/kernel-i386.S $(srcdir)/grub-core/tests/boot/qemu-shutdown-x86.S
	$(TARGET_CC) -o $@ $< -DSUCCESSFUL_BOOT_STRING=\"$(SUCCESSFUL_BOOT_STRING)\" -static -ffreestanding -nostdlib -nostdinc -Wl,--build-id=none -Wl,-N -Wl,-Ttext,0x100000 -m32 -I$(srcdir)/include

kfreebsd.aout: kfreebsd.elf
	$(TARGET_OBJCOPY) -O a.out-i386-linux $< $@ -j .text

pc-chainloader.elf: $(srcdir)/grub-core/tests/boot/kernel-8086.S $(srcdir)/grub-core/tests/boot/qemu-shutdown-x86.S
	$(TARGET_CC) -o $@ $< -static -DTARGET_CHAINLOADER=1 -DSUCCESSFUL_BOOT_STRING=\"$(SUCCESSFUL_BOOT_STRING)\" -ffreestanding -nostdlib -nostdinc -Wl,--build-id=none -Wl,-N -Wl,-Ttext,0x7c00 -m32

pc-chainloader.bin: pc-chainloader.elf
	$(TARGET_OBJCOPY) -O binary --strip-unneeded -R .note -R .comment -R .note.gnu.build-id -R .reginfo -R .rel.dyn -R .note.gnu.gold-version $< $@;

ntldr.elf: $(srcdir)/grub-core/tests/boot/kernel-8086.S $(srcdir)/grub-core/tests/boot/qemu-shutdown-x86.S
	$(TARGET_CC) -o $@ $< -DTARGET_NTLDR=1 -DSUCCESSFUL_BOOT_STRING=\"$(SUCCESSFUL_BOOT_STRING)\" -static -ffreestanding -nostdlib -nostdinc -Wl,--build-id=none -Wl,-N -Wl,-Ttext,0 -m32

ntldr.bin: ntldr.elf
	$(TARGET_OBJCOPY) -O binary --strip-unneeded -j .text $< $@;

multiboot2.elf: $(srcdir)/grub-core/tests/boot/kernel-i386.S $(srcdir)/grub-core/tests/boot/qemu-shutdown-x86.S
	$(TARGET_CC) -static -o $@ $< -DSUCCESSFUL_BOOT_STRING=\"$(SUCCESSFUL_BOOT_STRING)\" -ffreestanding -nostdlib -nostdinc -Wl,--build-id=none -Wl,-N -Wl,-Ttext,0x100000 -m32 -I$(srcdir)/include -DTARGET_MULTIBOOT2=1

kfreebsd.init.x86_64: $(srcdir)/grub-core/tests/boot/kfreebsd.init-x86_64.S $(srcdir)/grub-core/tests/boot/qemu-shutdown-x86.S
	$(TARGET_CC) -o $@ $< -m64 -static -nostdlib -nostdinc -DSUCCESSFUL_BOOT_STRING=\"$(SUCCESSFUL_BOOT_STRING)\" && freebsd-brandelf -t FreeBSD $@

kfreebsd.init.i386: $(srcdir)/grub-core/tests/boot/kfreebsd.init-i386.S $(srcdir)/grub-core/tests/boot/qemu-shutdown-x86.S
	$(TARGET_CC) -o $@ $< -m32 -static -nostdlib -nostdinc -DSUCCESSFUL_BOOT_STRING=\"$(SUCCESSFUL_BOOT_STRING)\" && freebsd-brandelf -t FreeBSD $@

knetbsd.init.i386: $(srcdir)/grub-core/tests/boot/kbsd.init-i386.S $(srcdir)/grub-core/tests/boot/qemu-shutdown-x86.S
	$(TARGET_CC) -o $@ $< -m32 -static -nostdlib -nostdinc -DTARGET_NETBSD=1 -DSUCCESSFUL_BOOT_STRING=\"$(SUCCESSFUL_BOOT_STRING)\"

kopenbsd.init.i386: $(srcdir)/grub-core/tests/boot/kbsd.init-i386.S $(srcdir)/grub-core/tests/boot/qemu-shutdown-x86.S
	$(TARGET_CC) -o $@ $< -m32 -static -nostdlib -nostdinc -DTARGET_OPENBSD=1 -DSUCCESSFUL_BOOT_STRING=\"$(SUCCESSFUL_BOOT_STRING)\"

knetbsd.init.x86_64: $(srcdir)/grub-core/tests/boot/kbsd.init-x86_64.S $(srcdir)/grub-core/tests/boot/qemu-shutdown-x86.S
	$(TARGET_CC) -o $@ $< -m64 -DTARGET_NETBSD=1 -static -nostdlib -nostdinc -DSUCCESSFUL_BOOT_STRING=\"$(SUCCESSFUL_BOOT_STRING)\"

kopenbsd.init.x86_64: $(srcdir)/grub-core/tests/boot/kbsd.init-x86_64.S $(srcdir)/grub-core/tests/boot/qemu-shutdown-x86.S
	$(TARGET_CC) -o $@ $< -m64 -DTARGET_OPENBSD=1 -static -nostdlib -nostdinc -DSUCCESSFUL_BOOT_STRING=\"$(SUCCESSFUL_BOOT_STRING)\"

linux-initramfs.mips: linux.init.mips Makefile
	TDIR=`mktemp -d "$${TMPDIR:-/tmp}/tmp.XXXXXXXXXX"` && cp $< $$TDIR/init && (cd $$TDIR && echo ./init | cpio -R 0:0 --quiet --dereference -o -H newc) | gzip > $@ && rm -rf $$TDIR

linux-initramfs.ppc: linux.init.ppc Makefile
	TDIR=`mktemp -d "$${TMPDIR:-/tmp}/tmp.XXXXXXXXXX"` && cp $< $$TDIR/init && (cd $$TDIR && echo ./init | cpio -R 0:0 --quiet --dereference -o -H newc) | gzip > $@ && rm -rf $$TDIR

linux-initramfs.mipsel: linux.init.mipsel Makefile
	TDIR=`mktemp -d "$${TMPDIR:-/tmp}/tmp.XXXXXXXXXX"` && cp $< $$TDIR/init && (cd $$TDIR && echo ./init | cpio -R 0:0 --quiet --dereference -o -H newc) | gzip > $@ && rm -rf $$TDIR

linux-initramfs.loongson: linux.init.loongson Makefile
	TDIR=`mktemp -d "$${TMPDIR:-/tmp}/tmp.XXXXXXXXXX"` && cp $< $$TDIR/init && (cd $$TDIR && echo ./init | cpio -R 0:0 --quiet --dereference -o -H newc) | gzip > $@ && rm -rf $$TDIR

linux-initramfs.i386: linux.init.i386 Makefile
	TDIR=`mktemp -d "$${TMPDIR:-/tmp}/tmp.XXXXXXXXXX"` && cp $< $$TDIR/init && (cd $$TDIR && echo ./init | cpio -R 0:0 --quiet --dereference -o -H newc) | gzip > $@ && rm -rf $$TDIR

linux-initramfs.x86_64: linux.init.x86_64 Makefile
	TDIR=`mktemp -d "$${TMPDIR:-/tmp}/tmp.XXXXXXXXXX"` && cp $< $$TDIR/init && (cd $$TDIR && echo ./init | cpio -R 0:0 --quiet --dereference -o -H newc) | gzip > $@ && rm -rf $$TDIR

kfreebsd-mfsroot.i386.img: kfreebsd.init.i386 Makefile
	TDIR=`mktemp -d "$${TMPDIR:-/tmp}/tmp.XXXXXXXXXX"` && mkdir $$TDIR/dev && mkdir $$TDIR/sbin && cp $< $$TDIR/sbin/init && makefs -t ffs -s 30m -f 1000 -o minfree=0,version=1 $@ $$TDIR && rm -rf $$TDIR

knetbsd.image.i386: knetbsd.init.i386 $(srcdir)/grub-core/tests/boot/kbsd.spec.txt
	TDIR=`mktemp -d "$${TMPDIR:-/tmp}/tmp.XXXXXXXXXX"` && mkdir $$TDIR/dev && mkdir $$TDIR/sbin && cp $< $$TDIR/sbin/init && makefs -F $(srcdir)/grub-core/tests/boot/kbsd.spec.txt -t ffs -s 64k -f 10 -o minfree=0,version=1 $@ $$TDIR && rm -rf $$TDIR

kopenbsd.image.i386: kopenbsd.init.i386 $(srcdir)/grub-core/tests/boot/kopenbsdlabel.txt
	TDIR=`mktemp -d "$${TMPDIR:-/tmp}/tmp.XXXXXXXXXX"` && mkdir $$TDIR/dev && mkdir $$TDIR/sbin && cp $< $$TDIR/sbin/init && makefs -F $(srcdir)/grub-core/tests/boot/kbsd.spec.txt -t ffs -s 128k -f 10 -o minfree=0,version=1 $@ $$TDIR && bsdlabel -f -R $@ $(srcdir)/grub-core/tests/boot/kopenbsdlabel.txt && rm -rf $$TDIR || rm -f $@

kopenbsd.image.x86_64: kopenbsd.init.x86_64 $(srcdir)/grub-core/tests/boot/kopenbsdlabel.txt
	TDIR=`mktemp -d "$${TMPDIR:-/tmp}/tmp.XXXXXXXXXX"` && mkdir $$TDIR/dev && mkdir $$TDIR/sbin && cp $< $$TDIR/sbin/init && makefs -F $(srcdir)/grub-core/tests/boot/kbsd.spec.txt -t ffs -s 128k -f 10 -o minfree=0,version=1 $@ $$TDIR && bsdlabel -f -R $@ $(srcdir)/grub-core/tests/boot/kopenbsdlabel.txt && rm -rf $$TDIR || rm -f $@

knetbsd.miniroot-image.i386.img: knetbsd.image.i386 $(GRUB_PAYLOADS_DIR)/knetbsd.miniroot.i386
	$(TARGET_OBJCOPY) --add-section=miniroot=$< $(GRUB_PAYLOADS_DIR)/knetbsd.miniroot.i386 $@

kfreebsd-mfsroot.x86_64.img: kfreebsd.init.x86_64 Makefile
	TDIR=`mktemp -d "$${TMPDIR:-/tmp}/tmp.XXXXXXXXXX"` && mkdir $$TDIR/dev && mkdir $$TDIR/sbin && cp $< $$TDIR/sbin/init && makefs -t ffs -s 30m -f 1000 -o minfree=0,version=1 $@ $$TDIR && rm -rf $$TDIR

knetbsd.image.x86_64: knetbsd.init.x86_64 $(srcdir)/grub-core/tests/boot/kbsd.spec.txt
	TDIR=`mktemp -d "$${TMPDIR:-/tmp}/tmp.XXXXXXXXXX"` && mkdir $$TDIR/dev && mkdir $$TDIR/sbin && cp $< $$TDIR/sbin/init && makefs -F $(srcdir)/grub-core/tests/boot/kbsd.spec.txt -t ffs -s 64k -f 10 -o minfree=0,version=1 $@ $$TDIR && rm -rf $$TDIR

knetbsd.miniroot-image.x86_64.img: knetbsd.image.x86_64 $(GRUB_PAYLOADS_DIR)/knetbsd.miniroot.x86_64
	$(TARGET_OBJCOPY) --add-section=miniroot=$< $(GRUB_PAYLOADS_DIR)/knetbsd.miniroot.x86_64 $@

CLEANFILES += linux.init.i386 kfreebsd.init.i386 linux.init.x86_64 linux-initramfs.i386 linux-initramfs.x86_64

kfreebsd-mfsroot.i386.gz: kfreebsd-mfsroot.i386.img
	gzip < $< > $@

bootcheck-kfreebsd-i386: kfreebsd-mfsroot.i386.gz $(GRUB_PAYLOADS_DIR)/kfreebsd.i386 $(GRUB_PAYLOADS_DIR)/kfreebsd_env.i386 $(srcdir)/grub-core/tests/boot/kfreebsd.cfg grub-shell
	./grub-shell --timeout=$(BOOTCHECK_TIMEOUT) --qemu=$(QEMU32) --files=/mfsroot.gz=kfreebsd-mfsroot.i386.gz --files=/kfreebsd=$(GRUB_PAYLOADS_DIR)/kfreebsd.i386 --files=/kfreebsd_env=$(GRUB_PAYLOADS_DIR)/kfreebsd_env.i386 $(srcdir)/grub-core/tests/boot/kfreebsd.cfg | grep $(SUCCESSFUL_BOOT_STRING) > /dev/null

kfreebsd-mfsroot.x86_64.gz: kfreebsd-mfsroot.x86_64.img
	gzip < $< > $@

bootcheck-kfreebsd-x86_64: kfreebsd-mfsroot.x86_64.gz $(GRUB_PAYLOADS_DIR)/kfreebsd.x86_64 $(GRUB_PAYLOADS_DIR)/kfreebsd_env.x86_64 $(srcdir)/grub-core/tests/boot/kfreebsd.cfg grub-shell
	./grub-shell --timeout=$(BOOTCHECK_TIMEOUT) --qemu=qemu-system-x86_64 --files=/mfsroot.gz=kfreebsd-mfsroot.x86_64.gz --files=/kfreebsd=$(GRUB_PAYLOADS_DIR)/kfreebsd.x86_64 --files=/kfreebsd_env=$(GRUB_PAYLOADS_DIR)/kfreebsd_env.x86_64 $(srcdir)/grub-core/tests/boot/kfreebsd.cfg | grep $(SUCCESSFUL_BOOT_STRING) > /dev/null

knetbsd.miniroot-image.i386.gz: knetbsd.miniroot-image.i386.img
	gzip < $< > $@

bootcheck-knetbsd-i386: knetbsd.miniroot-image.i386.gz $(GRUB_PAYLOADS_DIR)/knetbsd.i386 $(srcdir)/grub-core/tests/boot/knetbsd.cfg grub-shell
	./grub-shell --timeout=$(BOOTCHECK_TIMEOUT) --qemu=$(QEMU32) --files=/miniroot.gz=knetbsd.miniroot-image.i386.gz --files=/knetbsd=$(GRUB_PAYLOADS_DIR)/knetbsd.i386 $(srcdir)/grub-core/tests/boot/knetbsd.cfg | grep $(SUCCESSFUL_BOOT_STRING) > /dev/null

bootcheck-kopenbsd-i386: kopenbsd.image.i386 $(GRUB_PAYLOADS_DIR)/kopenbsd.i386 $(srcdir)/grub-core/tests/boot/kopenbsd.cfg grub-shell
	./grub-shell --timeout=$(BOOTCHECK_TIMEOUT) --qemu=$(QEMU32) --files=/ramdisk=kopenbsd.image.i386 --files=/kopenbsd=$(GRUB_PAYLOADS_DIR)/kopenbsd.i386 $(srcdir)/grub-core/tests/boot/kopenbsd.cfg | grep $(SUCCESSFUL_BOOT_STRING) > /dev/null

bootcheck-kopenbsd-x86_64: kopenbsd.image.x86_64 $(GRUB_PAYLOADS_DIR)/kopenbsd.x86_64 $(srcdir)/grub-core/tests/boot/kopenbsd.cfg grub-shell
	./grub-shell --timeout=$(BOOTCHECK_TIMEOUT) --qemu=qemu-system-x86_64 --files=/ramdisk=kopenbsd.image.x86_64 --files=/kopenbsd=$(GRUB_PAYLOADS_DIR)/kopenbsd.x86_64 $(srcdir)/grub-core/tests/boot/kopenbsd.cfg | grep $(SUCCESSFUL_BOOT_STRING) > /dev/null

knetbsd.miniroot-image.x86_64.gz: knetbsd.miniroot-image.x86_64.img
	gzip < $< > $@

bootcheck-knetbsd-x86_64: knetbsd.miniroot-image.x86_64.gz $(GRUB_PAYLOADS_DIR)/knetbsd.x86_64 $(srcdir)/grub-core/tests/boot/knetbsd.cfg grub-shell
	./grub-shell --timeout=$(BOOTCHECK_TIMEOUT) --qemu=qemu-system-x86_64 --files=/miniroot.gz=knetbsd.miniroot-image.x86_64.gz --files=/knetbsd=$(GRUB_PAYLOADS_DIR)/knetbsd.x86_64 $(srcdir)/grub-core/tests/boot/knetbsd.cfg | grep $(SUCCESSFUL_BOOT_STRING) > /dev/null

bootcheck-linux-i386: linux-initramfs.i386 $(GRUB_PAYLOADS_DIR)/linux.i386 $(srcdir)/grub-core/tests/boot/linux.cfg grub-shell
	./grub-shell --timeout=$(BOOTCHECK_TIMEOUT) --qemu=$(QEMU32) --files=/initrd=linux-initramfs.i386 --files=/linux=$(GRUB_PAYLOADS_DIR)/linux.i386 $(srcdir)/grub-core/tests/boot/linux.cfg --qemu-opts="-cpu $(MINIMUM_CPU_LINUX)" | grep $(SUCCESSFUL_BOOT_STRING) > /dev/null

bootcheck-linux-x86_64: linux-initramfs.x86_64 $(GRUB_PAYLOADS_DIR)/linux.x86_64 $(srcdir)/grub-core/tests/boot/linux.cfg grub-shell
	./grub-shell --timeout=$(BOOTCHECK_TIMEOUT) --qemu=qemu-system-x86_64 --files=/initrd=linux-initramfs.x86_64 --files=/linux=$(GRUB_PAYLOADS_DIR)/linux.x86_64 $(srcdir)/grub-core/tests/boot/linux.cfg | grep $(SUCCESSFUL_BOOT_STRING) > /dev/null

bootcheck-linux-mips: linux-initramfs.mips $(GRUB_PAYLOADS_DIR)/linux.mips $(srcdir)/grub-core/tests/boot/linux.cfg grub-shell
	./grub-shell --timeout=$(BOOTCHECK_TIMEOUT) --files=/initrd=linux-initramfs.mips --files=/linux=$(GRUB_PAYLOADS_DIR)/linux.mips $(srcdir)/grub-core/tests/boot/linux.cfg | grep $(SUCCESSFUL_BOOT_STRING) > /dev/null

bootcheck-linux-ppc: linux-initramfs.ppc $(GRUB_PAYLOADS_DIR)/linux.ppc $(srcdir)/grub-core/tests/boot/linux.cfg grub-shell
	./grub-shell --timeout=$(BOOTCHECK_TIMEOUT) --files=/initrd=linux-initramfs.ppc --files=/linux=$(GRUB_PAYLOADS_DIR)/linux.ppc $(srcdir)/grub-core/tests/boot/linux-ppc.cfg | grep $(SUCCESSFUL_BOOT_STRING) > /dev/null

bootcheck-linux-mipsel: linux-initramfs.mipsel $(GRUB_PAYLOADS_DIR)/linux.mipsel $(srcdir)/grub-core/tests/boot/linux.cfg grub-shell
	./grub-shell --timeout=$(BOOTCHECK_TIMEOUT) --files=/initrd=linux-initramfs.mipsel --files=/linux=$(GRUB_PAYLOADS_DIR)/linux.mipsel $(srcdir)/grub-core/tests/boot/linux.cfg | grep $(SUCCESSFUL_BOOT_STRING) > /dev/null

bootcheck-linux-loongson: linux-initramfs.loongson $(GRUB_PAYLOADS_DIR)/linux.loongson $(srcdir)/grub-core/tests/boot/linux.cfg grub-shell
	./grub-shell --timeout=$(BOOTCHECK_TIMEOUT) --files=/initrd=linux-initramfs.loongson --files=/linux=$(GRUB_PAYLOADS_DIR)/linux.loongson $(srcdir)/grub-core/tests/boot/linux.cfg | grep $(SUCCESSFUL_BOOT_STRING) > /dev/null

bootcheck-linux16-i386: linux-initramfs.i386 $(GRUB_PAYLOADS_DIR)/linux.i386 $(srcdir)/grub-core/tests/boot/linux.cfg grub-shell
	./grub-shell --timeout=$(BOOTCHECK_TIMEOUT) --qemu=$(QEMU32) --files=/initrd=linux-initramfs.i386 --files=/linux=$(GRUB_PAYLOADS_DIR)/linux.i386 $(srcdir)/grub-core/tests/boot/linux16.cfg | grep $(SUCCESSFUL_BOOT_STRING) > /dev/null

bootcheck-linux16-x86_64: linux-initramfs.x86_64 $(GRUB_PAYLOADS_DIR)/linux.x86_64 $(srcdir)/grub-core/tests/boot/linux.cfg grub-shell
	./grub-shell --timeout=$(BOOTCHECK_TIMEOUT) --qemu=qemu-system-x86_64 --files=/initrd=linux-initramfs.x86_64 --files=/linux=$(GRUB_PAYLOADS_DIR)/linux.x86_64 $(srcdir)/grub-core/tests/boot/linux16.cfg | grep $(SUCCESSFUL_BOOT_STRING) > /dev/null

bootcheck-multiboot: multiboot.elf $(srcdir)/grub-core/tests/boot/multiboot.cfg grub-shell
	./grub-shell --timeout=$(BOOTCHECK_TIMEOUT) --qemu=$(QEMU32) --files=/multiboot.elf=multiboot.elf $(srcdir)/grub-core/tests/boot/multiboot.cfg | grep $(SUCCESSFUL_BOOT_STRING) > /dev/null

bootcheck-multiboot2: multiboot2.elf $(srcdir)/grub-core/tests/boot/multiboot2.cfg grub-shell
	./grub-shell --timeout=$(BOOTCHECK_TIMEOUT) --qemu=$(QEMU32) --files=/multiboot2.elf=multiboot2.elf $(srcdir)/grub-core/tests/boot/multiboot2.cfg | grep $(SUCCESSFUL_BOOT_STRING) > /dev/null

bootcheck-kfreebsd-aout: kfreebsd.aout $(srcdir)/grub-core/tests/boot/kfreebsd-aout.cfg grub-shell
	./grub-shell --timeout=$(BOOTCHECK_TIMEOUT) --qemu=$(QEMU32) --files=/kfreebsd.aout=kfreebsd.aout $(srcdir)/grub-core/tests/boot/kfreebsd-aout.cfg | grep $(SUCCESSFUL_BOOT_STRING) > /dev/null

bootcheck-pc-chainloader: pc-chainloader.bin $(srcdir)/grub-core/tests/boot/pc-chainloader.cfg grub-shell
	./grub-shell --timeout=$(BOOTCHECK_TIMEOUT) --qemu=$(QEMU32) --files=/pc-chainloader.bin=pc-chainloader.bin $(srcdir)/grub-core/tests/boot/pc-chainloader.cfg | grep $(SUCCESSFUL_BOOT_STRING) > /dev/null

bootcheck-ntldr: ntldr.bin $(srcdir)/grub-core/tests/boot/ntldr.cfg grub-shell
	./grub-shell --timeout=$(BOOTCHECK_TIMEOUT) --qemu=$(QEMU32) --files=/ntldr.bin=ntldr.bin $(srcdir)/grub-core/tests/boot/ntldr.cfg | grep $(SUCCESSFUL_BOOT_STRING) > /dev/null

if COND_i386_efi
# NetBSD has no support for finding ACPI on EFI
BOOTCHECKS = bootcheck-kfreebsd-aout bootcheck-kopenbsd-i386 bootcheck-kopenbsd-x86_64 bootcheck-multiboot bootcheck-multiboot2 bootcheck-linux-i386 bootcheck-linux-x86_64 bootcheck-kfreebsd-x86_64 bootcheck-kfreebsd-i386
endif

if COND_x86_64_efi
# NetBSD has no support for finding ACPI on EFI
BOOTCHECKS = bootcheck-kfreebsd-aout bootcheck-kopenbsd-i386 bootcheck-kopenbsd-x86_64 bootcheck-multiboot bootcheck-multiboot2 bootcheck-linux-i386 bootcheck-linux-x86_64 bootcheck-kfreebsd-x86_64 bootcheck-kfreebsd-i386
endif

if COND_i386_multiboot
# *BSD requires ACPI
BOOTCHECKS = bootcheck-kfreebsd-aout bootcheck-multiboot bootcheck-multiboot2 bootcheck-linux-i386 bootcheck-linux-x86_64
endif


if COND_i386_qemu
# *BSD requires ACPI
BOOTCHECKS = bootcheck-kfreebsd-aout bootcheck-multiboot bootcheck-multiboot2 bootcheck-linux-i386 bootcheck-linux-x86_64
endif

if COND_i386_coreboot
BOOTCHECKS = bootcheck-kfreebsd-aout bootcheck-kopenbsd-i386 bootcheck-kopenbsd-x86_64 bootcheck-multiboot bootcheck-multiboot2 bootcheck-linux-i386 bootcheck-linux-x86_64 bootcheck-knetbsd-x86_64 bootcheck-kfreebsd-x86_64 bootcheck-kfreebsd-i386
endif

if COND_i386_ieee1275
# *BSD requires ACPI
#legacy protocol (linux16) makes early BIOS calls.
BOOTCHECKS = bootcheck-kfreebsd-aout bootcheck-multiboot bootcheck-multiboot2 bootcheck-linux-i386 bootcheck-linux-x86_64
endif

if COND_i386_pc
#pc chainloader by definition is only for i386-pc
#ntldr and bootmgr require BIOS.
#legacy protocol (linux16) makes early BIOS calls.
# 32-bit NetBSD crashes early on non-BIOS
BOOTCHECKS = bootcheck-kfreebsd-aout bootcheck-kopenbsd-i386 bootcheck-kopenbsd-x86_64 bootcheck-multiboot bootcheck-multiboot2 bootcheck-linux-i386 bootcheck-linux-x86_64 bootcheck-knetbsd-x86_64 bootcheck-kfreebsd-x86_64 bootcheck-kfreebsd-i386 bootcheck-pc-chainloader bootcheck-ntldr bootcheck-linux16-i386 bootcheck-linux16-x86_64 bootcheck-knetbsd-i386
endif

if COND_mips_loongson
BOOTCHECKS = bootcheck-linux-loongson
endif

if COND_mipsel
if COND_mips_qemu_mips
BOOTCHECKS = bootcheck-linux-mipsel
endif
endif
if COND_mipseb
if COND_mips_qemu_mips
BOOTCHECKS = bootcheck-linux-mips
endif
endif

if COND_powerpc_ieee1275
BOOTCHECKS = bootcheck-linux-ppc
endif

.PHONY: bootcheck-linux-i386 bootcheck-linux-x86_64 \
       bootcheck-kfreebsd-i386 bootcheck-kfreebsd-x86_64 \
       bootcheck-knetbsd-i386 bootcheck-knetbsd-x86_64 \
	bootcheck-linux-mips FORCE

# Randomly generated
SUCCESSFUL_BOOT_STRING=3e49994fd5d82b7c9298d672d774080d
# tianocore cd access is very slow
BOOTCHECK_TIMEOUT=180

bootcheck: $(BOOTCHECKS)

if COND_i386_coreboot
FS_PAYLOAD_MODULES ?= $(shell cat grub-core/fs.lst)
default_payload.elf: grub-mkstandalone grub-mkimage FORCE
	test -f $@ && rm $@ || true
	pkgdatadir=. ./grub-mkstandalone --grub-mkimage=./grub-mkimage -O i386-coreboot -o $@ --modules='ahci pata xhci ehci uhci ohci usb_keyboard usbms part_msdos ext2 fat at_keyboard part_gpt usbserial_usbdebug cbfs' --install-modules='ls linux search configfile normal cbtime cbls memrw iorw minicmd lsmmap lspci halt reboot hexdump pcidump regexp setpci lsacpi chain test serial multiboot cbmemc linux16 gzio echo help syslinuxcfg xnu $(FS_PAYLOAD_MODULES) password_pbkdf2 $(EXTRA_PAYLOAD_MODULES)' --fonts= --themes= --locales= -d grub-core/ /boot/grub/grub.cfg=$(srcdir)/coreboot.cfg
endif

endif

EXTRA_DIST += grub-core/tests/boot/kbsd.init-i386.S grub-core/tests/boot/kbsd.init-x86_64.S grub-core/tests/boot/kbsd.spec.txt grub-core/tests/boot/kernel-8086.S grub-core/tests/boot/kernel-i386.S grub-core/tests/boot/kfreebsd-aout.cfg grub-core/tests/boot/kfreebsd.cfg grub-core/tests/boot/kfreebsd.init-i386.S grub-core/tests/boot/kfreebsd.init-x86_64.S grub-core/tests/boot/knetbsd.cfg grub-core/tests/boot/kopenbsd.cfg grub-core/tests/boot/kopenbsdlabel.txt grub-core/tests/boot/linux16.cfg grub-core/tests/boot/linux.cfg grub-core/tests/boot/linux.init-i386.S grub-core/tests/boot/linux.init-mips.S grub-core/tests/boot/linux.init-ppc.S grub-core/tests/boot/linux.init-x86_64.S grub-core/tests/boot/linux-ppc.cfg grub-core/tests/boot/multiboot2.cfg grub-core/tests/boot/multiboot.cfg grub-core/tests/boot/ntldr.cfg grub-core/tests/boot/pc-chainloader.cfg grub-core/tests/boot/qemu-shutdown-x86.S

windowsdir=$(top_builddir)/$(PACKAGE)-$(VERSION)-for-windows
windowsdir: $(PROGRAMS) $(starfield_DATA) $(platform_DATA)
	test -d $(windowsdir) && rm -rf $(windowsdir) || true
	test -d $(windowsdir) || mkdir $(windowsdir)
	$(MAKE) -C po $(AM_MAKEFLAGS) windowsdir
	$(MAKE) -C grub-core $(AM_MAKEFLAGS) windowsdir
	test -d $(windowsdir)/themes || mkdir $(windowsdir)/themes
	test -d $(windowsdir)/themes/starfield || mkdir $(windowsdir)/themes/starfield
	for x in $(PROGRAMS); do \
		if [ x$(STRIP) != x ]; then $(STRIP) $$x -o $(windowsdir)/$$x; \
		else cp -fp $$x $(windowsdir)/$$x; fi; \
	done
	for x in $(pkgdata_DATA); do \
		cp -fp $$x $(windowsdir)/$$x; \
	done
	for x in $(starfield_DATA); do \
		cp -fp $$x $(windowsdir)/themes/starfield/$$(basename $$x); \
	done
	for x in $(GRUB_WINDOWS_EXTRA_DIST); do \
		cp -fp $$x $(windowsdir); \
	done

windowszip=$(top_builddir)/$(PACKAGE)-$(VERSION)-for-windows.zip
windowszip: windowsdir
	test -f $(windowszip) && rm $(windowszip) || true
	zip -r $(windowszip) $(windowsdir)
	rm -rf $(windowsdir)

EXTRA_DIST += linguas.sh

changelog_start_date = 2015-01-23
gitlog_to_changelog = $(top_srcdir)/build-aux/gitlog-to-changelog

ChangeLog: FORCE
	if test -d $(top_srcdir)/.git; then \
		$(gitlog_to_changelog) --srcdir=$(top_srcdir) --since=$(changelog_start_date) > '$@.tmp'; \
		rm -f '$@'; mv '$@.tmp' '$@'; \
	else \
		touch $@; \
	fi

syslinux_test: $(top_builddir)/config.status tests/syslinux/ubuntu10.04_grub.cfg

# Mimic simplify_filename from grub-core/lib/syslinux_parse.c, so that we
# can predict its behaviour in tests.  We have to pre-substitute this before
# calling config.status, as config.status offers no reliable way to hook in
# a command between setting ac_abs_top_srcdir and emitting output files.
tests/syslinux/ubuntu10.04_grub.cfg: $(top_builddir)/config.status tests/syslinux/ubuntu10.04_grub.cfg.in
	simplified_abs_top_srcdir=`echo "$(abs_top_srcdir)" | sed 's,//,/,g; s,/\./,/,g; :loop; s,/[^/][^/]*/\.\.\(/\|$$\),\1,; t loop'`; \
	sed "s,@simplified_abs_top_srcdir@,$$simplified_abs_top_srcdir,g" $(srcdir)/tests/syslinux/ubuntu10.04_grub.cfg.in | $(top_builddir)/config.status --file=$@:-
CLEANFILES += tests/syslinux/ubuntu10.04_grub.cfg
