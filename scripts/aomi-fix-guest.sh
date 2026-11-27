#!/bin/sh
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
#
# Planted as /boot/home/aomitest by scripts/qemu-aomi.py (AOMI_GUEST, with
# AOMI_LINK_OFF=1) for the fix round of the Aomi review (PR 4707): Aomi
# starts without network and recovers, typed text, the same link twice,
# the Downloads and Settings windows, Downloads closed while a download
# starts, Quit during a download, and the syslog.
mkdir /tmp/aomitest.lock 2>/dev/null || exit 0
say() { echo "aomi: $*" > /dev/dprintf; }
say start
sleep 4
screenmode 1280 800 32 >/dev/null 2>&1

export AOMI_LOG=/boot/home/aomi-events.log
rm -f "$AOMI_LOG"
AOMI=/boot/system/apps/Aomi
SIG=application/x-vnd.R5-Aomi
RESOLV=/boot/system/settings/network/resolv.conf
DESK=/boot/home/Desktop
FIELD_X=420
FIELD_Y=86

wait_log() {
	i=0
	while [ "$i" -lt "$2" ]; do
		grep -q "$1" "$AOMI_LOG" 2>/dev/null && return 0
		sleep 1
		i=$((i + 1))
	done
	return 1
}

running() { roster 2>/dev/null | grep -c "$SIG"; }

desktop() {
	ls -l "$DESK" 2>/dev/null | grep -i -E "dat|part" | while IFS= read -r line; do
		say "desktop $1 $line"
	done
}

# 1. No network at the start, then the cable goes in.
NIC=/dev/net/virtio/0
ifconfig $NIC down
rm -f "$RESOLV"
"$AOMI" "https://example.com" >/boot/home/aomi.txt 2>&1 &
wait_log "title tab=0" 300 || say "no-title-yet"
sleep 8
say shot 01-offline
say link on
ifconfig $NIC up
ifconfig $NIC auto-config
i=0
while [ "$i" -lt 60 ]; do
	grep -q nameserver "$RESOLV" 2>/dev/null && break
	sleep 1
	i=$((i + 1))
done
say "dhcp after ${i}s"
cat "$RESOLV" 2>/dev/null | while IFS= read -r line; do
	say "resolv $line"
done
# No Reload: the tab has to load again by itself.
wait_log "title tab=0 url=https://example.com/ title=Example Domain" 120 	&& say "recovered by itself" || say "NOT recovered"
sleep 5
say shot 02-online-recovered
grep -E "network back|title tab=0" "$AOMI_LOG" | tail -n 4 | while IFS= read -r line; do
	say "evt $line"
done

# 2. Typed text in the address field.
say click $FIELD_X $FIELD_Y
sleep 2
say keys example.org
sleep 3
say shot 03-typed
say key esc
sleep 2

# 3. The same link twice at once: two files, both complete.
rm -f "$DESK"/10Mb*
"$AOMI" --download "https://proof.ovh.net/files/10Mb.dat" >/dev/null 2>&1
"$AOMI" --download "https://proof.ovh.net/files/10Mb.dat" >/dev/null 2>&1
i=0
while [ "$i" -lt 120 ]; do
	[ "$(grep -c '10Mb.*(ok)' "$AOMI_LOG")" -ge 2 ] && break
	sleep 1
	i=$((i + 1))
done
sleep 10
say shot 04-downloads-twice
desktop twice
for f in "$DESK"/10Mb*; do
	[ -f "$f" ] && say "sum $(sha256sum "$f" 2>&1 || md5sum "$f" 2>&1)"
done

# 4. The Downloads and Settings windows.
"$AOMI" --downloads --downloads-window >/dev/null 2>&1
sleep 20
say shot 05-downloads-window
"$AOMI" --settings >/dev/null 2>&1
sleep 25
say shot 06-settings
"$AOMI" --settings "--settings-page=appearance" >/dev/null 2>&1
sleep 10
say shot 07-settings-appearance
"$AOMI" --close-settings >/dev/null 2>&1
sleep 3

# 5. Downloads closed (Escape) while a download starts, which opens it
# again. Aomi must still answer afterwards.
"$AOMI" --downloads --downloads-window >/dev/null 2>&1
sleep 6
"$AOMI" --download "https://proof.ovh.net/files/1Mb.dat" >/dev/null 2>&1 &
say key esc
sleep 1
say key esc
sleep 8
"$AOMI" --go "https://example.net" >/dev/null 2>&1
if wait_log "example.net" 60; then
	say "after-close answers"
else
	say "after-close NO ANSWER"
fi
sleep 4
say shot 08-after-close
desktop after-close

# 6. Quit during a download: no ".part" may stay behind.
"$AOMI" --download "https://proof.ovh.net/files/1Gb.dat" >/dev/null 2>&1
sleep 8
desktop before-quit
hey "$SIG" quit >/dev/null 2>&1 &
sleep 6
say shot 09-quit-question
# "Close" is the default button.
say key ret
i=0
while [ "$i" -lt 30 ]; do
	[ "$(running)" = "0" ] && break
	sleep 1
	i=$((i + 1))
done
say "quit running=$(running) after ${i}s"
sleep 2
desktop after-quit
ls "$DESK"/*.part >/dev/null 2>&1 && say "part LEFT" || say "part none"
ls /boot/home/config/cache/Aomi 2>/dev/null | while IFS= read -r line; do
	say "cache $line"
done
ls /tmp | grep -c aomi-download | while IFS= read -r line; do
	say "tmp-heads $line"
done

# 7. Crashes in the syslog.
grep -i -E "debug_server|crash|Aomi.*(signal|vm_page_fault)|team .* killed|net_server" \
	/var/log/syslog | tail -n 20 | while IFS= read -r line; do
	say "syslog $line"
done
grep -E "download" "$AOMI_LOG" | tail -n 20 | while IFS= read -r line; do
	say "evt $line"
done
sync
say done
