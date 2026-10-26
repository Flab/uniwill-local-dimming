#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail
[ "$(id -u)" = 0 ] || { echo "run as root: sudo $0" >&2; exit 1; }
VER=0.1.0
systemctl disable --now uniwill-local-dimming.service 2>/dev/null
rmmod uniwill_local_dimming 2>/dev/null
rm -f /etc/systemd/system/uniwill-local-dimming.service /usr/local/sbin/uniwill-local-dimming-apply
systemctl daemon-reload
dkms remove -m uniwill-local-dimming -v $VER --all 2>/dev/null
rm -rf /usr/src/uniwill-local-dimming-$VER
echo "removed (config /etc/default/uniwill-local-dimming kept)"
