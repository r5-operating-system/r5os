/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Host fuzz harness for the Wi-Fi parsers. Build with AddressSanitizer
 * and UBSan (scripts/fuzz-wifi.sh).
 * Usage: wifi-fuzz <target> <seconds>
 * Targets: ies, rsn, action, mtkrx.
 * Prints "wifi_fuzz <target>: <seconds>s 0 crashes" when it ends.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "prelude.h"

int ieee80211_ies_init(struct ieee80211_ies*, const uint8_t*, int);
void ieee80211_ies_expand(struct ieee80211_ies*);
int ieee80211_parse_beacon(struct ieee80211_node*, struct mbuf*,
	struct ieee80211_channel*, struct ieee80211_scanparams*);
void ieee80211_parse_htcap(struct ieee80211_node*, const uint8_t*);
void ieee80211_parse_htinfo(struct ieee80211_node*, const uint8_t*);
void ieee80211_parse_vhtcap(struct ieee80211_node*, const uint8_t*);
void ieee80211_parse_vhtopmode(struct ieee80211_node*, const uint8_t*);
int ieee80211_parse_wmeie(uint8_t*, const struct ieee80211_frame*,
	struct ieee80211_node*);
int ieee80211_parse_wmeparams(struct ieee80211vap*, uint8_t*,
	const struct ieee80211_frame*, uint8_t*);
void ieee80211_parse_csaparams(struct ieee80211vap*, uint8_t*,
	const struct ieee80211_frame*);
int ieee80211_parse_wpa(struct ieee80211vap*, const uint8_t*,
	struct ieee80211_rsnparms*, const struct ieee80211_frame*);
int ieee80211_parse_rsn(struct ieee80211vap*, const uint8_t*,
	struct ieee80211_rsnparms*, const struct ieee80211_frame*);
int ieee80211_parse_action(struct ieee80211_node*, struct mbuf*);
void mtk_receive_frame(struct mtk_softc*, const uint8_t*, size_t, int, int);
void ieee80211_parse_hecap(struct ieee80211_node*, const uint8_t*, int);
void ieee80211_parse_heop(struct ieee80211_node*, const uint8_t*, int);

__attribute__((weak)) int
ieee80211_ies_init(struct ieee80211_ies* ies, const uint8_t* data, int len)
{
	(void)ies;
	(void)data;
	(void)len;
	abort();
	return 0;
}
__attribute__((weak)) void
ieee80211_ies_expand(struct ieee80211_ies* ies) { (void)ies; abort(); }
__attribute__((weak)) int
ieee80211_parse_beacon(struct ieee80211_node* ni, struct mbuf* m,
	struct ieee80211_channel* c, struct ieee80211_scanparams* s)
{
	(void)ni; (void)m; (void)c; (void)s; abort(); return 0;
}
__attribute__((weak)) void
ieee80211_parse_htcap(struct ieee80211_node* ni, const uint8_t* ie)
{ (void)ni; (void)ie; abort(); }
__attribute__((weak)) void
ieee80211_parse_htinfo(struct ieee80211_node* ni, const uint8_t* ie)
{ (void)ni; (void)ie; abort(); }
__attribute__((weak)) void
ieee80211_parse_vhtcap(struct ieee80211_node* ni, const uint8_t* ie)
{ (void)ni; (void)ie; abort(); }
__attribute__((weak)) void
ieee80211_parse_vhtopmode(struct ieee80211_node* ni, const uint8_t* ie)
{ (void)ni; (void)ie; abort(); }
__attribute__((weak)) int
ieee80211_parse_wmeie(uint8_t* frm, const struct ieee80211_frame* wh,
	struct ieee80211_node* ni)
{ (void)frm; (void)wh; (void)ni; abort(); return 0; }
__attribute__((weak)) int
ieee80211_parse_wmeparams(struct ieee80211vap* vap, uint8_t* frm,
	const struct ieee80211_frame* wh, uint8_t* qos)
{ (void)vap; (void)frm; (void)wh; (void)qos; abort(); return 0; }
__attribute__((weak)) void
ieee80211_parse_csaparams(struct ieee80211vap* vap, uint8_t* frm,
	const struct ieee80211_frame* wh)
{ (void)vap; (void)frm; (void)wh; abort(); }
__attribute__((weak)) int
ieee80211_parse_wpa(struct ieee80211vap* vap, const uint8_t* frm,
	struct ieee80211_rsnparms* rsn, const struct ieee80211_frame* wh)
{ (void)vap; (void)frm; (void)rsn; (void)wh; abort(); return 0; }
__attribute__((weak)) int
ieee80211_parse_rsn(struct ieee80211vap* vap, const uint8_t* frm,
	struct ieee80211_rsnparms* rsn, const struct ieee80211_frame* wh)
{ (void)vap; (void)frm; (void)rsn; (void)wh; abort(); return 0; }
__attribute__((weak)) int
ieee80211_parse_action(struct ieee80211_node* ni, struct mbuf* m)
{ (void)ni; (void)m; abort(); return 0; }
__attribute__((weak)) void
mtk_receive_frame(struct mtk_softc* sc, const uint8_t* data, size_t got,
	int which, int slot)
{ (void)sc; (void)data; (void)got; (void)which; (void)slot; abort(); }
__attribute__((weak)) void
ieee80211_parse_hecap(struct ieee80211_node* ni, const uint8_t* ie, int avail)
{ (void)ni; (void)ie; (void)avail; abort(); }
__attribute__((weak)) void
ieee80211_parse_heop(struct ieee80211_node* ni, const uint8_t* ie, int avail)
{ (void)ni; (void)ie; (void)avail; abort(); }

static uint64_t gState = 0x9e3779b97f4a7c15ULL;


static uint32_t
next_word(void)
{
	gState ^= gState << 13;
	gState ^= gState >> 7;
	gState ^= gState << 17;
	return (uint32_t)gState;
}


static int
timed_out(time_t start, int seconds)
{
	return time(NULL) - start >= seconds;
}


static void
fill(uint8_t* buf, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++)
		buf[i] = (uint8_t)next_word();
}


static void
setup(struct ieee80211com* ic, struct ieee80211vap* vap,
	struct ieee80211_node* ni, struct ieee80211_channel* chan)
{
	memset(ic, 0, sizeof(*ic));
	memset(vap, 0, sizeof(*vap));
	memset(ni, 0, sizeof(*ni));
	memset(chan, 0, sizeof(*chan));
	chan->ic_ieee = 6;
	ic->ic_bsschan = chan;
	ic->ic_phytype = (next_word() & 1) ? IEEE80211_T_FH : 0;
	vap->iv_ic = ic;
	vap->iv_flags = IEEE80211_F_WPA1 | IEEE80211_F_WPA2 | IEEE80211_F_DOTH;
	vap->iv_flags_ht = IEEE80211_FHT_HTCOMPAT;
	vap->iv_state = IEEE80211_S_RUN;
	ni->ni_vap = vap;
	ni->ni_ic = ic;
}


static void
fuzz_ies(void)
{
	struct ieee80211com ic;
	struct ieee80211vap vap;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;
	struct ieee80211_scanparams scan;
	struct ieee80211_ies ies;
	struct mbuf m;
	uint8_t* buf;
	size_t n;
	uint8_t qos;

	setup(&ic, &vap, &ni, &chan);
	n = next_word() % 800;
	buf = malloc(n + 1);
	fill(buf, n);
	m.m_data = buf;
	m.m_len = (int)n;
	ieee80211_parse_beacon(&ni, &m, &chan, &scan);
	if (scan.htcap != NULL)
		ieee80211_parse_htcap(&ni, scan.htcap);
	if (scan.htinfo != NULL)
		ieee80211_parse_htinfo(&ni, scan.htinfo);
	if (scan.vhtcap != NULL)
		ieee80211_parse_vhtcap(&ni, scan.vhtcap);
	if (scan.vhtopmode != NULL)
		ieee80211_parse_vhtopmode(&ni, scan.vhtopmode);
	if (scan.wme != NULL && n >= sizeof(struct ieee80211_frame)) {
		ieee80211_parse_wmeie(scan.wme, (struct ieee80211_frame*)buf, &ni);
		ieee80211_parse_wmeparams(&vap, scan.wme,
			(struct ieee80211_frame*)buf, &qos);
	}
	if (scan.csa != NULL)
		ieee80211_parse_csaparams(&vap, scan.csa,
			(struct ieee80211_frame*)buf);

	memset(&ies, 0, sizeof(ies));
	if (n >= 24 + 12) {
		int len = (int)(n - 24 - 12);
		if (ieee80211_ies_init(&ies, buf + 24 + 12, len))
			ieee80211_ies_expand(&ies);
		if (ies.htcap_ie != NULL)
			ieee80211_parse_htcap(&ni, ies.htcap_ie);
		if (ies.htinfo_ie != NULL)
			ieee80211_parse_htinfo(&ni, ies.htinfo_ie);
		if (ies.vhtcap_ie != NULL)
			ieee80211_parse_vhtcap(&ni, ies.vhtcap_ie);
		if (ies.vhtopmode_ie != NULL)
			ieee80211_parse_vhtopmode(&ni, ies.vhtopmode_ie);
	}
	ieee80211_ies_init(&ies, buf, -1);
	ieee80211_ies_init(&ies, NULL, 10);
	ieee80211_ies_init(&ies, buf, 100000);
	if (ies.data != NULL)
		free(ies.data);
	free(buf);
}


static void
fuzz_rsn(void)
{
	struct ieee80211com ic;
	struct ieee80211vap vap;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;
	struct ieee80211_frame wh;
	struct ieee80211_rsnparms rsn;
	uint8_t* buf;
	size_t n;

	setup(&ic, &vap, &ni, &chan);
	memset(&wh, 0, sizeof(wh));
	n = 2 + (next_word() % 250);
	buf = malloc(n);
	fill(buf, n);
	buf[1] = (uint8_t)(n - 2);
	ieee80211_parse_wpa(&vap, buf, &rsn, &wh);
	ieee80211_parse_rsn(&vap, buf, &rsn, &wh);
	free(buf);
}


static void
fuzz_action(void)
{
	struct ieee80211com ic;
	struct ieee80211vap vap;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;
	struct mbuf m;
	uint8_t* buf;
	size_t n;

	setup(&ic, &vap, &ni, &chan);
	n = next_word() % 80;
	buf = malloc(n + 1);
	fill(buf, n);
	m.m_data = buf;
	m.m_len = (int)n;
	ieee80211_parse_action(&ni, &m);
	free(buf);
}


static void
fuzz_he(void)
{
	struct ieee80211com ic;
	struct ieee80211vap vap;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;
	uint8_t* buf;
	int n;

	setup(&ic, &vap, &ni, &chan);
	buf = malloc(300);
	fill(buf, 300);
	n = (int)(next_word() % 300);
	ieee80211_parse_hecap(&ni, buf, n);
	ieee80211_parse_heop(&ni, buf, n);
	ieee80211_parse_hecap(&ni, NULL, 10);
	ieee80211_parse_heop(&ni, buf, 0);
	ieee80211_parse_hecap(&ni, buf, 1);
	free(buf);
}


static void
fuzz_mtkrx(void)
{
	struct mtk_softc sc;
	uint8_t* buf;
	size_t n;

	memset(&sc, 0, sizeof(sc));
	n = next_word() % 400;
	buf = malloc(n + 1);
	fill(buf, n);
	mtk_receive_frame(&sc, buf, n, (int)(next_word() % 5) - 1,
		(int)(next_word() % 8));
	free(buf);
}


int
main(int argc, char** argv)
{
	const char* target;
	int seconds;
	time_t start;
	unsigned rounds = 0;

	if (argc != 3) {
		fprintf(stderr, "usage: wifi-fuzz <target> <seconds>\n");
		return 2;
	}
	target = argv[1];
	seconds = atoi(argv[2]);
	if (seconds < 1)
		seconds = 1;
	start = time(NULL);
	for (;;) {
		if ((rounds & 0x3ff) == 0 && timed_out(start, seconds))
			break;
		if (strcmp(target, "ies") == 0)
			fuzz_ies();
		else if (strcmp(target, "rsn") == 0)
			fuzz_rsn();
		else if (strcmp(target, "action") == 0)
			fuzz_action();
		else if (strcmp(target, "mtkrx") == 0)
			fuzz_mtkrx();
		else if (strcmp(target, "he") == 0)
			fuzz_he();
		else {
			fprintf(stderr, "wifi-fuzz: unknown target %s\n", target);
			return 2;
		}
		rounds++;
	}
	printf("wifi_fuzz %s: %u rounds\n", target, rounds);
	printf("wifi_fuzz %s: %ds 0 crashes\n", target, seconds);
	return 0;
}
