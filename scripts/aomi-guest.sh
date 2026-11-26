#!/bin/sh
# Planted as /boot/home/aomitest. The host watches /dev/dprintf for "aomi:".
mkdir /tmp/aomitest.lock 2>/dev/null || exit 0
echo "aomi: start" > /dev/dprintf
sleep 4
screenmode 1280 800 32 >/dev/null 2>&1
ifconfig /dev/net/virtio/0 inet 10.0.2.15 netmask 255.255.255.0 up >/boot/home/ifconfig.txt 2>&1
route add default 10.0.2.2 >/boot/home/route.txt 2>&1
mkdir -p /boot/system/settings/network
printf 'nameserver 10.0.2.3\n' > /boot/system/settings/network/resolv.conf
echo "aomi: net-set" > /dev/dprintf

export GALLIUM_DRIVER=zink
export LIBGL_ALWAYS_SOFTWARE=1
export R5_GL_TRACE=1
export AOMI_LOG=/boot/home/aomi-events.log
rm -f "$AOMI_LOG"

AOMI=/boot/system/apps/Aomi
HELPER=/boot/home/aomi-test-helper
chmod +x "$HELPER" 2>/dev/null || true

wait_log() {
	pat="$1"
	max="$2"
	i=0
	while [ "$i" -lt "$max" ]; do
		if grep -q "$pat" "$AOMI_LOG" 2>/dev/null; then
			return 0
		fi
		sleep 1
		i=$((i + 1))
	done
	return 1
}

# Verify Deskbar Applications symlink and WebPositive still installed
if [ -L "/boot/system/data/deskbar/menu/Applications/Aomi (experimental)" ]; then
	echo "aomi: menu-entry=Aomi (experimental)" > /dev/dprintf
else
	echo "aomi: menu-entry=MISSING" > /dev/dprintf
fi
if [ -x /boot/system/apps/WebPositive ]; then
	echo "aomi: webpositive=installed" > /dev/dprintf
else
	echo "aomi: webpositive=MISSING" > /dev/dprintf
fi

# Render HVIF app icon preview (64, 32, 16 px on white and black)
"$HELPER" icon-preview /boot/home/aomi-icon-preview.ppm >/boot/home/icon-preview.txt 2>&1 || true

# 1. Start Aomi on example.com and wait until first Servo frame + load finished
"$AOMI" "https://example.com" >/boot/home/aomi.txt 2>&1 &
AOMI_PID=$!
wait_log "title tab=0" 150 || true
sleep 3
echo "aomi: shot page" > /dev/dprintf
sleep 1

# Light mode chrome scenes (newtab is native, instant)
"$AOMI" aomi:newtab >/dev/null 2>&1
sleep 2
echo "aomi: shot newtab" > /dev/dprintf
sleep 1

"$AOMI" --downloads >/dev/null 2>&1
sleep 2
echo "aomi: shot downloads" > /dev/dprintf
sleep 1

"$AOMI" --settings >/dev/null 2>&1
sleep 2
echo "aomi: shot settings" > /dev/dprintf
sleep 1
"$AOMI" --close-settings >/dev/null 2>&1
sleep 2

"$AOMI" --menu >/dev/null 2>&1
sleep 2
echo "aomi: shot menu" > /dev/dprintf
sleep 2

"$AOMI" --private >/dev/null 2>&1 &
sleep 4
echo "aomi: shot private" > /dev/dprintf
sleep 1
"$HELPER" quit-private >/dev/null 2>&1 || true
sleep 2

# Close newtab so tab 0 (example.com) is active
"$AOMI" --close-tab 1 >/dev/null 2>&1
sleep 2

# 2. Theme switch stress test: Light -> Dark -> Light -> Dark
echo "aomi: theme-switch-start pid=$AOMI_PID" > /dev/dprintf
"$HELPER" theme dark >> /boot/home/aomi-helper.txt 2>&1
sleep 2
"$HELPER" theme light >> /boot/home/aomi-helper.txt 2>&1
sleep 2
"$HELPER" theme dark >> /boot/home/aomi-helper.txt 2>&1
sleep 2
if kill -0 "$AOMI_PID" 2>/dev/null; then
	echo "aomi: theme-switch-alive=yes" > /dev/dprintf
else
	echo "aomi: theme-switch-alive=NO" > /dev/dprintf
fi

# 3. Dark mode chrome scenes
echo "aomi: shot dark-page" > /dev/dprintf
sleep 1
"$AOMI" aomi:newtab >/dev/null 2>&1
sleep 2
echo "aomi: shot dark-newtab" > /dev/dprintf
sleep 1
"$AOMI" --downloads >/dev/null 2>&1
sleep 2
echo "aomi: shot dark-downloads" > /dev/dprintf
sleep 1
"$AOMI" --settings >/dev/null 2>&1
sleep 2
echo "aomi: shot dark-settings" > /dev/dprintf
sleep 1
"$AOMI" --close-settings >/dev/null 2>&1
sleep 2
"$AOMI" --menu >/dev/null 2>&1
sleep 2
echo "aomi: shot dark-menu" > /dev/dprintf
sleep 2
"$AOMI" --private >/dev/null 2>&1 &
sleep 4
echo "aomi: shot dark-private" > /dev/dprintf
sleep 1
"$HELPER" quit-private >/dev/null 2>&1 || true
sleep 2
"$AOMI" --close-tab 1 >/dev/null 2>&1
sleep 2

# 4. About Aomi & Technical page (dark, then light) in the existing Servo WebView (--go)
"$AOMI" --go aomi:about >/dev/null 2>&1
sleep 5
echo "aomi: shot about-dark" > /dev/dprintf
sleep 1

"$AOMI" --go aomi:technical >/dev/null 2>&1
sleep 5
echo "aomi: shot tech-dark" > /dev/dprintf
sleep 1

"$HELPER" theme light >> /boot/home/aomi-helper.txt 2>&1
sleep 4
echo "aomi: shot tech-light" > /dev/dprintf
sleep 1

"$AOMI" --go aomi:about >/dev/null 2>&1
sleep 5
echo "aomi: shot about-light" > /dev/dprintf
sleep 1

# 5. Font scaling test: 18 pt (page + newtab), then back to 12 pt
"$HELPER" font 18 >> /boot/home/aomi-helper.txt 2>&1
sleep 2
echo "aomi: shot font18-page" > /dev/dprintf
sleep 1
"$AOMI" aomi:newtab >/dev/null 2>&1
sleep 2
echo "aomi: shot font18-newtab" > /dev/dprintf
sleep 1
"$AOMI" --close-tab 1 >/dev/null 2>&1
"$HELPER" font 12 >> /boot/home/aomi-helper.txt 2>&1
sleep 2

# 6. Probe pages in existing Servo WebView (--go) + close middle tab while loading
"$AOMI" aomi:newtab >/dev/null 2>&1
sleep 1
"$AOMI" --go "https://en.wikipedia.org/wiki/Haiku_(operating_system)" >/dev/null 2>&1
"$AOMI" --close-tab 1 >/dev/null 2>&1
"$AOMI" --go "https://en.wikipedia.org/wiki/Haiku_(operating_system)" >/dev/null 2>&1
sleep 16
echo "aomi: shot wiki" > /dev/dprintf
sleep 1

"$AOMI" --go "https://github.com/login" >/dev/null 2>&1
sleep 14
echo "aomi: shot github" > /dev/dprintf
sleep 1

"$AOMI" --go "https://www.haiku-os.org" >/dev/null 2>&1
sleep 14
echo "aomi: shot haiku" > /dev/dprintf
sleep 1

"$AOMI" --go "https://http2.pro/api/v1" >/dev/null 2>&1
sleep 14
echo "aomi: shot http2" > /dev/dprintf
sleep 1

# Lock panel on HTTP/2 site
"$AOMI" --lock >/dev/null 2>&1
sleep 2
echo "aomi: shot lock-h2" > /dev/dprintf
sleep 1

# 7. Functional checks: bookmark, zoom, find, back, forward, reload, stop, download
"$AOMI" --bookmark >/dev/null 2>&1
sleep 1
"$AOMI" --zoom-in >/dev/null 2>&1
sleep 1
"$AOMI" --zoom-out >/dev/null 2>&1
sleep 1
"$AOMI" --zoom-reset >/dev/null 2>&1
sleep 1
"$AOMI" --find >/dev/null 2>&1
sleep 1
"$AOMI" --reload >/dev/null 2>&1
sleep 1
"$AOMI" --stop >/dev/null 2>&1
sleep 1
"$AOMI" --back >/dev/null 2>&1
sleep 2
"$AOMI" --forward >/dev/null 2>&1
sleep 2
"$AOMI" --download "https://example.com/" >/dev/null 2>&1
sleep 4
DL_COUNT=$(ls -1 /boot/home/Desktop/* 2>/dev/null | wc -l)
BM_COUNT=$(ls -1 /boot/home/config/settings/WebPositive/Bookmarks/* 2>/dev/null | wc -l)
echo "aomi: func-check downloads=$DL_COUNT bookmarks=$BM_COUNT" > /dev/dprintf

# 8. Technical JSON check
"$AOMI" --technical-json > /boot/home/aomi-tech.json 2>/dev/null
if [ -s /tmp/aomi-technical.json ]; then
	cp /tmp/aomi-technical.json /boot/home/aomi-tech-live.json
fi
echo "aomi: tech-json-bytes=$(wc -c < /boot/home/aomi-tech.json 2>/dev/null || echo 0)" > /dev/dprintf
if command -v python3 >/dev/null 2>&1; then
	python3 -m json.tool /boot/home/aomi-tech.json >/dev/null 2>&1 && echo "aomi: tech-json-valid=yes" > /dev/dprintf || echo "aomi: tech-json-valid=NO" > /dev/dprintf
fi
head -c 600 /boot/home/aomi-tech.json | tr '\n' ' ' > /tmp/tech-one.txt
echo "aomi: tech-json=$(cat /tmp/tech-one.txt)" > /dev/dprintf

# Dump R5_GL_TRACE / GL renderer lines
grep -i -E 'zink|llvmpipe|r5-servo gl|protocol' /boot/home/aomi.txt 2>/dev/null | head -n 10 | while IFS= read -r line; do
	echo "aomi: gl $line" > /dev/dprintf
done

# Dump AOMI_LOG to serial
echo "aomi: events-begin" > /dev/dprintf
cat "$AOMI_LOG" 2>/dev/null | while IFS= read -r line; do
	echo "aomi: evt $line" > /dev/dprintf
done
echo "aomi: events-end" > /dev/dprintf
sync
echo "aomi: done" > /dev/dprintf
