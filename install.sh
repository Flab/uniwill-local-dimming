#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Install the module through DKMS (signed with the DKMS MOK key on Secure Boot)
# and enable the systemd unit that applies LOCAL_DIMMING at boot.
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "run as root: sudo $0" >&2; exit 1; }

VER=0.1.0
SRC=/usr/src/uniwill-local-dimming-$VER
HERE=$(cd "$(dirname "$0")" && pwd)

install -d "$SRC"
install -m 644 "$HERE/uniwill_local_dimming.c" "$HERE/Makefile" "$HERE/dkms.conf" "$SRC/"

dkms add -m uniwill-local-dimming -v $VER 2>/dev/null || true
dkms build -m uniwill-local-dimming -v $VER
dkms install -m uniwill-local-dimming -v $VER --force

install -m 755 "$HERE/scripts/uniwill-local-dimming-apply" /usr/local/sbin/
[ -e /etc/default/uniwill-local-dimming ] || echo 'LOCAL_DIMMING=0' > /etc/default/uniwill-local-dimming
install -m 644 "$HERE/systemd/uniwill-local-dimming.service" /etc/systemd/system/

# Replace a manually inserted copy, if any.
rmmod uniwill_local_dimming 2>/dev/null || true

systemctl daemon-reload
systemctl enable --now uniwill-local-dimming.service
echo "done: state $(cat /sys/bus/wmi/devices/ABBC0F6F-8EA1-11D1-00A0-C90629100000/local_dimming)"
