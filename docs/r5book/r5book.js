// SPDX-FileCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
//
// The R5Book frame: the top bar with search, the grouped contents on the left,
// and the footer, the same on every page. It also exports R5_SEARCH_INDEX for
// the top-bar search and class-index.html, and defines the line glyphs with
// the same geometry as distro/icons/R5Icons.cpp.
(function () {
	const here = location.pathname.split("/").pop() || "index.html";
	const page = document.body.dataset.page || "index";

	const groups = [
		{
			title: "Start here",
			nav: "start",
			pages: [
				{
					file: "index.html",
					label: "Introduction & Kit map",
					items: [
						["index.html#linking", "Linking a program"],
						["index.html#rules", "Rules for LibR5"],
					],
				},
			],
		},
		{
			title: "Kits",
			nav: "kits",
			pages: [
				{
					file: "admin-kit.html",
					label: "Admin Kit",
					items: [
						["admin-kit.html#why", "Why not ReturnAddress()"],
						["admin-kit.html#service", "Service side"],
						["admin-kit.html#client", "Client side"],
						["admin-kit.html#services", "Where it is used"],
					],
				},
				{
					file: "appearance-kit.html",
					label: "Appearance Kit",
					groupFiles: [
						"appearance-kit.html",
						"appearance-cards.html",
						"appearance-controls.html",
						"appearance-settings.html",
						"appearance-effects.html",
					],
					subpages: [
						{
							file: "appearance-cards.html",
							label: "Cards & layout",
							items: [
								["appearance-cards.html#BActionCard", "BActionCard"],
								["appearance-cards.html#BCard", "BCard"],
								["appearance-cards.html#BCardStrip", "BCardStrip"],
								["appearance-cards.html#BCardStripItem", "BCardStripItem"],
							],
						},
						{
							file: "appearance-controls.html",
							label: "Controls & views",
							items: [
								["appearance-controls.html#BAccentButton", "BAccentButton"],
								["appearance-controls.html#BAxisBar", "BAxisBar"],
								["appearance-controls.html#BIconView", "BIconView"],
								["appearance-controls.html#BPasswordControl", "BPasswordControl"],
								["appearance-controls.html#BScreenArrangement", "BScreenArrangement"],
								["appearance-controls.html#BTiltLevel", "BTiltLevel"],
							],
						},
						{
							file: "appearance-settings.html",
							label: "Settings & accounts",
							items: [
								["appearance-settings.html#BAdminLockBar", "BAdminLockBar"],
								["appearance-settings.html#BPageHeader", "BPageHeader"],
								["appearance-settings.html#BSidebar", "BSidebar"],
								["appearance-settings.html#BSidebarItem", "BSidebarItem"],
								["appearance-settings.html#BUserCard", "BUserCard"],
								["appearance-settings.html#BUserListItem", "BUserListItem"],
							],
						},
						{
							file: "appearance-effects.html",
							label: "Functions & effects",
							items: [
								["appearance-effects.html#functions", "Functions"],
								["appearance-effects.html#window-effects", "Window effects"],
							],
						},
					],
				},
				{
					file: "bluetooth-kit.html",
					label: "Bluetooth Kit",
					items: [
						["bluetooth-kit.html#view", "BluetoothView"],
						["bluetooth-kit.html#controller", "BluetoothController"],
						["bluetooth-kit.html#state", "BluetoothState"],
						["bluetooth-kit.html#switch", "The Bluetooth switch"],
						["bluetooth-kit.html#remote", "RemoteDevice"],
						["bluetooth-kit.html#hid", "Controllers, mice & keyboards"],
						["bluetooth-kit.html#le", "Low Energy devices"],
						["bluetooth-kit.html#le-l2cap", "sockaddr_l2cap_le"],
						["bluetooth-kit.html#audio", "Headphones and speakers"],
						["bluetooth-kit.html#settings", "Where people see it"],
					],
				},
				{
					file: "game-kit.html",
					label: "Game Kit",
					items: [
						["game-kit.html#controller", "BGameController"],
						["game-kit.html#example", "Example"],
						["game-kit.html#view", "BGameControllerView"],
						["game-kit.html#picture-bitmap", "PictureBitmap()"],
						["game-kit.html#feel", "Rumble, triggers & motion"],
						["game-kit.html#motion-view", "BGameControllerMotionView"],
						["game-kit.html#settings", "Where people see it"],
					],
				},
				{
					file: "icons-kit.html",
					label: "Icons Kit",
					items: [
						["icons-kit.html#icon-bitmaps", "Icon bitmaps"],
						["icons-kit.html#icon-buttons", "Icon buttons"],
						["icons-kit.html#r5_glyph", "r5_glyph"],
						["icons-kit.html#r5_level_glyph", "r5_level_glyph"],
						["icons-kit.html#r5_icon", "r5_icon"],
					],
				},
				{
					file: "midi2-kit.html",
					label: "MIDI 2 Kit",
					items: [
						["midi2-kit.html#sending", "BMidiLocalProducer"],
						["midi2-kit.html#ump", "The Universal MIDI Packet"],
						["midi2-kit.html#receiving", "BMidiLocalConsumer"],
						["midi2-kit.html#per-note", "Per-note controllers"],
						["midi2-kit.html#property-exchange", "Property Exchange"],
						["midi2-kit.html#profiles", "Profiles"],
						["midi2-kit.html#discovery", "Discovery"],
						["midi2-kit.html#identity", "Your program's identity"],
						["midi2-kit.html#usb", "USB MIDI 2.0 descriptors"],
						["midi2-kit.html#drivers", "For driver writers"],
					],
				},
				{
					file: "wifi-kit.html",
					label: "Wi-Fi Kit",
					items: [
						["wifi-kit.html#view", "WiFiView"],
						["wifi-kit.html#controller", "WiFiController"],
						["wifi-kit.html#state", "WiFiState"],
						["wifi-kit.html#glyphs", "Glyphs"],
						["wifi-kit.html#list", "Scan results"],
						["wifi-kit.html#roster", "BNetworkRoster"],
						["wifi-kit.html#link", "The link"],
						["wifi-kit.html#settings", "Where people see it"],
					],
				},
			],
		},
		{
			title: "Guides",
			nav: "guides",
			pages: [
				{
					file: "settings-modules.html",
					label: "Settings modules",
					items: [
						["settings-modules.html#files", "Where it goes"],
						["settings-modules.html#entry-points", "Entry points"],
						["settings-modules.html#SettingsModule", "SettingsModule"],
						["settings-modules.html#SettingsContext", "SettingsContext"],
						["settings-modules.html#storage", "Where the values go"],
						["settings-modules.html#admin", "The lock"],
						["settings-modules.html#example", "Example"],
						["settings-modules.html#limits", "Not yet"],
					],
				},
				{
					file: "accounts.html",
					label: "Accounts & sessions",
					items: [
						["accounts.html#who", "Who your program runs as"],
						["accounts.html#paths", "Folders and settings"],
						["accounts.html#admin", "Administrator changes"],
						["accounts.html#session", "Lock and log out"],
						["accounts.html#example", "Example: Sign in"],
					],
				},
				{
					file: "vulkan.html",
					label: "Vulkan in a window",
					items: [
						["vulkan.html#path", "How a frame reaches the screen"],
						["vulkan.html#surface", "vkCreateViewSurfaceRFIVE"],
						["vulkan.html#present", "vkQueuePresentKHR"],
						["vulkan.html#modes", "Present modes"],
						["vulkan.html#fallback", "GPU or software"],
						["vulkan.html#porting", "Porting a program"],
					],
				},
				{
					file: "guidelines.html",
					label: "Interface guidelines",
					items: [
						["guidelines.html#forms", "Forms"],
						["guidelines.html#cards", "Cards"],
						["guidelines.html#actions", "Buttons"],
						["guidelines.html#icons", "Icons"],
						["guidelines.html#settings", "Settings pages"],
						["guidelines.html#people", "People and accounts"],
						["guidelines.html#words", "Words"],
					],
				},
			],
		},
		{
			title: "Reference",
			nav: "reference",
			pages: [
				{
					file: "haiku-additions.html",
					label: "Additions to Haiku kits",
					items: [
						["haiku-additions.html#BFormattingConventions", "BFormattingConventions"],
						["haiku-additions.html#BLocaleRoster", "BLocaleRoster"],
						["haiku-additions.html#BUSBEndpoint", "BUSBEndpoint"],
						["haiku-additions.html#decorator", "Decorator hooks"],
						["haiku-additions.html#wheel-phase", "input_wheel_phase"],
						["haiku-additions.html#usb-speed", "USB device speed"],
					],
				},
				{
					file: "class-index.html",
					label: "Class & call index",
					items: [],
				},
			],
		},
	];

	// Search & Class Index entries (single source of truth, works from file://)
	window.R5_SEARCH_INDEX = [
		// Classes & types
		{ name: "BAccentButton", kind: "class", where: "Appearance: Controls", header: "<R5UI.h>", lib: "libr5ui.so", href: "appearance-controls.html#BAccentButton", desc: "Accent-colored primary action button." },
		{ name: "BActionCard", kind: "class", where: "Appearance: Cards", header: "<Card.h>", lib: "libr5ui.so", href: "appearance-cards.html#BActionCard", desc: "Clickable card with title, subtitle, and tags." },
		{ name: "BAdminLockBar", kind: "class", where: "Appearance: Settings", header: "<R5UI.h>", lib: "libr5ui.so", href: "appearance-settings.html#BAdminLockBar", desc: "Lock button paired with Revert and Defaults." },
		{ name: "BAxisBar", kind: "class", where: "Appearance: Controls", header: "<R5UI.h>", lib: "libr5ui.so", href: "appearance-controls.html#BAxisBar", desc: "Live bar for a game controller axis or trigger." },
		{ name: "BCard", kind: "class", where: "Appearance: Cards", header: "<Card.h>", lib: "libr5ui.so", href: "appearance-cards.html#BCard", desc: "Framed card container for child views." },
		{ name: "BCardStrip", kind: "class", where: "Appearance: Cards", header: "<CardStrip.h>", lib: "libr5ui.so", href: "appearance-cards.html#BCardStrip", desc: "Horizontal row of selectable picture cards." },
		{ name: "BCardStripItem", kind: "class", where: "Appearance: Cards", header: "<CardStripItem.h>", lib: "libr5ui.so", href: "appearance-cards.html#BCardStripItem", desc: "One card item inside a BCardStrip." },
		{ name: "BFormattingConventions::CoerceFormatForClock()", kind: "method", where: "Haiku kits", header: "<FormattingConventions.h>", lib: "libbe.so", href: "haiku-additions.html#BFormattingConventions", desc: "Adjusts a time pattern to 12- or 24-hour preference." },
		{ name: "BGameController", kind: "class", where: "Game Kit", header: "<GameController.h>", lib: "libr5game.so", href: "game-kit.html#controller", desc: "Reads game controller buttons, axes, battery, and rumble." },
		{ name: "BGameControllerMotionView", kind: "class", where: "Game Kit", header: "<GameControllerView.h>", lib: "libr5game.so", href: "game-kit.html#motion-view", desc: "Live view of controller gyro and accelerometer." },
		{ name: "BGameControllerView", kind: "class", where: "Game Kit", header: "<GameControllerView.h>", lib: "libr5game.so", href: "game-kit.html#view", desc: "Live picture of a game controller with pressed inputs." },
		{ name: "BGameControllerView::PictureBitmap()", kind: "method", where: "Game Kit", header: "<GameControllerView.h>", lib: "libr5game.so", href: "game-kit.html#picture-bitmap", desc: "Renders a controller outline bitmap at a chosen size." },
		{ name: "BIconView", kind: "class", where: "Appearance: Controls", header: "<R5IconView.h>", lib: "libr5ui.so", href: "appearance-controls.html#BIconView", desc: "Draws an icon bitmap pixel-for-pixel without stretching." },
		{ name: "BLocaleRoster::ApplySettings()", kind: "method", where: "Haiku kits", header: "<LocaleRoster.h>", lib: "libbe.so", href: "haiku-additions.html#BLocaleRoster", desc: "Applies changed locale settings immediately." },
		{ name: "BMidiLocalConsumer", kind: "class", where: "MIDI 2 Kit", header: "<MidiLocalConsumer.h>", lib: "libmidi2.so", href: "midi2-kit.html#receiving", desc: "Receives MIDI 1.0 or MIDI 2.0 UMP packets." },
		{ name: "BMidiLocalProducer", kind: "class", where: "MIDI 2 Kit", header: "<MidiLocalProducer.h>", lib: "libmidi2.so", href: "midi2-kit.html#sending", desc: "Sends MIDI 1.0 or MIDI 2.0 UMP packets." },
		{ name: "BNetworkRoster", kind: "addition", where: "Wi-Fi Kit", header: "<NetworkRoster.h>", lib: "libbnetapi.so", href: "wifi-kit.html#roster", desc: "R5 additions for Wi-Fi scan and connection details." },
		{ name: "BPageHeader", kind: "class", where: "Appearance: Settings", header: "<R5UI.h>", lib: "libr5ui.so", href: "appearance-settings.html#BPageHeader", desc: "Settings page title, subtitle, and icon header." },
		{ name: "BPasswordControl", kind: "class", where: "Appearance: Controls", header: "<R5UI.h>", lib: "libr5ui.so", href: "appearance-controls.html#BPasswordControl", desc: "Password input field with show/hide eye glyph." },
		{ name: "BScreenArrangement", kind: "class", where: "Appearance: Controls", header: "<R5UI.h>", lib: "libr5ui.so", href: "appearance-controls.html#BScreenArrangement", desc: "Interactive screen placement and selection view." },
		{ name: "BSidebar", kind: "class", where: "Appearance: Settings", header: "<R5UI.h>", lib: "libr5ui.so", href: "appearance-settings.html#BSidebar", desc: "Category and page list for the Settings window." },
		{ name: "BSidebarItem", kind: "class", where: "Appearance: Settings", header: "<R5UI.h>", lib: "libr5ui.so", href: "appearance-settings.html#BSidebarItem", desc: "One row inside a BSidebar." },
		{ name: "BTiltLevel", kind: "class", where: "Appearance: Controls", header: "<R5UI.h>", lib: "libr5ui.so", href: "appearance-controls.html#BTiltLevel", desc: "Two-axis bubble level indicator." },
		{ name: "BUSBEndpoint", kind: "addition", where: "Haiku kits", header: "<USBKit.h>", lib: "libdevice.so", href: "haiku-additions.html#BUSBEndpoint", desc: "Isochronous USB stream transfers." },
		{ name: "BUserCard", kind: "class", where: "Appearance: Settings", header: "<R5UI.h>", lib: "libr5ui.so", href: "appearance-settings.html#BUserCard", desc: "Selectable account card with avatar, name, and role." },
		{ name: "BUserListItem", kind: "class", where: "Appearance: Settings", header: "<R5UI.h>", lib: "libr5ui.so", href: "appearance-settings.html#BUserListItem", desc: "Account row for a BListView." },
		{ name: "B_USB_IOCTL_GET_DEVICE_SPEED", kind: "constant", where: "Haiku kits", header: "<USB3.h>", lib: "kernel / USB", href: "haiku-additions.html#usb-speed", desc: "Queries negotiated USB device speed." },
		{ name: "BluetoothController", kind: "class", where: "Bluetooth Kit", header: "<R5Bluetooth.h>", lib: "libr5bt.so", href: "bluetooth-kit.html#controller", desc: "Drives Bluetooth radio, discovery, pairing, and audio." },
		{ name: "BluetoothState", kind: "struct", where: "Bluetooth Kit", header: "<R5Bluetooth.h>", lib: "libr5bt.so", href: "bluetooth-kit.html#state", desc: "Snapshot of Bluetooth adapter and device state." },
		{ name: "BluetoothView", kind: "class", where: "Bluetooth Kit", header: "<R5Bluetooth.h>", lib: "libr5bt.so", href: "bluetooth-kit.html#view", desc: "Embeddable Bluetooth settings and device panel." },
		{ name: "Decorator hooks", kind: "abi", where: "Haiku kits", header: "<Decorator.h>", lib: "app_server", href: "haiku-additions.html#decorator", desc: "R5 decorator ABI version and window hooks." },
		{ name: "RemoteDevice", kind: "addition", where: "Bluetooth Kit", header: "<bluetooth/RemoteDevice.h>", lib: "libbluetooth.so", href: "bluetooth-kit.html#remote", desc: "R5 additions on Haiku's Bluetooth RemoteDevice." },
		{ name: "SettingsContext", kind: "struct", where: "Settings modules", header: "<SettingsModule.h>", lib: "Settings add-on", href: "settings-modules.html#SettingsContext", desc: "Host callbacks passed to a SettingsModule." },
		{ name: "SettingsModule", kind: "class", where: "Settings modules", header: "<SettingsModule.h>", lib: "Settings add-on", href: "settings-modules.html#SettingsModule", desc: "Interface implemented by a Settings page add-on." },
		{ name: "WiFiController", kind: "class", where: "Wi-Fi Kit", header: "<R5WiFi.h>", lib: "libr5wifi.so", href: "wifi-kit.html#controller", desc: "Drives Wi-Fi scanning, joining, and link state." },
		{ name: "WiFiState", kind: "struct", where: "Wi-Fi Kit", header: "<R5WiFi.h>", lib: "libr5wifi.so", href: "wifi-kit.html#state", desc: "Snapshot of Wi-Fi interface, scan list, and link." },
		{ name: "WiFiView", kind: "class", where: "Wi-Fi Kit", header: "<R5WiFiView.h>", lib: "libr5wifi.so", href: "wifi-kit.html#view", desc: "Embeddable Wi-Fi settings panel." },
		{ name: "input_wheel_phase", kind: "enum", where: "Haiku kits", header: "<InterfaceDefs.h>", lib: "libbe.so", href: "haiku-additions.html#wheel-phase", desc: "Touchpad scroll gesture phase on wheel messages." },
		{ name: "r5_admin_send()", kind: "function", where: "Admin Kit", header: "<R5Admin.h>", lib: "libr5admin.so", href: "admin-kit.html#client", desc: "Sends a privileged request to a service admin port." },
		{ name: "r5_avatar_draw()", kind: "function", where: "Appearance: Functions", header: "<R5UI.h>", lib: "libr5ui.so", href: "appearance-effects.html#functions", desc: "Draws a circular user avatar badge." },
		{ name: "r5_button_set_icon()", kind: "function", where: "Icons Kit", header: "<R5Icons.h>", lib: "libr5icons.so", href: "icons-kit.html#icon-buttons", desc: "Sizes and sets an icon on a BButton." },
		{ name: "r5_button_set_lock()", kind: "function", where: "Icons Kit", header: "<R5Icons.h>", lib: "libr5icons.so", href: "icons-kit.html#icon-buttons", desc: "Sets the Settings administrator lock icon on a BButton." },
		{ name: "r5_glyph", kind: "enum", where: "Icons Kit", header: "<R5Icons.h>", lib: "libr5icons.so", href: "icons-kit.html#r5_glyph", desc: "Monochrome line glyphs (power, restart, eye, Bluetooth)." },
		{ name: "r5_icon", kind: "enum", where: "Icons Kit", header: "<R5Icons.h>", lib: "libr5icons.so", href: "icons-kit.html#r5_icon", desc: "Predefined icon IDs (Back, Forward, Up, Home, Lock)." },
		{ name: "r5_icon_bitmap()", kind: "function", where: "Icons Kit", header: "<R5Icons.h>", lib: "libr5icons.so", href: "icons-kit.html#icon-bitmaps", desc: "Renders a predefined, HVIF, file, or MIME icon bitmap." },
		{ name: "r5_level_glyph", kind: "enum", where: "Icons Kit", header: "<R5Icons.h>", lib: "libr5icons.so", href: "icons-kit.html#r5_level_glyph", desc: "Multi-level line glyphs (battery, volume, signal)." },
		{ name: "r5_screen_draw()", kind: "function", where: "Appearance: Functions", header: "<R5UI.h>", lib: "libr5ui.so", href: "appearance-effects.html#functions", desc: "Draws a monitor screen preview with Deskbar." },
		{ name: "r5_settings_apply_prefs_overlay()", kind: "function", where: "Appearance: Functions", header: "<R5UI.h>", lib: "libr5ui.so", href: "appearance-effects.html#functions", desc: "Applies the Settings wrench overlay to a bitmap." },
		{ name: "sockaddr_l2cap_le", kind: "struct", where: "Bluetooth Kit", header: "<R5Bluetooth.h>", lib: "libr5bt.so", href: "bluetooth-kit.html#le-l2cap", desc: "Socket address for Bluetooth Low Energy L2CAP." },
		{ name: "vkCreateViewSurfaceRFIVE()", kind: "function", where: "Vulkan", header: "<vulkan/vulkan_r5.h>", lib: "vulkan", href: "vulkan.html#surface", desc: "Creates a VkSurfaceKHR for a Haiku BView." },
		{ name: "Window effects", kind: "guide", where: "Appearance: Effects", header: "<Window.h>", lib: "app_server", href: "appearance-effects.html#window-effects", desc: "Alpha-ready drawing and glass window hints." },
		// Guides & concepts
		{ name: "Accounts & sessions", kind: "guide", where: "Guides", header: "", lib: "", href: "accounts.html", desc: "User uid, home folders, admin changes, lock and log out." },
		{ name: "Interface guidelines", kind: "guide", where: "Guides", header: "", lib: "", href: "guidelines.html", desc: "Forms, cards, buttons, icons, settings pages, words." },
		{ name: "Linking a program", kind: "guide", where: "Start here", header: "", lib: "", href: "index.html#linking", desc: "Libraries, headers, and Jamfile rules for LibR5." },
		{ name: "Rules for LibR5", kind: "guide", where: "Start here", header: "", lib: "", href: "index.html#rules", desc: "Additive ABI, MIT license, sharp icons, system colors." },
		{ name: "Vulkan in a window", kind: "guide", where: "Guides", header: "<vulkan/vulkan_r5.h>", lib: "vulkan", href: "vulkan.html", desc: "VK_RFIVE_view_surface and software fallback." },
	];

	// Determine active top-level nav section
	let activeNav = "start";
	for (const g of groups) {
		for (const p of g.pages) {
			if (p.file === here || (p.groupFiles && p.groupFiles.includes(here))) {
				activeNav = g.nav;
			}
		}
	}
	if (here === "class-index.html") activeNav = "index";

	const pebble = `
		<svg viewBox="0 0 100 100" aria-hidden="true">
			<defs>
				<linearGradient id="r5book-grad" x1="12%" y1="12%" x2="88%" y2="88%">
					<stop offset="0%" stop-color="#0084ff"/>
					<stop offset="45%" stop-color="#3898ff"/>
					<stop offset="72%" stop-color="#65b891"/>
					<stop offset="100%" stop-color="#ffb800"/>
				</linearGradient>
			</defs>
			<path d="M50,8 C74,8 92,25 92,49 C92,74 73,92 49,92 C25,92 8,75 8,50 C8,24 27,8 50,8 Z
				M54,24 C42,24 37,32 43,42 C49,51 46,62 35,66 C26,69 27,77 41,77 C64,77 77,64 77,48 C77,33 66,24 54,24 Z"
				fill="url(#r5book-grad)" fill-rule="evenodd"/>
			<path d="M22,29 C30,17 44,12 58,14" fill="none" stroke="#ffffff"
				stroke-width="2.5" stroke-linecap="round" opacity="0.38"/>
		</svg>`;

	const header = document.createElement("header");
	header.className = "site-nav";
	header.innerHTML = `
		<a href="index.html" class="brand-lockup">
			${pebble}
			<div class="brand-lockup-text"><strong>R5Book</strong><span>for developers</span></div>
		</a>
		<nav class="nav-links">
			<a href="index.html" class="${activeNav === "start" ? "active" : ""}">Start here</a>
			<a href="index.html#kits" class="${activeNav === "kits" ? "active" : ""}">Kits</a>
			<a href="index.html#guides" class="${activeNav === "guides" ? "active" : ""}">Guides</a>
			<a href="haiku-additions.html" class="${activeNav === "reference" ? "active" : ""}">Reference</a>
			<a href="class-index.html" class="${activeNav === "index" ? "active" : ""}">Index</a>
			<div class="nav-search">
				<svg class="search-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
					<circle cx="11" cy="11" r="8"/><path d="M21 21l-4.35-4.35"/>
				</svg>
				<input type="search" id="r5-search" placeholder="Search classes, calls…" aria-label="Search R5Book" autocomplete="off" />
				<div class="search-results" id="r5-search-results"></div>
			</div>
			<a href="https://github.com/r5-operating-system/r5os" class="nav-pill">GitHub</a>
		</nav>`;
	document.body.prepend(header);

	// Wire up top-bar client-side search
	const searchInput = document.getElementById("r5-search");
	const searchBox = document.getElementById("r5-search-results");
	if (searchInput && searchBox) {
		searchInput.addEventListener("input", () => {
			const q = searchInput.value.trim().toLowerCase();
			if (!q) {
				searchBox.classList.remove("open");
				searchBox.innerHTML = "";
				return;
			}
			const hits = window.R5_SEARCH_INDEX.filter((item) =>
				item.name.toLowerCase().includes(q)
				|| item.where.toLowerCase().includes(q)
				|| item.desc.toLowerCase().includes(q)
				|| item.lib.toLowerCase().includes(q)
			).slice(0, 10);

			if (hits.length === 0) {
				searchBox.innerHTML = '<div class="sr-empty">No matching class, call, or guide.</div>';
			} else {
				searchBox.innerHTML = hits.map((h) =>
					`<a href="${h.href}"><span class="sr-name">${h.name}</span><span class="sr-where">${h.where}</span></a>`
				).join("");
			}
			searchBox.classList.add("open");
		});

		searchInput.addEventListener("keydown", (e) => {
			if (e.key === "Escape") {
				searchBox.classList.remove("open");
				searchInput.blur();
			} else if (e.key === "Enter") {
				const first = searchBox.querySelector("a");
				if (first) location.href = first.getAttribute("href");
			}
		});

		document.addEventListener("click", (e) => {
			if (!e.target.closest(".nav-search")) {
				searchBox.classList.remove("open");
			}
		});
	}

	// Render grouped contents sidebar: only the current page's sub-items are expanded
	const toc = document.querySelector("aside.toc");
	if (toc) {
		toc.innerHTML = groups.map((group) => {
			const pagesHtml = group.pages.map((p) => {
				const isGroupActive = p.groupFiles ? p.groupFiles.includes(here) : (p.file === here);
				const isExactCurrent = p.file === here;

				if (p.subpages) {
					const parentCls = isExactCurrent ? "current" : (isGroupActive ? "parent-active" : "");
					let html = `<a href="${p.file}" class="${parentCls}">${p.label}</a>`;
					if (isGroupActive) {
						html += `<div class="toc-sub-group">` + p.subpages.map((sp) => {
							const spCurrent = sp.file === here;
							let subHtml = `<a href="${sp.file}" class="sub${spCurrent ? " current" : ""}">${sp.label}</a>`;
							if (spCurrent) {
								subHtml += sp.items.map(([href, label]) =>
									`<a href="${href}" class="sub2">${label}</a>`
								).join("");
							}
							return subHtml;
						}).join("") + `</div>`;
					}
					return html;
				}

				let html = `<a href="${p.file}" class="${isExactCurrent ? "current" : ""}">${p.label}</a>`;
				if (isExactCurrent && p.items.length > 0) {
					html += `<div class="toc-sub-group">` + p.items.map(([href, label]) =>
						`<a href="${href}" class="sub">${label}</a>`
					).join("") + `</div>`;
				}
				return html;
			}).join("");

			return `<h4>${group.title}</h4>${pagesHtml}`;
		}).join("");
	}

	const footer = document.createElement("footer");
	footer.className = "site-footer";
	footer.innerHTML = `
		<div><strong>R5 Operating System</strong> &nbsp;&bull;&nbsp; R5Book, LibR5 developer guide</div>
		<div>
			<a href="index.html">R5Book</a>
			<a href="class-index.html">Index</a>
			<a href="../LIBR5.md">LIBR5.md</a>
			<a href="../ICONS.md">ICONS.md</a>
			<a href="https://github.com/r5-operating-system/r5os">GitHub</a>
		</div>`;
	document.body.append(footer);
})();


// The line glyphs of libr5icons.so, on the same 24 x 24 grid with a 2.2
// unit pen as glyph_paint() in R5Icons.cpp. Use <svg data-glyph="power">.
const R5_GLYPHS = {
	power: '<path d="M5.64 6.64 A9 9 0 1 0 18.36 6.64"/><path d="M12 2 V12"/>',
	restart: '<path d="M20.23 7.25 A9.5 9.5 0 1 0 20.61 16.01"/><path d="M20.5 3 V8.5 H15"/>',
	clock: '<circle cx="12" cy="12" r="10"/><path d="M12 6 V12 L16 14"/>',
	eye: '<path d="M1 12 A11.82 11.82 0 0 1 23 12"/><path d="M1 12 A11.82 11.82 0 0 0 23 12"/>'
		+ '<circle cx="12" cy="12" r="3"/>',
	"eye-off": '<g mask="url(#r5book-slash)"><path d="M1 12 A11.82 11.82 0 0 1 23 12"/>'
		+ '<path d="M1 12 A11.82 11.82 0 0 0 23 12"/><circle cx="12" cy="12" r="3"/></g>'
		+ '<path d="M2 2 L22 22"/>',
	"log-out": '<path d="M9 3 H4 V21 H9"/><path d="M9 12 H21"/><path d="M16 7 L21 12 L16 17"/>',
};

(function () {
	const defs = document.createElementNS("http://www.w3.org/2000/svg", "svg");
	defs.setAttribute("width", "0");
	defs.setAttribute("height", "0");
	defs.style.position = "absolute";
	defs.innerHTML = '<mask id="r5book-slash" maskUnits="userSpaceOnUse" x="0" y="0" width="24" height="24">'
		+ '<rect width="24" height="24" fill="white"/>'
		+ '<path d="M1 1 L23 23" stroke="black" stroke-width="4.8"/></mask>';
	document.body.append(defs);

	document.querySelectorAll("svg[data-glyph]").forEach((svg) => {
		svg.setAttribute("viewBox", "0 0 24 24");
		svg.setAttribute("fill", "none");
		svg.setAttribute("stroke", svg.getAttribute("stroke") || "currentColor");
		svg.setAttribute("stroke-width", "2.2");
		svg.setAttribute("stroke-linecap", "round");
		svg.setAttribute("stroke-linejoin", "round");
		svg.innerHTML = R5_GLYPHS[svg.dataset.glyph] || "";
	});
})();
