/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The Wi-Fi page: the WiFiView from libr5wifi.so, the same panel as the
 * separate WiFi program. Joining a network, leaving it and forgetting it
 * take effect at once and need no administrator. There are no values to put
 * back and no factory setup, so Revert and Defaults stay disabled.
 */



#include "SettingsModule.h"

#include <Catalog.h>
#include <R5WiFiView.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Settings: Wi-Fi"


class WiFiModule : public SettingsModule {
public:
	virtual	const char*			Identifier() const { return "wifi"; }
	virtual	const char*			Name() const
									{ return B_TRANSLATE("Wi-Fi"); }
	virtual	const char*			Category() const { return "network"; }
	virtual	const char*			Description() const
									{ return B_TRANSLATE("Connect to wireless networks"); }
	virtual	const char*			Keywords() const
									{ return B_TRANSLATE_COMMENT("wifi wireless wlan ssid "
										"password network",
										"Search words, separated by spaces. Keep "
										"the English words and add words in your "
										"language."); }
	virtual	bool				CanSave() const { return false; }

	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame)
								{
									(void)context;
									(void)frame;
									return new WiFiView(true);
								}
	virtual	status_t			LoadSettings(SettingsContext* context)
									{ (void)context; return B_OK; }
	virtual	status_t			SaveSettings(SettingsContext* context)
									{ (void)context; return B_OK; }
	virtual	status_t			RevertSettings(SettingsContext* context)
									{ (void)context; return B_OK; }
};


// The add-on settings_wifi (distro/settings/ModuleAPI.md): every
// Settings page is an add-on of its own.
extern "C" _EXPORT int32
settings_api_version()
{
	return kSettingsApiVersion;
}


extern "C" _EXPORT SettingsModule*
instantiate_settings_module()
{
	return new WiFiModule();
}
