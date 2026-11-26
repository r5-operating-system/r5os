#!/bin/sh
# Planted as /boot/home/aomitest. The host watches /dev/dprintf for "aomi:".
echo "aomi: start" > /dev/dprintf
screenmode 1280 800 32 >/dev/null 2>&1
# QEMU user-net. The well-known address is used when DHCP has not answered.
ifconfig /dev/net/virtio/0 inet 10.0.2.15 netmask 255.255.255.0 up >/boot/home/ifconfig.txt 2>&1
route add default 10.0.2.2 >/boot/home/route.txt 2>&1
mkdir -p /boot/system/settings/network
printf 'nameserver 10.0.2.3\n' > /boot/system/settings/network/resolv.conf
ifconfig /dev/net/virtio/0 > /dev/dprintf 2>&1
echo "aomi: net-set" > /dev/dprintf
AOMI=/boot/system/apps/Aomi
"$AOMI" "https://example.com" >/boot/home/aomi.txt 2>&1 &
sleep 16
echo "aomi: shot page" > /dev/dprintf
sleep 2
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
"$AOMI" --private >/dev/null 2>&1
sleep 3
echo "aomi: shot private" > /dev/dprintf
sleep 1
"$AOMI" --menu >/dev/null 2>&1
sleep 2
echo "aomi: shot menu" > /dev/dprintf
sleep 3
/bin/curl -sI -o /dev/null -w "aomi: curl-cloudflare %{http_version} %{http_code}\n" \
	--max-time 25 https://www.cloudflare.com > /dev/dprintf 2>/dev/null
/bin/curl -s --max-time 25 https://http2.pro/api/v1 > /boot/home/http2.txt 2>/dev/null
head -c 200 /boot/home/http2.txt > /dev/dprintf
echo "" > /dev/dprintf
echo "aomi: curl-done" > /dev/dprintf
"$AOMI" --dark >/dev/null 2>&1
sleep 3
echo "aomi: shot dark-page" > /dev/dprintf
sleep 1
"$AOMI" --settings >/dev/null 2>&1
sleep 2
echo "aomi: shot dark-settings" > /dev/dprintf
"$AOMI" --private >/dev/null 2>&1
sleep 3
echo "aomi: shot dark-private" > /dev/dprintf
"$AOMI" "https://en.wikipedia.org/wiki/Haiku_(operating_system)" >/dev/null 2>&1
sleep 14
echo "aomi: shot wiki" > /dev/dprintf
"$AOMI" "https://github.com/login" >/dev/null 2>&1
sleep 14
echo "aomi: shot github" > /dev/dprintf
"$AOMI" "https://www.haiku-os.org" >/dev/null 2>&1
sleep 14
echo "aomi: shot haiku" > /dev/dprintf
echo "aomi: done" > /dev/dprintf
