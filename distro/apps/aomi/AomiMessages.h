/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef AOMI_MESSAGES_H
#define AOMI_MESSAGES_H


enum {
	MSG_GO				= 'go!!',
	MSG_BACK			= 'back',
	MSG_FORWARD			= 'fwrd',
	MSG_RELOAD			= 'reld',
	MSG_HOME			= 'home',
	MSG_NEW_TAB			= 'ntab',
	MSG_CLOSE_TAB		= 'ctab',
	MSG_SELECT_TAB		= 'stab',
	MSG_NEW_WINDOW		= 'nwin',
	MSG_NEW_PRIVATE		= 'nprv',
	MSG_DOWNLOADS		= 'dnld',
	MSG_MENU			= 'menu',
	MSG_STAR			= 'star',
	MSG_LOCK			= 'lock',
	MSG_FIND			= 'find',
	MSG_FIND_NEXT		= 'fndn',
	MSG_ZOOM_IN			= 'zmin',
	MSG_ZOOM_OUT		= 'zmot',
	MSG_ZOOM_RESET		= 'zmrs',
	MSG_SAVE			= 'save',
	MSG_SOURCE			= 'src!',
	MSG_PRINT			= 'prnt',
	MSG_MENU_BAR		= 'mbar',
	MSG_BOOKMARKS_BAR	= 'bbar',
	MSG_SETTINGS		= 'set!',
	MSG_ABOUT			= 'abut',
	MSG_OPEN_BOOKMARK	= 'obmk',
	MSG_MANAGE_BOOKMARKS = 'mbmk',
	MSG_BOOKMARK_PAGE	= 'bmkp',
	MSG_SETTINGS_CHANGED = 'stch',
	// The Aomi settings window asks a browser window for its address; the
	// reply has "url".
	MSG_GET_ADDRESS		= 'gadr',
	MSG_SAVE_FILE		= 'svfl',
	MSG_AUTH_OK			= 'auok',
	MSG_AUTH_CANCEL		= 'aucx',
	MSG_SHOW_ALL_DOWNLOADS = 'dall',
	MSG_OPEN_FOLDER		= 'ofld',
	MSG_NTP_GO			= 'ntp!',
	MSG_TECHNICAL		= 'tech',
	MSG_COPY_TECHNICAL	= 'ctec',
	MSG_DOWNLOAD_URL	= 'dlur',
	MSG_OPEN_URL		= 'ourl',
	MSG_FOCUS_ADDRESS	= 'flct',
	MSG_DOWNLOAD_SAVE	= 'dlsv',
	MSG_NETWORK_RETRY	= 'nrty',
	MSG_FIND_PREVIOUS	= 'fndp',
	MSG_FIND_CLOSE		= 'fndx',
	MSG_OPEN_BAR_ITEM	= 'obar',
	MSG_BOOKMARKS_CHANGED = 'bmch'
};


#endif
