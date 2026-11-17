/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * HE (802.11ax) element parsing and the association element we send.
 * The caller of a parser guarantees that avail bytes are readable at ie.
 */
#ifndef _NET80211_IEEE80211_HE_H_
#define _NET80211_IEEE80211_HE_H_

struct ieee80211_node;
struct ieee80211com;

void ieee80211_parse_hecap(struct ieee80211_node *ni, const uint8_t *ie,
	int avail);
void ieee80211_parse_heop(struct ieee80211_node *ni, const uint8_t *ie,
	int avail);
void ieee80211_he_update(struct ieee80211_node *ni);
int ieee80211_he_is_on(const struct ieee80211com *ic);
void ieee80211_he_set(struct ieee80211com *ic, const uint8_t mac[6],
	const uint8_t phy[11], uint16_t rx_mcs, uint16_t tx_mcs);
void ieee80211_he_clear(struct ieee80211com *ic);
uint8_t *ieee80211_add_hecap(uint8_t *frm, struct ieee80211_node *ni);

#endif
