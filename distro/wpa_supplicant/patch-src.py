#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
"""Apply the R5 WPA3 edits to a checkout of haiku/wpa_supplicant bdf3144.

The checkout is not stored in git. This script is the edit; build-wpa.sh
runs it on a fresh tree. A second run fails if the marker is already there.
"""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5-wpa-src")
MARK = "R5_HAIKU_SAE"


def replace(path: pathlib.Path, old: str, new: str, label: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path.name} {label}: found {count}, want 1")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")
    print(f"patched {path.name} ({label})")


def main() -> None:
    driver = ROOT / "src/drivers/driver_bsd.c"
    text = driver.read_text(encoding="utf-8")
    if MARK in text:
        raise SystemExit("driver_bsd.c already patched")

    replace(driver, "\tenum ieee80211_opmode opmode;\t/* operation mode */\n};",
        "\tenum ieee80211_opmode opmode;\t/* operation mode */\n"
        "#ifdef __HAIKU__\n"
        "\t/* R5_HAIKU_SAE: the join we owe a second association after SAE. */\n"
        "\tu8 sae_bssid[ETH_ALEN];\n"
        "\tu8 sae_ssid[32];\n"
        "\tsize_t sae_ssid_len;\n"
        "\tint sae_pending;\n"
        "\tu8 sae_last[400];\n"
        "\tu16 sae_last_len;\n"
        "#endif /* __HAIKU__ */\n"
        "};", "struct")

    replace(driver,
        "\tcase WPA_ALG_CCMP:\n"
        "\t\twk.ik_type = IEEE80211_CIPHER_AES_CCM;\n"
        "\t\tbreak;\n"
        "\tdefault:\n",
        "\tcase WPA_ALG_CCMP:\n"
        "\t\twk.ik_type = IEEE80211_CIPHER_AES_CCM;\n"
        "\t\tbreak;\n"
        "#ifdef __HAIKU__\n"
        "\tcase WPA_ALG_BIP_CMAC_128:\n"
        "\t\tif (key == NULL || key_len != 16\n"
        "\t\t    || (key_idx != 4 && key_idx != 5)) {\n"
        "\t\t\twpa_printf(MSG_ERROR,\n"
        "\t\t\t\t\"%s: unexpected BIP key idx=%d len=%zu\",\n"
        "\t\t\t\t__func__, key_idx, key_len);\n"
        "\t\t\treturn -1;\n"
        "\t\t}\n"
        "\t\t/* Index 4 or 5 is sent as a WPA key. The net80211 setter\n"
        "\t\t * accepts BIP there and hands the key to the driver. A\n"
        "\t\t * direct HAIKU_IGTK ioctl has no setter and returns\n"
        "\t\t * EINVAL. The key bytes are not written to the log. */\n"
        "\t\twk.ik_type = IEEE80211_CIPHER_BIP_CMAC_128;\n"
        "\t\tbreak;\n"
        "#endif /* __HAIKU__ */\n"
        "\tdefault:\n", "igtk")

    replace(driver,
        "\telse if (auth_alg & WPA_AUTH_ALG_SHARED)\n"
        "\t\tauthmode = IEEE80211_AUTH_SHARED;\n"
        "\telse\n"
        "\t\tauthmode = IEEE80211_AUTH_OPEN;\n",
        "\telse if (auth_alg & WPA_AUTH_ALG_SHARED)\n"
        "\t\tauthmode = IEEE80211_AUTH_SHARED;\n"
        "#ifdef __HAIKU__\n"
        "\telse if (auth_alg & WPA_AUTH_ALG_SAE)\n"
        "\t\tauthmode = IEEE80211_AUTH_SAE;\n"
        "#endif /* __HAIKU__ */\n"
        "\telse\n"
        "\t\tauthmode = IEEE80211_AUTH_OPEN;\n", "auth")

    replace(driver,
        "\tif (wpa_driver_bsd_set_auth_alg(drv, params->auth_alg) < 0)\n"
        "\t\tret = -1;\n",
        "#ifdef __HAIKU__\n"
        "\tdrv->sae_pending = 0;\n"
        "\teloop_cancel_timeout(haiku_sae_poll, drv, NULL);\n"
        "\tif (params->key_mgmt_suite == WPA_KEY_MGMT_SAE) {\n"
        "\t\tif (set80211param(drv, IEEE80211_IOC_AUTHMODE,\n"
        "\t\t\tIEEE80211_AUTH_SAE) < 0)\n"
        "\t\t\treturn -1;\n"
        "\t} else\n"
        "#endif /* __HAIKU__ */\n"
        "\tif (wpa_driver_bsd_set_auth_alg(drv, params->auth_alg) < 0)\n"
        "\t\tret = -1;\n", "associate auth")

    replace(driver,
        "\tif (set80211var(drv, IEEE80211_IOC_MLME, &mlme, sizeof(mlme)) < 0)\n"
        "\t\treturn -1;\n"
        "\treturn ret;\n"
        "}\n",
        "\tif (set80211var(drv, IEEE80211_IOC_MLME, &mlme, sizeof(mlme)) < 0)\n"
        "\t\treturn -1;\n"
        "#ifdef __HAIKU__\n"
        "\tif (params->key_mgmt_suite == WPA_KEY_MGMT_SAE)\n"
        "\t\thaiku_sae_started(drv, params);\n"
        "#endif /* __HAIKU__ */\n"
        "\treturn ret;\n"
        "}\n", "associate start")

    replace(driver,
        "wpa_driver_bsd_deinit(void *priv)\n"
        "{\n"
        "\tstruct bsd_driver_data *drv = priv;\n",
        "wpa_driver_bsd_deinit(void *priv)\n"
        "{\n"
        "\tstruct bsd_driver_data *drv = priv;\n"
        "\n"
        "#ifdef __HAIKU__\n"
        "\tdrv->sae_pending = 0;\n"
        "\teloop_cancel_timeout(haiku_sae_poll, drv, NULL);\n"
        "#endif /* __HAIKU__ */\n", "deinit")

    replace(driver,
        "\tdrv->capa.auth = WPA_DRIVER_AUTH_OPEN |\n"
        "\t\tWPA_DRIVER_AUTH_SHARED |\n"
        "\t\tWPA_DRIVER_AUTH_LEAP;\n"
        "\treturn 0;\n"
        "}\n",
        "\tdrv->capa.auth = WPA_DRIVER_AUTH_OPEN |\n"
        "\t\tWPA_DRIVER_AUTH_SHARED |\n"
        "\t\tWPA_DRIVER_AUTH_LEAP;\n"
        "#ifdef __HAIKU__\n"
        "\t/* SAE is done by the supplicant. FLAGS_SAE keeps the AKM.\n"
        "\t * FLAGS_SME is not set: that would move WPA2 onto a path this\n"
        "\t * driver does not complete. */\n"
        "\tdrv->capa.flags |= WPA_DRIVER_FLAGS_SAE;\n"
        "\tdrv->capa.key_mgmt |= WPA_DRIVER_CAPA_KEY_MGMT_SAE;\n"
        "#endif /* __HAIKU__ */\n"
        "\treturn 0;\n"
        "}\n", "capa")

    replace(driver,
        "\t.set_key\t\t= bsd_set_key,\n",
        "#ifdef __HAIKU__\n"
        "\t.send_mlme\t\t= wpa_driver_bsd_send_mlme,\n"
        "\t.send_external_auth_status = haiku_external_auth_status,\n"
        "#endif /* __HAIKU__ */\n"
        "\t.set_key\t\t= bsd_set_key,\n", "ops")

    # Declarations and the Haiku helpers, before the first use in associate.
    # associate is after set_auth_alg. Insert the helpers just before
    # wpa_driver_bsd_set_auth_alg.
    replace(driver,
        "static int\n"
        "wpa_driver_bsd_set_auth_alg(void *priv, int auth_alg)\n",
        HAIKU_HELPERS +
        "static int\n"
        "wpa_driver_bsd_set_auth_alg(void *priv, int auth_alg)\n", "helpers")

    config = ROOT / "wpa_supplicant/.config"
    cfg = config.read_text(encoding="utf-8")
    cfg2 = cfg.replace("#CONFIG_IEEE80211W=y", "CONFIG_IEEE80211W=y", 1)
    cfg2 = cfg2.replace("#CONFIG_OWE=y", "CONFIG_OWE=y", 1)
    if "\nCONFIG_SAE=y\n" not in cfg2:
        cfg2 = cfg2.replace("CONFIG_IEEE80211W=y\n",
            "CONFIG_IEEE80211W=y\nCONFIG_SAE=y\n", 1)
    # The SAE receive path in sme.c is a stub unless this is set. It does
    # not set the driver's SME flag, so WPA2 still uses associate().
    if "\nNEED_SME=y\n" not in cfg2:
        cfg2 = cfg2.replace("CONFIG_SAE=y\n", "CONFIG_SAE=y\nNEED_SME=y\n", 1)
    if cfg2 == cfg:
        raise SystemExit(".config did not change")
    config.write_text(cfg2, encoding="utf-8", newline="\n")
    print("patched .config")

    gui = ROOT / "wpa_supplicant/wpa_gui-haiku/main_haiku.cpp"
    replace(gui,
        "\tuint32 authMode = B_NETWORK_AUTHENTICATION_NONE;\n"
        "\tstatus = message->FindUInt32(\"authentication\", &authMode);\n"
        "\tif (status != B_OK)\n"
        "\t\treturn status;\n"
        "\n"
        "\tconst char *password = NULL;\n"
        "\tif (authMode > B_NETWORK_AUTHENTICATION_NONE) {\n"
        "\t\tstatus = message->FindString(\"password\", &password);\n"
        "\t\tif (status != B_OK)\n"
        "\t\t\treturn status;\n"
        "\t}\n",
        "\tuint32 authMode = B_NETWORK_AUTHENTICATION_NONE;\n"
        "\tstatus = message->FindUInt32(\"authentication\", &authMode);\n"
        "\tif (status != B_OK)\n"
        "\t\treturn status;\n"
        "\n"
        "\tuint32 keyMode = 0;\n"
        "\tmessage->FindUInt32(\"key_mode\", &keyMode);\n"
        "\t/* A network that also offers a passphrase stays on WPA-PSK\n"
        "\t * until a WPA3 join has been shown to work. SAE alone, or an\n"
        "\t * explicit WPA3 network, still uses SAE. */\n"
        "\tbool pskOffered = (keyMode & (B_KEY_MODE_PSK\n"
        "\t\t| B_KEY_MODE_FT_PSK | B_KEY_MODE_PSK_SHA256)) != 0;\n"
        "\tbool sae = !pskOffered && (authMode == B_NETWORK_AUTHENTICATION_WPA3\n"
        "\t\t|| (keyMode & B_KEY_MODE_SAE) != 0);\n"
        "\tbool owe = !sae && !pskOffered && (keyMode & B_KEY_MODE_OWE) != 0;\n"
        "\n"
        "\tconst char *password = NULL;\n"
	"\tif ((authMode > B_NETWORK_AUTHENTICATION_NONE || sae) && !owe) {\n"
        "\t\tstatus = message->FindString(\"password\", &password);\n"
        "\t\tif (status != B_OK)\n"
        "\t\t\treturn status;\n"
        "\t}\n", "join read")

    replace(gui,
        "\tif (authMode >= B_NETWORK_AUTHENTICATION_WPA) {\n"
        "\t\t// Nothing to do here.\n"
        "\t} else {\n"
        "\t\t// Open or WEP.\n"
        "\t\tif (result == 0)\n"
        "\t\t\tresult = wpa_config_set(network, \"key_mgmt\", \"NONE\", 6);\n"
        "\t}\n",
        "\tif (sae || owe || authMode >= B_NETWORK_AUTHENTICATION_WPA) {\n"
        "\t\t// WPA, WPA2, SAE or OWE. The key is set below.\n"
        "\t} else {\n"
        "\t\t// Open or WEP.\n"
        "\t\tif (result == 0)\n"
        "\t\t\tresult = wpa_config_set(network, \"key_mgmt\", \"NONE\", 6);\n"
        "\t}\n", "key_mgmt none")

    replace(gui,
        "\t\t} else if (authMode >= B_NETWORK_AUTHENTICATION_WPA) {\n"
        "\t\t\t// WPA/WPA2\n"
        "\t\t\tvalue = \"\\\"\";\n"
        "\t\t\tvalue += password;\n"
        "\t\t\tvalue += \"\\\"\";\n"
        "\t\t\tresult = wpa_config_set(network, \"psk\", value.String(), 10);\n"
        "\n"
        "\t\t\tif (result == 0) {\n"
        "\t\t\t\t// We need to actually \"apply\" the PSK\n"
        "\t\t\t\twpa_config_update_psk(network);\n"
        "\t\t\t}\n"
        "\t\t}\n",
        "\t\t} else if (sae) {\n"
        "\t\t\t// WPA3. The password is the SAE password, not a PSK.\n"
        "\t\t\tif (result == 0)\n"
        "\t\t\t\tresult = wpa_config_set(network, \"key_mgmt\", \"SAE\", 0);\n"
        "\t\t\tif (result == 0)\n"
        "\t\t\t\tresult = wpa_config_set(network, \"ieee80211w\", \"2\", 0);\n"
        "\t\t\tif (result == 0)\n"
        "\t\t\t\tresult = wpa_config_set(network, \"proto\", \"RSN\", 0);\n"
        "\t\t\tvalue = \"\\\"\";\n"
        "\t\t\tvalue += password;\n"
        "\t\t\tvalue += \"\\\"\";\n"
        "\t\t\tif (result == 0)\n"
        "\t\t\t\tresult = wpa_config_set(network, \"sae_password\",\n"
        "\t\t\t\t\tvalue.String(), 0);\n"
        "\t\t} else if (owe) {\n"
        "\t\t\tif (result == 0)\n"
        "\t\t\t\tresult = wpa_config_set(network, \"key_mgmt\", \"OWE\", 0);\n"
        "\t\t\tif (result == 0)\n"
        "\t\t\t\tresult = wpa_config_set(network, \"ieee80211w\", \"2\", 0);\n"
        "\t\t\tif (result == 0)\n"
        "\t\t\t\tresult = wpa_config_set(network, \"proto\", \"RSN\", 0);\n"
        "\t\t} else if (authMode >= B_NETWORK_AUTHENTICATION_WPA) {\n"
        "\t\t\t// WPA/WPA2\n"
        "\t\t\tvalue = \"\\\"\";\n"
        "\t\t\tvalue += password;\n"
        "\t\t\tvalue += \"\\\"\";\n"
        "\t\t\tresult = wpa_config_set(network, \"psk\", value.String(), 10);\n"
        "\n"
        "\t\t\tif (result == 0) {\n"
        "\t\t\t\t// We need to actually \"apply\" the PSK\n"
        "\t\t\t\twpa_config_update_psk(network);\n"
        "\t\t\t}\n"
        "\t\t}\n", "sae password")

    replace(gui,
        "\tif (interface == NULL) {\n"
        "\t\t// Attempt to connect using the Haiku extension ioctl\n",
        "\tif (interface == NULL) {\n"
        "\t\tif (sae || owe)\n"
        "\t\t\treturn B_ERROR;\n"
        "\t\t// Attempt to connect using the Haiku extension ioctl\n",
        "no direct join")

    # The enums are in R5's NetworkDevice.h. A sysroot from before that
    # header still builds.
    replace(gui,
        "#include <NetworkDevice.h>\n",
        "#include <NetworkDevice.h>\n"
        "#ifndef B_NETWORK_AUTHENTICATION_WPA3\n"
        "#define B_NETWORK_AUTHENTICATION_WPA3 5\n"
        "#endif\n"
        "#ifndef B_KEY_MODE_SAE\n"
        "#define B_KEY_MODE_SAE 0x0400\n"
        "#endif\n"
        "#ifndef B_KEY_MODE_OWE\n"
        "#define B_KEY_MODE_OWE 0x0800\n"
        "#endif\n", "enums")

    print("ok")


HAIKU_HELPERS = r'''#ifdef __HAIKU__
#ifndef IEEE80211_AUTH_SAE
#define IEEE80211_AUTH_SAE 6
#endif
#ifndef IEEE80211_AUTH_ALG_SAE
#define IEEE80211_AUTH_ALG_SAE 3
#endif
#ifndef IEEE80211_SAE_MAX
#define IEEE80211_SAE_MAX 400
#endif
#ifndef IEEE80211_IOC_HAIKU_SAE_AUTH
#define IEEE80211_IOC_HAIKU_SAE_AUTH 0x6004
struct ieee80211_haiku_sae_auth {
	uint8_t i_mac[IEEE80211_ADDR_LEN];
	uint16_t i_len;
	uint8_t i_body[IEEE80211_SAE_MAX];
};
#endif
#ifndef IEEE80211_IOC_HAIKU_IGTK
#define IEEE80211_IOC_HAIKU_IGTK 0x6005
struct ieee80211_haiku_igtk {
	uint8_t i_key[16];
	uint8_t i_keyix;
	uint8_t i_keylen;
};
#endif

static void haiku_sae_poll(void *eloop_ctx, void *timeout_ctx);

static int
haiku_sae_take(struct bsd_driver_data *drv,
	struct ieee80211_haiku_sae_auth *frame)
{
	struct ieee80211req ireq;

	os_memset(frame, 0, sizeof(*frame));
	os_memset(&ireq, 0, sizeof(ireq));
	os_strlcpy(ireq.i_name, drv->ifname, sizeof(ireq.i_name));
	ireq.i_type = IEEE80211_IOC_HAIKU_SAE_AUTH;
	ireq.i_len = sizeof(*frame);
	ireq.i_data = frame;
	/* ENOENT is the usual result until the access point answers.
	 * Do not log it, and do not log the frame. */
	if (ioctl(drv->global->sock, SIOCG80211, &ireq, sizeof(ireq)) < 0)
		return -1;
	return 0;
}

static void
haiku_sae_poll(void *eloop_ctx, void *timeout_ctx)
{
	struct bsd_driver_data *drv = eloop_ctx;
	struct ieee80211_haiku_sae_auth frame;
	u8 mgmt[24 + IEEE80211_SAE_MAX];
	union wpa_event_data event;

	(void) timeout_ctx;
	if (!drv->sae_pending)
		return;
	if (haiku_sae_take(drv, &frame) == 0
	    && frame.i_len >= 6 && frame.i_len <= IEEE80211_SAE_MAX
	    && (frame.i_len != drv->sae_last_len
		|| os_memcmp(frame.i_body, drv->sae_last, frame.i_len) != 0)) {
		os_memcpy(drv->sae_last, frame.i_body, frame.i_len);
		drv->sae_last_len = frame.i_len;
		os_memset(mgmt, 0, sizeof(mgmt));
		mgmt[0] = 0xb0;
		os_memcpy(mgmt + 10, frame.i_mac, ETH_ALEN);
		os_memcpy(mgmt + 16, frame.i_mac, ETH_ALEN);
		os_memcpy(mgmt + 24, frame.i_body, frame.i_len);
		os_memset(&event, 0, sizeof(event));
		event.rx_mgmt.frame = mgmt;
		event.rx_mgmt.frame_len = 24 + frame.i_len;
		wpa_supplicant_event(drv->ctx, EVENT_RX_MGMT, &event);
	}
	if (drv->sae_pending)
		eloop_register_timeout(0, 50000, haiku_sae_poll, drv, NULL);
}

static void
haiku_sae_started(struct bsd_driver_data *drv,
	struct wpa_driver_associate_params *params)
{
	union wpa_event_data event;

	os_memset(drv->sae_bssid, 0, ETH_ALEN);
	if (params->bssid)
		os_memcpy(drv->sae_bssid, params->bssid, ETH_ALEN);
	drv->sae_ssid_len = params->ssid_len;
	if (drv->sae_ssid_len > sizeof(drv->sae_ssid))
		drv->sae_ssid_len = sizeof(drv->sae_ssid);
	if (params->ssid != NULL && drv->sae_ssid_len)
		os_memcpy(drv->sae_ssid, params->ssid, drv->sae_ssid_len);
	/* The kernel keeps the previous SAE body. Treat that one as already
	 * seen so a new attempt does not replay it. */
	{
		struct ieee80211_haiku_sae_auth held;

		if (haiku_sae_take(drv, &held) == 0
		    && held.i_len >= 6 && held.i_len <= IEEE80211_SAE_MAX) {
			os_memcpy(drv->sae_last, held.i_body, held.i_len);
			drv->sae_last_len = held.i_len;
		} else
			drv->sae_last_len = 0;
	}
	drv->sae_pending = 1;
	eloop_register_timeout(0, 50000, haiku_sae_poll, drv, NULL);

	os_memset(&event, 0, sizeof(event));
	event.external_auth.action = EXT_AUTH_START;
	event.external_auth.bssid = drv->sae_bssid;
	event.external_auth.ssid = drv->sae_ssid;
	event.external_auth.ssid_len = drv->sae_ssid_len;
	/* The supplicant compares this with the RSN selector 00-0F-AC:8,
	 * not with WPA_KEY_MGMT_SAE. */
	event.external_auth.key_mgmt_suite = RSN_AUTH_KEY_MGMT_SAE;
	wpa_supplicant_event(drv->ctx, EVENT_EXTERNAL_AUTH, &event);
}

static int
wpa_driver_bsd_send_mlme(void *priv, const u8 *data, size_t data_len,
	int noack, unsigned int freq, const u16 *csa_offs,
	size_t csa_offs_len, int no_encrypt, unsigned int wait, int link_id)
{
	struct ieee80211_haiku_sae_auth frame;
	u16 algo;

	(void) noack;
	(void) freq;
	(void) csa_offs;
	(void) csa_offs_len;
	(void) no_encrypt;
	(void) wait;
	(void) link_id;
	if (data == NULL || data_len < 24 + 6
	    || data_len - 24 > IEEE80211_SAE_MAX)
		return -1;
	algo = (u16) data[24] | ((u16) data[25] << 8);
	if (algo != IEEE80211_AUTH_ALG_SAE)
		return -1;
	os_memset(&frame, 0, sizeof(frame));
	os_memcpy(frame.i_mac, data + 4, ETH_ALEN);
	frame.i_len = (uint16_t) (data_len - 24);
	os_memcpy(frame.i_body, data + 24, frame.i_len);
	if (set80211var(priv, IEEE80211_IOC_HAIKU_SAE_AUTH, &frame,
	    sizeof(frame)) < 0)
		return -1;
	return 0;
}

static int
haiku_external_auth_status(void *priv, struct external_auth *params)
{
	struct bsd_driver_data *drv = priv;
	struct ieee80211req_mlme mlme;

	drv->sae_pending = 0;
	eloop_cancel_timeout(haiku_sae_poll, drv, NULL);
	if (params == NULL || params->status != 0)
		return 0;
	if (set80211param(drv, IEEE80211_IOC_AUTHMODE, IEEE80211_AUTH_SAE) < 0)
		return -1;
	os_memset(&mlme, 0, sizeof(mlme));
	mlme.im_op = IEEE80211_MLME_ASSOC;
	if (drv->sae_ssid_len)
		os_memcpy(mlme.im_ssid, drv->sae_ssid, drv->sae_ssid_len);
	mlme.im_ssid_len = drv->sae_ssid_len;
	os_memcpy(mlme.im_macaddr, drv->sae_bssid, ETH_ALEN);
	if (set80211var(drv, IEEE80211_IOC_MLME, &mlme, sizeof(mlme)) < 0)
		return -1;
	return 0;
}
#endif /* __HAIKU__ */

'''


if __name__ == "__main__":
    main()
