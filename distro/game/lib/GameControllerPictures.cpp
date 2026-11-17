/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The controller pictures and the choice by vendor and product ID
 * (GameControllerPictures.h).
 */


#include "GameControllerPictures.h"

#include <stddef.h>
#include <string.h>


namespace R5GameController {


#include "GameControllerPictureData.h"


static const int32 kModelCount = sizeof(kModels) / sizeof(kModels[0]);
static const int32 kIDCount = sizeof(kModelIDs) / sizeof(kModelIDs[0]);


int32
CountPictures()
{
	return kModelCount;
}


const gc_model*
PictureAt(int32 index)
{
	if (index < 0 || index >= kModelCount)
		return NULL;
	return &kModels[index];
}


const gc_model*
PictureNamed(const char* name)
{
	if (name == NULL)
		return NULL;
	for (int32 i = 0; i < kModelCount; i++) {
		if (strcmp(kModels[i].name, name) == 0)
			return &kModels[i];
	}
	return NULL;
}


const gc_model*
PictureForFamily(game_controller_family family)
{
	switch (family) {
		case B_GAME_CONTROLLER_FAMILY_PLAYSTATION:
			return PictureNamed("dualsense");
		case B_GAME_CONTROLLER_FAMILY_XBOX:
			return PictureNamed("xbox-series");
		case B_GAME_CONTROLLER_FAMILY_NINTENDO:
			return PictureNamed("switch-pro");
		case B_GAME_CONTROLLER_FAMILY_SEGA_GENESIS:
			return PictureNamed("genesis-m30");
		default:
			return NULL;
	}
}


const gc_model*
PictureFor(uint16 vendor, uint16 product, game_controller_family family)
{
	const gc_model* model = NULL;
	uint32 wanted = (uint32)vendor << 16 | product;
	int32 low = 0;
	int32 high = kIDCount - 1;
	while (low <= high) {
		int32 middle = (low + high) / 2;
		uint32 id = (uint32)kModelIDs[middle].vendor << 16
			| kModelIDs[middle].product;
		if (id == wanted) {
			model = &kModels[kModelIDs[middle].model];
			break;
		}
		if (id < wanted)
			low = middle + 1;
		else
			high = middle - 1;
	}

	if (model == NULL) {
		switch (vendor) {
			case 0x054c:
				model = PictureNamed("dualsense");
				break;
			case 0x045e:
				model = PictureNamed("xbox-series");
				break;
			case 0x057e:
				model = PictureNamed("switch-pro");
				break;
		}
	}

	if (model == NULL || model->family != (int32)family)
		model = PictureForFamily(family);
	return model;
}


int32
CountPictureIDs()
{
	return kIDCount;
}


const gc_model_id*
PictureIDAt(int32 index)
{
	if (index < 0 || index >= kIDCount)
		return NULL;
	return &kModelIDs[index];
}


}	// namespace R5GameController
