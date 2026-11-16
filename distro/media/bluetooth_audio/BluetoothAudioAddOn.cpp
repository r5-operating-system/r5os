/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * bluetooth_audio.media_addon: one audio output, "Bluetooth audio", that
 * Media preferences lists with the other outputs (docs/BLUETOOTH.md,
 * step 5). Which headset it plays to is each person's own choice
 * (AudioSettings.h).
 */


#include <MediaAddOn.h>
#include <MediaDefs.h>
#include <new>

#include "BluetoothAudioNode.h"


class BluetoothAudioAddOn : public BMediaAddOn {
public:
								BluetoothAudioAddOn(image_id image);

	virtual	status_t			InitCheck(const char** failureText);
	virtual	int32				CountFlavors();
	virtual	status_t			GetFlavorAt(int32 index,
									const flavor_info** info);
	virtual	BMediaNode*			InstantiateNodeFor(const flavor_info* info,
									BMessage* config, status_t* error);
	virtual	status_t			GetConfigurationFor(BMediaNode* node,
									BMessage* config);
	virtual	bool				WantsAutoStart();
	virtual	status_t			AutoStart(int32 index, BMediaNode** node,
									int32* internalID, bool* hasMore);

private:
			flavor_info			fFlavor;
			media_format		fFormat;
};


BluetoothAudioAddOn::BluetoothAudioAddOn(image_id image)
	:
	BMediaAddOn(image)
{
	BluetoothAudioNode::PreferredFormat(&fFormat);

	fFlavor.name = (char*)"Bluetooth audio";
	fFlavor.info = (char*)"Plays to a Bluetooth headset or speaker (A2DP, "
		"SBC).";
	fFlavor.kinds = B_BUFFER_CONSUMER | B_PHYSICAL_OUTPUT | B_CONTROLLABLE;
	fFlavor.flavor_flags = 0;
	fFlavor.internal_id = 0;
	fFlavor.possible_count = 1;
	fFlavor.in_format_count = 1;
	fFlavor.in_format_flags = 0;
	fFlavor.in_formats = &fFormat;
	fFlavor.out_format_count = 0;
	fFlavor.out_format_flags = 0;
	fFlavor.out_formats = NULL;
}


status_t
BluetoothAudioAddOn::InitCheck(const char** failureText)
{
	return B_OK;
}


int32
BluetoothAudioAddOn::CountFlavors()
{
	return 1;
}


status_t
BluetoothAudioAddOn::GetFlavorAt(int32 index, const flavor_info** info)
{
	if (index != 0)
		return B_BAD_INDEX;
	*info = &fFlavor;
	return B_OK;
}


BMediaNode*
BluetoothAudioAddOn::InstantiateNodeFor(const flavor_info* info,
	BMessage* config, status_t* error)
{
	if (info == NULL || info->internal_id != 0) {
		*error = B_BAD_INDEX;
		return NULL;
	}
	BluetoothAudioNode* node
		= new(std::nothrow) BluetoothAudioNode(this, 0);
	*error = node != NULL ? B_OK : B_NO_MEMORY;
	return node;
}


status_t
BluetoothAudioAddOn::GetConfigurationFor(BMediaNode* node, BMessage* config)
{
	return B_OK;
}


bool
BluetoothAudioAddOn::WantsAutoStart()
{
	return false;
}


status_t
BluetoothAudioAddOn::AutoStart(int32 index, BMediaNode** node,
	int32* internalID, bool* hasMore)
{
	return B_ERROR;
}


extern "C" _EXPORT BMediaAddOn*
make_media_addon(image_id image)
{
	return new(std::nothrow) BluetoothAudioAddOn(image);
}
