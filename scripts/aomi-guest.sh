#!/bin/sh
# Planted as /boot/home/aomitest. The host watches /dev/dprintf for "aomi:".
echo "aomi: start" > /dev/dprintf
screenmode 1280 800 32 >/dev/null 2>&1
ifconfig /dev/net/virtio/0 inet 10.0.2.15 netmask 255.255.255.0 up >/boot/home/ifconfig.txt 2>&1
route add default 10.0.2.2 >/boot/home/route.txt 2>&1
mkdir -p /boot/system/settings/network
printf 'nameserver 10.0.2.3\n' > /boot/system/settings/network/resolv.conf
echo "aomi: net-set" > /dev/dprintf

export AOMI_LOG=/boot/home/aomi-events.log
rm -f "$AOMI_LOG"
AOMI=/boot/system/apps/Aomi
HELPER=/boot/home/aomi-test-helper
chmod +x "$HELPER" 2>/dev/null || true

# 1. Light mode scenes
"$AOMI" "https://example.com" >/boot/home/aomi.txt 2>&1 &
AOMI_PID=$!
sleep 14
echo "aomi: shot page" > /dev/dprintf
sleep 1

"$AOMI" aomi:newtab >/dev/null 2>&1
sleep 3
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
sleep 1

"$AOMI" --menu >/dev/null 2>&1
sleep 2
echo "aomi: shot menu" > /dev/dprintf
sleep 2

"$AOMI" --private >/dev/null 2>&1 &
sleep 5
echo "aomi: shot private" > /dev/dprintf
sleep 1
"$HELPER" quit-private >/dev/null 2>&1 || true
sleep 2
"$AOMI" --close-tab 1 >/dev/null 2>&1
sleep 1

# 2. Theme switch stress test: Light -> Dark -> Light -> Dark (3 switches with page open)
echo "aomi: theme-switch-start pid=$AOMI_PID" > /dev/dprintf
"$HELPER" theme dark >> /boot/home/aomi-helper.txt 2>&1
sleep 3
"$HELPER" theme light >> /boot/home/aomi-helper.txt 2>&1
sleep 3
"$HELPER" theme dark >> /boot/home/aomi-helper.txt 2>&1
sleep 3
if kill -0 "$AOMI_PID" 2>/dev/null; then
	echo "aomi: theme-switch-alive=yes" > /dev/dprintf
else
	echo "aomi: theme-switch-alive=NO" > /dev/dprintf
fi

# 3. Dark mode scenes
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
sleep 1
"$AOMI" --menu >/dev/null 2>&1
sleep 2
echo "aomi: shot dark-menu" > /dev/dprintf
sleep 2
"$AOMI" --private >/dev/null 2>&1 &
sleep 5
echo "aomi: shot dark-private" > /dev/dprintf
sleep 1
"$HELPER" quit-private >/dev/null 2>&1 || true
sleep 2

# Switch back to light for probe pages and tests
"$HELPER" theme light >> /boot/home/aomi-helper.txt 2>&1
sleep 2

# 4. Probe pages + stale-tab close test (open tab 3 while closing middle tab)
"$AOMI" "https://en.wikipedia.org/wiki/Haiku_(operating_system)" >/dev/null 2>&1
sleep 1
"$AOMI" --close-tab 1 >/dev/null 2>&1
sleep 14
echo "aomi: shot wiki" > /dev/dprintf
sleep 1

"$AOMI" "https://github.com/login" >/dev/null 2>&1
sleep 14
echo "aomi: shot github" > /dev/dprintf
sleep 1

"$AOMI" "https://www.haiku-os.org" >/dev/null 2>&1
sleep 14
echo "aomi: shot haiku" > /dev/dprintf
sleep 1

# 5. Private window cookie isolation test
"$AOMI" --private "https://httpbin.org/cookies/set?aomi=private" >/dev/null 2>&1 &
sleep 12
echo "aomi: shot priv-set" > /dev/dprintf
sleep 1
"$AOMI" "https://httpbin.org/cookies" >/dev/null 2>&1
sleep 10
echo "aomi: shot priv-check-normal" > /dev/dprintf
sleep 1
/boot/system/apps/WebPositive "https://httpbin.org/cookies" >/dev/null 2>&1 &
sleep 10
echo "aomi: shot priv-check-webpositive" > /dev/dprintf
sleep 1
killall WebPositive >/dev/null 2>&1 || true
"$HELPER" quit-private >/dev/null 2>&1 || true
sleep 2
"$AOMI" --private "https://httpbin.org/cookies" >/dev/null 2>&1 &
sleep 10
echo "aomi: shot priv-check-newpriv" > /dev/dprintf
sleep 1
"$HELPER" quit-private >/dev/null 2>&1 || true
sleep 1

# Check shared WebPositive cookie jar for any leak of "aomi=private"
if strings /boot/home/config/settings/WebPositive/cookie.jar.db 2>/dev/null | grep -q 'private'; then
	echo "aomi: cookie-jar-leak=YES" > /dev/dprintf
else
	echo "aomi: cookie-jar-leak=NO" > /dev/dprintf
fi

# 6. curl HTTP/2 evidence
/bin/curl -sI -o /dev/null -w "aomi: curl-cloudflare %{http_version} %{http_code}\n" \
	--max-time 25 https://www.cloudflare.com > /dev/dprintf 2>/dev/null

# Dump AOMI_LOG to serial
echo "aomi: events-begin" > /dev/dprintf
cat "$AOMI_LOG" 2>/dev/null | while IFS= read -r line; do
	echo "aomi: evt $line" > /dev/dprintf
done
echo "aomi: events-end" > /dev/dprintf
sync
echo "aomi: done" > /dev/dprintf
