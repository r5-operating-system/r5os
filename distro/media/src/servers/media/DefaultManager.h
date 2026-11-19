/*
 * Copyright 2010, Haiku. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Marcus Overhagen
 *		Jérôme Duval
 */
/*
 * Copyright 2002, Marcus Overhagen. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _DEFAULT_MANAGER_H
#define _DEFAULT_MANAGER_H


/*!	Manager for defaults (audio and video, input and output)
*/


#include "DataExchange.h"

#include <Autolock.h>
#include <MediaRoster.h>
#include <Message.h>

class NodeManager;


class DefaultManager {
public:
								DefaultManager();
								~DefaultManager();

			status_t 			LoadState();
			status_t			SaveState(NodeManager *node_manager);

			status_t			Set(media_node_id nodeid,
									const char *input_name, int32 input_id,
									node_type type);
			status_t			Get(media_node_id *nodeid, char *input_name,
									int32 *input_id, node_type type);
			status_t			Rescan();

			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
	static	void				SetBluetoothAudioReady(bool ready);
			// SPDX-SnippetEnd

			void				Dump();

			void 				CleanupTeam(team_id team);

private:
			static int32		rescan_thread(void *arg);
			void				_RescanThread();

			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			// savedOnly leaves *id alone when the saved device is absent.
			void				_FindPhysical(media_node_id *id,
									uint32 default_type, bool isInput,
									media_type type, bool savedOnly = false,
									int32 *inputID = NULL);
			media_node_id		_FindAudioMixer();
			media_node_id		_FindTimeSource(media_node_id physicalAudioOut);

			status_t			_ConnectMixerToOutput(media_node_id audioMixer,
									media_node_id physicalAudioOut,
									int32 inputID, media_output& newOutput,
									media_input& newInput);
			// SPDX-SnippetEnd

			// See _FindPhysical().
			static	int32		_TypeIndex(uint32 defaultType);
			BMessage*			_SavedDefault(uint32 defaultType) const;

			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			// The rescan thread and the server looper both use the saved
			// defaults and the fallback flags. These take fSavedLock.
			bool				_CopySavedDefault(uint32 defaultType,
									BMessage& saved) const;
			bool				_IsFallback(uint32 defaultType) const;
			void				_SetFallback(uint32 defaultType,
									bool fallback);
			void				_RestoreSaved(media_node_id* id,
									uint32 defaultType, bool isInput,
									media_type type, int32* inputID = NULL);
			void				_DisconnectMixerFromOutput();
			void				_SetBluetoothAudioReady(bool ready);
			bool				_IsBluetoothAudioReady() const;
			// SPDX-SnippetEnd

private:
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
	mutable	BLocker				fLock;
			uint32				fAudioOutGeneration;
			bool		 		fMixerConnected;
			bool				fBluetoothAudioReady;
			media_node_id		fPhysicalVideoOut;
			media_node_id		fPhysicalVideoIn;
			media_node_id		fPhysicalAudioOut;
			media_node_id		fPhysicalAudioIn;
			media_node_id		fSystemTimeSource;
			media_node_id		fTimeSource;
			media_node_id		fAudioMixer;
			int32		 		fPhysicalAudioOutInputID;
			// SPDX-SnippetEnd
			char fPhysicalAudioOutInputName[B_MEDIA_NAME_LENGTH];

			media_output 		fMixerToPhysicalAudioOut;
			media_input 		fPhysicalAudioOutFromMixer;

			BList				fMsgList;

			// Per default type (_TypeIndex()). True when the current default
			// is standing in because the saved device was absent. SaveState()
			// then keeps the saved preference.
			bool				fUsingFallback[4];
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			// Guards fMsgList and fUsingFallback.
	mutable	BLocker				fSavedLock;
			// SPDX-SnippetEnd

			uint32				fBeginHeader[3];
			uint32				fEndHeader[3];
			thread_id			fRescanThread;
			int32 				fRescanRequested;
			BLocker				fRescanLock;
			BMediaRoster*		fRoster;
};

#endif // _DEFAULT_MANAGER_H
