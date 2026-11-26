<!-- SPDX-License-Identifier: MIT -->
<!-- SPDX-FileCopyrightText: 2026 R5OS Contributors -->

# Aomi Browser

## Status (26 Nov 2026 — Build 10078, Servo-only)

Aomi now ships in Applications as **Aomi (experimental)** on **Servo 0.8.0** as its sole web engine (no WebKit dependency in Aomi). WebPositive stays installed and stays the default browser.

- **Engine contract**: `BWebContentView` (`distro/apps/aomi/WebContentView.h`, documented in R5Book `docs/r5book/web-content-view.html`). `BServoContentView` loads `/boot/system/lib/libr5servo.so` via `dlopen()` so a missing library shows a clear native message rather than crashing or drawing an empty window.
- **Built-in pages**:
  - `aomi:newtab`: R5 wave pebble HVIF (72 px, full colour) above "Aomi" (or "Private window"), rounded search field, shortcut tiles.
  - `aomi:about`: R5Book-styled About Aomi page (mockup: `docs/design/aomi-mockup/about-aomi.html`).
  - `aomi:technical` (aliases `aomi:version`, `aomi:gpu`): versions, engine, network, graphics, and recent log, with **Copy as text** (`aomi:copy-technical`) and machine-readable JSON (`aomi:technical.json`, `/tmp/aomi-technical.json`, and `Aomi --technical-json`).
- **Packaging**: `r5servo-0.8.0-6-x86_64.hpkg`, built from servo-haiku (`distro/servo/servo-haiku.ref`), is fetched by sha256 from a GitHub Release asset of servo-haiku (`distro/servo/r5servo.sha256`); no Servo binary is committed to git. The package carries `documentation/packages/r5servo/THIRD_PARTY_NOTICES.txt` with every licence and copyright file of what is inside the library.
- **One Servo per process, on its own thread**: Servo allows one instance per process (`servo_config` options are a `OnceLock`) and its objects must stay on the thread that built them. Aomi runs it on one engine looper (`ServoEngine` in `ServoContentView.cpp`); every normal window of the process shares it, and with it one profile. Views talk to the engine thread with messages. A tab's frame is copied there into a shared bitmap under a lock and drawn by the view on its own thread. A private window is a process of its own (`--private-process`); Alt+N there asks the normal process for a window (`--window`).
- **Driving Servo**: Servo's `EventLoopWaker` posts one message to the engine looper (`r5_servo_set_waker`); it runs `r5_servo_pump()` and paints each visible tab with `r5_servo_paint()`, at most once per 16 ms, plus a 250 ms tick for a wake-up that could not be posted. A frame is read back only when Servo reports a new one. A hidden tab is told so (`r5_servo_set_visible`), and Servo stops its animation frames.
- **Input**: mouse, wheel and keys go to Servo through `r5_servo_input_message()` (in `libr5servo`, shared with `BServoView`). Haiku's shortcut key acts as Control inside a page; Alt+C, X, V go in as Servo's copy, cut and paste, Alt+A as Control+A. The page uses the R5 clipboard (`BClipboard`).
- **Drawing**: a tab's GL storage is made once at screen size (`r5_servo_set_max_size`); surfman's Mesa surfaceless backend cannot grow it, and destroying it aborts in Haiku Mesa. The tabs of one window share one context (`r5_servo_create_sharing`), so Servo keeps one painter per window and compiles its shaders once per window, not per tab; only the visible tab is read back. A context returns to a pool when the last tab of its window that uses it closes. The frame is drawn at its own size, never stretched.
- **GL path**: `libr5servo` chooses like R5's EGL driver: Zink on the GPU when the radeon_hd settings and a render node allow it, else Zink on Lavapipe. A program or user that sets `GALLIUM_DRIVER` or `LIBGL_ALWAYS_SOFTWARE` keeps that choice.
- **Page questions**: alert, confirm, prompt, sign-in (HTTP authentication; the user name and password go in the Authorization header only, `patches/servo-net-0.8.0-no-credentials-in-uri.patch` in servo-haiku), file upload (`BFilePanel`), the context menu and select lists are shown by the view (`r5_servo_ask_control()` in `libr5servo`) and answered to the page (`r5_servo_control_answer()`). Permission requests (camera, location, notifications) are denied: Servo on R5 has no such service yet.
- **New windows from a page**: `target=_blank` and `window.open()` open a tab. Servo builds the WebView; Aomi adopts it (`r5_servo_set_callback`).
- **Profiles**: normal windows keep cookies, HSTS and logins in `~/config/settings/Aomi/Servo`; Servo writes them when it shuts down, after the last window (`aomi_servo_shutdown()` from `AomiApp::QuitRequested`). A private window keeps them in memory only, and its scratch folder is made with `mkdtemp()` under the user's cache folder and removed when it closes.
- **5625U next combined stick step (`servo-radv-glass`)**: Boot the 5625U with RADV + desktop compositor enabled, open **Aomi (experimental)** on `https://example.com` and `https://http2.pro/api/v1`, verify `aomi:technical` shows `zink Vulkan 1.4(AMD Radeon Graphics (RADV ...))` (not llvmpipe/Lavapipe), lock panel says `HTTP/2, secure`, and frosted glass renders behind the tab strip and toolbar.

---

Plan, herzien 19 november 2026. De motorkeuze is gemaakt op 19 november 2026: de Haiku-WebKit-poort, een schil op `BView`. WebKit blijft de engine. Dit plan is de volgorde daarna. Deze landing bouwt niets.

`generated/BROWSER-PLAN-REVIEW.md` stond niet op deze machine. Geen worktree `R5OS-amd64-stick`, geen kopie in de repo en geen kopie in `/cursor/stores/self`. Gekozen: herzien vanuit `origin/main` (`git show` en `git grep`), en bij een open punt de keuze hieronder zetten. De naam en de rondes komen uit de review van 19 november 2026.

Nick heeft de schil gekozen. Aomi Browser krijgt een nieuwe GUI, gebaseerd op WebPositive, op de Interface Kit en op R5 UI. De oude `BrowserWindow` is niet het product. Hij blijft alleen de sonde tot Aomi in ronde 1 dezelfde pagina’s opent.

Officieel eerst AMD64. `origin/main` is de bron van waarheid.

## Naam en uiterlijk

De eigenaar heeft de naam vastgelegd op 19 november 2026. Het product is Aomi Browser. In het venster en in de Deskbar heet het Aomi. De signatuur is `application/x-vnd.R5-Aomi`, vast in de eerste codestap (ronde 1). De bron staat in `distro/apps/aomi`.

De merkkleuren, hemelblauw, wit en diep marineblauw, zitten in het app-icoon, de startpagina en de promo. Het venster houdt het R5-kader: gele tab, lijn van 1 px. De stijl is minimaal en glad. Dat is het glas van R5Book (`BAppearanceKit::set_window_glass`, `docs/r5book/appearance-effects.html`), met de terugval zonder GPU: app_server neemt de aanroep aan en bewaart hem, en in de CPU-stand verandert het scherm niet. `B_TRANSPARENT_COLOR` wordt geen glas. De slogan "Browse beyond the horizon." is voor de website en voor What's new, niet voor het venster.

De bladwijzermap blijft `WebPositive`.

De WebKit-headers staan niet in git. Ronde 0 heeft ze uit het devel-pakket gelezen. De aanroepen hieronder komen uit de WebPositive-bron. Waar de header daarvan afwijkt, staat dat in het verslag.

## Keuzes waar het open lag

- Het programma komt nieuw onder `distro/apps/aomi`. Naam, signatuur en kleuren staan hierboven en hangen niet meer open. WebPositive blijft de naam van het Haiku-pakket tot ronde 3 dat pakket uit de image haalt. De oude signatuur is `application/x-vnd.Haiku-WebPositive` (`haiku/src/apps/webpositive/BrowserApp.cpp:64` en `haiku/src/apps/webpositive/WebPositive.rdef:1`). Het rdef-versieblok is app 1.3.0, korte tekst "WebPositive", lange tekst "WebPositive ©2007-2021 The WebKit Haiku Project" (`WebPositive.rdef:4-10`). Dat is de app, niet de engine. Aomi krijgt een eigen rdef. `WebPositive.rdef` gaat niet mee.
- Bladwijzers blijven de bestanden die er al zijn. `_BookmarkPath` doet `find_directory(B_USER_SETTINGS_DIRECTORY)`, dan `kApplicationName`, dan `"Bookmarks"`, en `create_directory` met modus `0777` (`BrowserWindow.cpp:1921-1935`). `kApplicationName` is `B_TRANSLATE_SYSTEM_NAME("WebPositive")` (`BrowserApp.cpp:65`). `B_USER_SETTINGS_DIRECTORY` is een gebruikersmap (`haiku/headers/os/storage/FindDirectory.h:58-66`). Type `application/x-vnd.Be-bookmark`, attributen `META:url` en `META:title` (`BrowserWindow.cpp:2008-2015`). Submap `Bookmark bar` (`BrowserWindow.cpp:144`). De mapnaam blijft `WebPositive`, ook als het programma anders heet: de constante is de toepassingsnaam, geen aparte bladwijzerconstante. Een nieuwe browser die zijn eigen naam appendt, maakt een tweede map. Modus in de bron is `0777`, ook voor de instellingenmap (`BrowserApp.cpp:204`). Een map die er al is, blijft zoals die is: `create_directory` maakt alleen aan wat ontbreekt.
- Pagina’s lopen via de client die haikuwebkit al linkt. `BrowserApp.cpp:112-117` zet `CURL_COOKIE_JAR_PATH` op `B_USER_SETTINGS_DIRECTORY` / toepassingsnaam / `cookie.jar.db`. WebPositive roept `BHttpRequest` niet aan (geen treffer in `haiku/src/apps/webpositive`). De browser praat niet met `WiFiController` en linkt `libr5wifi.so` niet om te laden. `BHttpRequest` in `distro/network/src/kits/network/libnetservices/HttpRequest.cpp` zet `fHttpVersion(B_HTTP_11)` en is de client van Haiku-programma’s. Overstappen daarop zou die client in de plaats van de poort zetten. HTTP/2: de curl-backend vraagt het op `https` als de gelinkte curl het kenmerk heeft. Het verslag van ronde 0 heeft die aanroep gelezen. Een pagina die het protocol toont, is er nog niet. De Wi-Fi Kit zet de interface op via `net_server`. De browser gebruikt die interface zoals elk ander programma: sockets, route, DNS.
- De download-haak zit in de app, ook zonder de header. `BWebPage::SetDownloadListener(BMessenger(fDownloadWindow))` (`BrowserApp.cpp:243`). `DownloadWindow::MessageReceived` neemt `B_DOWNLOAD_ADDED` en `B_DOWNLOAD_REMOVED`, veld `"download"` als pointer naar `BWebDownload` (`DownloadWindow.cpp:248-263`). `DownloadProgressView` neemt `B_DOWNLOAD_STARTED`, `B_DOWNLOAD_PROGRESS` en `B_DOWNLOAD_REMOVED` (`DownloadProgressView.cpp:389`, `:409`, `:419`). De waarden staan in de uitgepakte headers: `B_DOWNLOAD_ADDED` is `'dwna'` en `B_DOWNLOAD_REMOVED` is `'dwnr'` (`WebPage.h:73-74`), `B_DOWNLOAD_STARTED` is `'dwns'` en `B_DOWNLOAD_PROGRESS` is `'dwnp'` (`WebDownload.h:44-45`). Er komt geen eigen downloader naast de poort. Gekozen: de MIT-vensters kopiëren (zie licenties), niet een tweede lijst verzinnen.
- Glas: `BAppearanceKit::set_window_glass()` op het venster, zoals andere R5-vensters. De pagina zelf blijft een dekkende `BWebView`. Blijkt het glas de pagina te matten, dan gaat die aanroep in diezelfde stap weer uit. Dat is onbewezen tot een boot.
- Stop en herladen krijgen geen nieuw nummer in `r5_icon`. De enum in `distro/icons/R5Icons.h` eindigt bij `R5_ICON_LOCKED`. Terug, vooruit en home gebruiken `r5_icon_bitmap()`. Stop en herladen zijn tekstknoppen tot iemand een bestaand vectorplaatje aanwijst. De oude knoppen zijn `BPrivate::BIconButton` met resource 201, 202, 204 en 206 (`BrowserWindow.cpp:515-527`; stop op `:522`). De header is `haiku/headers/private/shared/IconButton.h`, MIT, copyright 2006-2011 Haiku, Stephan Aßmus.
- De vijf sonde-pagina’s, tot de eigenaar een andere lijst geeft. De vijfde pagina blijft open: de eigenaar noemt haar vóór ronde 1: de welkomstpagina of de R5Book via `file://`; `https://example.com`; een lange tekstpagina (Wikipedia); `https://github.com/login` zonder account, alleen of het formulier verschijnt; en een vijfde adres dat Nick noemt als een pagina die hij nu stuk ziet. Zonder dat vijfde adres start ronde 1 niet voor zijn gevallen. Geen wachtwoorden in het verslag. De standaardstart van WebPositive is `file:///boot/home/config/settings/WebPositive/LoaderPages/Welcome` (`SettingsKeys.cpp:31-32`).
- De image vandaag levert Legacy. In deze git-boom staat geen `--no-webkit2` en geen `libJavaScriptCore`. `haiku/build/jam/BuildFeatures:303-311` zet de feature `webkit` aan als `haikuwebkit_devel` er is, en noemt als bibliotheek alleen `libWebKitLegacy.so`. De recipe zet WebKit2 uit met `--no-webkit2` (regel 103, gelezen in ronde 0). Op die pin blijven is niet het doel. Het verslag hieronder is die lezing. OpenGL alleen als Zink op RADV of als llvmpipe. Lavapipe blijft voor Vulkan en krijgt geen browserwerk.
- `BTextControl::MarkAsInvalid(bool)` bestaat (`haiku/headers/os/interface/TextControl.h:43`). De implementatie zet `BControlLook::B_INVALID` op de look (`haiku/src/kits/interface/TextControl.cpp:592-604`). De URL-balk van WebPositive is geen `BTextControl`: `URLInputGroup::MarkAsInvalid` zet een vlag en tekent de rand (`URLInputGroup.cpp:648-671`). Lege Go roept die vlag aan (`BrowserWindow.cpp:837-838`). Gekozen: de nieuwe balk is een `BTextControl` en roept `MarkAsInvalid` aan. Een extra foutregel is niet nodig voor een leeg veld.
- De uitgepakte header van 1.10.0-2 zegt `class BWebView : public BView` (`WebView.h:51`). Een `BWebWindow` eist zij niet. De bestaande schil erft `BWebWindow` (`BrowserWindow.h:98`) en maakt de view met `new BWebView("web view", fContext)` (`BrowserWindow.cpp:1420`). Gekozen, na het verslag: het nieuwe venster is een gewone `BWindow`. De view blijft een `BWebView`. De meldingen komen binnen via `BWebPage::SetListener` (`WebPage.h:93`), niet via een basisklasse die WebKit2 niet heeft.

## Wat er al is

WebPositive in de image is het pakket `webpositive.hpkg`. `distro/rest/build/jam/OptionalPackages:152-157` zet het erin als het optionele pakket WebPositive aan staat en de bouwfeature `webpositive` aan staat. De engine-pin voor AMD64 staat in git: `haiku/build/jam/repositories/HaikuPorts/x86_64:1-3` is het archief `https://eu.hpkg.haiku-os.org/haikuports/master/build-packages`, en regels 108-109 noemen `haikuwebkit-1.10.0-2` en `haikuwebkit_devel-1.10.0-2`. Het 32-bits archief noemt `haikuwebkit-1.5.4-1` (`haiku/build/jam/repositories/HaikuPorts/x86:449`); dat is geen officieel doel. Een opgeslagen syslog noemt hetzelfde AMD64-pakket bij de eerste start: `generated/boot-5625u-10/syslog:2044` (`haikuwebkit-1.10.0-2-x86_64.hpkg`). Deze sessie heeft `package info` op een imagebestand niet gedraaid. Het pakket op schijf is wel gelezen, met het `package`-programma van de boom. De WebKit-revisie van die bron is 625.1.25. Het verslag zet dat naast 626.1.6 op de huidige branches.

## De motor

De eigenaar, 19 november 2026: een nieuwere motor hoort bij het doel. Aomi blijft niet op `haikuwebkit-1.10.0-2` met de Legacy-API in één proces. Het Haiku-project heeft MiniBrowser getoond op de WebKit2-poort. `haikuwebkit-1.10.0-2` is de pin van de image vandaag (`HaikuPorts/x86_64:108-109`), niet het doel. De vragen staan in ronde 0, een verslag vóór code.

De bron van de app staat in git, 46 bestanden onder `haiku/src/apps/webpositive`. Daarnaast ligt één R5-kopie: `distro/apps/webpositive/src/apps/webpositive/BrowserWindow.cpp`. `scripts/apply-webpositive.py` kopieert die over `src/apps/webpositive/BrowserWindow.cpp`. `scripts/apply-overlay.sh` roept dat script aan. Het verschil met `origin/main` is alleen `LoadNegotiating`. Upstream (`BrowserWindow.cpp:1536-1552`) bewaart de URL van een andere tab in `PageUserData` als die string leeg is, en valt daarna door naar `fURLInputGroup->SetText`, dus de zichtbare balk. De overlay (`distro/apps/webpositive/src/apps/webpositive/BrowserWindow.cpp:1538-1549`) zet een statusregel en `return`t, zodat de balk blijft. Dat is Haiku #20319. De overlay heeft geen SPDX-snippet boven die regels; de BSD-header van het bestand blijft staan.

`CHANGELOG.md` meldt dat `jam -q WebPositive` op 3 november 2026 linkte (`BrowserWindow.o`, daarna `WebPositive`). Dat bewijst de schil op die dag, niet een versie die afwijkt van de pin hierboven, en er ging geen achtergrondtab open.

`BWebView` bestaat voor dit bestand. Wat `BrowserWindow.cpp` aanroept, met regel: `LoadURL` (`:1009`, `:1431`), `MainFrameURL` (`:744`), `MainFrameTitle` (`:851`), `Reload` (`:823`), `StopLoading` (`:881`), `GoBack` (`:873`), `GoForward` (`:877`), `IncreaseZoomFactor` (`:792`, `:1014`), `DecreaseZoomFactor` (`:794`, `:1017`), `Shutdown` (`:1907`), `WebPage()` (`:866`), `GetContentsAsMHTML` (`:866`), `SendPageSource` (`:1043`), `FindString` (`:1050`), `SendEditingCapabilities` (`:2413`), `SetUserData` (`:1340`), `GetUserData` (`:1337`), `SetAutoHidePointer` (`:1183`), `ResendNotifications` (`:1396`). De schil overschrijft, in `BrowserWindow.h:134-170`: `NavigationRequested`, `NewWindowRequested`, `NewPageCreated`, `CloseWindowRequested`, `LoadNegotiating`, `LoadCommitted`, `LoadProgress`, `LoadFailed`, `LoadFinished`, `MainDocumentError`, `TitleChanged`, `IconReceived`, `ResizeRequested`, `SetToolBarsVisible`, `SetStatusBarVisible`, `SetMenuBarVisible`, `SetResizable`, `AuthenticationChallenge`, `UpdateGlobalHistory`, `NavigationCapabilitiesChanged`, `StatusChanged`. `BrowserApp.cpp:198-199` roept `BWebPage::InitializeOnce` en `BWebPage::SetCacheModel(B_WEBKIT_CACHE_MODEL_WEB_BROWSER)` aan; `:449` roept `BWebPage::ShutdownOnce` aan; `:206` roept `BWebSettings::SetPersistentStoragePath` aan. Dat is de Legacy-poort: één proces, voor zover deze bron dat laat zien. WebKit2 is upstream MiniBrowser, niet dit venster.

De GUI die `BrowserWindow.cpp` bouwt, van boven naar onder (`:630-639`):

- `BMenuBar`: Window (`:396`), Edit (`:436`), View (`:455`), History (`:482`), Bookmarks (`:497`, klasse `BookmarkMenu` in hetzelfde bestand, `:155`, afgeleid van `BNavMenu`).
- Tabs via `TabManager` (`:392`). De klasse staat in `haiku/src/apps/webpositive/tabview/`. `WebTabView` zit in `TabManager.cpp:452`.
- Een navigatierij: terug, vooruit, stop, home (`BIconButton`, plaatjes 201, 202, 204, 206), dan `URLInputGroup`.
- Een bladwijzerbalk (`BookmarkBar`) als de submap bestaat (`:504-506`). Ontbreekt de submap, dan blijft het menu-item uit (`:509`).
- De `BWebView`, in de containerview van `TabManager` (`:637`).
- Een zoekbalk (`BTextControl` "Find:", `:558`), vorige, volgende, hoofdlettergevoelig. De groep begint verborgen (`:651`).
- Een statusregel (`BStringView`) en een `BStatusBar`.

Downloads, instellingen, cookies en de scriptconsole zijn menu-items (`:417-424`). `SHOW_DOWNLOAD_WINDOW`, `SHOW_SETTINGS_WINDOW`, `SHOW_CONSOLE_WINDOW` en `SHOW_COOKIE_WINDOW` gaan naar `be_app` (`:1112-1117`). De vensters maakt `BrowserApp::ReadyToRun`: `DownloadWindow` (`:226`), `SettingsWindow` (`:241`), `ConsoleWindow` (`:245`), `CookieWindow` (`:246`).

De URL-balk, in `BrowserWindow.cpp` en in de overlay:

- Return in het veld haalt de Go-knop in (`DispatchMessage`, `:732-740`). Een veld dat alleen de focus verliest, laadt niet. De comment zegt dat met zoveel woorden (`:733-736`).
- Escape in het veld zet `MainFrameURL` terug (`:741-744`). Escape buiten het veld stuurt `STOP` (`:758-764`).
- Een lege Go zet `URLInputGroup` ongeldig (`:837-838`).
- Een tab die niet geselecteerd is, schrijft in de overlay niet in de balk (`distro/.../BrowserWindow.cpp:1549`). De URL blijft op die tab in `PageUserData` (`:230-237`, `SetURLInputContents`).
- `_SmartURLHandler` (`:2647`): `http`, `https`, `file`, `about`, `data` (`kHandledProtocols`, `:136-142`) laadt het zelf. Een ander schema gaat naar `application/x-vnd.Be.URL.<schema>` via `BRoster::Launch` (`:2676-2683`). `localhost` wordt `http://localhost/` (`:2692-2693`). Een tekenreeks met een punt en zonder spatie tot de eerste `/` krijgt `http://` (`:2701-2723`). Anders zoeken (`_VisitSearchEngine`, `:2729`). De zoek-URL komt uit de instelling `search page url`, standaard `https://noai.duckduckgo.com/?q=%s` (`SettingsKeys.cpp:28` en `:33`). Voorvoegsels van twee tekens wijzen een andere motor aan (`SettingsKeys.cpp:44-54`: Baidu, Bing, DuckDuckGo, Ecosia, Google, Qwant, Wikipedia, Yandex, Yahoo).

Bladwijzers zijn gewone bestanden. `_CreateBookmark` schrijft `META:url` en `META:title` en het type (`:2008-2015`). Een icoon gaat via `BNodeInfo::SetIcon` als `B_MINI_ICON` of `B_LARGE_ICON`; ontbreekt het grote icoon, dan vergroot de code het kleine (`:2016-2030`). Beheren opent de map in Tracker (`_ShowBookmarks`, `:2156-2164`, `BRoster::Launch`). Slepen naar Tracker vult hetzelfde bestand (`B_COPY_TARGET`, typecontrole op `:968` en `:1219`).

Geschiedenis is een platgedrukt `BMessage` in `B_USER_SETTINGS_DIRECTORY` / toepassingsnaam / `BrowsingHistory` (`BrowsingHistory.cpp:350-358`). Velden: `"max history item age"` (standaard 7 dagen, `:159` en `:304-306`) en herhaald `"history item"`. `_UpdateHistoryMenu` (`BrowserWindow.cpp:2293`) vult het History-menu daaruit, gegroepeerd per dag. `UpdateGlobalHistory` voegt een item toe (`:1797-1799`).

Cookies: `SettingsMessage` op `WebPositive/Cookies` (`BrowserApp.cpp:101-104`), plus het curl-bestand `cookie.jar.db` (`:112-117`). Aanmeldgegevens: `WebPositive/CredentialsStorage`, ook een platgedrukt `BMessage` (`CredentialsStorage.cpp:244-252`). Sessie: `WebPositive/Session` (`BrowserApp.cpp:120-123`). Overige instellingen: `WebPositive/Application` (`:209-212`).

Downloads landen in de instelling `"download path"`. Ontbreekt die, dan `find_directory(B_DESKTOP_DIRECTORY)` en anders de string `"/boot/home/Desktop"` (`DownloadWindow.cpp:133-136`). De standaard in `SettingsKeys.cpp:30` is `"/boot/home/Desktop/"`. `FindDirectory.h` heeft `B_DESKTOP_DIRECTORY` (`:14`) en `B_USER_SETTINGS_DIRECTORY` (`:66`). Een zoektocht op `B_DOWNLOAD` in die header gaf geen downloadmap.

`docs/MULTIUSER.md` zegt dat WebPositive het image-pakket blijft en dat standaardpaden in dat pakket nog vast staan. `distro/TODO.md` wijst daarvoor naar GitHub #67. Een herbouw van `webpositive.hpkg` stond uitdrukkelijk buiten de multi-user-pas. De vaste paden zitten in `SettingsKeys.cpp:30-33` (`/boot/home/Desktop/` en de welkomst-URL onder `/boot/home/config/settings/WebPositive`).

`docs/ROADMAP.md` punt 4: een huidige WebKit of een Qt-browser. WebPositive is de zwakke dagelijkse toepassing. Dit plan vult dat punt met WebKit en de kits. Qt valt af, zoals het onderzoek al koos.

`docs/VPN-KIT.md`: WebPositive doet veel moderne aanmeldpagina’s niet. Browser-aanmelden voor VPN (`r5vpn:`) wacht tot er een huidige browser in de image staat. Dat hoort niet bij de eerste stappen hier.

Netwerk eromheen, wel in de boom. De Wi-Fi Kit (`docs/LIBR5.md`, “Wi-Fi Kit”; `distro/wifi/lib/R5WiFi.h`) laat `WiFiController` het werk met de adapter en `net_server` op een eigen thread doen. `WiFiController.cpp` loopt langs `BNetworkRoster` en de interfaces. Een geslaagde verbinding is een interface die opstaat, met adres en route. De kit is geen HTTP-stack.

GPU, uit `docs/GPU-ARCHITECTURE.md` en `docs/ROADMAP.md`. De softwarepaden blijven: Painter, llvmpipe, Lavapipe. OpenGL is Zink op RADV als de GPU zijn checks haalt, anders llvmpipe. Vulkan is RADV of Lavapipe. Skia-tekenen is volgens dat GPU-document de standaard sinds 19 november 2026 (M13 stap 5): RADV als de GPU slaagt, anders Lavapipe; `gpu_draw false` zet het uit. De browser tekent niet zelf op de GPU. Een `BWebView` gaat door app_server. `CHANGELOG.md`: stick 57, app_server-crash in FreeType op de teken-thread van WebPositive; stick 51, GPU-tekenen ging uit terwijl WebPositive openging omdat de VRAM-paginatabel vol zat. Beide zijn app_server. Een nieuwe schil haalt ze niet weg.

De `Jamfile` (`haiku/src/apps/webpositive/Jamfile:67-78`) zet private headers aan voor interface, netservices, shared en tracker, en linkt `libnetservices.a`, `libcolumnlistview.a`, de webkit-bibliotheken van de feature (dat is `libWebKitLegacy.so`), `localestub`, `shared`, `be`, `network`, `tracker`, `translation`. Het bladwijzerarchief gaat de image in als `home/config/settings/WebPositive` (`Jamfile:87-88`).

### Verslag ronde 0

19 november 2026. Geen code, geen jam, geen stick, geen WebKit-bouw. Wat hier "nagekeken" staat, is een bestand of een pakket dat deze sessie heeft geopend. Wat "gelezen" staat, is een pagina. De bugtracker van Haiku gaf een controlesom in plaats van het ticket. Die pagina is niet gelezen.

#### Bron

De nieuwste Haiku-WebKit staat op Codeberg, `https://codeberg.org/haiku/haikuwebkit`, nagekeken 19 november 2026 via de branch-lijst.

Twee takken horen bij de poort. `haiku` is de tak waarvan de releases komen. De punt is `56704ac66ae8fda3781e5c44a10a67a15ed9ffa1`, "Make the CI do CI things", 11 oktober 2026, 13:28 Europe/Amsterdam. `haiku-webkit2` is de nieuwere motor. De punt is `90e4d4e60b5dd8b3161e8f16e6c827a05b261ead`, "Haiku WebKit2: fix x86 32-bit cross-compilation", vastgelegd 12 oktober 2026, 13:04 Europe/Amsterdam. Die tak staat 104 commits voor op `haiku` (vergelijking `haiku...haiku-webkit2`, 104 commits, 466 bestanden). De oudste van die 104 is "Enable building WebKit2 (non-legacy)", 30 augustus 2026.

De tak `main` in dezelfde repository is de upstream-spiegel, niet de Haiku-poort. De punt die de branch-lijst gaf is `ef1d79aebd04ab35a6a9d193f7d887fea8270852`, "Versioning. WebKit-7627.1.2", 21 september 2026, 20:31 Europe/Amsterdam. Verder dan die punt is upstream hier niet nagekeken.

Het pakket in de image is de release [HaikuWebKit-1.10.0](https://codeberg.org/haiku/haikuwebkit/releases/tag/HaikuWebKit-1.10.0), gepubliceerd 24 juli 2026, 18:04 Europe/Amsterdam, `target_commitish` `haiku`. Er is geen nieuwere 64-bit release. De heruitgave 1.9.19 van 6 augustus 2026 zegt dat 64-bit 1.10.0 moet nemen.

Upstream-versie van die release, nagekeken in de gedownloade tarball `haikuwebkit-1.10.0.tar.gz` (38 MB; de sha256 uit de recipe is niet herberekend): `Configurations/Version.xcconfig` zet `MAJOR_VERSION = 625`, `MINOR_VERSION = 1`, `TINY_VERSION = 25`. Dat is WebKit 625.1.25. Dezelfde samenvoeging staat op de repositorypagina als "Merge WebKit 625.1.25 into Haiku", 10 juli 2026, 20:27 Europe/Amsterdam.

Beide levende takken zetten in datzelfde bestand `MAJOR_VERSION = 626`, `MINOR_VERSION = 1`, `TINY_VERSION = 6` (regels 24-26). De bestanden op `haiku` en op `haiku-webkit2` zijn byte voor byte gelijk. De commit die het bestand het laatst schreef is `75e5c1219429`, "Versioning. WebKit-7626.1.6", 24 augustus 2026, 18:35 Europe/Amsterdam. De titel zegt 7626.1.6. Het bestand zegt 626.1.6. Allebei nagekeken. Waarom de titel een 7 ervoor zet, staat niet in de drie regels van het bestand.

1.10.0-2 loopt dus één versieregel achter: 625.1.25 tegen 626.1.6. In tijd is dat de release van 24 juli tegen de versiecommit van 24 augustus, ongeveer een maand upstream, en daarna nog Haiku-commits tot 12 oktober op hetzelfde versienummer. Vandaag is 19 november. Een telling van elke upstream-tag tussen 625.1.25 en 626.1.6 is niet gemaakt. De HaikuPorts-lijst heeft geen nieuwer pakket dan 1.10.0-2.

`OptionsHaiku.cmake` op beide bronnen zet de poortversie nog op 1.10.0 (`PROJECT_VERSION_MAJOR 1`, `MINOR 10`, `PATCH 0`). Dat nummer is de Haiku-release, niet de upstream-revisie. In de tarball van 1.10.0 staat `ENABLE_WEBKIT OFF` en `ENABLE_WEBKIT_LEGACY ON`. Op `haiku-webkit2` staan allebei aan.

#### Recipe van HaikuPorts

[haikuwebkit-1.10.0.recipe](https://github.com/haikuports/haikuports/blob/master/haiku-libs/haikuwebkit/haikuwebkit-1.10.0.recipe), gelezen 19 november 2026. Revisie 2 (regel 12). Bron is de tarball hierboven (regel 14). De licentieregels noemen GNU LGPL v2, GNU LGPL v2.1 en MIT (regel 9). `BUILD()` roept `Tools/Scripts/build-webkit --haiku --no-webkit2 --no-fatal-warnings` aan (regel 103). Het pakket levert `libWebKitLegacy` en `libJavaScriptCore`, en `cmd:jsc`. Geen WebKit2-bibliotheek.

Het geïnstalleerde pakket is nagekeken met het `package`-programma van de boom, `/home/nickr/r5os/haiku/generated.x86_64/objects/linux/x86_64/release/tools/package/package`, op `generated.x86_64/download/haikuwebkit-1.10.0-2-x86_64.hpkg` (42 MB) en het devel-pakket (24 KB). De gedeelde boom is niet gewijzigd. Het devel-pakket is uitgepakt naar `/tmp/aomi-webkit-1.10.0-devel`.

Het runtime-pakket eist onder meer `lib:libcurl`, `lib:libgl`, `lib:libicuuc`, jpeg, png, sqlite, ssl, webp, woff2, xml2, xslt, avif, lcms2 en zlib. Het archief zet de bibliotheken op 10 augustus 2026, 23:17, zonder tijdzone in de lijst. `libJavaScriptCore.so.18.7.4` en `libWebKitLegacy.so.1.10.0`. `curl-8.21.0-1` eist `lib:libnghttp2`. `nghttp2-1.63.0-1` levert `lib:libnghttp2 = 14.28.2`. Alle drie de hpkgs liggen in die downloadmap. `package info` op een imagebestand is niet gedraaid.

#### WebKit2 op Haiku

MiniBrowser is het proefprogramma, niet de GUI. De geschiedenis die deze sessie heeft geopend:

- 8 mei 2024, [plannen](https://www.haiku-os.org/blog/zardshard/2024-05-08_gsoc_2024_plans_for_fixing_haikus_webkit2_port/): de poort tekende geen pagina meer. MiniBrowser staat niet in HaikuDepot. Een bouw kost 15 GB als je zuinig bent, en ettelijke uren.
- 20 augustus 2024, [eindverslag](https://www.haiku-os.org/blog/zardshard/2024-08-16_gsoc_2024_porting_webkit2_final_report/) (de pagina zegt 20 augustus; de URL zegt 16 augustus): pagina's verschijnen weer. Drie processen: het UI-proces, een WebProcess per pagina, en een NetworkProcess. IPC is UNIX-sockets. Gedeeld geheugen is het UNIX-gedeelde geheugen, en Haiku vult dat onder water met zijn eigen primitieven. Versneld compositing ging uit, omdat EGL op Haiku niet deugde. Open stond toen nog: crash bij sluiten, bij vergroten en bij terug in de geschiedenis, geen muiswiel, geen toetsenbord, geen contextmenu, geen kopiëren. De API voor programma's was "een beetje een rommel". WebPositive was niet omgezet.
- 18 april 2026 en de mei-reacties, [Haikuwebkit 1.9.27](https://discuss.haiku-os.org/t/haikuwebkit-1-9-27/19108), gelezen als de forumtekst in het zoekresultaat, de draad niet als één pagina opnieuw geopend: toetsenbord beter, minder crashes, en op 21 mei de compositor-crash. DigitalBox schrijft dat `AcceleratedSurfacePlayStation.cpp` geen software-terugval heeft. Op 22 mei tekenen sites weer. PulkoMandy, 1 mei: de 1.9-releases komen van de Legacy-tak, en een release bevat MiniBrowser niet. Bouwen: 32 GB in release, met 16 GB bevriest de machine, en `-j4` is dan het veilige aantal.
- 23 mei 2026, ticket 19646, "webkit2: Threaded compositor crash". De ticketpagina op dev.haiku-os.org ging niet open. De [mailinglijstkopie](https://www.freelists.org/post/haiku-bugs/Haiku-19646-webkit2-Threaded-compositor-crash,4) wel: comment 4 zegt dat de fix in [pull request 24](https://codeberg.org/haiku/haikuwebkit/pulls/24) zit, en dat die toen nog liep. Of het ticket nu dicht is, is niet nagekeken.
- 8 juni 2026, [MiniBrowser-voorbeeld](https://discuss.haiku-os.org/t/haikuwebkit-minibrowser-preview/19400): een vroege zip met tabs. De tekst zegt dat de browser kan blijven hangen, dat sites crashen, dat video langzaam is, en dat niet elk contextmenu af is. Een latere noot op die draad zegt dat een tab-crash de andere tabs met rust laat. De zip [package-minibrowser-v0.3.zip](https://www.haikuinsider.org/files/package-minibrowser-v0.3.zip) geeft vandaag 404. Er is geen HaikuPorts-pakket voor MiniBrowser of WebKit2. Deze sessie heeft er geen gebouwd en geen geboot.
- 12 oktober 2026, [cross-compileren](https://www.haikuinsider.org/cross-compiling-webkit2/): tak `haiku-webkit2`, 32-bit, vanaf Debian. De proef kopieert `MiniBrowser`, `NetworkProcess` en `WebProcess`. De bouw vraagt GLES2 en EGL via Mesa. Het stuk zegt dat een pakket de volgende stap is, en dat die stap nog niet gezet is. Getest door de auteur op Haiku R1/beta6 32-bit in QEMU. Dat is geen R5-boot, en het is 32-bit. R5 bouwt 64-bit.

Wat de bron vandaag zelf laat zien, nagekeken op `90e4d4e`. `ProcessLauncherHaiku.cpp` start twee soorten processen, `ProcessType::Web` en `ProcessType::Network`, met `posix_spawn`, een `SOCK_SEQPACKET`-paar, en het environ van de ouder. Geen `setuid`. `ProcessExecutablePathHaiku.cpp:57-68` noemt de programma's `WebProcess` en `NetworkProcess`, naast de applicatie. Het UI-proces is het programma dat de gebruiker start. MiniBrowser is dat programma in de poort. Aomi zou dat zijn.

De poort gebruikt geen Haiku-port als IPC. Dat is de UNIX-socket uit het eindverslag van 2024, en de code hierboven doet hetzelfde. Gedeeld geheugen is eveneens het UNIX-pad uit dat verslag. Deze sessie heeft de bitmap-code niet opnieuw geopend.

Compositor, nagekeken in `Source/cmake/OptionsHaiku.cmake` op `haiku-webkit2`: `USE_COORDINATED_GRAPHICS ON`, `USE_TEXTURE_MAPPER ON`, `USE_NICOSIA ON`, `ENABLE_GRAPHICS_CONTEXT_GL OFF`, `ENABLE_WEBGL OFF` (regels 93 en 202-206). Bovenaan eist het bestand toch `OpenGLES2` en `EGL` (regels 43-44). De 1.10.0-pakketlijst eist `lib:libgl`. Op R5 is dat Zink op RADV, of llvmpipe (`docs/GPU-ARCHITECTURE.md:30-31`). Lavapipe blijft het Vulkan-softwarepad en krijgt hier geen werk. De launcher opent `/dev/gpu` niet. Een zoektocht door de hele WebKit-boom op die padnaam is niet gedaan. GFX9, de ring, de reset en de stop bij afsluiten blijven zoals ze zijn. Deze ronde wijzigt ze niet.

Meerdere gebruikers: het proces is het programma van de aangemelde persoon. `posix_spawn` neemt die gebruiker mee. Het wordt geen uid 0. Waar NetworkProcess de cookies schrijft, is in deze sessie niet gelezen. De schil blijft `find_directory` gebruiken, zoals de rest van dit plan.

#### De API

1.10.0-2, nagekeken in de uitgepakte headers.

`BWebView` is een `BView` (`WebView.h:51`). Aanroepen die de schil al noemt en die de header heeft: `LoadURL` (`:99`), `Reload` (`:102`), `GoBack` (`:103`), `GoForward` (`:104`), `StopLoading` (`:105`), `IncreaseZoomFactor` en `DecreaseZoomFactor` (`:106-107`), `FindString` (`:110`), `MainFrameURL` (`:97`), `MainFrameTitle` (`:95`), `WebPage` (`:93`), `Shutdown` (`:65`), `SetUserData` (`:121`). Er is ook `ResetZoomFactor` (`:108`) en `SetDarkMode` (`:116`).

`BWebPage` (`WebPage.h`) heeft `InitializeOnce` (`:86`), `ShutdownOnce` (`:87`), `SetCacheModel` (`:88`), `SetDownloadListener` (`:94`), `GetContentsAsMHTML` (`:111`), `SendEditingCapabilities` (`:125`), `SendPageSource` (`:126`), `SetListener` (`:93`), `ResendNotifications` (`:123`). De downloadcodes staan hierboven. `BWebDownload` (`WebDownload.h:56`) heeft `Start`, `Cancel`, `URL`, `Path`, `Filename`, `CurrentSize`, `ExpectedSize`.

`BWebWindow` (`WebWindow.h:38`) is een `BWindow` met de virtuele haken die de oude schil overschrijft, onder meer `NewWindowRequested` (`:58`), `LoadNegotiating` (`:64`), `AuthenticationChallenge` (`:91`) en `UpdateGlobalHistory` (`:90`). De header eist die klasse niet voor de view.

WebKit2, nagekeken in `Source/WebKit/UIProcess/API/haiku/WebView.h` op `haiku-webkit2`. De klasse heet ook `BWebView`. Zij is geen `BView`. De constructor neemt een `BWindow*` (`:45`). De aanroepen zijn `loadURIRequest` (`:47`), `goForward` (`:49`), `goBackward` (`:50`), `stop` (`:51`), `zoomIn`, `zoomOut`, `resetZoom` (`:64-66`), `findString` (`:68`), `title` (`:61`), `getCurrentURL` (`:54`), `progress` (`:59`), `isLoading` (`:60`), `shutdown` (`:67`), `paintContent` (`:52`). Er is geen `BWebPage` en geen `BWebWindow` in die header.

Downloads zijn een andere haak. `DownloadListenerHaiku.h` zet `WebKit::setDownloadListener(const BMessenger&)`. De codes zijn `B_WEBKIT_DOWNLOAD_ADDED` `'dwna'` en `B_WEBKIT_DOWNLOAD_PROGRESS` `'dwnp'`, dezelfde vier tekens als de Legacy-codes, en daarnaast `FINISHED` `'dwnf'`, `FAILED` `'dwnx'` en `CANCELLED` `'dwnc'`. Er is geen klasse `BWebDownload` in die header. Klaar, mislukt en afgebroken zijn nieuw. `B_DOWNLOAD_REMOVED` (`'dwnr'`) komt daar niet voor.

Wat meegaat als gedrag, met een andere naam: laden, terug, vooruit, stop, zoom, zoeken, titel, URL, voortgang. Wat de WebKit2-header niet heeft: `AuthenticationChallenge`, geschiedenis, een nieuw venster, MHTML, de paginabron, en de bewerk-mogelijkheden. "Open link in new tab" zit in MiniBrowser en in het contextmenu (commit `24a55b742131`, 7 oktober 2026), niet als virtuele haak voor een ander programma. Cookies en het wachtwoord blijven in ronde 1 de bestanden van de schil. Het WebKit2-netwerkproces heeft zijn eigen pad, en dat pad is hier niet gelezen.

Aomi kan in ronde 1 niet op die WebKit2-klasse zitten. De schil zou de view, de luisteraar en de downloads een tweede keer moeten schrijven, en auth, geschiedenis en een nieuw venster hebben daar nog geen haak. De 1.10.0-view is een `BView` in een gewone `BWindow`. De meldingen komen via `SetListener`. Dat venster blijft staan als de motor wisselt. Alleen het object achter de view wisselt, en op WebKit2 roept die view `paintContent` aan omdat de nieuwe `BWebView` zelf geen `BView` is.

#### Proef

Geen. HaikuPorts levert geen MiniBrowser en geen WebKit2-pakket voor x86_64. De zip van 8 juni 2026 geeft 404. Er is niets geboot. QEMU usernet is geen LAN. WebKit is niet gebouwd om aan een pakket te komen.

#### Een R5-recipe, zoals Mesa

Mesa gaat via `distro/mesa/build-radv.sh`: een Linux-host, een vastgezette bron, een cross-bestand, en een hpkg dat na de bouw op de image komt. WebKit hoort hetzelfde soort recipe, niet een kopie van de bron in `distro/`. De R5-code blijft MIT. WebKit blijft een dynamisch pakket. Hij gaat niet in een MIT-bestand, niet in `libbe` en niet in `libr5ui`.

De host is een 64-bit Linux, omdat de cross-compile van 12 oktober 2026 laat zien dat 32-bit Haiku de bouw niet trekt, en omdat R5 64-bit is. Geheugen: 32 GB is wat DigitalBox op 21 mei 2026 en het videostuk van 8 juni 2026 noemen. Met 16 GB bevriest de bouw, en `-j4` is dan de grens die PulkoMandy noemt. Schijf: 15 GB als je zuinig bent (8 mei 2024), 25 GB als je de tests niet weglaat (20 augustus 2024). Tijd: ettelijke uren voor één release-bouw. Een kloktijd heeft deze sessie niet gemeten, want er is niet gebouwd.

De patches zijn de Haiku-tak zelf. `haiku-webkit2` is 104 commits vóór `haiku`, bovenop upstream 626.1.6. Een latere upstream-stap is dezelfde beweging die de poort al doet: een "Merge WebKit … into Haiku", daarna het versiebestand lezen, daarna de recipe-pin. R5 pint de commit, zoals Mesa een tarball pint (`docs/GPU-ARCHITECTURE.md:183`). Geen derde vork in deze repository.

Licenties, van de bestanden die open gingen. De tarball bevat `Source/WebCore/LICENSE-LGPL-2` en `LICENSE-LGPL-2.1`. `WebKitVersion.h.in` zegt Library GPL versie 2 of later. De Haiku-headers van `BWebView` in 1.10.0 zijn BSD met de Apple-disclaimer. `WebKitInfo.cpp` is MIT, copyright 2012 Alexandre Deckner. Het hpkg noemt in zijn attributen GNU LGPL v2, GNU LGPL v2.1 en MIT, copyright 1998-2026 Apple Inc., Google Inc., Sony, Samsung, Igalia, et al. Het enige licentiebestand in het pakket is de BSD-tekst "WebKit Apple". De LGPL-teksten zitten in de bron-tarball. Niet elk `LICENSE`-bestand in de boom is gelezen.

Het bronaanbod is de tarball van de commit die de recipe pint, plus de patches als die buiten die commit vallen. `docs/LICENSING.md` zegt dat een publieke image de bron van die bouw moet kunnen naleveren, inclusief de scripts. `LICENSES/LGPL-2.1.txt` bestaat al voor een ander onderdeel. Die tekst wordt niet herschreven. Bij de image-stap krijgen `THIRD_PARTY_NOTICES.md` en `docs/LICENSING.md` een rij: pakketnaam, versie, commit, de drie licenties, de copyrightregel uit de recipe, en de URL van de tarball. Tot die stap blijft de rij weg, want het pakket is dan nog het HaikuPorts-pakket dat de image al laadt.

#### HTTP/2

De netwerkbackend is curl. `OptionsHaiku.cmake` op `haiku-webkit2` zet `USE_CURL ON` (regel 239) en zoekt `CURL`. Hetzelfde bestand zoekt ook het private headerpad van netservices, op `UrlProtocolDispatchingListener.h` (regels 51-56). Dat is een configure-eis. De laadweg die is nagekeken is curl.

In de tag 1.10.0, `Source/WebCore/platform/network/curl/CurlContext.cpp`, regel 131: `m_isHttp2Enabled` komt uit `curl_version_info`, het kenmerk `CURL_VERSION_HTTP2`. `CurlHandle::enableHttp` (regels 480-494): is de URL `https` en staat dat kenmerk aan, dan `CURLOPT_HTTP_VERSION` op `CURL_HTTP_VERSION_NONE`, zodat curl via ALPN kiest, en dat kan HTTP/2 zijn. Anders dwingt de code HTTP/1.1. `enableConnectionOnly` (regel 689) dwingt ook 1.1. De code zet HTTP/2 dus niet als vaste eis. Zij vraagt het op `https` als de gelinkte curl het kan.

De curl in de downloadmap is 8.21.0-1 en eist nghttp2 1.63.0-1. Dat is de bouw met HTTP/2. Of een pagina het ook spreekt, is niet getoond. `CurlContext.cpp` op `haiku-webkit2` is niet apart geopend. Het bestand zat niet in de 466 bestanden die `haiku-webkit2` vóór `haiku` heeft. Of de tag en de tak `haiku` daar nog uiteenlopen, is open.

`BHttpRequest` in libnetservices blijft HTTP/1.1 (`HttpRequest.cpp`, `fHttpVersion(B_HTTP_11)`). WebPositive roept die klasse niet aan. De Legacy-header neemt wel een `BUrlContext` aan (`WebPage.h:35` en `:240`). Dat is de cookie-context die de schil meegeeft, niet de client die de bytes haalt.

#### Aanbeveling

Optie C. Ronde 1 bouwt Aomi op `haikuwebkit-1.10.0-2`. Het venster is een `BWindow`. De pagina is de `BWebView` uit dat pakket, een `BView`. De meldingen komen via `SetListener`. De motorronde vervangt het object achter die view door de WebKit2-`BWebView`, en de view roept dan `paintContent` aan. MiniBrowser wordt de GUI niet.

De reden: de nieuwere motor is het doel, en zij is nog geen pakket. Zij crasht volgens het forum van mei en juni 2026. Haar `BWebView` is een andere klasse, zonder auth, zonder geschiedenis en zonder een haak voor een nieuw venster. Ronde 1 op die klasse bouwt de schil tegen een API die de vijf pagina's nog niet betrouwbaar opent, en ronde 2 kan de downloads en de auth niet afmaken. Testen is de flessenhals, niet het schrijven. Op 1.10.0-2 kan een boot een pagina veroordelen. Op WebKit2 is een crash eerst een vraag over de motor.

Het risico van C: de namen van laden, terug, zoom en zoeken veranderen in de motorronde, en de downloadcodes voor klaar en mislukt zijn nieuw. Dat is één object en een paar meldingen, niet een tweede venster. Het risico van meteen op WebKit2 beginnen is dat elke proef op een alpha draait, en dat de ontbrekende haken de schil alsnog een tweede keer raken. Het risico van een `BWebWindow` als basis is dat die klasse in de WebKit2-header niet bestaat, zodat het venster later opnieuw moet.

Sessies. Ronde 1 blijft het handjevol dat de schil al kost. Het trage deel is QEMU, één boot voor de pagina's. De motorronde is het dure deel. Eén sessie haalt een WebKit-bouw van ettelijke uren en een boot niet samen af. Reken op een sessie voor de recipe en de bouw, en daarna sessies per crash, tot het UI-proces de vijf pagina's overleeft. Daarna één sessie om het pagina-object om te zetten en die pagina's opnieuw te doen. Schrijven is daar het kleine stuk.

### Onbewezen, ronde 0

- Of een imagebestand op schijf dezelfde pin heeft. De hpkg in de downloadmap en de repositorylijst wel.
- Of 1.10.0-2 of MiniBrowser een pagina tekent. Er is niet geboot.
- De architectuur van de zip van 8 juni. De URL geeft 404.
- Of ticket 19646 dicht is. De ticketpagina ging niet open.
- De commit die 625.1.25 naar 626.1.6 brengt, buiten de versiecommit `75e5c1219429`. Het aantal upstream-tags ertussen is niet geteld.
- Of `CurlContext.cpp` op de tak `haiku` gelijk is aan de tag.
- Het cookiepad in NetworkProcess.
- Hoeveel commits de tak `haiku` heeft die `haiku-webkit2` niet heeft. De andere richting is 104.
- Een zoektocht door de hele WebKit-boom op `/dev/gpu`.
- Een paginatijd, WebGL, en een pagina die HTTP/2 in de status toont.

## Licentie per klasse

Nieuwe R5-bestanden zijn MIT, copyright 2026 R5OS Contributors. Een bestand van iemand anders houdt zijn eigen licentie (`AGENTS.md`). Kopiëren naar `distro/apps/aomi` mag als de header meegaat en het bestand geen Tracker-privéstuk opent. Een bestand zonder licentieregel gaat niet mee.

| Klasse | Header | Mee als bestand |
| --- | --- | --- |
| `AuthenticationPanel` | MIT, 2010 Stephan Aßmus (`AuthenticationPanel.cpp:1-5`) | Licentie ja. Het wachtwoord is een `BTextControl` met `HideTyping` (`AuthenticationPanel.cpp:40` en `:129`). Gekozen: niet kopiëren. Het nieuwe venster gebruikt `BPasswordControl`. |
| `BookmarkBar` | MIT, 2014 Adrien Destugues (`BookmarkBar.cpp:1-4`) | Nee. Include van `tracker_private.h` (`:22`) en `NavMenu.h` (`:25`). `NavMenu.h` is de Open Tracker License, copyright 1991-2000 Be Incorporated. `AGENTS.md` houdt Tracker-privéstukken dicht. |
| `BookmarkMenu` | Zelfde BSD-bestand als `BrowserWindow` (`BrowserWindow.cpp:155`) | Nee. Afgeleid van `BNavMenu`. |
| `BrowserApp` | BSD met Apple-disclaimer, copyright 2007 Ryan Leavengood en 2010 Stephan Aßmus (`BrowserApp.cpp:1-25`) | Licentie ja, header blijft BSD. Gekozen: niet het product. De nieuwe app is een eigen `BApplication`. |
| `BrowserWindow` | BSD met Apple-disclaimer, copyrights 2007-2015 Andrea Anzani, Ryan Leavengood, Maxime Simon, Stephan Aßmus, Michael Lotz, Rene Gollent, Haiku, Inc. (`BrowserWindow.cpp:1-30`) | Licentie ja, header blijft BSD. Gekozen: niet het product. Het gedrag van Return, Escape en de achtergrondtab wordt opnieuw geschreven in het MIT-venster, kort. Een letterlijke kopie van die functie zou de BSD-header van dit bestand moeten houden; die kopie maken we niet. |
| `BrowsingHistory` | MIT, 2010 Stephan Aßmus (`BrowsingHistory.cpp:1-5`) | Ja. Het bestand include’t `BrowserApp.h` voor de toepassingsnaam. De nieuwe kopie zet de mapnaam `WebPositive` zelf, zodat `BrowserApp` niet mee hoeft. |
| `ConsoleWindow` | MIT, 2014-2023 Haiku, Inc., Zhuowei Zhang, Humdinger (`ConsoleWindow.cpp:1-8`) | Licentie ja. Include van `WebViewConstants.h`, en die header staat niet in git. Niet in de eerste schil. |
| `CookieWindow` | MIT, 2015 Haiku, Inc., Adrien Destugues (`CookieWindow.cpp:1-7`) | Licentie ja. De include is `ColumnListView.h` (`CookieWindow.cpp:14`). De header zelf is `haiku/headers/private/interface/ColumnListView.h`, Open Tracker License. Het vensterbestand blijft MIT; de kolomlijst blijft haar eigen licentie. Niet in de eerste schil. `BColumnListView` is publiek genoeg om te linken: de `Jamfile` linkt `libcolumnlistview.a` (`:73`). |
| `CredentialsStorage` | MIT, 2010 Stephan Aßmus (`CredentialsStorage.cpp:1-5`) | Ja. `HashMap.h` is private shared, MIT, copyright 2004-2009 Ingo Weinhold en 2019 Haiku, Inc. Het wachtwoordbestand blijft buiten log, argv en What’s new. |
| `DownloadProgressView` | MIT, 2010 Stephan Aßmus (`DownloadProgressView.cpp:1-5`) | Ja, samen met het downloadvenster. Include van `shared/BarberPole.h` (`DownloadProgressView.h:13`), MIT, copyright 2017 Julian Harnath, en van `BWebDownload`. |
| `DownloadWindow` | MIT, 2010 Stephan Aßmus (`DownloadWindow.cpp:1-5`) | Ja. Include van `WebDownload.h` (niet in git) en van `SettingsMessage`. |
| `SettingsKeys` | MIT, 2010 Stephan Aßmus en 2014 Haiku, Inc. (`SettingsKeys.cpp:1-10`; het woord "Coptright" staat zo in de bron) | Licentie ja. Gekozen: niet als bestand, want de standaarden zijn vaste `/boot/home`-paden (`:30-32`). De sleutelnamen mogen in een nieuw MIT-bestand terugkomen. |
| `SettingsWindow` | MIT, 2010 Stephan Aßmus en 2019 Haiku, Inc. (`SettingsWindow.cpp:1-5`) | Licentie ja. Include van `FontSelectionView`, en die opent `private/interface/FontPrivate.h` (`FontSelectionView.cpp:15`). Gekozen: niet kopiëren. Het nieuwe formulier is kleiner. |
| `URLInputGroup` | MIT, 2010 Stephan Aßmus (`URLInputGroup.cpp:1-4`) | Licentie ja, zodra `WebView.h` op het include-pad staat. Gekozen: niet de product-balk. Een `BTextControl` dekt Return, Escape en `MarkAsInvalid`. |
| `AutoCompleter`, `AutoCompleterDefaultImpl`, `TextViewCompleter` | MIT, project beam 2002-2006, Oliver Tappe | Licentie ja. Alleen nodig als de aanvulling van `URLInputGroup` later meegaat. Niet in de eerste balk. |
| `BaseURL` | MIT, 2010 Stephan Aßmus (`BaseURL.cpp:1-4`) | Ja, als een geschiedenisregel de host groepeert zoals `_UpdateHistoryMenu`. |
| `FontSelectionView` | MIT, 2001-2010 Haiku (`FontSelectionView.cpp:1-8`) | Licentie ja. De private font-header maakt het geen zelfstandig bestand. Gekozen: niet kopiëren. |
| `WindowIcon.h` | Geen copyrightregel. Het bestand begint bij `kWindowIconWidth` | Nee. Geen licentie aangenomen. |
| `TabManager`, `TabView`, `TabContainerView` | MIT, 2010 Rene Gollent en Stephan Aßmus (`TabManager.cpp:1-6`, `TabView.cpp:1-6`, `TabContainerView.cpp:1-6`) | Ja. De includes in `TabManager.cpp` zijn publieke kits (`Application`, `Button`, `CardLayout`, `GroupView`, `MenuBar`, `Window`, en de rest van dat blok). Geen Tracker-private. Gekozen: deze drie bestanden gaan mee, header ongewijzigd. Ze doen sluiten, een view per tab, en middenklik sluit de tab (`WebTabView::MouseDown`, `TabManager.cpp:560-562`). Een tweede tab-widget in `libr5ui` komt er niet. |
| `WebPositive.rdef` | Geen licentieregel | Nee. Haiku-signatuur en de naam WebPositive. |
| `bookmarks/WebPositiveBookmarks.zip` | Archief, deze lezing heeft de bytes niet geopend | Nee, tot iemand de licentie van de inhoud vaststelt. De `Jamfile` zet het in de home van de image (`:87-88`). |

`SettingsMessage` is geen WebPositive-klasse. `haiku/headers/private/shared/SettingsMessage.h:1-6` is de Be Sample Code-licentie, copyright 2008-2010 Stephan Aßmus en 1998 Eric Shepherd. `docs/LICENSING.md` houdt die licentie apart op gekopieerde headers. Gaat het downloadvenster mee, dan gaat deze header mee met die tekst, niet herschreven naar MIT.

## Wat ontbreekt

- Een `package info` van één concreet imagebestand in deze sessie. De pin in git en de syslog hierboven noemen `haikuwebkit-1.10.0-2`. Dat is de revisie die de boom vraagt, niet een meting van een image die nu op schijf ligt.
- Een sitelijst met een boot. Geen bewijs dat 1.10.0-2 een aanmeldpagina tekent. `docs/VPN-KIT.md` zegt dat veel moderne aanmeldpagina’s falen, zonder adressen.
- De nieuwe schil. Er is één overlappend bestand, geen programma onder `distro/apps/aomi`.
- De tekst van die headers is gelezen. Wat er nog open is, staat onder Onbewezen: een imagebestand, een boot, en de WebKit2-tak van `CurlContext.cpp` tegenover de tag.
- Paden per aangemelde persoon in het oude pakket. #67 staat open. `SettingsKeys.cpp:30-33` heeft de vaste paden nog.
- Een proef dat een pagina de interface gebruikt die de Wi-Fi Kit heeft opgezet. QEMU usernet is geen Wi-Fi en geen LAN (`docs/ROADMAP.md`, `AGENTS.md`).
- WebKit2 op R5. De poort tekent via coordinated graphics, met de GL-context uit (`OptionsHaiku.cmake` op `haiku-webkit2`). MiniBrowser blijft volgens het forum van mei en juni 2026 instabiel. Geen R5-boot. Deze boom levert `libWebKitLegacy.so` (`BuildFeatures:310`).
- WebGL. Op de WebKit2-tak staat `ENABLE_WEBGL` uit (`OptionsHaiku.cmake`). `libgl` op R5 is Zink of llvmpipe. Dat is later.
- Snelheid. Geen paginatijd in `CHANGELOG.md`.
- Multiprocess en een sandbox. De bron hier is één `BApplication` die `BWebPage::InitializeOnce` aanroept. seccomp staat niet in `distro/`. Dat gat blijft tot een latere stap. Het houdt de schil niet tegen.
- VPN-browserlogin en een captive portal. Roadmap-punt 5 noemt de portal apart. Allebei buiten dit plan tot de vijf pagina’s op de nieuwe schil slagen.

## De nieuwe GUI

De indeling volgt WebPositive, omdat die al een Haiku-venster is en omdat de R5-fix in de URL-balk daar zit. Nieuwe code staat in `distro/apps/aomi`. Wat van Haiku meegaat, houdt de header uit de tabel. WebKit-bron wordt nergens gekopieerd.

### Wat mee gaat

De indeling. Menubalk, tabs, navigatierij (terug, vooruit, stop, home, adres), bladwijzerbalk als de submap er is, de pagina, zoekbalk, statusregel met voortgang. Het venster is een `BWindow` met een `BGroupLayout`. De pagina is een `BWebView`, en die is in 1.10.0-2 een `BView` (`WebView.h:51`). Tabs zijn de drie MIT-bestanden `TabManager`, `TabView` en `TabContainerView`, gekopieerd met hun header. Ze gaan niet naar `libr5ui` en niet naar `libbe`. Tot één pagina laadt, is één view genoeg; de klasse mag er dan al in zitten.

De URL-balk als `BTextControl` en een Go-`BButton`. Return laadt (`DispatchMessage` alleen bij `B_RETURN`, `BrowserWindow.cpp:737-740`). Focus verliezen laadt niet. Escape herstelt `MainFrameURL` van de geselecteerde tab. Leeg bij Go roept `BTextControl::MarkAsInvalid(true)` aan (`TextControl.h:43`). Een achtergrondtab laat de balk met rust en bewaart de URL op die tab, zoals de overlay op `distro/.../BrowserWindow.cpp:1549`. `_SmartURLHandler` komt terug als gedrag: de vijf schema’s uit `kHandledProtocols` (`BrowserWindow.cpp:136-142`), andere schema’s via `BRoster` en `application/x-vnd.Be.URL.*`, `localhost`, een domein als `http://`, anders zoeken. De zoek-URL is een instelling van de persoon. De voorvoegsels uit `SettingsKeys.cpp:44-54` mogen mee als gedrag; de standaardmotor blijft een instelling, met als fabriekswaarde de URL die daar staat.

De bladwijzers als bestanden en als idee. Zelfde map `WebPositive/Bookmarks`, zelfde type, zelfde attributen, submap `Bookmark bar`. Het menu is een `BMenu` dat de map leest. Geen `BNavMenu` en geen `BookmarkBar`. “Bookmark this page” schrijft het bestand. “Manage bookmarks” opent de map met `BRoster::Launch`, zoals `_ShowBookmarks` (`:2156`). Slepen naar Tracker kan mee zodra één bladwijzer schrijft. De icoonvergroting in `_CreateBookmark` (`:2030`) gaat niet mee. Een icoon op het bestand is `r5_file_icon_bitmap` of het icoon dat `BWebView` al gaf (`IconReceived`).

De downloads als de gekopieerde `DownloadWindow` en `DownloadProgressView`, MIT-header intact, zodra `WebDownload.h` op het include-pad staat. Doelmap: de bestaande instelling als het bestand er is, anders `find_directory(B_DESKTOP_DIRECTORY)` (`DownloadWindow.cpp:133-136`). `FindDirectory.h` heeft geen downloadmap. Een vaste string `"/boot/home/Desktop"` gaat niet mee. Het menu-item en de sneltoets blijven het idee van het Window-menu (`BrowserWindow.cpp:417-418`, `'D'`). De haak is `BWebPage::SetDownloadListener`. De browser haalt de bytes niet zelf met een tweede HTTP-client.

Verder het gedrag dat de schil al aan de view koppelt: titel en favicon op de tab, voortgang, statusregel, terug en vooruit en stop aan of uit via `NavigationCapabilitiesChanged` (`:1781-1789`, drie bools: achteruit, vooruit, stop), zoomen, pagina opslaan als MHTML via `GetContentsAsMHTML` en een `BFilePanel` (`:849-866`), bron bekijken via `WebPage()->SendPageSource` (`:1043`). Geschiedenis is `BrowsingHistory`, gekopieerd, MIT-header intact. Het menu wordt gevuld zoals `_UpdateHistoryMenu` (`:2293`): het bestand `WebPositive/BrowsingHistory`, platgedrukt `BMessage`, items per dag. Het bestand ligt per `find_directory`-gebruiker.

Sneltoetsen die de bron zet, als `BMenuItem`-shortcut (op Haiku is dat Command): nieuw venster `'N'` (`:400`), nieuwe tab `'T'` (`:404`), sluit venster `'W'` met Shift (`:411`), sluit tab `'W'` (`:413`), zoek `'F'` (`:445`), herladen `'R'` (`:456`), zoom `'+'` `'-'` `'0'` (`:463-468`), bron `'U'` (`:479`), tab 1 tot 9 met Command (`:666-671`), Command-Tab wisselt (`:675`). F5 herlaadt, F11 is volledig scherm (`:720-725`).

`NewWindowRequested` post `NEW_TAB` met `select` gelijk aan `primaryAction` (`:1499-1502`). Een aanvraag die niet primair is, opent de tab en selecteert hem niet. Middenklik op de tab zelf sluit hem (`TabManager.cpp:560-562`). Of een middenklik op een link `primaryAction` false geeft, staat in de ontbrekende header; de app-kant is deze post.

### Wat vervangen wordt

- `BrowserWindow` als product. De basis is een `BWindow`. De header laat de view daarin toe. De rest van het venster is nieuw en MIT.
- `URLInputGroup`, `BIconButton` met resource 201–206, `BookmarkBar`, `BookmarkMenu`.
- `AuthenticationPanel`. Wegens HTTP-auth: een klein venster volgens `docs/STYLE.md`. Paneelkleur, label rechts, veld links, één regel hoog. Het wachtwoord is `BAppearanceKit::BPasswordControl` (`docs/APPEARANCE-KIT.md`, `docs/LIBR5.md`). Geen `BCard` achter een formulier. Glas met `set_window_glass`, zoals `WiFiConnectWindow`. Onthouden schrijft naar `CredentialsStorage` (het MIT-bestand) of naar een nieuw bestand met hetzelfde pad `WebPositive/CredentialsStorage`. Niets in de log, niets in argv, niets in What’s new. `AuthenticationChallenge` leest eerst de bewaarde gegevens, anders het venster (`BrowserWindow.cpp:1808-1849`).
- Het instellingenvenster van de browser. Zelfde formulierregels: startpagina, zoek-URL, home-knop aan of uit, zoom alleen tekst (`"zoom text only"`, `BrowserWindow.cpp:371`), bladwijzerbalk aan of uit (`"show bookmarks bar"`, `SettingsKeys.cpp:42`). Geen nieuw item in de Settings-lijst. Die lijst en de vijf beschrijvingen blijven zoals `AGENTS.md` ze vastlegt. Dit venster is het antwoord op #67 voor het nieuwe programma. `webpositive.hpkg` wordt niet herbouwd om die paden te repareren. `FontSelectionView` gaat niet mee.
- Cookievenster en scriptconsole. Later, met de MIT-bestanden als de headers er zijn. Ze horen niet bij de eerste schil die vijf pagina’s moet openen.
- `scripts/apply-webpositive.py` en de aanroep in `scripts/apply-overlay.sh`, in de stap waarin de image omgaat. Tot die stap blijft de overlay, omdat de sonde de oude schil nog gebruikt.
- Harde `/boot/home`-paden. Alles via `find_directory` / `getpwuid`. Een standaardaccount schrijft onder de eigen home (`docs/MULTIUSER.md`). De mapnaam `WebPositive` blijft de bladwijzer- en geschiedenisnaam, als letterlijke string, niet als home-pad.

`libr5ui` levert wat al bestaat: `BPasswordControl`, `set_window_glass`, en verder niets voor deze schil. Geen tweede widgetset. Geen tab- of URL-klasse in de bibliotheek voor één programma. Geen nieuwe aanroep in `libbe`. Iconen via `libr5icons.so`: `r5_icon_bitmap` voor terug, vooruit en home. Het moersleutel-overlay is alleen voor Settings (`r5_settings_apply_prefs_overlay`). De browser krijgt die niet. Een ander plaatje is een vectorbuffer of een pad naar `r5_vector_icon_bitmap` / `r5_file_icon_bitmap`, geen Tracker-resource.

## Netwerk en de Wi-Fi Kit

De sessie gebruikt de Wi-Fi Kit om te verbinden en te onthouden. Daarna is het een gewone interface. De browser moet dan pagina’s laden zonder een eigen stack, een eigen DNS of een eigen aanroep naar `wpa_supplicant`.

Wi-Fi op de 5625U is getoond. Stick 59 en stick 60 (19 november 2026): WPA2 verbindt op de MT7922 van de laptop en een grote download loopt af. `CHANGELOG.md` legt bij stick 59 vast dat die grote WPA2-download op de 5625U bewezen is. Stick 60 heeft in de changelog op `origin/main` van deze lezing geen eigen kop; de eigenaar noemt die stick erbij. `docs/ROADMAP.md` punt 3 zegt nog dat Wi-Fi op de eigen kaart niet op het metaal is getoond. Die regel loopt achter. WPA3 blijft open en is voor ronde 3 niet nodig.

Ronde 3 (oude stap 15) draait op de 5625U met WPA2: `WiFiController` heeft het netwerk verbonden en Aomi laadt daar één `https`-pagina. QEMU usernet telt niet. VirtualBox bridged is een LAN-proef, geen Wi-Fi-proef. Faalt alleen Aomi terwijl `ping` of een ander programma dezelfde route haalt, dan is de fix in de WebKit-poort (curl, proxy, DNS), in een volgende kleine stap. Faalt de hele machine, dan is het de stack of de kit, en dit plan stopt bij dat feit.

Captive portal en VPN-browserlogin blijven buiten deze stappen. Een `r5vpn:`-handler komt pas als een aanmeldpagina op de nieuwe schil het formulier toont.

## GPU

Geen wijziging aan de GFX9-ring, de workload, compute, de kernelstage, de interrupt, GART, de reset of de stop bij afsluiten. Die contracten in `AGENTS.md` en `docs/GRAPHICS.md` blijven.

De browser opent `/dev/gpu/radeon_hd/<n>` niet. Tekenen is app_server. Stick 57 en stick 51 blijven limieten voor elke `BWebView` die tekst en bitmaps tekent. WebGL en een Vulkan-pad in WebKit zijn geen stap in dit plan. Als de poort `libgl` laadt, is dat Zink op RADV of llvmpipe, zoals `docs/GPU-ARCHITECTURE.md`. Lavapipe blijft geïnstalleerd en krijgt geen werk vanuit de browser.

Skia-opwarming. Op Lavapipe compileert het eerste Aomi-venster nieuwe shaders. De opwarmronde (de gewone shaders vooraf compileren, de splash laten staan tot dat klaar is) is een afhankelijkheid van ronde 3. Het is geen stap in dit plan.

## Licentie bij elke stap

- Nieuwe bestanden onder `distro/apps/aomi`: MIT, SPDX, copyright 2026 R5OS Contributors.
- Gekopieerde WebPositive-bestanden: de header uit de tabel, ongewijzigd. Een R5-wijziging in zo’n bestand krijgt een SPDX-snippet, copyright 2026 R5OS Contributors, MIT. De overlay op `LoadNegotiating` heeft die snippet nog niet; die snippet hoort bij een latere landing, niet bij dit plan.
- `libWebKitLegacy`: eigen pakket, dynamisch. `BuildFeatures:310` noemt dat bestand. LGPL zoals de bron per bestand is, plus de BSD- en MIT-delen, blijft de uitspraak van het onderzoek van 19 november 2026; het hpkg is hier niet uitgepakt. Niet in de R5-bron, niet in `libbe`, niet in `libr5ui`.
- `BrowserWindow.cpp` in `distro/apps/webpositive` blijft het Haiku-bestand met de BSD-header zolang de overlay bestaat. Geen MIT-header eroverheen.
- `SettingsMessage.h`, als het meegaat: Be Sample Code, niet MIT.
- Bij de stap die de image omzet: `THIRD_PARTY_NOTICES.md` noemt het pakket `haikuwebkit-1.10.0-2`, de licentie, de copyrightregel uit de recipe en waar de bron staat. De tekst van de LGPL blijft in `LICENSES/` als die er al staat, en wordt niet herschreven.
- Geen combinatie met GPL-2.0-only of met de GPL-3-artwork.

## Rondes

Ronde 0 is het verslag hieronder, vóór code. Daarna drie bouwrondes, elk één pull request, en een motorronde. De oude stappen 1–16 blijven de inhoud van ronde 1 tot 3. De vijfde pagina noemt de eigenaar vóór ronde 1. Ronde 0 raadt aan: ronde 1 op 1.10.0-2, het venster een `BWindow`, en de nieuwere motor in de motorronde. De schil wordt één keer gebouwd. Alleen het pagina-object wisselt later.

### Ronde 0

Het verslag staat onder De motor. Geen jam, geen code, geen stick. De aanbeveling is optie C: ronde 1 op 1.10.0-2, met een `BWindow`, en de nieuwere motor daarna.

### Ronde 1

Kopiëren en hernoemen (Aomi, signatuur `application/x-vnd.R5-Aomi`), in de image bouwen naast WebPositive, en het headercontract (stap 4). Dat contract leest een agent met WSL, uit `haikuwebkit_devel-1.10.0-2` in `/home/nickr/r5os/haiku/generated.x86_64/download/`. De vijf pagina’s op Aomi in QEMU. Oude stappen 1–7.

1. **Versie van de image.** De AMD64-pin is `haikuwebkit-1.10.0-2` en `haikuwebkit_devel-1.10.0-2` (`haiku/build/jam/repositories/HaikuPorts/x86_64:108-109`). Die pin volgt WebKit 625.1.25, gelezen in de bron-tarball. Een gebouwde image bevestigen met `package info` op het imagebestand blijft nuttig en is deze sessie niet gedaan. Geen code, geen jam.

2. **Vijf pagina’s op Aomi, in QEMU.** De lijst hierboven, inclusief het vijfde adres van de eigenaar en minstens het aanmeldformulier. De eigenaar noemt het vijfde adres vóór ronde 1. Per pagina: opent, formulier zichtbaar, scriptfout, crash, of de balk de URL toont. Geen claim over de 5625U. De oude schil wordt hier niet verbeterd, behalve als de sonde niet eens start. Dit is niet het product.

3. **Revisie, alleen als een image niet 1.10.0-2 heeft.** De recipe-revisie uit het onderzoek in de image brengen en stap 2 opnieuw. Wijkt de pin af van wat HaikuPorts bouwt, dan hoort het versienummer in `CHANGELOG.md` en in `THIRD_PARTY_NOTICES.md`. Geen WebKit2 in deze stap.

4. **Contract van `BWebView`.** Gelezen in ronde 0, uit `haikuwebkit_devel-1.10.0-2` in `/home/nickr/r5os/haiku/generated.x86_64/download/`, uitgepakt naar `/tmp` met het `package`-programma van de boom. `BWebView` is een `BView` (`WebView.h:51`). Een gewone `BWindow` mag haar houden. `B_DOWNLOAD_ADDED` is `'dwna'` (`WebPage.h:73`). Een tweede lezing is alleen nodig als het pakket wijzigt. Geen code in de browser.

5. **Leeg programma.** `distro/apps/aomi`: `BApplication`, één `BWindow`, één `BWebView`, één `file://`-pagina. De luisteraar is `BWebPage::SetListener`. MIT, behalve een gekopieerd bestand uit de tabel. Linken zoals `Jamfile:73-76`: `libWebKitLegacy.so`, `libnetservices.a`, `be`, `network`, en de rest die de view nodig heeft. `tracker` alleen als een gekopieerd bestand dat nog eist; de nieuwe bladwijzers linken Tracker niet voor `BNavMenu`. Aomi komt in de image naast WebPositive. De overlay van WebPositive blijft tot ronde 3. Klaar als het venster die ene lokale pagina toont. QEMU is genoeg. De 5625U niet.

6. **URL-balk.** `BTextControl`, Go, Return, Escape, `MarkAsInvalid` bij lege invoer, slim adres, achtergrondgedrag ook al is er nog één tab (de tak voor “niet de huidige view” staat er meteen, zodat tabs hem niet later breken). Klaar als example.com of de lokale pagina via de balk laadt en Escape de vorige URL terugzet.

7. **Menu’s en navigatie.** `BMenuBar` met Window, Edit, View, History. Terug, vooruit, home via `r5_icon_bitmap`. Stop en herladen als tekst (`StopLoading` en `Reload`, `BrowserWindow.cpp:881` en `:823`). Knippen, kopiëren en plakken via `B_EDITING_CAPABILITIES_RESULT` (`:1089-1108`, bools `"can cut"`, `"can copy"`, `"can paste"`). Klaar als terug na een tweede pagina de eerste toont.

### Ronde 2

Eerst een mockup, goedgekeurd door de eigenaar, vóór er UI wordt gebouwd. Daarna de R5-look, glas, R5-iconen, bladwijzers, de bladwijzerbalk, downloads, auth, zoeken en zoom, tabs, en paden per persoon met het instellingenformulier (#67). Oude stappen 8–14.

8. **Bladwijzers.** Schrijven en het menu vullen vanuit `WebPositive/Bookmarks`. Beheren opent Tracker. Klaar als een bladwijzer die WebPositive al schreef in het menu staat en een nieuwe in diezelfde map belandt, onder de home van de aangemelde persoon. Modus van een nieuwe map: de bron gebruikt `0777` (`:1935`). Gekozen: dezelfde modus, zodat een map die WebPositive aanmaakte en een map die de nieuwe app aanmaakt niet uit elkaar lopen.

9. **Bladwijzerbalk.** Rij knoppen uit de submap `Bookmark bar`, zonder `BookmarkBar.cpp`. View-menu zet hem aan en uit. Klaar als één bestand in die submap een knop is die laadt.

10. **Downloads.** `DownloadWindow` en `DownloadProgressView` kopiëren, MIT-header intact, `SetDownloadListener` zoals `BrowserApp.cpp:243`. Doelmap via `find_directory(B_DESKTOP_DIRECTORY)` als er nog geen instelling is. Klaar als één bestand van `https://example.com` (of een ander klein bestand op die sonde) op schijf staat en in de lijst zichtbaar is. Ontbreekt de header op het include-pad: stap overslaan en dat in `CHANGELOG.md` zetten. Geen tweede client.

11. **HTTP-auth.** Venster met `BPasswordControl` en `docs/STYLE.md`. `AuthenticationChallenge` zoals de oude schil: eerst opgeslagen gegevens, anders het venster (`:1808-1849`). Wachtwoord niet loggen. Klaar als een lokale testpagina om een naam vraagt en het venster verschijnt. Een echte site telt pas in stap 2-herhaling op de nieuwe schil.

12. **Zoeken, zoom, status.** Zoekbalk onder de pagina (`BTextControl`, `FindString` op `:1050`), `BStatusBar`, zoom in het View-menu, `IncreaseZoomFactor` / `DecreaseZoomFactor` met de bool zoom-alleen-tekst. Klaar op de lange tekstpagina uit de sonde.

13. **Tabs.** De gekopieerde `TabManager`. Sneltoetsen zoals nu (Command-T, Command-W, Command-1 tot 9, Command-Tab). Een tab op de achtergrond laat de URL-balk met rust. Middenklik op de tab sluit hem (`TabManager.cpp:560-562`). Een niet-primaire `NewWindowRequested` opent een tab zonder hem te selecteren (`BrowserWindow.cpp:1502`). Klaar als die aanvraag de voorgrond niet vervangt.

14. **Instellingen.** Formuliervenster, `find_directory`, startpagina, zoek-URL, home-knop, zoom alleen tekst, bladwijzerbalk. Geen Settings-item. Geen `FontSelectionView`. Klaar als twee accounts op één image verschillende startpagina’s houden. Virtuele machine is genoeg. Geen fast user switching: uitloggen, andere persoon, opnieuw (`docs/MULTIUSER.md`).

### Ronde 3

De image omzetten, en één stick op de 5625U met Wi-Fi. URL-types, WebPositive eruit, `THIRD_PARTY_NOTICES`, What's new. Oude stappen 15–16.

15. **Wi-Fi.** Op de 5625U met WPA2, de MT7922 van de laptop: één `https`-pagina in Aomi. Stick 59 en stick 60 hebben de verbinding en een grote download al getoond. WPA3 blijft open en is hier niet nodig. De browser linkt `libr5wifi.so` niet. Opschrijven dat het de 5625U was en niet QEMU usernet. Zonder die pagina blijft de stap open en gaat stap 16 niet naar een claim dat Aomi op die Wi-Fi laadt.

16. **Image omzetten.** Vijf pagina’s opnieuw, nu op de nieuwe schil, waarvan minstens de aanmeldpagina. Daarna pas: URL-types `http`, `https`, `file` en `ftp` naar `application/x-vnd.R5-Aomi` (`WebPositive.rdef:25-28`: `application/x-vnd.Be.URL.file`, `.ftp`, `.http`, `.https`). `webpositive.hpkg` uit `OptionalPackages`, `apply-webpositive.py` uit `apply-overlay.sh`. De welkomstpagina moet de nieuwe app openen. `CHANGELOG.md` en, als de ticketstatus wijzigt, `python3 scripts/tracker-report.py` met `tracker/r5-status.tsv` en `tracker/STATUS.md`. #67 dicht als de nieuwe app de paden heeft en het oude pakket weg is. Blijft het oude pakket ernaast, dan blijft #67 open.

Skia-opwarming is een afhankelijkheid van deze ronde, geen stap hier. Op Lavapipe compileert het eerste Aomi-venster nieuwe shaders. De opwarmronde compileert de gewone shaders vooraf en laat de splash staan tot dat klaar is.

Ronde 3 is het moment waarop roadmap-punt 4 de gekozen motor in de image heeft, op de kits, onder de naam Aomi. Ronde 0 zet die motor op de nieuwere WebKit, na ronde 1 op `haikuwebkit-1.10.0-2`. Snelheid, WebGL en de sandbox blijven dan nog open als ze niet in die boot zaten. Wi-Fi op de 5625U is al getoond; deze ronde laat Aomi daar een pagina laden.

### Motorronde

Na ronde 1, en vóór ronde 3 als de schil dan al vijf pagina’s toont. De nieuwere motor als R5-recipe (oude stap 17). De schil blijft. Het pagina-object wisselt. MiniBrowser wordt niet de GUI.

17. **WebKit2.** Softwarecompositor, bitmaps die app_server toont, content-proces. GL alleen via Zink of llvmpipe. Lavapipe onaangeroerd. Geen ring- of resetwijziging. Eerste bewijs is dezelfde vijf pagina’s, plus een notitie of het UI-proces blijft hangen. Een crash zoals het forum die in mei 2026 beschreef, stopt deze stap. De schil die al gebouwd is blijft het product als de API dat toelaat. MiniBrowser wordt niet de GUI.

## Onbewezen

- Of een bepaald imagebestand op schijf dezelfde pin `haikuwebkit-1.10.0-2` heeft. De repositorylijst en `generated/boot-5625u-10/syslog:2044` zeggen dat wel voor de boom en voor die ene eerste start.
- Of 1.10.0-2 een aanmeldpagina tekent.
- De vijfde pagina, tot de eigenaar die noemt, vóór ronde 1.
- Het cookiepad in het WebKit2-netwerkproces. De schil blijft `find_directory` gebruiken.
- Of `set_window_glass` de pagina met rust laat.
- Een `https`-pagina van Aomi op de Wi-Fi van de 5625U. WPA2 en een grote download zijn getoond (stick 59 en stick 60). WPA3 blijft open.
- Stick 57 en stick 51: de lettercrash en de volle VRAM-tabel zijn niet opnieuw geboot.
- WebKit2 op R5. De bron en de recipe zijn gelezen. Er is niet gebouwd en niet geboot.
- Paginatijd en WebGL. HTTP/2: de curl-backend zet op `https` `CURL_HTTP_VERSION_NONE` als curl het kenmerk heeft (`CurlContext.cpp:131` en `:485-487`, tag 1.10.0). Een pagina die het protocol toont, is er niet.
- Dat een standaardaccount de bladwijzermap niet deelt met de administrator. Uid 0 leest homes nog (`docs/MULTIUSER.md`). De nieuwe code gebruikt `find_directory`. Dat is geen bewijs dat de ene persoon de cookies van de ander niet kan lezen zolang uid 0 dat kan. Het geschiedenisbestand en het credentialbestand liggen naast de bladwijzers, onder dezelfde instellingenmap.

## Bronnen die ik gelezen heb

In git op `origin/main`, via `git show` en `git grep`:

- `AGENTS.md`
- `docs/ROADMAP.md` (punt 4 en de GPU-alinea)
- `docs/MULTIUSER.md` (bundled applications, WebPositive, #67)
- `docs/GPU-ARCHITECTURE.md`
- `docs/LIBR5.md`
- `docs/APPEARANCE-KIT.md`
- `docs/STYLE.md`
- `docs/LICENSING.md` (Be Sample Code op een andere header; de licentie van `SettingsMessage.h` is in die header zelf gelezen)
- `docs/VPN-KIT.md`
- `docs/BUGFIX-RELEASE.md` (WebPositive en de Web Kit laatst)
- `distro/TODO.md`
- `distro/rest/build/jam/OptionalPackages:151-163`
- `distro/apps/webpositive/src/apps/webpositive/BrowserWindow.cpp` (alleen het verschil in `LoadNegotiating`, regels 1536-1557)
- `haiku/src/apps/webpositive/` (alle 46 paden; licentieregels van elk `.cpp` en `.h`; de gedragsregels die dit plan noemt)
- `haiku/src/apps/webpositive/Jamfile:67-90`
- `haiku/build/jam/BuildFeatures:302-317`
- `haiku/build/jam/repositories/HaikuPorts/x86_64:1-3` en `:108-109`
- `haiku/headers/os/interface/TextControl.h:43` en `haiku/src/kits/interface/TextControl.cpp:592-604`
- `haiku/headers/os/storage/FindDirectory.h:14` en `:58-66`
- `haiku/headers/private/shared/SettingsMessage.h:1-6`, `IconButton.h`, `BarberPole.h`, `HashMap.h`
- `haiku/headers/private/interface/ColumnListView.h` en `haiku/headers/private/tracker/NavMenu.h` (eerste licentieregels)
- `generated/boot-5625u-10/syslog:2044`
- `scripts/apply-webpositive.py` en de aanroep in `scripts/apply-overlay.sh` (uit het vorige plan; het overlay-verschil is opnieuw gediffd)
- `distro/wifi/lib/R5WiFi.h` en het begin van `distro/wifi/lib/WiFiController.cpp`
- `distro/network/src/kits/network/libnetservices/HttpRequest.cpp` (`fHttpVersion(B_HTTP_11)`)
- `distro/icons/R5Icons.h`
- `CHANGELOG.md`: 3 november 2026, stick 57, stick 51

Ronde 0, 19 november 2026, nagekeken op deze machine:

- `haikuwebkit_devel-1.10.0-2-x86_64.hpkg` en `haikuwebkit-1.10.0-2-x86_64.hpkg`, `curl-8.21.0-1` en `nghttp2-1.63.0-1`, met het `package`-programma van de boom. Headers uitgepakt naar `/tmp`. `WebView.h:51`, `WebPage.h:73-74` en `:93`, `WebDownload.h:44-45`, `WebWindow.h`.
- De tarball `haikuwebkit-1.10.0.tar.gz`: `Configurations/Version.xcconfig` (625.1.25), `OptionsHaiku.cmake` (`ENABLE_WEBKIT OFF`), `CurlContext.cpp:131` en `:480-494`, `UserAgentHaiku.cpp`, `LICENSE-LGPL-2` en `LICENSE-LGPL-2.1`.
- Codeberg, takken `haiku` (`56704ac`, 11 oktober 2026) en `haiku-webkit2` (`90e4d4e`, 12 oktober 2026). `Version.xcconfig` regels 24-26 (626.1.6) op allebei. Commit `75e5c1219429`. `OptionsHaiku.cmake`, `WebView.h`, `ProcessLauncherHaiku.cpp`, `ProcessExecutablePathHaiku.cpp:57-68`, `DownloadListenerHaiku.h`. Vergelijking `haiku...haiku-webkit2`: 104 commits.
- De recipe, regel 12, 14 en 103.

Gelezen, niet als een bestand op schijf nagekeken:

- [GSoC-plan, 8 mei 2024](https://www.haiku-os.org/blog/zardshard/2024-05-08_gsoc_2024_plans_for_fixing_haikus_webkit2_port/)
- [GSoC-eindverslag, 20 augustus 2024](https://www.haiku-os.org/blog/zardshard/2024-08-16_gsoc_2024_porting_webkit2_final_report/)
- [Cross-compileren, 12 oktober 2026](https://www.haikuinsider.org/cross-compiling-webkit2/)
- Het forum van 18 april 2026 en de zip-draad van 8 juni 2026, als zoektekst. De ticketpagina 19646 ging niet open. De mailinglijstkopie van 23 mei 2026 wel.

Dit bestand is `docs/AOMI.md`. De licentietabel, `MarkAsInvalid`, `SetDownloadListener` (`BrowserApp.cpp:243`) en de pin van de image vandaag (`haikuwebkit-1.10.0-2`) blijven. Die pin is de motor van ronde 1. Zij is niet het einddoel.
