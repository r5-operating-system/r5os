/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * 802.11ax HE Capabilities (extension 35) and HE Operation (extension 36).
 * The field sizes are from IEEE 802.11ax-2021 9.4.2.248 and 9.4.2.249.
 * A short or unknown element is ignored. Nothing here reads past avail.
 */

#include "opt_inet.h"
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_he.h>

/*
 * What this station advertises. It lives in ic_spare, which is
 * uint64_t[7] (56 bytes). The struct is 22 bytes.
 */
struct ieee80211_he_self {
	uint8_t		enabled;
	uint8_t		mac[6];
	uint8_t		phy[11];
	uint16_t	rx_mcs;
	uint16_t	tx_mcs;
};

_Static_assert(sizeof(struct ieee80211_he_self) <=
	sizeof(((struct ieee80211com *)0)->ic_spare),
	"HE state has to fit in ic_spare");

/* Fixed HE Capabilities body: extension id, 6 mac, 11 phy, 4 MCS bytes. */
#define HE_CAP_FIXED		22


static struct ieee80211_he_self *
he_self(struct ieee80211com *ic)
{
	return (struct ieee80211_he_self *)ic->ic_spare;
}


static int
he_room(const uint8_t *ie, int avail, int need)
{
	if (ie == NULL || avail < 2)
		return 0;
	if ((int)ie[1] < need)
		return 0;
	if (avail < 2 + (int)ie[1])
		return 0;
	return 1;
}


static uint16_t
he_le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}


void
ieee80211_parse_hecap(struct ieee80211_node *ni, const uint8_t *ie, int avail)
{
	int has160;

	ni->ni_flags &= ~IEEE80211_NODE_HE;
	ni->ni_he_cap_ok = 0;
	/* 255 is the extension element. 35 is HE Capabilities. */
	/* 22 is the extension id, 6 MAC bytes, 11 PHY bytes and the 80 MHz MCS map. */
	if (!he_room(ie, avail, 22) || ie[0] != 255 || ie[2] != 35)
		return;

	memcpy(ni->ni_he_mac, ie + 3, 6);
	memcpy(ni->ni_he_phy, ie + 9, 11);
	ni->ni_he_rx_mcs80 = he_le16(ie + 20);
	ni->ni_he_tx_mcs80 = he_le16(ie + 22);
	ni->ni_he_rx_mcs160 = 0xffff;
	ni->ni_he_tx_mcs160 = 0xffff;
	/* Bit 3 of the first PHY byte is 160 MHz. The next four bytes are
	 * that MCS map, and only when the element is long enough.
	 */
	has160 = (ni->ni_he_phy[0] & 0x08) != 0;
	if (has160 && ie[1] >= 26) {
		ni->ni_he_rx_mcs160 = he_le16(ie + 24);
		ni->ni_he_tx_mcs160 = he_le16(ie + 26);
	}
	ni->ni_he_cap_ok = 1;
	/* The element is stored either way. The station is HE only when
	 * this image is sending HE. A beacon must not turn that on.
	 */
	if (ni->ni_ic != NULL && ieee80211_he_is_on(ni->ni_ic))
		ni->ni_flags |= IEEE80211_NODE_HE;
}


void
ieee80211_parse_heop(struct ieee80211_node *ni, const uint8_t *ie, int avail)
{
	uint32_t params;

	ni->ni_he_op_ok = 0;
	/* 36 is HE Operation. The fixed part is 3 parameter bytes, the
	 * BSS color, and the basic MCS map.
	 */
	/* 7 is the extension id, 3 parameter bytes, the color and the basic MCS map. */
	if (!he_room(ie, avail, 7) || ie[0] != 255 || ie[2] != 36)
		return;

	params = (uint32_t)ie[3] | ((uint32_t)ie[4] << 8) | ((uint32_t)ie[5] << 16);
	ni->ni_he_pe = (uint8_t)(params & 0x7);
	ni->ni_he_rts = (uint16_t)((params >> 4) & 0x3ff);
	ni->ni_he_er_su = (uint8_t)((params >> 16) & 0x1);
	ni->ni_he_bss_color = (uint8_t)(ie[6] & 0x3f);
	if ((ie[6] & 0x80) != 0)
		ni->ni_he_bss_color = 0;
	ni->ni_he_op_ok = 1;
}


void
ieee80211_he_update(struct ieee80211_node *ni)
{
	const uint8_t *cap = ni->ni_ies.hecap_ie;
	const uint8_t *op = ni->ni_ies.heop_ie;

	ni->ni_flags &= ~IEEE80211_NODE_HE;
	ni->ni_he_cap_ok = 0;
	ni->ni_he_op_ok = 0;
	memset(ni->ni_he_mac, 0, sizeof(ni->ni_he_mac));
	memset(ni->ni_he_phy, 0, sizeof(ni->ni_he_phy));
	ni->ni_he_rx_mcs80 = 0xffff;
	ni->ni_he_tx_mcs80 = 0xffff;
	ni->ni_he_rx_mcs160 = 0xffff;
	ni->ni_he_tx_mcs160 = 0xffff;
	ni->ni_he_pe = 0;
	ni->ni_he_rts = 0;
	ni->ni_he_bss_color = 0;
	ni->ni_he_er_su = 0;
	if (cap != NULL)
		ieee80211_parse_hecap(ni, cap, 2 + cap[1]);
	if (op != NULL)
		ieee80211_parse_heop(ni, op, 2 + op[1]);
}


int
ieee80211_he_is_on(const struct ieee80211com *ic)
{
	const struct ieee80211_he_self *self;

	if (ic == NULL)
		return 0;
	self = (const struct ieee80211_he_self *)ic->ic_spare;
	return self->enabled != 0;
}


void
ieee80211_he_set(struct ieee80211com *ic, const uint8_t mac[6],
	const uint8_t phy[11], uint16_t rx_mcs, uint16_t tx_mcs)
{
	struct ieee80211_he_self *self = he_self(ic);

	memset(self, 0, sizeof(*self));
	memcpy(self->mac, mac, 6);
	memcpy(self->phy, phy, 11);
	self->rx_mcs = rx_mcs;
	self->tx_mcs = tx_mcs;
	self->enabled = 1;
}


void
ieee80211_he_clear(struct ieee80211com *ic)
{
	memset(he_self(ic), 0, sizeof(struct ieee80211_he_self));
}


uint8_t *
ieee80211_add_hecap(uint8_t *frm, struct ieee80211_node *ni)
{
	const struct ieee80211_he_self *self;
	int extra = 0;
	int n;

	if (!ieee80211_he_is_on(ni->ni_ic))
		return frm;
	self = (const struct ieee80211_he_self *)ni->ni_ic->ic_spare;
	if ((self->phy[0] & IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G) != 0)
		extra += 4;
	if ((self->phy[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G) != 0)
		extra += 4;
	n = HE_CAP_FIXED + extra;
	frm[0] = IEEE80211_ELEMID_EXTFIELD;
	frm[1] = (uint8_t)n;
	frm[2] = IEEE80211_ELEMID_EXT_HE_CAPA;
	memcpy(frm + 3, self->mac, 6);
	memcpy(frm + 9, self->phy, 11);
	/* PPE Thresholds Present is PHY byte 6, bit 7. The element would
	 * then need a variable tail, and this builder does not add one.
	 */
	frm[15] &= (uint8_t)~0x80;
	frm[20] = (uint8_t)self->rx_mcs;
	frm[21] = (uint8_t)(self->rx_mcs >> 8);
	frm[22] = (uint8_t)self->tx_mcs;
	frm[23] = (uint8_t)(self->tx_mcs >> 8);
	if (extra >= 4)
		memcpy(frm + 24, frm + 20, 4);
	if (extra == 8)
		memcpy(frm + 28, frm + 20, 4);
	return frm + 2 + n;
}
