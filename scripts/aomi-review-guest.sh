#!/bin/sh
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
#
# Planted as /boot/home/aomitest by scripts/qemu-aomi.py (AOMI_GUEST, with
# AOMI_LINK_OFF=1). Aomi starts without network, the cable goes in, then
# typing, Downloads, Settings, dark mode and a cookie wall. The same steps
# run on an older Aomi: flags it does not know start with "-" and are
# ignored there.
mkdir /tmp/aomitest.lock 2>/dev/null || exit 0
say() { echo "aomi: $*" > /dev/dprintf; }
say start
sleep 4
screenmode 1280 800 32 >/dev/null 2>&1

export AOMI_LOG=/boot/home/aomi-events.log
rm -f "$AOMI_LOG"
AOMI=/boot/system/apps/Aomi
RESOLV=/boot/system/settings/network/resolv.conf

wait_log() {
	i=0
	while [ "$i" -lt "$2" ]; do
		grep -q "$1" "$AOMI_LOG" 2>/dev/null && return 0
		sleep 1
		i=$((i + 1))
	done
	return 1
}

# Window content starts at 14, 32 (AomiWindow::_Frame at 1280 x 800); the
# address field is the middle of the toolbar row.
FIELD_X=420
FIELD_Y=86

# 1. No network: the interface is down and nothing set a name server (as
# before joining a Wi-Fi network). With AOMI_LINK_OFF=1 the cable is out
# as well.
NIC=/dev/net/virtio/0
ifconfig $NIC down
rm -f "$RESOLV"
say "net-before $(ifconfig 2>&1 | grep -c 'inet addr')"
"$AOMI" "https://example.com" >/boot/home/aomi.txt 2>&1 &
wait_log "title tab=0" 300 || say "no-title-yet"
sleep 8
say shot 01-offline

# 2. The cable goes in. The DHCP client writes resolv.conf; when it does
# not within 60 s, set the QEMU usernet values by hand (that also writes
# the file, which is what Aomi's process has to notice).
say link on
ifconfig $NIC up
ifconfig $NIC auto-config
i=0
while [ "$i" -lt 60 ]; do
	grep -q nameserver "$RESOLV" 2>/dev/null && break
	sleep 1
	i=$((i + 1))
done
if grep -q nameserver "$RESOLV" 2>/dev/null; then
	say "dhcp=ok after ${i}s"
else
	say "dhcp=none, by hand"
	ifconfig /dev/net/virtio/0 inet 10.0.2.15 netmask 255.255.255.0 up
	route add default 10.0.2.2
	mkdir -p /boot/system/settings/network
	printf 'nameserver 10.0.2.3\n' > "$RESOLV"
fi
sleep 40
say shot 02-online-no-reload
"$AOMI" --reload >/dev/null 2>&1
sleep 30
say shot 03-online-reload
grep "title tab=0" "$AOMI_LOG" | tail -n 3 | while IFS= read -r line; do
	say "evt $line"
done

# 3. Typing in the address field.
say click $FIELD_X $FIELD_Y
sleep 2
say keys example.org
sleep 3
say shot 04-typed
say key ret
sleep 25
say shot 05-typed-loaded
# The same without frosted glass (CPU look of the toolbar).
r5-effects-test frosted off >/boot/home/effects.txt 2>&1
sleep 3
say click $FIELD_X $FIELD_Y
sleep 2
say keys wikipedia.org
sleep 3
say shot 04b-typed-no-glass
say key esc
r5-effects-test frosted on >>/boot/home/effects.txt 2>&1
sleep 2

# 4. Downloads: one small file that finishes, one large one that runs.
"$AOMI" --download "https://proof.ovh.net/files/1Mb.dat" >/dev/null 2>&1
"$AOMI" --download "https://proof.ovh.net/files/100Mb.dat" >/dev/null 2>&1
sleep 12
say click 1207 $FIELD_Y
# Under Lavapipe a new window takes about ten seconds to be drawn.
sleep 14
say shot 06-downloads-button
say key esc
sleep 2
"$AOMI" --downloads --downloads-window >/dev/null 2>&1
sleep 6
say shot 07-downloads

# 5. Settings, every page (an older Aomi shows its one page each time).
"$AOMI" --settings >/dev/null 2>&1
sleep 14
say shot 08-settings
for page in search appearance downloads privacy; do
	"$AOMI" --settings "--settings-page=$page" >/dev/null 2>&1
	sleep 5
	say shot "09-settings-$page"
done
"$AOMI" --close-settings >/dev/null 2>&1
sleep 2

# 6. Dark mode, with a page that has a dark look of its own.
"$AOMI" --go "data:text/html,<style>body{background:%23fff;color:%23000;font:24px sans-serif}@media(prefers-color-scheme:dark){body{background:%23123;color:%23eee}}</style><p>This page follows light or dark mode." >/dev/null 2>&1
sleep 8
say shot 10a-light-page
r5-effects-test theme mode dark >/boot/home/theme.txt 2>&1
sleep 8
say shot 10-dark-page
"$AOMI" --settings >/dev/null 2>&1
sleep 14
say shot 11-dark-settings
"$AOMI" --close-settings >/dev/null 2>&1
"$AOMI" --downloads --downloads-window >/dev/null 2>&1
sleep 14
say shot 12-dark-downloads
r5-effects-test theme mode light >>/boot/home/theme.txt 2>&1
sleep 4

# 6b. A page that links to a file (a download) and to an e-mail address
# (another program). The page clicks its own link.
"$AOMI" --go "data:text/html,<a id=a href='https://github.com/r5-operating-system/servo-haiku/archive/refs/heads/main.zip'>zip</a><script>setTimeout(function(){document.getElementById('a').click()},800)</script>" >/dev/null 2>&1
sleep 20
say shot 12b-link-download
"$AOMI" --go "data:text/html,<a id=a href='mailto:test@example.com'>mail</a><script>setTimeout(function(){document.getElementById('a').click()},800)</script>" >/dev/null 2>&1
sleep 16
say shot 12c-link-mailto
say key esc
ls -la /boot/home/Desktop | grep -i "zip\|dat" | while IFS= read -r line; do
	say "desktop $line"
done

# 7. A cookie wall (tweakers.net sends the browser to DPG Media's consent
# page first).
"$AOMI" --go "https://tweakers.net/" >/dev/null 2>&1
sleep 60
say shot 13-tweakers
grep -i -E "error|warn|console|uncaught|exception" /boot/home/aomi.txt \
	| grep -v -i "fontconfig" | tail -n 40 | while IFS= read -r line; do
	say "err $line"
done

# 8. A normal page.
"$AOMI" --go "https://en.wikipedia.org/wiki/Haiku_(operating_system)" \
	>/dev/null 2>&1
sleep 30
say shot 14-wiki

# 9. Crashes in the syslog.
grep -i -E "debug_server|crash|Aomi.*(signal|vm_page_fault)|team .* killed" \
	/var/log/syslog | tail -n 20 | while IFS= read -r line; do
	say "syslog $line"
done
cat "$AOMI_LOG" | tail -n 60 | while IFS= read -r line; do
	say "evt $line"
done
sync
say done
