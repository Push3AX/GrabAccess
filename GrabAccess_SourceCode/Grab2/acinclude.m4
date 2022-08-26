dnl Redefine AC_LANG_PROGRAM with a "-Wstrict-prototypes -Werror"-friendly
dnl version.  Patch submitted to bug-autoconf in 2009-09-16.
m4_define([AC_LANG_PROGRAM(C)],
[$1
int
main (void)
{
dnl Do *not* indent the following line: there may be CPP directives.
dnl Don't move the `;' right after for the same reason.
$2
  ;
  return 0;
}])


dnl Check whether target compiler is working
AC_DEFUN([grub_PROG_TARGET_CC],
[AC_MSG_CHECKING([whether target compiler is working])
AC_CACHE_VAL(grub_cv_prog_target_cc,
[AC_LINK_IFELSE([AC_LANG_PROGRAM([[
asm (".globl start; start:");
void __main (void);
void __main (void) {}
int main (void);
]], [[]])],
  		[grub_cv_prog_target_cc=yes],
		[grub_cv_prog_target_cc=no])
])
AC_MSG_RESULT([$grub_cv_prog_target_cc])

if test "x$grub_cv_prog_target_cc" = xno; then
  AC_MSG_ERROR([cannot compile for the target])
fi
])


dnl grub_ASM_USCORE checks if C symbols get an underscore after
dnl compiling to assembler.
dnl Written by Pavel Roskin. Based on grub_ASM_EXT_C written by
dnl Erich Boleyn and modified by Yoshinori K. Okuji.
AC_DEFUN([grub_ASM_USCORE],
[AC_REQUIRE([AC_PROG_CC])
AC_REQUIRE([AC_PROG_EGREP])
AC_MSG_CHECKING([if C symbols get an underscore after compilation])
AC_CACHE_VAL(grub_cv_asm_uscore,
[cat > conftest.c <<\EOF
int func (int *);
int
func (int *list)
{
  *list = 0;
  return *list;
}
EOF

if AC_TRY_COMMAND([${CC-cc} ${CFLAGS} -S conftest.c]) && test -s conftest.s; then
  true
else
  AC_MSG_ERROR([${CC-cc} failed to produce assembly code])
fi

if $EGREP '(^|[^_[:alnum]])_func' conftest.s >/dev/null 2>&1; then
  HAVE_ASM_USCORE=1
  grub_cv_asm_uscore=yes
else
  HAVE_ASM_USCORE=0
  grub_cv_asm_uscore=no
fi

rm -f conftest*])

AC_MSG_RESULT([$grub_cv_asm_uscore])
])


dnl Some versions of `objcopy -O binary' vary their output depending
dnl on the link address.
AC_DEFUN([grub_PROG_OBJCOPY_ABSOLUTE],
[AC_MSG_CHECKING([whether ${TARGET_OBJCOPY} works for absolute addresses])
AC_CACHE_VAL(grub_cv_prog_objcopy_absolute,
[cat > conftest.c <<\EOF
void cmain (void);
void
cmain (void)
{
   *((int *) 0x1000) = 2;
}
EOF

if AC_TRY_EVAL(ac_compile) && test -s conftest.o; then :
else
  AC_MSG_ERROR([${CC-cc} cannot compile C source code])
fi
grub_cv_prog_objcopy_absolute=yes
for link_addr in 0x2000 0x8000 0x7C00; do
  if AC_TRY_COMMAND([${CC-cc} ${TARGET_CFLAGS} ${TARGET_LDFLAGS} -nostdlib ${TARGET_IMG_LDFLAGS_AC} ${TARGET_IMG_BASE_LDOPT},$link_addr conftest.o -o conftest.exec]); then :
  else
    AC_MSG_ERROR([${CC-cc} cannot link at address $link_addr])
  fi
  if AC_TRY_COMMAND([${TARGET_OBJCOPY-objcopy} --only-section=.text -O binary conftest.exec conftest]); then :
  else
    AC_MSG_ERROR([${TARGET_OBJCOPY-objcopy} cannot create binary files])
  fi
  if test ! -f conftest.old || AC_TRY_COMMAND([cmp -s conftest.old conftest]); then
    mv -f conftest conftest.old
  else
    grub_cv_prog_objcopy_absolute=no
    break
  fi
done
rm -f conftest*])
AC_MSG_RESULT([$grub_cv_prog_objcopy_absolute])

if test "x$grub_cv_prog_objcopy_absolute" = xno; then
  AC_MSG_ERROR([GRUB requires a working absolute objcopy; upgrade your binutils])
fi
])


dnl Supply --build-id=none to ld if building modules.
dnl This suppresses warnings from ld on some systems
AC_DEFUN([grub_PROG_LD_BUILD_ID_NONE],
[AC_MSG_CHECKING([whether linker accepts --build-id=none])
AC_CACHE_VAL(grub_cv_prog_ld_build_id_none,
[save_LDFLAGS="$LDFLAGS"
LDFLAGS="$LDFLAGS -Wl,--build-id=none"
AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[]])],
	       [grub_cv_prog_ld_build_id_none=yes],
	       [grub_cv_prog_ld_build_id_none=no])
LDFLAGS="$save_LDFLAGS"
])
AC_MSG_RESULT([$grub_cv_prog_ld_build_id_none])

if test "x$grub_cv_prog_ld_build_id_none" = xyes; then
  TARGET_LDFLAGS="$TARGET_LDFLAGS -Wl,--build-id=none"
fi
])

dnl Check nm
AC_DEFUN([grub_PROG_NM_WORKS],
[AC_MSG_CHECKING([whether nm works])
AC_CACHE_VAL(grub_cv_prog_nm_works,
[
nm_works_tmp_dir="$(mktemp -d "./confXXXXXX")"
AC_LANG_CONFTEST([AC_LANG_PROGRAM([[]], [[]])])
$TARGET_CC $TARGET_CFLAGS -c conftest.c -o "$nm_works_tmp_dir/ef"
if $TARGET_NM "$nm_works_tmp_dir/ef" > /dev/null; then
   grub_cv_prog_nm_works=yes
else
   grub_cv_prog_nm_minus_p=no
fi
rm "$nm_works_tmp_dir/ef"
rmdir "$nm_works_tmp_dir"
])
AC_MSG_RESULT([$grub_cv_prog_nm_works])

if test "x$grub_cv_prog_nm_works" != xyes; then
  AC_MSG_ERROR([nm does not work])
fi
])

dnl Supply -P to nm
AC_DEFUN([grub_PROG_NM_MINUS_P],
[AC_MSG_CHECKING([whether nm accepts -P])
AC_CACHE_VAL(grub_cv_prog_nm_minus_p,
[
nm_minus_p_tmp_dir="$(mktemp -d "./confXXXXXX")"
AC_LANG_CONFTEST([AC_LANG_PROGRAM([[]], [[]])])
$TARGET_CC $TARGET_CFLAGS -c conftest.c -o "$nm_minus_p_tmp_dir/ef"
if $TARGET_NM -P "$nm_minus_p_tmp_dir/ef" 2>&1 > /dev/null; then
   grub_cv_prog_nm_minus_p=yes
else
   grub_cv_prog_nm_minus_p=no
fi
rm "$nm_minus_p_tmp_dir/ef"
rmdir "$nm_minus_p_tmp_dir"
])
AC_MSG_RESULT([$grub_cv_prog_nm_minus_p])

if test "x$grub_cv_prog_nm_minus_p" = xyes; then
  TARGET_NMFLAGS_MINUS_P="-P"
else
  TARGET_NMFLAGS_MINUS_P=
fi
])

dnl Supply --defined-only to nm
AC_DEFUN([grub_PROG_NM_DEFINED_ONLY],
[AC_MSG_CHECKING([whether nm accepts --defined-only])
AC_CACHE_VAL(grub_cv_prog_nm_defined_only,
[
nm_defined_only_tmp_dir="$(mktemp -d "./confXXXXXX")"
AC_LANG_CONFTEST([AC_LANG_PROGRAM([[]], [[]])])
$TARGET_CC $TARGET_CFLAGS -c conftest.c -o "$nm_defined_only_tmp_dir/ef"
if $TARGET_NM --defined-only "$nm_defined_only_tmp_dir/ef" 2>&1 > /dev/null; then
   grub_cv_prog_nm_defined_only=yes
else
   grub_cv_prog_nm_defined_only=no
fi
rm "$nm_defined_only_tmp_dir/ef"
rmdir "$nm_defined_only_tmp_dir"
])
AC_MSG_RESULT([$grub_cv_prog_nm_defined_only])

if test "x$grub_cv_prog_nm_defined_only" = xyes; then
  TARGET_NMFLAGS_DEFINED_ONLY=--defined-only
else
  TARGET_NMFLAGS_DEFINED_ONLY=
fi
])


dnl Check what symbol is defined as a bss start symbol.
dnl Written by Michael Hohmoth and Yoshinori K. Okuji.
AC_DEFUN([grub_CHECK_BSS_START_SYMBOL],
[AC_REQUIRE([AC_PROG_CC])
AC_MSG_CHECKING([if __bss_start is defined by the compiler])
AC_CACHE_VAL(grub_cv_check_uscore_uscore_bss_start_symbol,
[AC_LINK_IFELSE([AC_LANG_PROGRAM([[
asm (".globl start; start:");
void __main (void);
void __main (void) {}
int main (void);
]],
		[[asm ("incl __bss_start")]])],
		[grub_cv_check_uscore_uscore_bss_start_symbol=yes],
		[grub_cv_check_uscore_uscore_bss_start_symbol=no])])

AC_MSG_RESULT([$grub_cv_check_uscore_uscore_bss_start_symbol])

AC_MSG_CHECKING([if edata is defined by the compiler])
AC_CACHE_VAL(grub_cv_check_edata_symbol,
[AC_LINK_IFELSE([AC_LANG_PROGRAM([[
asm (".globl start; start:");
void __main (void);
void __main (void) {}
int main (void);]],
		[[asm ("incl edata")]])],
		[grub_cv_check_edata_symbol=yes],
		[grub_cv_check_edata_symbol=no])])

AC_MSG_RESULT([$grub_cv_check_edata_symbol])

AC_MSG_CHECKING([if _edata is defined by the compiler])
AC_CACHE_VAL(grub_cv_check_uscore_edata_symbol,
[AC_LINK_IFELSE([AC_LANG_PROGRAM([[
asm (".globl start; start:");
void __main (void);
void __main (void) {}
int main (void);]],
		[[asm ("incl _edata")]])],
		[grub_cv_check_uscore_edata_symbol=yes],
		[grub_cv_check_uscore_edata_symbol=no])])

AC_MSG_RESULT([$grub_cv_check_uscore_edata_symbol])

if test "x$grub_cv_check_uscore_uscore_bss_start_symbol" = xyes; then
  BSS_START_SYMBOL=__bss_start
elif test "x$grub_cv_check_edata_symbol" = xyes; then
  BSS_START_SYMBOL=edata
elif test "x$grub_cv_check_uscore_edata_symbol" = xyes; then
  BSS_START_SYMBOL=_edata
else
  AC_MSG_ERROR([none of __bss_start, edata or _edata is defined])
fi
])

dnl Check what symbol is defined as an end symbol.
dnl Written by Yoshinori K. Okuji.
AC_DEFUN([grub_CHECK_END_SYMBOL],
[AC_REQUIRE([AC_PROG_CC])
AC_MSG_CHECKING([if end is defined by the compiler])
AC_CACHE_VAL(grub_cv_check_end_symbol,
[AC_LINK_IFELSE([AC_LANG_PROGRAM([[
asm (".globl start; start:");
void __main (void);
void __main (void) {}
int main (void);]],
		[[asm ("incl end")]])],
		[grub_cv_check_end_symbol=yes],
		[grub_cv_check_end_symbol=no])])

AC_MSG_RESULT([$grub_cv_check_end_symbol])

AC_MSG_CHECKING([if _end is defined by the compiler])
AC_CACHE_VAL(grub_cv_check_uscore_end_symbol,
[AC_LINK_IFELSE([AC_LANG_PROGRAM([[
asm (".globl start; start:");
void __main (void);
void __main (void) {}
int main (void);]],
		[[asm ("incl _end")]])],
		[grub_cv_check_uscore_end_symbol=yes],
		[grub_cv_check_uscore_end_symbol=no])])

AC_MSG_RESULT([$grub_cv_check_uscore_end_symbol])

if test "x$grub_cv_check_end_symbol" = xyes; then
  END_SYMBOL=end
elif test "x$grub_cv_check_uscore_end_symbol" = xyes; then
  END_SYMBOL=_end
else
  AC_MSG_ERROR([neither end nor _end is defined])
fi
])


dnl Check if the C compiler supports the stack protector
AC_DEFUN([grub_CHECK_STACK_PROTECTOR],[
[# Stack smashing protector.
ssp_possible=yes]
AC_MSG_CHECKING([whether `$CC' accepts `-fstack-protector'])
# Is this a reliable test case?
AC_LANG_CONFTEST([AC_LANG_SOURCE([[
void foo (void) { volatile char a[8]; a[3]; }
]])])
[# `$CC -c -o ...' might not be portable.  But, oh, well...  Is calling
# `ac_compile' like this correct, after all?
if eval "$ac_compile -S -fstack-protector -o conftest.s" 2> /dev/null; then]
  AC_MSG_RESULT([yes])
  [# Should we clear up other files as well, having called `AC_LANG_CONFTEST'?
  rm -f conftest.s
else
  ssp_possible=no]
  AC_MSG_RESULT([no])
[fi]
[# Strong stack smashing protector.
ssp_strong_possible=yes]
AC_MSG_CHECKING([whether `$CC' accepts `-fstack-protector-strong'])
# Is this a reliable test case?
AC_LANG_CONFTEST([AC_LANG_SOURCE([[
void foo (void) { volatile char a[8]; a[3]; }
]])])
[# `$CC -c -o ...' might not be portable.  But, oh, well...  Is calling
# `ac_compile' like this correct, after all?
if eval "$ac_compile -S -fstack-protector-strong -o conftest.s" 2> /dev/null; then]
  AC_MSG_RESULT([yes])
  [# Should we clear up other files as well, having called `AC_LANG_CONFTEST'?
  rm -f conftest.s
else
  ssp_strong_possible=no]
  AC_MSG_RESULT([no])
[fi]
[# Global stack smashing protector.
ssp_global_possible=yes]
AC_MSG_CHECKING([whether `$CC' accepts `-mstack-protector-guard=global'])
# Is this a reliable test case?
AC_LANG_CONFTEST([AC_LANG_SOURCE([[
void foo (void) { volatile char a[8]; a[3]; }
]])])
[# `$CC -c -o ...' might not be portable.  But, oh, well...  Is calling
# `ac_compile' like this correct, after all?
if eval "$ac_compile -S -fstack-protector -mstack-protector-guard=global -o conftest.s" 2> /dev/null; then]
  AC_MSG_RESULT([yes])
  [# Should we clear up other files as well, having called `AC_LANG_CONFTEST'?
  rm -f conftest.s
else
  ssp_global_possible=no]
  AC_MSG_RESULT([no])
[fi]
])

dnl Check if the C compiler supports `-mstack-arg-probe' (Cygwin).
AC_DEFUN([grub_CHECK_STACK_ARG_PROBE],[
[# Smashing stack arg probe.
sap_possible=yes]
AC_MSG_CHECKING([whether `$CC' accepts `-mstack-arg-probe'])
AC_LANG_CONFTEST([AC_LANG_SOURCE([[
void foo (void) { volatile char a[8]; a[3]; }
]])])
[if eval "$ac_compile -S -mstack-arg-probe -Werror -o conftest.s" 2> /dev/null; then]
  AC_MSG_RESULT([yes])
  [# Should we clear up other files as well, having called `AC_LANG_CONFTEST'?
  rm -f conftest.s
else
  sap_possible=no]
  AC_MSG_RESULT([no])
[fi]
])

dnl Check if ln -s can handle directories properly (mingw).
AC_DEFUN([grub_CHECK_LINK_DIR],[
AC_MSG_CHECKING([whether ln -s can handle directories properly])
[mkdir testdir 2>/dev/null
case $srcdir in
[\\/$]* | ?:[\\/]* ) reldir=$srcdir/include/grub/util ;;
    *) reldir=../$srcdir/include/grub/util ;;
esac
if ln -s $reldir testdir/util 2>/dev/null && rm -f testdir/util 2>/dev/null ; then]
  AC_MSG_RESULT([yes])
  [link_dir=yes
else
  link_dir=no]
  AC_MSG_RESULT([no])
[fi
rm -rf testdir]
])

dnl Check if the C compiler supports `-fPIE'.
AC_DEFUN([grub_CHECK_PIE],[
[# Position independent executable.
pie_possible=yes]
AC_MSG_CHECKING([whether `$CC' has `-fPIE' as default])
# Is this a reliable test case?
AC_LANG_CONFTEST([AC_LANG_SOURCE([[
#ifdef __PIE__
int main() {
	return 0;
}
#else
#error NO __PIE__ DEFINED
#endif
]])])

[# `$CC -c -o ...' might not be portable.  But, oh, well...  Is calling
# `ac_compile' like this correct, after all?
if eval "$ac_compile -S -o conftest.s" 2> /dev/null; then]
  AC_MSG_RESULT([yes])
  [# Should we clear up other files as well, having called `AC_LANG_CONFTEST'?
  rm -f conftest.s
else
  pie_possible=no]
  AC_MSG_RESULT([no])
[fi]
])

AC_DEFUN([grub_CHECK_LINK_PIE],[
[# Position independent executable.
link_nopie_needed=no]
AC_MSG_CHECKING([whether linker needs disabling of PIE to work])
AC_LANG_CONFTEST([AC_LANG_SOURCE([[]])])

[if eval "$ac_compile -Wl,-r -nostdlib -Werror -o conftest.o" 2> /dev/null; then]
  AC_MSG_RESULT([no])
  [# Should we clear up other files as well, having called `AC_LANG_CONFTEST'?
  rm -f conftest.o
else
  link_nopie_needed=yes]
  AC_MSG_RESULT([yes])
[fi]
])


dnl Check if the Linker supports `-no-pie'.
AC_DEFUN([grub_CHECK_NO_PIE],
[AC_MSG_CHECKING([whether linker accepts -no-pie])
AC_CACHE_VAL(grub_cv_cc_ld_no_pie,
[save_LDFLAGS="$LDFLAGS"
LDFLAGS="$LDFLAGS -no-pie -nostdlib -Werror"
AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[]])],
	       [grub_cv_cc_ld_no_pie=yes],
	       [grub_cv_cc_ld_no_pie=no])
LDFLAGS="$save_LDFLAGS"
])
AC_MSG_RESULT([$grub_cv_cc_ld_no_pie])
nopie_possible=no
if test "x$grub_cv_cc_ld_no_pie" = xyes ; then
  nopie_possible=yes
fi
])

AC_DEFUN([grub_CHECK_NO_PIE_ONEWORD],
[AC_MSG_CHECKING([whether linker accepts -nopie])
AC_CACHE_VAL(grub_cv_cc_ld_no_pie_oneword,
[save_LDFLAGS="$LDFLAGS"
LDFLAGS="$LDFLAGS -nopie -nostdlib -Werror"
AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[]])],
	       [grub_cv_cc_ld_no_pie_oneword=yes],
	       [grub_cv_cc_ld_no_pie_oneword=no])
LDFLAGS="$save_LDFLAGS"
])
AC_MSG_RESULT([$grub_cv_cc_ld_no_pie_oneword])
nopie_oneword_possible=no
if test "x$grub_cv_cc_ld_no_pie_oneword" = xyes ; then
  nopie_oneword_possible=yes
fi
])

dnl Check if the C compiler supports `-fPIC'.
AC_DEFUN([grub_CHECK_PIC],[
[# Position independent executable.
pic_possible=yes]
AC_MSG_CHECKING([whether `$CC' has `-fPIC' as default])
# Is this a reliable test case?
AC_LANG_CONFTEST([AC_LANG_SOURCE([[
#ifdef __PIC__
int main() {
	return 0;
}
#else
#error NO __PIC__ DEFINED
#endif
]])])

[# `$CC -c -o ...' might not be portable.  But, oh, well...  Is calling
# `ac_compile' like this correct, after all?
if eval "$ac_compile -S -o conftest.s" 2> /dev/null; then]
  AC_MSG_RESULT([yes])
  [# Should we clear up other files as well, having called `AC_LANG_CONFTEST'?
  rm -f conftest.s
else
  pic_possible=no]
  AC_MSG_RESULT([no])
[fi]
])

dnl Create an output variable with the transformed name of a GRUB utility
dnl program.
AC_DEFUN([grub_TRANSFORM],[dnl
AC_SUBST(AS_TR_SH([$1]), [`AS_ECHO([$1]) | sed "$program_transform_name"`])dnl
])
