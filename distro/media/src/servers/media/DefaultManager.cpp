/*
 * Copyright 2002, 2003 Marcus Overhagen, Jérôme Duval. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "DefaultManager.h"

#include <Application.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <MediaNode.h>
#include <OS.h>
#include <Path.h>
#include <TimeSource.h>
#include <string.h>
#include <syslog.h>

#include "MediaDebug.h"
#include "DormantNodeManager.h"
#include "media_server.h"
#include "NodeManager.h"


/* no locking used in this file, we assume that the caller (NodeManager) does it.
 */


#define MAX_NODE_INFOS 10
#define MAX_INPUT_INFOS 10

const uint32 kMsgHeader = 'sepx';
const uint32 kMsgTypeVideoIn = 0xffffffef;
const uint32 kMsgTypeVideoOut = 0xffffffee;
const uint32 kMsgTypeAudioIn = 0xfffffffe;
const uint32 kMsgTypeAudioOut = 0xffffffff;

const char *kDefaultManagerType 			= "be:_default";
const char *kDefaultManagerAddon 			= "be:_addon_id";
const char *kDefaultManagerFlavorId 		= "be:_internal_id";
const char *kDefaultManagerFlavorName 		= "be:_flavor_name";
const char *kDefaultManagerPath 			= "be:_path";
const char *kDefaultManagerInput 			= "be:_input_id";

const char *kDefaultManagerSettingsDirectory			= "Media";
const char *kDefaultManagerSettingsFile				= "MDefaultManager";

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static BLocker sInstanceLock("default manager instance");
static DefaultManager* sInstance = NULL;
// SPDX-SnippetEnd


DefaultManager::DefaultManager()
	:
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	fLock("default manager"),
	fAudioOutGeneration(0),
	// SPDX-SnippetEnd
	fMixerConnected(false),
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	fBluetoothAudioReady(false),
	// SPDX-SnippetEnd
	fPhysicalVideoOut(-1),
	fPhysicalVideoIn(-1),
	fPhysicalAudioOut(-1),
	fPhysicalAudioIn(-1),
	fSystemTimeSource(-1),
	fTimeSource(-1),
	fAudioMixer(-1),
	fPhysicalAudioOutInputID(0),
	fSavedLock("saved defaults"),
	fRescanThread(-1),
	fRescanRequested(0),
	fRescanLock("rescan default manager"),
	fRoster(NULL)
{
	strlcpy(fPhysicalAudioOutInputName, "default", B_MEDIA_NAME_LENGTH);
	for (int32 i = 0; i < 4; i++)
		fUsingFallback[i] = false;
	fBeginHeader[0] = 0xab00150b;
	fBeginHeader[1] = 0x18723462;
	fBeginHeader[2] = 0x00000002;
	fEndHeader[0] = 0x7465726d;
	fEndHeader[1] = 0x6d666c67;
	fEndHeader[2] = 0x00000002;

	fRoster = BMediaRoster::Roster();
	if (fRoster == NULL)
		TRACE("DefaultManager: The roster is NULL\n");

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	{
		BAutolock instanceLock(sInstanceLock);
		sInstance = this;
	}
	// SPDX-SnippetEnd
}


DefaultManager::~DefaultManager()
{
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	{
		BAutolock instanceLock(sInstanceLock);
		if (sInstance == this)
			sInstance = NULL;
	}
	thread_id rescanThread = -1;
	{
		BAutolock locker(fRescanLock);
		rescanThread = fRescanThread;
	}
	if (rescanThread >= 0) {
		status_t dummy;
		wait_for_thread(rescanThread, &dummy);
	}

	BAutolock savedLocker(fSavedLock);
	for (int32 i = 0; i < fMsgList.CountItems(); i++)
		delete (BMessage*)fMsgList.ItemAt(i);
	fMsgList.MakeEmpty();
	// SPDX-SnippetEnd
}


// this is called by the media_server *before* any add-ons have been loaded
status_t
DefaultManager::LoadState()
{
	CALLED();
	status_t err = B_OK;
	BPath path;
	if ((err = find_directory(B_USER_SETTINGS_DIRECTORY, &path)) != B_OK)
		return err;

	path.Append(kDefaultManagerSettingsDirectory);
	path.Append(kDefaultManagerSettingsFile);

	BFile file(path.Path(), B_READ_ONLY);

	uint32 categoryCount;
	ssize_t size = sizeof(uint32) * 3;
	if (file.Read(fBeginHeader, size) < size)
		return B_ERROR;
	TRACE("0x%08lx %ld\n", fBeginHeader[0], fBeginHeader[0]);
	TRACE("0x%08lx %ld\n", fBeginHeader[1], fBeginHeader[1]);
	TRACE("0x%08lx %ld\n", fBeginHeader[2], fBeginHeader[2]);
	size = sizeof(uint32);
	if (file.Read(&categoryCount, size) < size) {
		fprintf(stderr,
			"DefaultManager::LoadState() failed to read categoryCount\n");
		return B_ERROR;
	}
	TRACE("DefaultManager::LoadState() categoryCount %ld\n", categoryCount);
	while (categoryCount--) {
		BMessage settings;
		uint32 msg_header;
		uint32 default_type;
		if (file.Read(&msg_header, size) < size) {
			fprintf(stderr,
				"DefaultManager::LoadState() failed to read msg_header\n");
			return B_ERROR;
		}
		if (file.Read(&default_type, size) < size) {
			fprintf(stderr,
				"DefaultManager::LoadState() failed to read default_type\n");
			return B_ERROR;
		}
		if (settings.Unflatten(&file) == B_OK)
			fMsgList.AddItem(new BMessage(settings));
		else
			fprintf(stderr, "DefaultManager::LoadState() failed to unflatten\n");
	}
	size = sizeof(uint32) * 3;
	if (file.Read(fEndHeader,size) < size) {
		fprintf(stderr,
			"DefaultManager::LoadState() failed to read fEndHeader\n");
		return B_ERROR;
	}

	TRACE("LoadState returns B_OK\n");
	return B_OK;
}


status_t
DefaultManager::SaveState(NodeManager *node_manager)
{
	CALLED();
	status_t err = B_OK;
	BPath path;
	BList list;
	if ((err = find_directory(B_USER_SETTINGS_DIRECTORY, &path, true)) != B_OK)
		return err;
	path.Append(kDefaultManagerSettingsDirectory);
	if ((err = create_directory(path.Path(), 0755)) != B_OK)
		return err;
	path.Append(kDefaultManagerSettingsFile);

	uint32 default_types[] = {kMsgTypeVideoIn, kMsgTypeVideoOut,
		kMsgTypeAudioIn, kMsgTypeAudioOut};
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	media_node_id media_node_ids[4];
	int32 audioOutInputID;
	{
		BAutolock stateLock(fLock);
		media_node_ids[0] = fPhysicalVideoIn;
		media_node_ids[1] = fPhysicalVideoOut;
		media_node_ids[2] = fPhysicalAudioIn;
		media_node_ids[3] = fPhysicalAudioOut;
		audioOutInputID = fPhysicalAudioOutInputID;
	}
	// SPDX-SnippetEnd
	for (uint32 i = 0; i < sizeof(default_types) / sizeof(default_types[0]);
		i++) {

		// A fallback, or no node at all, must not replace the saved device.
		// One boot without that device would otherwise forget it.
		BMessage saved;
		if (_CopySavedDefault(default_types[i], saved)
			&& (media_node_ids[i] == -1 || _IsFallback(default_types[i]))) {
			list.AddItem(new BMessage(saved));
			continue;
		}

		// we call the node manager to have more infos about nodes
		dormant_node_info info;
		media_node node;
		entry_ref ref;
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		bool haveClone = node_manager->GetCloneForID(media_node_ids[i],
			be_app->Team(), &node) == B_OK;
		status_t infoStatus = haveClone
			? node_manager->GetDormantNodeInfo(node, &info) : B_ERROR;
		status_t releaseStatus = haveClone
			? node_manager->ReleaseNodeReference(media_node_ids[i],
				be_app->Team()) : B_ERROR;
		if (!haveClone || infoStatus != B_OK || releaseStatus != B_OK
			|| node_manager->GetAddOnRef(info.addon, &ref) != B_OK) {
			if (media_node_ids[i] != -1) {
				// failed to get node info thus just return
				for (int32 j = 0; j < list.CountItems(); j++)
					delete (BMessage*)list.ItemAt(j);
				return B_ERROR;
			}
			continue;
		}
		// SPDX-SnippetEnd

		BMessage *settings = new BMessage();
		settings->AddInt32(kDefaultManagerType, default_types[i]);
		BPath path(&ref);
		settings->AddInt32(kDefaultManagerAddon, info.addon);
		settings->AddInt32(kDefaultManagerFlavorId, info.flavor_id);
		settings->AddInt32(kDefaultManagerInput,
			default_types[i] == kMsgTypeAudioOut ? audioOutInputID : 0);
		settings->AddString(kDefaultManagerFlavorName, info.name);
		settings->AddString(kDefaultManagerPath, path.Path());

		list.AddItem(settings);
		TRACE("message %s added\n", info.name);
	}

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// What is written is now the saved default. NodeManager calls this right
	// after every Set(), so a device chosen in this session is the one the
	// rescan restores, not the one that was saved at startup.
	{
		BAutolock locker(fSavedLock);
		for (int32 i = 0; i < fMsgList.CountItems(); i++)
			delete (BMessage*)fMsgList.ItemAt(i);
		fMsgList.MakeEmpty();
		fMsgList.AddList(&list);
	}
	// SPDX-SnippetEnd

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);

	if (file.Write(fBeginHeader, sizeof(uint32)*3) < (int32)sizeof(uint32)*3)
		return B_ERROR;
	int32 categoryCount = list.CountItems();
	if (file.Write(&categoryCount, sizeof(uint32)) < (int32)sizeof(uint32))
		return B_ERROR;

	for (int32 i = 0; i < categoryCount; i++) {
		BMessage *settings = (BMessage *)list.ItemAt(i);
		uint32 default_type;
		if (settings->FindInt32(kDefaultManagerType,
			(int32*)&default_type) < B_OK)
			return B_ERROR;
		if (file.Write(&kMsgHeader, sizeof(uint32)) < (int32)sizeof(uint32))
			return B_ERROR;
		if (file.Write(&default_type, sizeof(uint32)) < (int32)sizeof(uint32))
			return B_ERROR;
		if (settings->Flatten(&file) < B_OK)
			return B_ERROR;
		// fMsgList owns settings now.
	}
	if (file.Write(fEndHeader, sizeof(uint32)*3) < (int32)sizeof(uint32)*3)
		return B_ERROR;

	return B_OK;
}


status_t
DefaultManager::Set(media_node_id node_id, const char *input_name,
	int32 input_id, node_type type)
{
	CALLED();
	TRACE("DefaultManager::Set type : %i, node : %li, input : %li\n", type,
		node_id, input_id);
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	switch (type) {
		// An explicit choice replaces a fallback, so it is saved as such.
		case VIDEO_INPUT:
		{
			BAutolock stateLock(fLock);
			fPhysicalVideoIn = node_id;
			stateLock.Unlock();
			_SetFallback(kMsgTypeVideoIn, false);
			return B_OK;
		}
		case AUDIO_INPUT:
		{
			BAutolock stateLock(fLock);
			fPhysicalAudioIn = node_id;
			stateLock.Unlock();
			_SetFallback(kMsgTypeAudioIn, false);
			return B_OK;
		}
		case VIDEO_OUTPUT:
		{
			BAutolock stateLock(fLock);
			fPhysicalVideoOut = node_id;
			stateLock.Unlock();
			_SetFallback(kMsgTypeVideoOut, false);
			return B_OK;
		}
		case AUDIO_MIXER:
			return B_ERROR;
		case AUDIO_OUTPUT:
		{
			// Choosing the device that stands in is still a choice. Clear the
			// flag first, so SaveState() keeps it instead of the absent one.
			_SetFallback(kMsgTypeAudioOut, false);
			media_node_id prevNode = -1;
			{
				BAutolock stateLock(fLock);
				if (fPhysicalAudioOut == node_id
					&& fPhysicalAudioOutInputID == input_id)
					return B_OK;

				prevNode = fPhysicalAudioOut;
				fPhysicalAudioOut = node_id;
				fPhysicalAudioOutInputID = input_id;
				strlcpy(fPhysicalAudioOutInputName,
					input_name ? input_name : "<null>", B_MEDIA_NAME_LENGTH);

				fMixerConnected = false;
				fAudioOutGeneration++;
			}
			syslog(LOG_NOTICE, "media_server: default audio output set: "
				"node %" B_PRId32 " -> %" B_PRId32 " (input %" B_PRId32
				" \"%s\")\n", prevNode, node_id, input_id,
				input_name ? input_name : "<null>");
			Rescan();
			return B_OK;
		}
		case TIME_SOURCE:
			return B_ERROR;

		// called by the media_server's ServerApp::StartSystemTimeSource()
		case SYSTEM_TIME_SOURCE:
		{
			BAutolock stateLock(fLock);
			ASSERT(fSystemTimeSource == -1);
			fSystemTimeSource = node_id;
			return B_OK;
		}
	// SPDX-SnippetEnd

		default:
		{
			ERROR("DefaultManager::Set Error: called with unknown type %d\n",
				type);
			return B_ERROR;
		}
	}
}


status_t
DefaultManager::Get(media_node_id *nodeid, char *input_name, int32 *inputid,
	node_type type)
{
	CALLED();
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	BAutolock stateLock(fLock);
	// SPDX-SnippetEnd
	switch (type) {
		case VIDEO_INPUT: 		// output: nodeid
			if (fPhysicalVideoIn == -1)
				return B_NAME_NOT_FOUND;
			*nodeid = fPhysicalVideoIn;
			return B_OK;

		case AUDIO_INPUT: 		// output: nodeid
			if (fPhysicalAudioIn == -1)
				return B_NAME_NOT_FOUND;
			*nodeid = fPhysicalAudioIn;
			return B_OK;

		case VIDEO_OUTPUT: 		// output: nodeid
			if (fPhysicalVideoOut == -1)
				return B_NAME_NOT_FOUND;
			*nodeid = fPhysicalVideoOut;
			return B_OK;

		case AUDIO_OUTPUT:		// output: nodeid
			if (fPhysicalAudioOut == -1)
				return B_NAME_NOT_FOUND;
			*nodeid = fPhysicalAudioOut;
			return B_OK;

		case AUDIO_OUTPUT_EX:	// output: nodeid, input_name, input_id
			if (fPhysicalAudioOut == -1)
				return B_NAME_NOT_FOUND;
			*nodeid = fPhysicalAudioOut;
			*inputid = fPhysicalAudioOutInputID;
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			strlcpy(input_name, fPhysicalAudioOutInputName,
				B_MEDIA_NAME_LENGTH);
			// SPDX-SnippetEnd
			return B_OK;

		case AUDIO_MIXER:		// output: nodeid
			if (fAudioMixer == -1)
				return B_NAME_NOT_FOUND;
			*nodeid = fAudioMixer;
			return B_OK;

		case TIME_SOURCE:
			if (fTimeSource != -1)
				*nodeid = fTimeSource;
			else
				*nodeid = fSystemTimeSource;
			return B_OK;

		case SYSTEM_TIME_SOURCE:
			*nodeid = fSystemTimeSource;
			return B_OK;

		default:
		{
			ERROR("DefaultManager::Get Error: called with unknown type %d\n",
				type);
			return B_ERROR;
		}
	}
}


// this is called by the media_server *after* the initial add-on loading
// has been done
status_t
DefaultManager::Rescan()
{
	BAutolock locker(fRescanLock);
	atomic_add(&fRescanRequested, 1);
	if (fRescanThread < 0) {
		fRescanThread = spawn_thread(rescan_thread, "rescan defaults",
			B_NORMAL_PRIORITY - 2, this);
		resume_thread(fRescanThread);
	}

	return B_OK;
}


int32
DefaultManager::rescan_thread(void *arg)
{
	reinterpret_cast<DefaultManager *>(arg)->_RescanThread();
	return 0;
}


void
DefaultManager::_RescanThread()
{
	TRACE("DefaultManager::_RescanThread() enter\n");

	BAutolock locker(fRescanLock);

	while (atomic_and(&fRescanRequested, 0) != 0) {
		locker.Unlock();

		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// Snapshot fields under fLock before IPC, and only commit results if
		// Set() did not change them concurrently.
		media_node_id systemTimeSource;
		media_node_id videoOut;
		media_node_id videoIn;
		media_node_id audioOut;
		media_node_id audioIn;
		media_node_id audioMixer;
		media_node_id timeSource;
		uint32 audioOutGen;
		{
			BAutolock stateLock(fLock);
			systemTimeSource = fSystemTimeSource;
			videoOut = fPhysicalVideoOut;
			videoIn = fPhysicalVideoIn;
			audioOut = fPhysicalAudioOut;
			audioIn = fPhysicalAudioIn;
			audioMixer = fAudioMixer;
			timeSource = fTimeSource;
			audioOutGen = fAudioOutGeneration;
		}

		// We do not search for the system time source,
		// it should already exist
		ASSERT(systemTimeSource != -1);

		if (videoOut == -1) {
			_FindPhysical(&videoOut, kMsgTypeVideoOut, false,
				B_MEDIA_RAW_VIDEO);
			_FindPhysical(&videoOut, kMsgTypeVideoOut, false,
				B_MEDIA_ENCODED_VIDEO);
			if (videoOut != -1) {
				BAutolock stateLock(fLock);
				if (fPhysicalVideoOut == -1)
					fPhysicalVideoOut = videoOut;
			}
		}
		if (videoIn == -1) {
			_FindPhysical(&videoIn, kMsgTypeVideoIn, true,
				B_MEDIA_RAW_VIDEO);
			_FindPhysical(&videoIn, kMsgTypeVideoIn, true,
				B_MEDIA_ENCODED_VIDEO);
			if (videoIn != -1) {
				BAutolock stateLock(fLock);
				if (fPhysicalVideoIn == -1)
					fPhysicalVideoIn = videoIn;
			}
		}
		if (audioOut != -1 && fRoster != NULL) {
			media_node existing;
			bool outputGone = false;
			if (fRoster->GetNodeFor(audioOut, &existing) != B_OK) {
				outputGone = true;
			} else {
				live_node_info liveInfo;
				if (!_IsBluetoothAudioReady()
					&& fRoster->GetLiveNodeInfo(existing, &liveInfo) == B_OK
					&& strcmp(liveInfo.name, "Bluetooth audio") == 0) {
					outputGone = true;
				}
				fRoster->ReleaseNode(existing);
			}
			if (outputGone) {
				// The output node is gone (or Bluetooth headset disconnected).
				// Look again, including a device that was plugged in after
				// startup (Haiku #12776, #12777).
				bool cleared = false;
				{
					BAutolock stateLock(fLock);
					if (fAudioOutGeneration == audioOutGen
						&& fPhysicalAudioOut == audioOut) {
						fPhysicalAudioOut = -1;
						fMixerConnected = false;
						fAudioOutGeneration++;
						fTimeSource = -1;
						audioOut = -1;
						audioOutGen = fAudioOutGeneration;
						timeSource = -1;
						cleared = true;
					}
				}
				// The mixer still holds its output for the dead input, and
				// the dead node was usually the time source too.
				if (cleared)
					_DisconnectMixerFromOutput();
			}
		}
		if (audioOut == -1) {
			int32 foundInputID = 0;
			_FindPhysical(&audioOut, kMsgTypeAudioOut, false,
				B_MEDIA_RAW_AUDIO, false, &foundInputID);
			if (audioOut != -1) {
				BAutolock stateLock(fLock);
				if (fAudioOutGeneration == audioOutGen
					&& fPhysicalAudioOut == -1) {
					fPhysicalAudioOut = audioOut;
					fPhysicalAudioOutInputID = foundInputID;
					fAudioOutGeneration++;
				}
			}
		}
		if (audioIn == -1) {
			_FindPhysical(&audioIn, kMsgTypeAudioIn, true,
				B_MEDIA_RAW_AUDIO);
			if (audioIn != -1) {
				BAutolock stateLock(fLock);
				if (fPhysicalAudioIn == -1)
					fPhysicalAudioIn = audioIn;
			}
		}

		// A fallback goes back to the saved device once that device has a
		// node. The mixer is reconnected below.
		{
			int32 restoredInputID = 0;
			{
				BAutolock stateLock(fLock);
				audioOut = fPhysicalAudioOut;
				restoredInputID = fPhysicalAudioOutInputID;
				audioOutGen = fAudioOutGeneration;
			}
			media_node_id restoredOut = audioOut;
			_RestoreSaved(&restoredOut, kMsgTypeAudioOut, false,
				B_MEDIA_RAW_AUDIO, &restoredInputID);
			if (restoredOut != audioOut) {
				BAutolock stateLock(fLock);
				if (fAudioOutGeneration == audioOutGen
					&& fPhysicalAudioOut == audioOut) {
					fPhysicalAudioOut = restoredOut;
					fPhysicalAudioOutInputID = restoredInputID;
					fMixerConnected = false;
					fAudioOutGeneration++;
				}
			}
		}
		{
			BAutolock stateLock(fLock);
			audioIn = fPhysicalAudioIn;
		}
		media_node_id restoredIn = audioIn;
		_RestoreSaved(&restoredIn, kMsgTypeAudioIn, true, B_MEDIA_RAW_AUDIO);
		if (restoredIn != audioIn) {
			BAutolock stateLock(fLock);
			if (fPhysicalAudioIn == audioIn)
				fPhysicalAudioIn = restoredIn;
		}
		{
			BAutolock stateLock(fLock);
			videoOut = fPhysicalVideoOut;
		}
		media_node_id restoredVideoOut = videoOut;
		_RestoreSaved(&restoredVideoOut, kMsgTypeVideoOut, false,
			B_MEDIA_RAW_VIDEO);
		_RestoreSaved(&restoredVideoOut, kMsgTypeVideoOut, false,
			B_MEDIA_ENCODED_VIDEO);
		if (restoredVideoOut != videoOut) {
			BAutolock stateLock(fLock);
			if (fPhysicalVideoOut == videoOut)
				fPhysicalVideoOut = restoredVideoOut;
		}
		{
			BAutolock stateLock(fLock);
			videoIn = fPhysicalVideoIn;
		}
		media_node_id restoredVideoIn = videoIn;
		_RestoreSaved(&restoredVideoIn, kMsgTypeVideoIn, true,
			B_MEDIA_RAW_VIDEO);
		_RestoreSaved(&restoredVideoIn, kMsgTypeVideoIn, true,
			B_MEDIA_ENCODED_VIDEO);
		if (restoredVideoIn != videoIn) {
			BAutolock stateLock(fLock);
			if (fPhysicalVideoIn == videoIn)
				fPhysicalVideoIn = restoredVideoIn;
		}

		if (audioMixer == -1) {
			media_node_id foundMixer = _FindAudioMixer();
			if (foundMixer != -1) {
				BAutolock stateLock(fLock);
				if (fAudioMixer == -1)
					fAudioMixer = foundMixer;
			}
		}

		// The normal time source is searched for after the
		// Physical Audio Out has been created.
		{
			BAutolock stateLock(fLock);
			timeSource = fTimeSource;
			audioOut = fPhysicalAudioOut;
		}
		if (timeSource == -1) {
			media_node_id foundTimeSource = _FindTimeSource(audioOut);
			if (foundTimeSource != -1) {
				BAutolock stateLock(fLock);
				if (fTimeSource == -1)
					fTimeSource = foundTimeSource;
			}
		}

		// Connect the mixer and physical audio out (soundcard)
		bool needConnect = false;
		int32 audioOutInputID = 0;
		{
			BAutolock stateLock(fLock);
			if (!fMixerConnected && fAudioMixer != -1
				&& fPhysicalAudioOut != -1) {
				needConnect = true;
				audioMixer = fAudioMixer;
				audioOut = fPhysicalAudioOut;
				audioOutInputID = fPhysicalAudioOutInputID;
				audioOutGen = fAudioOutGeneration;
			}
		}

		bool mixerConnected = false;
		if (needConnect) {
			media_output newOutput;
			media_input newInput;
			if (_ConnectMixerToOutput(audioMixer, audioOut, audioOutInputID,
					newOutput, newInput) == B_OK) {
				BAutolock stateLock(fLock);
				fMixerToPhysicalAudioOut = newOutput;
				fPhysicalAudioOutFromMixer = newInput;
				if (fAudioOutGeneration == audioOutGen) {
					fMixerConnected = true;
					mixerConnected = true;
				}
			} else {
				TRACE("DefaultManager: failed to connect mixer and "
					"soundcard\n");
			}
		} else {
			BAutolock stateLock(fLock);
			mixerConnected = fMixerConnected;
			TRACE("DefaultManager: Did not try to connect mixer and "
				"soundcard\n");
		}

		if (mixerConnected) {
			add_on_server_rescan_finished_notify_command cmd;
			SendToAddOnServer(ADD_ON_SERVER_RESCAN_FINISHED_NOTIFY, &cmd,
				sizeof(cmd));
		}
		// SPDX-SnippetEnd

		locker.Lock();
	}

	fRescanThread = -1;

	BMessage msg(MEDIA_SERVER_RESCAN_COMPLETED);
	be_app->PostMessage(&msg);

	TRACE("DefaultManager::_RescanThread() leave\n");
}


void
DefaultManager::_FindPhysical(media_node_id *id, uint32 default_type,
	bool isInput, media_type type, bool savedOnly, int32 *inputID)
{
	live_node_info info[MAX_NODE_INFOS];
	media_format format;
	int32 count;
	status_t rv;
	BPath msgPath;
	dormant_node_info msgDninfo;
	int32 input_id = 0;
	bool isAudio = (type == B_MEDIA_RAW_AUDIO)
		|| (type == B_MEDIA_ENCODED_AUDIO);

	// The old loop stored the message on every pass and left it pointing at
	// another type when this type had no saved entry.
	// A copy: SaveState() on the server looper replaces fMsgList.
	BMessage savedCopy;
	BMessage *msg = _CopySavedDefault(default_type, savedCopy)
		? &savedCopy : NULL;
	if (msg != NULL) {
		const char *name = NULL;
		const char *path = NULL;
		msgDninfo.addon = -1;
		msgDninfo.flavor_id = -1;
		msgDninfo.name[0] = '\0';
		msg->FindInt32(kDefaultManagerAddon, &msgDninfo.addon);
		msg->FindInt32(kDefaultManagerFlavorId, &msgDninfo.flavor_id);
		msg->FindInt32(kDefaultManagerInput, &input_id);
		msg->FindString(kDefaultManagerFlavorName, &name);
		msg->FindString(kDefaultManagerPath, &path);
		if (name)
			strlcpy(msgDninfo.name, name, sizeof(msgDninfo.name));
		if (path)
			msgPath = BPath(path);
	} else if (savedOnly)
		return;

	format.type = type;
	count = MAX_NODE_INFOS;
	rv = fRoster->GetLiveNodes(&info[0], &count,
		isInput ? NULL : &format, isInput ? &format : NULL, NULL,
		isInput ? B_BUFFER_PRODUCER | B_PHYSICAL_INPUT
			: B_BUFFER_CONSUMER | B_PHYSICAL_OUTPUT);
	if (rv != B_OK || count < 1) {
		TRACE("Couldn't find physical %s %s node\n",
			isAudio ? "audio" : "video", isInput ? "input" : "output");
		return;
	}
	for (int i = 0; i < count; i++)
		TRACE("info[%d].name %s\n", i, info[i].name);

	// A flavor id is the device's index in the add-on. One absent USB device
	// renumbers the rest, so an exact id match can miss a card that is here
	// under the same name. Match the name from the same add-on, and otherwise
	// use the first real device for this session without forgetting the saved
	// one. RenkuOS d1028141c2.
	media_node_id exactMatch = -1;
	media_node_id nameMatch = -1;
	int32 nameMatchIndex = -1;
	int32 nameMatchCount = 0;
	int32 firstReal = -1;

	for (int i = 0; i < count; i++) {
		if (isAudio) {
			if (isInput) {
				if (0 == strcmp(info[i].name, "None In")) {
					// we keep the Null audio driver if none else matchs
					if (!savedOnly)
						*id = info[i].node.node;
					continue;
				}
				// skip the Firewire audio driver
				if (0 == strcmp(info[i].name, "DV Input"))
					continue;
			} else {
				if (0 == strcmp(info[i].name, "None Out")) {
					// we keep the Null audio driver if none else matchs
					if (!savedOnly) {
						*id = info[i].node.node;
						if (msg && inputID != NULL)
							*inputID = input_id;
					}
					continue;
				}
				// skip the Firewire audio driver
				if (0 == strcmp(info[i].name, "DV Output"))
					continue;
			}
		}
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// media_addon_server starts every physical output, Bluetooth audio
		// too (docs/BLUETOOTH.md, step 5). It is the default output only
		// when someone chose it in Media, never the first one found: the
		// sound would go to a headset that may not be there.
		// Moreover, Bluetooth audio only counts as present while a headset
		// has A2DP up; otherwise keep the saved choice and fall back to
		// HD Audio until the headset connects.
		// The headset microphone (step 7c) likewise as the default input.
		if (isAudio && !isInput
			&& strcmp(info[i].name, "Bluetooth audio") == 0
			&& !_IsBluetoothAudioReady()) {
			continue;
		}
		bool chosenOnly = isAudio && ((!isInput
				&& strcmp(info[i].name, "Bluetooth audio") == 0)
			|| (isInput && strcmp(info[i].name,
				"Bluetooth headset microphone") == 0));
		if (firstReal < 0 && !chosenOnly)
			firstReal = i;

		if (msg == NULL) {
			if (firstReal >= 0)
				break;
			continue;
		}
		// SPDX-SnippetEnd

		dormant_node_info dninfo;
		if (fRoster->GetDormantNodeFor(info[i].node, &dninfo) != B_OK) {
			ERROR("Couldn't GetDormantNodeFor\n");
			continue;
		}
		if (strcmp(dninfo.name, msgDninfo.name) != 0) {
			ERROR("Doesn't match name\n");
			continue;
		}
		BPath path;
		if (gDormantNodeManager->FindAddOnPath(&path, dninfo.addon) != B_OK
			|| path != msgPath) {
			ERROR("Doesn't match : path\n");
			continue;
		}
		if (dninfo.flavor_id == msgDninfo.flavor_id) {
			exactMatch = info[i].node.node;
			break;
		}
		if (nameMatch < 0) {
			nameMatch = info[i].node.node;
			nameMatchIndex = i;
		}
		nameMatchCount++;
	}

	if (msg != NULL && (exactMatch >= 0 || nameMatch >= 0)) {
		*id = exactMatch >= 0 ? exactMatch : nameMatch;
		if (exactMatch < 0) {
			syslog(LOG_NOTICE, "media_server: default %s %s \"%s\" restored by"
				" name; its flavor id moved from %" B_PRId32 "\n",
				isAudio ? "audio" : "video", isInput ? "input" : "output",
				info[nameMatchIndex].name, msgDninfo.flavor_id);
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			if (nameMatchCount > 1) {
				syslog(LOG_NOTICE, "media_server: %" B_PRId32 " devices are"
					" named \"%s\"; took the first, which may be another one"
					" of them\n", nameMatchCount, msgDninfo.name);
			}
			// SPDX-SnippetEnd
		}
		if (isAudio && !isInput && inputID != NULL)
			*inputID = input_id;
		_SetFallback(default_type, false);
		if (savedOnly) {
			syslog(LOG_NOTICE, "media_server: default %s %s \"%s\" is present"
				" again; using it\n", isAudio ? "audio" : "video",
				isInput ? "input" : "output", msgDninfo.name);
		}
		TRACE("Default physical %s %s restored!\n",
			isAudio ? "audio" : "video", isInput ? "input" : "output");
		return;
	}

	if (savedOnly)
		return;

	if (firstReal < 0) {
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// Only a "None" node stands in. It must not replace the saved device
		// either.
		if (msg != NULL && *id != -1)
			_SetFallback(default_type, true);
		// SPDX-SnippetEnd
		return;
	}

	*id = info[firstReal].node.node;
	if (msg != NULL) {
		_SetFallback(default_type, true);
		syslog(LOG_NOTICE, "media_server: default %s %s \"%s\" not present;"
			" using \"%s\" until it is\n", isAudio ? "audio" : "video",
			isInput ? "input" : "output", msgDninfo.name,
			info[firstReal].name);
	} else
		_SetFallback(default_type, false);
	TRACE("Default physical %s %s \"%s\" created!\n",
		isAudio ? "audio" : "video", isInput ? "input" : "output",
		info[firstReal].name);
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
bool
DefaultManager::_CopySavedDefault(uint32 defaultType, BMessage& saved) const
{
	BAutolock locker(fSavedLock);
	BMessage* msg = _SavedDefault(defaultType);
	if (msg == NULL)
		return false;
	saved = *msg;
	return true;
}


bool
DefaultManager::_IsFallback(uint32 defaultType) const
{
	BAutolock locker(fSavedLock);
	return fUsingFallback[_TypeIndex(defaultType)];
}


void
DefaultManager::_SetFallback(uint32 defaultType, bool fallback)
{
	BAutolock locker(fSavedLock);
	fUsingFallback[_TypeIndex(defaultType)] = fallback;
}


void
DefaultManager::_RestoreSaved(media_node_id* id, uint32 defaultType,
	bool isInput, media_type type, int32* inputID)
{
	// A fallback goes back to the saved device once that device has a node.
	if (*id == -1 || !_IsFallback(defaultType))
		return;

	media_node_id saved = *id;
	_FindPhysical(&saved, defaultType, isInput, type, true, inputID);
	*id = saved;
}


void
DefaultManager::_DisconnectMixerFromOutput()
{
	// The roster tells the producer even when the consumer has gone, so the
	// mixer frees its one output. Without that, GetFreeOutputsFor() finds
	// none and the next card is never connected.
	media_output oldOutput;
	media_input oldInput;
	{
		BAutolock stateLock(fLock);
		oldOutput = fMixerToPhysicalAudioOut;
		oldInput = fPhysicalAudioOutFromMixer;
		fMixerToPhysicalAudioOut.node = media_node::null;
		fPhysicalAudioOutFromMixer.node = media_node::null;
	}
	if (oldInput.node != media_node::null) {
		status_t status = fRoster->Disconnect(oldOutput, oldInput);
		syslog(LOG_NOTICE, "media_server: mixer disconnect from output node "
			"%" B_PRId32 ": %s\n", oldInput.node.node, strerror(status));
	}
}


void
DefaultManager::SetBluetoothAudioReady(bool ready)
{
	BAutolock instanceLock(sInstanceLock);
	if (sInstance != NULL)
		sInstance->_SetBluetoothAudioReady(ready);
}


void
DefaultManager::_SetBluetoothAudioReady(bool ready)
{
	{
		BAutolock stateLock(fLock);
		if (fBluetoothAudioReady == ready)
			return;
		fBluetoothAudioReady = ready;
	}
	syslog(LOG_NOTICE, "media_server: Bluetooth audio headset %s\n",
		ready ? "connected (A2DP up)" : "disconnected");
	Rescan();
}


bool
DefaultManager::_IsBluetoothAudioReady() const
{
	BAutolock stateLock(fLock);
	return fBluetoothAudioReady;
}
// SPDX-SnippetEnd


int32
DefaultManager::_TypeIndex(uint32 defaultType)
{
	switch (defaultType) {
		case kMsgTypeVideoIn:	return 0;
		case kMsgTypeVideoOut:	return 1;
		case kMsgTypeAudioIn:	return 2;
		default:				return 3;
	}
}


BMessage*
DefaultManager::_SavedDefault(uint32 defaultType) const
{
	for (int32 i = 0; i < fMsgList.CountItems(); i++) {
		BMessage* msg = (BMessage*)fMsgList.ItemAt(i);
		int32 msgType;
		if (msg->FindInt32(kDefaultManagerType, &msgType) == B_OK
			&& (uint32)msgType == defaultType)
			return msg;
	}
	return NULL;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
media_node_id
DefaultManager::_FindTimeSource(media_node_id physicalAudioOut)
{
	live_node_info info[MAX_NODE_INFOS];
	media_format input; /* a physical audio output has a logical data input (DAC)*/
	int32 count;
	status_t rv;

	/* First try to use the current default physical audio out
	 */
	if (physicalAudioOut != -1) {
		media_node clone;
		if (fRoster->GetNodeFor(physicalAudioOut,
				&clone) == B_OK) {
			if (clone.kind & B_TIME_SOURCE) {
				media_node_id timeSource = clone.node;
				fRoster->StartTimeSource(clone,
					system_time() + 1000);
				fRoster->ReleaseNode(clone);
				TRACE("Default DAC timesource created!\n");
				return timeSource;
			}
			fRoster->ReleaseNode(clone);
		} else {
			TRACE("Default DAC is not a timesource!\n");
		}
	} else {
		TRACE("Default DAC node does not exist!\n");
	}

	/* Now try to find another physical audio out node
	 */
	input.type = B_MEDIA_RAW_AUDIO;
	count = MAX_NODE_INFOS;
	rv = fRoster->GetLiveNodes(&info[0], &count, &input, NULL, NULL,
		B_TIME_SOURCE | B_PHYSICAL_OUTPUT);
	if (rv == B_OK && count >= 1) {
		for (int i = 0; i < count; i++)
			printf("info[%d].name %s\n", i, info[i].name);

		for (int i = 0; i < count; i++) {
			// The BeOS R5 None Out node pretend to be a physical time source,
			// that is pretty dumb
			// skip the Null audio driver
			if (0 == strcmp(info[i].name, "None Out"))
				continue;
			// skip the Firewire audio driver
			if (0 != strstr(info[i].name, "DV Output"))
				continue;
			TRACE("Default DAC timesource \"%s\" created!\n", info[i].name);
			media_node_id timeSource = info[i].node.node;
			fRoster->StartTimeSource(info[i].node,
				system_time() + 1000);
			return timeSource;
		}
	} else {
		TRACE("Couldn't find DAC timesource node\n");
	}

	/* XXX we might use other audio or video clock timesources
	 */
	return -1;
}


media_node_id
DefaultManager::_FindAudioMixer()
{
	live_node_info info;
	int32 count;
	status_t rv;

	if (fRoster == NULL)
		fRoster = BMediaRoster::Roster();

	count = 1;
	rv = fRoster->GetLiveNodes(&info, &count, NULL, NULL, NULL,
		B_BUFFER_PRODUCER | B_BUFFER_CONSUMER | B_SYSTEM_MIXER);
	if (rv != B_OK || count != 1) {
		TRACE("Couldn't find audio mixer node\n");
		return -1;
	}
	TRACE("Default audio mixer node created\n");
	return info.node.node;
}


status_t
DefaultManager::_ConnectMixerToOutput(media_node_id audioMixer,
	media_node_id physicalAudioOut, int32 inputID, media_output& newOutput,
	media_input& newInput)
{
	media_output oldOutput;
	media_input oldInput;
	bool hadOldConnection = false;
	{
		BAutolock stateLock(fLock);
		if (fPhysicalAudioOutFromMixer.node != media_node::null) {
			oldOutput = fMixerToPhysicalAudioOut;
			oldInput = fPhysicalAudioOutFromMixer;
			fMixerToPhysicalAudioOut.node = media_node::null;
			fPhysicalAudioOutFromMixer.node = media_node::null;
			hadOldConnection = true;
		}
	}
	if (hadOldConnection) {
		// Disconnect the old output. (The mixer should be in auto-stop mode,
		// and stop the output on its own.)
		status_t status = fRoster->Disconnect(oldOutput, oldInput);
		syslog(LOG_NOTICE, "media_server: mixer disconnect from output node "
			"%" B_PRId32 ": %s\n", oldInput.node.node, strerror(status));

		// Find and start the new timesource.
		media_node_id newTimeSource = _FindTimeSource(physicalAudioOut);
		if (newTimeSource != -1) {
			BAutolock stateLock(fLock);
			fTimeSource = newTimeSource;
		}
	}

	media_node timesource, mixer, soundcard;
	media_input inputs[MAX_INPUT_INFOS];
	media_input input;
	media_output output;
	media_input newinput;
	media_output newoutput;
	media_format format;
	int32 count;

	if (fRoster == NULL)
		fRoster = BMediaRoster::Roster();

	status_t rv = fRoster->GetNodeFor(physicalAudioOut, &soundcard);
	if (rv != B_OK) {
		syslog(LOG_WARNING, "media_server: mixer connect failed: "
			"GetNodeFor(output %" B_PRId32 ") = %s\n", physicalAudioOut,
			strerror(rv));
		return B_ERROR;
	}

	rv = fRoster->GetNodeFor(audioMixer, &mixer);
// SPDX-SnippetEnd
	if (rv != B_OK) {
		fRoster->ReleaseNode(soundcard);
		syslog(LOG_WARNING, "media_server: mixer connect failed: "
			"GetNodeFor(mixer %" B_PRId32 ") = %s\n", audioMixer,
			strerror(rv));
		return B_ERROR;
	}

	// we now have the mixer and soundcard nodes,
	// find a free input/output and connect them

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// The default time source can be a node that was just unplugged.
	// GetTimeSource() adds a node reference, released in finish.
	bool haveTimeSource = fRoster->GetTimeSource(&timesource) == B_OK;
	if (haveTimeSource) {
		fRoster->SetTimeSourceFor(soundcard.node, timesource.node);
		fRoster->SetTimeSourceFor(mixer.node, timesource.node);
	} else {
		syslog(LOG_WARNING, "media_server: no time source for output node "
			"%" B_PRId32 "\n", soundcard.node);
	}
	// SPDX-SnippetEnd

	rv = fRoster->GetFreeOutputsFor(mixer, &output, 1, &count,
		B_MEDIA_RAW_AUDIO);
	if (rv != B_OK || count != 1) {
		syslog(LOG_WARNING, "media_server: mixer connect to node %" B_PRId32
			" failed: no free mixer output (%s, count=%" B_PRId32 ")\n",
			soundcard.node, strerror(rv), count);
		rv = B_ERROR;
		goto finish;
	}

	rv = fRoster->GetFreeInputsFor(soundcard, inputs, MAX_INPUT_INFOS, &count,
		B_MEDIA_RAW_AUDIO);
	if (rv != B_OK || count < 1) {
		syslog(LOG_WARNING, "media_server: mixer connect to node %" B_PRId32
			" failed: no free output inputs (%s, count=%" B_PRId32 ")\n",
			soundcard.node, strerror(rv), count);
		rv = B_ERROR;
		goto finish;
	}

	for (int32 i = 0; i < count; i++) {
		input = inputs[i];
		if (input.destination.id == inputID)
			break;
	}

	for (int i = 0; i < 6; i++) {
		switch (i) {
			case 0:
				TRACE("DefaultManager: Trying connect in native format (1)\n");
				if (fRoster->GetFormatFor(input, &format) != B_OK) {
					ERROR("DefaultManager: GetFormatFor failed\n");
					continue;
				}
				// XXX BeOS R5 multiaudio node bug workaround
				if (format.u.raw_audio.channel_count == 1) {
					TRACE("##### WARNING! DefaultManager: ignored mono format\n");
					continue;
				}
				break;

			case 1:
				TRACE("DefaultManager: Trying connect in format 1\n");
				format.Clear();
				format.type = B_MEDIA_RAW_AUDIO;
				format.u.raw_audio.frame_rate = 44100;
				format.u.raw_audio.channel_count = 2;
				format.u.raw_audio.format = 0x2;
				break;

			case 2:
				TRACE("DefaultManager: Trying connect in format 2\n");
				format.Clear();
				format.type = B_MEDIA_RAW_AUDIO;
				format.u.raw_audio.frame_rate = 48000;
				format.u.raw_audio.channel_count = 2;
				format.u.raw_audio.format = 0x2;
				break;

			case 3:
				TRACE("DefaultManager: Trying connect in format 3\n");
				format.Clear();
				format.type = B_MEDIA_RAW_AUDIO;
				break;

			case 4:
				// BeOS R5 multiaudio node bug workaround
				TRACE("DefaultManager: Trying connect in native format (2)\n");
				if (fRoster->GetFormatFor(input, &format) != B_OK) {
					ERROR("DefaultManager: GetFormatFor failed\n");
					continue;
				}
				break;

			case 5:
				TRACE("DefaultManager: Trying connect in format 4\n");
				format.Clear();
				break;

		}
		rv = fRoster->Connect(output.source, input.destination, &format,
			&newoutput, &newinput);
		if (rv == B_OK)
			break;
	}
	if (rv != B_OK) {
		syslog(LOG_WARNING, "media_server: mixer connect to node %" B_PRId32
			" (input %" B_PRId32 ") failed: %s\n", soundcard.node,
			input.destination.id, strerror(rv));
		goto finish;
	}

	fRoster->SetRunModeNode(mixer, BMediaNode::B_INCREASE_LATENCY);
	fRoster->SetRunModeNode(soundcard, BMediaNode::B_RECORDING);

	fRoster->PrerollNode(mixer);
	fRoster->PrerollNode(soundcard);

	{
		BTimeSource* ts = fRoster->MakeTimeSourceFor(mixer);
		bigtime_t startAt;
		if (ts == NULL) {
			syslog(LOG_WARNING, "media_server: mixer connect to node %" B_PRId32
				" failed: MakeTimeSourceFor returned NULL\n", soundcard.node);
			rv = B_ERROR;
			goto finish;
		}

		startAt = ts->Now() + 50000;
		status_t startStatus = fRoster->StartNode(mixer, startAt);
		syslog(LOG_NOTICE, "media_server: mixer (%" B_PRId32 ") connected to "
			"output (%" B_PRId32 ", input %" B_PRId32 ", ts %" B_PRId32 "): "
			"%.0f Hz %" B_PRIu32 " ch fmt 0x%" B_PRIx32 " buf %zu, start=%s\n",
			mixer.node, soundcard.node, newinput.destination.id,
			haveTimeSource ? timesource.node : -1,
			format.u.raw_audio.frame_rate, format.u.raw_audio.channel_count,
			format.u.raw_audio.format, format.u.raw_audio.buffer_size,
			strerror(startStatus));

		// The mixer will be in "auto-stop" mode, so it'll start the output on its own.
		ts->Release();
	}

	newOutput = newoutput;
	newInput = newinput;

finish:
	fRoster->ReleaseNode(mixer);
	fRoster->ReleaseNode(soundcard);
	if (haveTimeSource)
		fRoster->ReleaseNode(timesource);
	return rv;
}


void
DefaultManager::Dump()
{
}


void
DefaultManager::CleanupTeam(team_id team)
{
}
