/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Host stand-ins for the Wi-Fi parsers (scripts/fuzz-wifi.sh). The numbers
 * are the frame layout. The functions under test are the ones in distro/.
 */
#ifndef WIFI_HOST_PRELUDE_H
#define WIFI_HOST_PRELUDE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int u_int;
typedef uint8_t u_int8_t;

#define EINVAL 22
#define NBBY 8
#define __offsetof(t, f) offsetof(t, f)

static inline uint16_t le16dec(const void* p)
{
	const uint8_t* b = p;
	return (uint16_t)(b[0] | (b[1] << 8));
}

static inline uint32_t le32dec(const void* p)
{
	const uint8_t* b = p;
	return (uint32_t)b[0] | ((uint32_t)b[1] << 8)
		| ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

#define le16toh(x) (x)

#define IEEE80211_MALLOC(sz, t, f) malloc((size_t)(sz))
#define IEEE80211_FREE(p, t) free(p)
#define M_80211_NODE_IE 1
#define IEEE80211_M_NOWAIT 1
#define IEEE80211_M_ZERO 2
#define M_NOWAIT 0
#define MT_DATA 0
#define M_PKTHDR 0

#define net80211_printf(...) do {} while (0)
#define IEEE80211_DISCARD(...) do {} while (0)
#define IEEE80211_DISCARD_IE(...) do {} while (0)
#define IEEE80211_NOTE(...) do {} while (0)
#define IEEE80211_NOTE_FRAME(...) do {} while (0)
#define IEEE80211_DPRINTF(...) do {} while (0)
#define IEEE80211_NODE_STAT(ni, x) do {} while (0)
#define KASSERT(cond, msg) do {} while (0)
#define IEEE80211_LOCK(ic) do {} while (0)
#define IEEE80211_UNLOCK(ic) do {} while (0)

#define IEEE80211_VERIFY_ELEMENT(__elem, __maxlen, _action) do {	\
	if ((__elem) == NULL) {						\
		vap->iv_stats.is_rx_elem_missing++;			\
		_action;						\
	} else if ((__elem)[1] > (__maxlen)) {				\
		vap->iv_stats.is_rx_elem_toobig++;			\
		_action;						\
	}								\
} while (0)

#define IEEE80211_VERIFY_LENGTH(_len, _minlen, _action) do {		\
	if ((long)(_len) < (long)(_minlen)) {				\
		vap->iv_stats.is_rx_elem_toosmall++;			\
		_action;						\
	}								\
} while (0)

enum {
	IEEE80211_ELEMID_SSID = 0,
	IEEE80211_ELEMID_RATES = 1,
	IEEE80211_ELEMID_FHPARMS = 2,
	IEEE80211_ELEMID_DSPARMS = 3,
	IEEE80211_ELEMID_CFPARMS = 4,
	IEEE80211_ELEMID_TIM = 5,
	IEEE80211_ELEMID_IBSSPARMS = 6,
	IEEE80211_ELEMID_COUNTRY = 7,
	IEEE80211_ELEMID_BSSLOAD = 11,
	IEEE80211_ELEMID_PWRCNSTR = 32,
	IEEE80211_ELEMID_CSA = 37,
	IEEE80211_ELEMID_QUIET = 40,
	IEEE80211_ELEMID_ERP = 42,
	IEEE80211_ELEMID_HTCAP = 45,
	IEEE80211_ELEMID_RSN = 48,
	IEEE80211_ELEMID_XRATES = 50,
	IEEE80211_ELEMID_APCHANREP = 51,
	IEEE80211_ELEMID_HTINFO = 61,
	IEEE80211_ELEMID_EXTCAP = 127,
	IEEE80211_ELEMID_VHT_CAP = 191,
	IEEE80211_ELEMID_VHT_OPMODE = 192,
	IEEE80211_ELEMID_VHT_PWR_ENV = 195,
	IEEE80211_ELEMID_VENDOR = 221
};

#define IEEE80211_RATE_MAXSIZE 15
#define IEEE80211_NWID_LEN 32
#define IEEE80211_BINTVAL_MIN 10
#define IEEE80211_BINTVAL_MAX 10000
#define IEEE80211_T_FH 1
#define IEEE80211_FH_CHAN(a, b) ((uint8_t)(a))
#define IEEE80211_FHT_HTCOMPAT 0x0001
#define IEEE80211_NODE_HTCOMPAT 0x0001
#define IEEE80211_BPARSE_BADIELEN 0x0001
#define IEEE80211_BPARSE_RATES_INVALID 0x0002
#define IEEE80211_BPARSE_XRATES_INVALID 0x0004
#define IEEE80211_BPARSE_SSID_INVALID 0x0008
#define IEEE80211_BPARSE_OFFCHAN 0x0010
#define IEEE80211_BPARSE_BINTVAL_INVALID 0x0020
#define IEEE80211_BPARSE_CSA_INVALID 0x0040

#define WPA_OUI 0xf25000
#define WPA_OUI_TYPE 0x01
#define WPA_VERSION 1
#define WPA_CSE_NULL 0x00
#define WPA_CSE_WEP40 0x01
#define WPA_CSE_TKIP 0x02
#define WPA_CSE_CCMP 0x04
#define WPA_CSE_WEP104 0x05
#define WPA_ASE_NONE 0x00
#define WPA_ASE_8021X_UNSPEC 0x01
#define WPA_ASE_8021X_PSK 0x02
#define WME_OUI 0xf25000
#define WME_OUI_TYPE 0x02
#define WME_INFO_OUI_SUBTYPE 0x00
#define WME_PARAM_OUI_SUBTYPE 0x01
#define ATH_OUI 0x7f0300
#define ATH_OUI_TYPE 0x01
#define BCM_OUI 0x4c9000
#define BCM_OUI_HTCAP 51
#define BCM_OUI_HTINFO 52
#define RSN_OUI 0xac0f00
#define RSN_VERSION 1
#define RSN_CSE_NULL 0
#define RSN_CSE_WEP40 1
#define RSN_CSE_TKIP 2
#define RSN_CSE_WRAP 3
#define RSN_CSE_CCMP 4
#define RSN_CSE_WEP104 5
#define RSN_CSE_GCMP_128 8
#define RSN_ASE_NONE 0
#define RSN_ASE_8021X_UNSPEC 1
#define RSN_ASE_8021X_PSK 2

#define IEEE80211_CIPHER_WEP 0
#define IEEE80211_CIPHER_TKIP 1
#define IEEE80211_CIPHER_AES_OCB 2
#define IEEE80211_CIPHER_AES_CCM 3
#define IEEE80211_CIPHER_NONE 6
#define IEEE80211_CIPHER_AES_GCM_128 12
#define IEEE80211_F_WPA1 0x00800000
#define IEEE80211_F_WPA2 0x01000000
#define IEEE80211_REASON_IE_INVALID 13
#define IEEE80211_REASON_GROUP_CIPHER_INVALID 18
#define IEEE80211_REASON_PAIRWISE_CIPHER_INVALID 19
#define IEEE80211_REASON_UNSUPP_RSN_IE_VERSION 21

#define IEEE80211_ACTION_CAT_BA 3
#define IEEE80211_ACTION_CAT_HT 7
#define IEEE80211_ACTION_CAT_VHT 21
#define IEEE80211_ACTION_BA_ADDBA_REQUEST 0
#define IEEE80211_ACTION_BA_ADDBA_RESPONSE 1
#define IEEE80211_ACTION_BA_DELBA 2
#define IEEE80211_ACTION_HT_TXCHWIDTH 0
#define IEEE80211_ACTION_HT_MIMOPWRSAVE 1

#define IEEE80211_HTINFO_2NDCHAN 0x03
#define IEEE80211_HTINFO_2NDCHAN_S 0
#define IEEE80211_HTINFO_OPMODE 0x03
#define IEEE80211_HTINFO_OPMODE_S 0
#define IEEE80211_HTINFO_BASIC_STBCMCS 0x7f
#define IEEE80211_HTINFO_BASIC_STBCMCS_S 0
#define _IEEE80211_MASKSHIFT(_v, _f) (((_v) & _f) >> _f##_S)
#define _IEEE80211_SHIFTMASK(_v, _f) (((_v) << _f##_S) & _f)

#define WME_NUM_AC 4
#define WME_QOSINFO_COUNT 0x0f
#define WME_CAPINFO_IE_OFFSET 8
#define WME_PARAM_ACM 0x10
#define WME_PARAM_ACM_S 4
#define WME_PARAM_AIFSN 0x0f
#define WME_PARAM_AIFSN_S 0
#define WME_PARAM_LOGCWMIN 0x0f
#define WME_PARAM_LOGCWMIN_S 0
#define WME_PARAM_LOGCWMAX 0xf0
#define WME_PARAM_LOGCWMAX_S 4
#define IEEE80211_CSA_COUNT_MIN 2
#define IEEE80211_F_DOTH 0x0001
#define IEEE80211_F_CSAPENDING 0x0002
#define IEEE80211_CHAN_ALLTURBO 0
#define IEEE80211_CHAN_ALL 0
#define IEEE80211_S_RUN 4

struct ieee80211_ie_htcap {
	uint8_t hc_id;
	uint8_t hc_len;
	uint16_t hc_cap;
	uint8_t hc_param;
	uint8_t hc_mcsset[16];
	uint16_t hc_extcap;
	uint32_t hc_txbf;
	uint8_t hc_antenna;
} __attribute__((packed));

struct ieee80211_ie_htinfo {
	uint8_t hi_id;
	uint8_t hi_len;
	uint8_t hi_ctrlchannel;
	uint8_t hi_byte1;
	uint8_t hi_byte2;
	uint8_t hi_byte3;
	uint16_t hi_byte45;
	uint8_t hi_basicmcsset[16];
} __attribute__((packed));

struct ieee80211_vht_mcs_info {
	uint16_t rx_mcs_map;
	uint16_t rx_highest;
	uint16_t tx_mcs_map;
	uint16_t tx_highest;
} __attribute__((packed));

struct ieee80211_vht_cap {
	uint32_t vht_cap_info;
	struct ieee80211_vht_mcs_info supp_mcs;
} __attribute__((packed));

struct ieee80211_vht_operation {
	uint8_t chan_width;
	uint8_t center_freq_seq0_idx;
	uint8_t center_freq_seq1_idx;
	uint16_t basic_mcs_set;
} __attribute__((packed));

struct ieee80211_wme_acparams {
	uint8_t acp_aci_aifsn;
	uint8_t acp_logcwminmax;
	uint16_t acp_txop;
} __attribute__((packed));

struct ieee80211_wme_param {
	uint8_t param_id;
	uint8_t param_len;
	uint8_t param_oui[3];
	uint8_t param_oui_type;
	uint8_t param_oui_subtype;
	uint8_t param_version;
	uint8_t param_qosInfo;
	uint8_t param_reserved;
	struct ieee80211_wme_acparams params_acParams[WME_NUM_AC];
} __attribute__((packed));

struct wmeParams {
	uint8_t wmep_acm;
	uint8_t wmep_aifsn;
	uint8_t wmep_logcwmin;
	uint8_t wmep_logcwmax;
	uint8_t wmep_txopLimit;
	uint8_t wmep_noackPolicy;
};

struct chanAccParams {
	uint8_t cap_info;
	struct wmeParams cap_wmeParams[WME_NUM_AC];
};

struct ieee80211_wme_state {
	struct chanAccParams wme_wmeChanParams;
};

struct ieee80211_csa_ie {
	uint8_t csa_ie;
	uint8_t csa_len;
	uint8_t csa_mode;
	uint8_t csa_newchan;
	uint8_t csa_count;
} __attribute__((packed));

struct ieee80211_action {
	uint8_t ia_category;
	uint8_t ia_action;
} __attribute__((packed));

struct ieee80211_action_ba_addbarequest {
	struct ieee80211_action rq_header;
	uint8_t rq_dialogtoken;
	uint16_t rq_baparamset;
	uint16_t rq_batimeout;
	uint16_t rq_baseqctl;
} __attribute__((packed));

struct ieee80211_action_ba_addbaresponse {
	struct ieee80211_action rs_header;
	uint8_t rs_dialogtoken;
	uint16_t rs_statuscode;
	uint16_t rs_baparamset;
	uint16_t rs_batimeout;
} __attribute__((packed));

struct ieee80211_action_ba_delba {
	struct ieee80211_action dl_header;
	uint16_t dl_baparamset;
	uint16_t dl_reasoncode;
} __attribute__((packed));

struct ieee80211_action_ht_txchwidth {
	struct ieee80211_action at_header;
	uint8_t at_chwidth;
} __attribute__((packed));

struct ieee80211_action_ht_mimopowersave {
	struct ieee80211_action am_header;
	uint8_t am_control;
} __attribute__((packed));

struct ieee80211_frame {
	uint8_t i_fc[2];
	uint8_t i_dur[2];
	uint8_t i_addr1[6];
	uint8_t i_addr2[6];
	uint8_t i_addr3[6];
	uint8_t i_seq[2];
};

struct mbuf {
	int m_len;
	uint8_t* m_data;
	struct {
		int len;
		void* rcvif;
	} m_pkthdr;
};

#define mtod(m, t) ((t)((m)->m_data))

struct ieee80211_stats {
	int is_rx_elem_missing;
	int is_rx_elem_toobig;
	int is_rx_elem_toosmall;
	int is_rx_elem_unknown;
	int is_rx_chanmismatch;
	int is_rx_badbintval;
	int is_rx_action;
};

struct ieee80211_channel {
	int ic_flags;
	uint8_t ic_ieee;
};

struct ieee80211com {
	int ic_phytype;
	int ic_flags;
	int ic_csa_count;
	int ic_csa_mode;
	struct ieee80211_channel* ic_csa_newchan;
	struct ieee80211_channel* ic_bsschan;
	struct ieee80211_wme_state ic_wme;
};

struct ieee80211vap {
	struct ieee80211com* iv_ic;
	int iv_flags;
	int iv_flags_ht;
	int iv_state;
	int iv_debug;
	struct ieee80211_stats iv_stats;
};

struct ieee80211_ies {
	uint8_t* wpa_ie;
	uint8_t* rsn_ie;
	uint8_t* wme_ie;
	uint8_t* ath_ie;
	uint8_t* htcap_ie;
	uint8_t* htinfo_ie;
	uint8_t* tdma_ie;
	uint8_t* meshid_ie;
	uint8_t* vhtcap_ie;
	uint8_t* vhtopmode_ie;
	uint8_t* vhtpwrenv_ie;
	uint8_t* apchanrep_ie;
	uint8_t* bssload_ie;
	uint8_t* spare[4];
	uint8_t* data;
	int len;
};

struct ieee80211_node {
	struct ieee80211vap* ni_vap;
	struct ieee80211com* ni_ic;
	int ni_flags;
	uint16_t ni_htcap;
	uint8_t ni_htparam;
	uint8_t ni_htctlchan;
	uint8_t ni_ht2ndchan;
	uint8_t ni_htopmode;
	uint16_t ni_htstbc;
	uint8_t ni_vht_chanwidth;
	uint8_t ni_vht_chan1;
	uint8_t ni_vht_chan2;
	uint16_t ni_vht_basicmcs;
	uint32_t ni_vhtcap;
	struct ieee80211_vht_mcs_info ni_vht_mcsinfo;
	uint8_t ni_uapsd;
};

struct ieee80211_rsnparms {
	uint8_t rsn_mcastcipher;
	uint8_t rsn_mcastkeylen;
	uint8_t rsn_ucastcipher;
	uint8_t rsn_ucastkeylen;
	uint32_t rsn_keymgmt;
	uint16_t rsn_caps;
};

struct ieee80211_scanparams {
	int status;
	uint8_t* tstamp;
	uint16_t bintval;
	uint16_t capinfo;
	uint8_t bchan;
	uint8_t chan;
	uint8_t* ies;
	int ies_len;
	uint8_t* ssid;
	uint8_t* rates;
	uint8_t* country;
	uint8_t* csa;
	uint8_t* quiet;
	uint8_t* tim;
	uint8_t* xrates;
	uint8_t* htcap;
	uint8_t* vhtcap;
	uint8_t* vhtopmode;
	uint8_t* rsn;
	uint8_t* htinfo;
	uint8_t* wpa;
	uint8_t* wme;
	uint16_t fhdwell;
	uint8_t fhindex;
	int timoff;
	int erp;
};

static inline int iswpaoui(const uint8_t* frm)
{
	return frm[1] > 3 && le32dec(frm + 2) == ((WPA_OUI_TYPE << 24) | WPA_OUI);
}

static inline int iswmeoui(const uint8_t* frm)
{
	return frm[1] > 3 && le32dec(frm + 2) == ((WME_OUI_TYPE << 24) | WME_OUI);
}

static inline int iswmeparam(const uint8_t* frm)
{
	return frm[1] > 5 && le32dec(frm + 2) == ((WME_OUI_TYPE << 24) | WME_OUI)
		&& frm[6] == WME_PARAM_OUI_SUBTYPE;
}

static inline int iswmeinfo(const uint8_t* frm)
{
	return frm[1] > 5 && le32dec(frm + 2) == ((WME_OUI_TYPE << 24) | WME_OUI)
		&& frm[6] == WME_INFO_OUI_SUBTYPE;
}

static inline int ishtcapoui(const uint8_t* frm)
{
	return frm[1] > 3 && le32dec(frm + 2) == ((BCM_OUI_HTCAP << 24) | BCM_OUI);
}

static inline int ishtinfooui(const uint8_t* frm)
{
	return frm[1] > 3 && le32dec(frm + 2) == ((BCM_OUI_HTINFO << 24) | BCM_OUI);
}

static inline uint8_t ieee80211_chan2ieee(struct ieee80211com* ic,
	struct ieee80211_channel* c)
{
	(void)ic;
	return c != NULL ? c->ic_ieee : 0;
}

static inline struct ieee80211_channel* ieee80211_find_channel_byieee(
	struct ieee80211com* ic, uint8_t ieee, int flags)
{
	(void)ic;
	(void)ieee;
	(void)flags;
	return NULL;
}

static inline void ieee80211_csa_startswitch(struct ieee80211com* ic,
	struct ieee80211_channel* c, int mode, int count)
{
	(void)ic;
	(void)c;
	(void)mode;
	(void)count;
}

static inline void ieee80211_csa_cancelswitch(struct ieee80211com* ic)
{
	(void)ic;
}

static inline void ieee80211_csa_completeswitch(struct ieee80211com* ic)
{
	(void)ic;
}

/* Receive path of mt7922wifi, only the fields mtk_receive_frame touches. */
#define MTK_TX_RING_COUNT 256
#define MTK_RSSI 40
#define MTK_NOISE_FLOOR (-95)
#define MTK_RX_TYPE_NORMAL 2
#define MTK_RX_TYPE_SENT 6
#define MTK_RX_TYPE_EVENT 7
#define MTK_RX_TYPE_NORMAL_MCU 8
#define MTK_RXD1_GROUP_1 (1 << 11)
#define MTK_RXD1_GROUP_2 (1 << 12)
#define MTK_RXD1_GROUP_3 (1 << 13)
#define MTK_RXD1_GROUP_4 (1 << 14)
#define MTK_RXD1_GROUP_5 (1 << 15)
#define MTK_RXD1_FCS_ERROR (1 << 27)
#define MTK_RXD_FIXED 24
#define MTK_MGMT_HEADER 24
#define MTK_MCU_RXD_SIZE 36
#define MTK_MCU_EVENT_SCAN_DONE 0x0d
#define MTK_MCU_UNI_EVENT_ROC 0x27
#define MTK_MCU_UNI_UNSOLICITED (1 << 2)
#define IEEE80211_R_NF 0x01
#define IEEE80211_R_RSSI 0x02
#define IEEE80211_R_FREQ 0x04
#define IEEE80211_R_IEEE 0x08
#define IEEE80211_R_BAND 0x10
#define IEEE80211_CHAN_2GHZ 0x01
#define IEEE80211_CHAN_5GHZ 0x02

struct taskqueue;
struct task { int unused; };
typedef void* device_t;

struct ieee80211_rx_stats {
	int r_flags;
	int c_nf;
	int c_rssi;
	uint8_t c_ieee;
	int c_band;
	int c_freq;
};

struct mtk_softc {
	struct ieee80211com sc_ic;
	device_t sc_dev;
	int sc_debug;
	uint8_t sc_txbusy[MTK_TX_RING_COUNT];
	uint32_t sc_txdone;
	uint32_t sc_raw[3];
	int sc_events_shown;
	uint8_t sc_seq;
	uint8_t sc_roc_token;
	int sc_roc_granted;
	int sc_scan_done;
	struct taskqueue* sc_tq;
	struct task sc_work;
	uint8_t sc_reply[256];
	size_t sc_replylen;
	int sc_replyready;
	uint32_t sc_dropped;
	uint32_t sc_typemask;
	uint32_t sc_ragged;
	uint32_t sc_mgmt;
	uint32_t sc_subtype[16];
	uint32_t sc_data;
	uint32_t sc_ctrl;
	uint32_t sc_shown[3];
	uint32_t sc_received;
	uint32_t sc_sent;
	uint32_t sc_txstale;
	uint32_t sc_txfull;
};

#define MTK_DEBUG(sc, ...) do {} while (0)
#define wmb() do {} while (0)

static inline void taskqueue_enqueue(struct taskqueue* t, struct task* w)
{
	(void)t;
	(void)w;
}

static inline struct mbuf* m_get2(size_t len, int how, int type, int flags)
{
	struct mbuf* m;
	(void)how;
	(void)type;
	(void)flags;
	m = calloc(1, sizeof(*m));
	if (m == NULL)
		return NULL;
	m->m_data = malloc(len > 0 ? len : 1);
	if (m->m_data == NULL) {
		free(m);
		return NULL;
	}
	return m;
}

static inline void m_freem(struct mbuf* m)
{
	if (m == NULL)
		return;
	free(m->m_data);
	free(m);
}

static inline int ieee80211_add_rx_params(struct mbuf* m,
	struct ieee80211_rx_stats* rxs)
{
	(void)m;
	(void)rxs;
	return 1;
}

static inline void ieee80211_input_mimo_all(struct ieee80211com* ic,
	struct mbuf* m)
{
	(void)ic;
	m_freem(m);
}

static inline int ieee80211_ieee2mhz(uint8_t channel, int band)
{
	(void)band;
	return 2407 + 5 * channel;
}

#endif
