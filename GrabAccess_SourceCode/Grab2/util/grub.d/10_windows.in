#! /bin/sh
set -e

# grub-mkconfig helper script.
# Copyright (C) 2008,2009,2010  Free Software Foundation, Inc.
#
# GRUB is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# GRUB is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GRUB.  If not, see <http://www.gnu.org/licenses/>.

prefix="@prefix@"
exec_prefix="@exec_prefix@"
datarootdir="@datarootdir@"

export TEXTDOMAIN=@PACKAGE@
export TEXTDOMAINDIR="@localedir@"

. "$pkgdatadir/grub-mkconfig_lib"

case "`uname 2>/dev/null`" in
  CYGWIN*)  ;;
  *) exit 0 ;;
esac

# Try C: even if current system is on other partition.
case "$SYSTEMDRIVE" in
  [Cc]:)     drives="C:"              ;;
  [D-Zd-z]:) drives="C: $SYSTEMDRIVE" ;;
  *) exit 0 ;;
esac

get_os_name_from_boot_ini ()
{
  # Fail if no or more than one partition.
  test "`sed -n 's,^\(\(multi\|scsi\)[^=]*\)=.*$,\1,p' "$1" 2>/dev/null | \
    sort | uniq | wc -l`" = 1 || return 1

  # Search 'default=PARTITION'
  get_os_name_from_boot_ini_part=`sed -n 's,^default=,,p' "$1" | sed 's,\\\\,/,g;s,[ $grub_tab\r]*$,,;1q'`
  test -n "$get_os_name_from_boot_ini_part" || return 1

  # Search 'PARTITION="NAME" ...'
  get_os_name_from_boot_ini_name=`sed -n 's,\\\\,/,g;s,^'"$get_os_name_from_boot_ini_part"'="\([^"]*\)".*$,\1,p' "$1" | sed 1q`
  test -n "$get_os_name_from_boot_ini_name" || return 1

  echo "$get_os_name_from_boot_ini_name"
}


for drv in $drives ; do

  # Convert to Cygwin path.
  dir=`cygpath "$drv"`
  test -n "$dir" || continue

  needmap=
  osid=

  # Check for Vista bootmgr.
  if [ -f "$dir"/bootmgr ] && [ -f "$dir"/boot/bcd ] ; then
    OS="$(gettext "Windows Vista/7 (loader)")"
    osid=bootmgr
  # Check for NTLDR.
  elif [ -f "$dir"/ntldr ] && [ -f "$dir"/ntdetect.com ] && [ -f "$dir"/boot.ini ] ; then
    OS=`get_os_name_from_boot_ini "$dir"/boot.ini` || OS="$(gettext "Windows NT/2000/XP (loader)")"
    osid=ntldr
    needmap=t

  else
    continue
  fi

  # Get boot device.
  dev=`${grub_probe} -t device "$dir" 2>/dev/null` || continue

  gettext_printf "Found %s on %s (%s)\n" "$OS" "$drv" "$dev" >&2
  cat << EOF
menuentry '$(echo "$OS" | grub_quote)' \$menuentry_id_option '$osid-$(grub_get_device_id "${dev}")' {
EOF

  save_default_entry | sed -e 's,^,$grub_tab,'
  prepare_grub_to_access_device "$dev" | sed 's,^,$grub_tab,'
  test -z "$needmap" || cat <<EOF
	drivemap -s (hd0) \$root
EOF
  cat << EOF
	chainloader +1
}
EOF
done

