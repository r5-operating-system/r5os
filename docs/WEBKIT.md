<!-- SPDX-License-Identifier: MIT -->
<!-- SPDX-FileCopyrightText: 2026 R5OS Contributors -->

# WebKit op R5

Plan, herzien 19 november 2026. Alleen lezen. Geen poort, geen bouw, geen commit. De motorkeuze is gemaakt op 19 november 2026: de Haiku-WebKit-poort, een schil op `BView`. Dit plan is de volgorde daarna.

`generated/BROWSER-PLAN-REVIEW.md` stond niet op deze machine. Geen worktree `R5OS-amd64-stick`, geen kopie in de repo en geen kopie in `/cursor/stores/self`. Gekozen: herzien vanuit `origin/main` (`git show` en `git grep`), en bij een open punt de keuze hieronder zetten.

Nick heeft de schil gekozen. De systeembrowser krijgt een nieuwe GUI, gebaseerd op WebPositive, op de Interface Kit en op R5 UI. De oude `BrowserWindow` is niet het product. Hij blijft alleen de sonde in de eerste twee stappen, tot de nieuwe schil dezelfde pagina’s opent.

Officieel eerst AMD64. `origin/main` is de bron van waarheid.

De WebKit-headers (`WebView.h`, `WebPage.h`, `WebDownload.h`, `WebWindow.h`, `WebViewConstants.h`) staan niet in git. Het pakket `haikuwebkit_devel-1.10.0-2-x86_64.hpkg` stond niet op deze machine (`wsl` ontbreekt, `/home/nickr/r5os` ontbreekt). Aanroepen hieronder komen uit de WebPositive-bron. De declaratie in de header is niet gelezen en wordt niet verzonnen.

## Keuzes waar het open lag

- Het programma komt nieuw onder `distro/apps/browser`. Werknaam in het venster: Browser. Signatuur, als het pakket die krijgt: `application/x-vnd.R5-Browser`. De zichtbare naam kan Nick nog wijzigen; de stappen hangen er niet aan. WebPositive blijft de naam van het Haiku-pakket tot dat pakket uit de image gaat. De oude signatuur is `application/x-vnd.Haiku-WebPositive` (`haiku/src/apps/webpositive/BrowserApp.cpp:64` en `haiku/src/apps/webpositive/WebPositive.rdef:1`). Het rdef-versieblok is app 1.3.0, korte tekst "WebPositive", lange tekst "WebPositive ©2007-2021 The WebKit Haiku Project" (`WebPositive.rdef:4-10`). Dat is de app, niet de engine.
- Bladwijzers blijven de bestanden die er al zijn. `_BookmarkPath` doet `find_directory(B_USER_SETTINGS_DIRECTORY)`, dan `kApplicationName`, dan `"Bookmarks"`, en `create_directory` met modus `0777` (`BrowserWindow.cpp:1921-1935`). `kApplicationName` is `B_TRANSLATE_SYSTEM_NAME("WebPositive")` (`BrowserApp.cpp:65`). `B_USER_SETTINGS_DIRECTORY` is een gebruikersmap (`haiku/headers/os/storage/FindDirectory.h:58-66`). Type `application/x-vnd.Be-bookmark`, attributen `META:url` en `META:title` (`BrowserWindow.cpp:2008-2015`). Submap `Bookmark bar` (`BrowserWindow.cpp:144`). De mapnaam blijft `WebPositive`, ook als het programma anders heet: de constante is de toepassingsnaam, geen aparte bladwijzerconstante. Een nieuwe browser die zijn eigen naam appendt, maakt een tweede map. Modus in de bron is `0777`, ook voor de instellingenmap (`BrowserApp.cpp:204`). Een map die er al is, blijft zoals die is: `create_directory` maakt alleen aan wat ontbreekt.
- Pagina’s lopen via de client die haikuwebkit al linkt. `BrowserApp.cpp:112-117` zet `CURL_COOKIE_JAR_PATH` op `B_USER_SETTINGS_DIRECTORY` / toepassingsnaam / `cookie.jar.db`. WebPositive roept `BHttpRequest` niet aan (geen treffer in `haiku/src/apps/webpositive`). De browser praat niet met `WiFiController` en linkt `libr5wifi.so` niet om te laden. `BHttpRequest` in `distro/network/src/kits/network/libnetservices/HttpRequest.cpp` zet `fHttpVersion(B_HTTP_11)` en is de client van Haiku-programma’s. Overstappen daarop zou die client in de plaats van de poort zetten. Of de curl in het pakket HTTP/2 heeft, staat niet in deze bron. De Wi-Fi Kit zet de interface op via `net_server`. De browser gebruikt die interface zoals elk ander programma: sockets, route, DNS.
- De download-haak zit in de app, ook zonder de header. `BWebPage::SetDownloadListener(BMessenger(fDownloadWindow))` (`BrowserApp.cpp:243`). `DownloadWindow::MessageReceived` neemt `B_DOWNLOAD_ADDED` en `B_DOWNLOAD_REMOVED`, veld `"download"` als pointer naar `BWebDownload` (`DownloadWindow.cpp:248-263`). `DownloadProgressView` neemt `B_DOWNLOAD_STARTED`, `B_DOWNLOAD_PROGRESS` en `B_DOWNLOAD_REMOVED` (`DownloadProgressView.cpp:389`, `:409`, `:419`). De numerieke waarden en de klasse `BWebDownload` staan in de ontbrekende header. Er komt geen eigen downloader naast de poort. Gekozen: de MIT-vensters kopiëren (zie licenties), niet een tweede lijst verzinnen.
- Glas: `BAppearanceKit::set_window_glass()` op het venster, zoals andere R5-vensters. De pagina zelf blijft een dekkende `BWebView`. Blijkt het glas de pagina te matten, dan gaat die aanroep in diezelfde stap weer uit. Dat is onbewezen tot een boot.
- Stop en herladen krijgen geen nieuw nummer in `r5_icon`. De enum in `distro/icons/R5Icons.h` eindigt bij `R5_ICON_LOCKED`. Terug, vooruit en home gebruiken `r5_icon_bitmap()`. Stop en herladen zijn tekstknoppen tot iemand een bestaand vectorplaatje aanwijst. De oude knoppen zijn `BPrivate::BIconButton` met resource 201, 202, 204 en 206 (`BrowserWindow.cpp:515-527`; stop op `:522`). De header is `haiku/headers/private/shared/IconButton.h`, MIT, copyright 2006-2011 Haiku, Stephan Aßmus.
- De vijf sonde-pagina’s, tot Nick een andere lijst geeft: de welkomstpagina of de R5Book via `file://`; `https://example.com`; een lange tekstpagina (Wikipedia); `https://github.com/login` zonder account, alleen of het formulier verschijnt; en een vijfde adres dat Nick noemt als een pagina die hij nu stuk ziet. Zonder dat vijfde adres is de sonde niet klaar voor zijn gevallen. Geen wachtwoorden in het verslag. De standaardstart van WebPositive is `file:///boot/home/config/settings/WebPositive/LoaderPages/Welcome` (`SettingsKeys.cpp:31-32`).
- WebKit2 wacht tot de nieuwe schil die vijf pagina’s op Legacy opent. In deze git-boom staat geen `--no-webkit2` en geen `libJavaScriptCore`. `haiku/build/jam/BuildFeatures:303-311` zet de feature `webkit` aan als `haikuwebkit_devel` er is, en noemt als bibliotheek alleen `libWebKitLegacy.so`. De softwarecompositor en een `BView` komen eerst. OpenGL alleen als Zink op RADV of als llvmpipe. Lavapipe blijft voor Vulkan en krijgt geen browserwerk. Dat de recipe WebKit2 uit zet, blijft een feit uit het onderzoek van 19 november 2026; deze lezing heeft de recipe niet opnieuw geopend.
- `BTextControl::MarkAsInvalid(bool)` bestaat (`haiku/headers/os/interface/TextControl.h:43`). De implementatie zet `BControlLook::B_INVALID` op de look (`haiku/src/kits/interface/TextControl.cpp:592-604`). De URL-balk van WebPositive is geen `BTextControl`: `URLInputGroup::MarkAsInvalid` zet een vlag en tekent de rand (`URLInputGroup.cpp:648-671`). Lege Go roept die vlag aan (`BrowserWindow.cpp:837-838`). Gekozen: de nieuwe balk is een `BTextControl` en roept `MarkAsInvalid` aan. Een extra foutregel is niet nodig voor een leeg veld.
- Of `BWebView` een `BWebWindow` eist, staat in de ontbrekende header. De bestaande schil is `class BrowserWindow : public BWebWindow` (`BrowserWindow.h:98`) en maakt de view met `new BWebView("web view", fContext)` (`BrowserWindow.cpp:1420`). Ze roept `BWebWindow::SetCurrentWebView` en `CurrentWebView()` aan. Gekozen: het nieuwe venster erft `BWebWindow`, dun, tot de header iets anders laat zien.

## Wat er al is

WebPositive in de image is het pakket `webpositive.hpkg`. `distro/rest/build/jam/OptionalPackages:152-157` zet het erin als het optionele pakket WebPositive aan staat en de bouwfeature `webpositive` aan staat. De engine-pin voor AMD64 staat in git: `haiku/build/jam/repositories/HaikuPorts/x86_64:1-3` is het archief `https://eu.hpkg.haiku-os.org/haikuports/master/build-packages`, en regels 108-109 noemen `haikuwebkit-1.10.0-2` en `haikuwebkit_devel-1.10.0-2`. Het 32-bits archief noemt `haikuwebkit-1.5.4-1` (`haiku/build/jam/repositories/HaikuPorts/x86:449`); dat is geen officieel doel. Een opgeslagen syslog noemt hetzelfde AMD64-pakket bij de eerste start: `generated/boot-5625u-10/syslog:2044` (`haikuwebkit-1.10.0-2-x86_64.hpkg`). Deze sessie heeft `package info` op een imagebestand niet gedraaid. WebKit-revisie 625.1.25 staat niet in deze boom; die blijft aan het onderzoek van 19 november 2026 hangen.

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

## Licentie per klasse

Nieuwe R5-bestanden zijn MIT, copyright 2026 R5OS Contributors. Een bestand van iemand anders houdt zijn eigen licentie (`AGENTS.md`). Kopiëren naar `distro/apps/browser` mag als de header meegaat en het bestand geen Tracker-privéstuk opent. Een bestand zonder licentieregel gaat niet mee.

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
- De nieuwe schil. Er is één overlappend bestand, geen programma onder `distro/apps/browser`.
- De tekst van `WebView.h`, `WebPage.h`, `WebDownload.h` en `WebWindow.h`. De aanroepen staan hierboven. De basis van `BWebView` en de waarden van `B_DOWNLOAD_*` zijn ongelezen.
- Paden per aangemelde persoon in het oude pakket. #67 staat open. `SettingsKeys.cpp:30-33` heeft de vaste paden nog.
- Een proef dat een pagina de interface gebruikt die de Wi-Fi Kit heeft opgezet. QEMU usernet is geen Wi-Fi en geen LAN (`docs/ROADMAP.md`, `AGENTS.md`).
- WebKit2 op een `BView` met een softwarecompositor. Het forum van mei 2026, in het onderzoek, noemt MiniBrowser instabiel. Geen R5-boot. Deze boom levert `libWebKitLegacy.so` (`BuildFeatures:310`).
- WebGL. In de Haiku-WebKit2-demo stond het uit, volgens datzelfde onderzoek. `libgl` kan Zink of llvmpipe zijn. Dat is later.
- Snelheid. Geen paginatijd in `CHANGELOG.md`.
- Multiprocess en een sandbox. De bron hier is één `BApplication` die `BWebPage::InitializeOnce` aanroept. seccomp staat niet in `distro/`. Dat gat blijft tot een latere stap. Het houdt de schil niet tegen.
- VPN-browserlogin en een captive portal. Roadmap-punt 5 noemt de portal apart. Allebei buiten dit plan tot de vijf pagina’s op de nieuwe schil slagen.

## De nieuwe GUI

De indeling volgt WebPositive, omdat die al een Haiku-venster is en omdat de R5-fix in de URL-balk daar zit. Nieuwe code staat in `distro/apps/browser`. Wat van Haiku meegaat, houdt de header uit de tabel. WebKit-bron wordt nergens gekopieerd.

### Wat mee gaat

De indeling. Menubalk, tabs, navigatierij (terug, vooruit, stop, home, adres), bladwijzerbalk als de submap er is, de pagina, zoekbalk, statusregel met voortgang. Het venster is een `BWebWindow` (dun) met een `BGroupLayout`. De pagina is een `BWebView`. Tabs zijn de drie MIT-bestanden `TabManager`, `TabView` en `TabContainerView`, gekopieerd met hun header. Ze gaan niet naar `libr5ui` en niet naar `libbe`. Tot één pagina laadt, is één view genoeg; de klasse mag er dan al in zitten.

De URL-balk als `BTextControl` en een Go-`BButton`. Return laadt (`DispatchMessage` alleen bij `B_RETURN`, `BrowserWindow.cpp:737-740`). Focus verliezen laadt niet. Escape herstelt `MainFrameURL` van de geselecteerde tab. Leeg bij Go roept `BTextControl::MarkAsInvalid(true)` aan (`TextControl.h:43`). Een achtergrondtab laat de balk met rust en bewaart de URL op die tab, zoals de overlay op `distro/.../BrowserWindow.cpp:1549`. `_SmartURLHandler` komt terug als gedrag: de vijf schema’s uit `kHandledProtocols` (`BrowserWindow.cpp:136-142`), andere schema’s via `BRoster` en `application/x-vnd.Be.URL.*`, `localhost`, een domein als `http://`, anders zoeken. De zoek-URL is een instelling van de persoon. De voorvoegsels uit `SettingsKeys.cpp:44-54` mogen mee als gedrag; de standaardmotor blijft een instelling, met als fabriekswaarde de URL die daar staat.

De bladwijzers als bestanden en als idee. Zelfde map `WebPositive/Bookmarks`, zelfde type, zelfde attributen, submap `Bookmark bar`. Het menu is een `BMenu` dat de map leest. Geen `BNavMenu` en geen `BookmarkBar`. “Bookmark this page” schrijft het bestand. “Manage bookmarks” opent de map met `BRoster::Launch`, zoals `_ShowBookmarks` (`:2156`). Slepen naar Tracker kan mee zodra één bladwijzer schrijft. De icoonvergroting in `_CreateBookmark` (`:2030`) gaat niet mee. Een icoon op het bestand is `r5_file_icon_bitmap` of het icoon dat `BWebView` al gaf (`IconReceived`).

De downloads als de gekopieerde `DownloadWindow` en `DownloadProgressView`, MIT-header intact, zodra `WebDownload.h` op het include-pad staat. Doelmap: de bestaande instelling als het bestand er is, anders `find_directory(B_DESKTOP_DIRECTORY)` (`DownloadWindow.cpp:133-136`). `FindDirectory.h` heeft geen downloadmap. Een vaste string `"/boot/home/Desktop"` gaat niet mee. Het menu-item en de sneltoets blijven het idee van het Window-menu (`BrowserWindow.cpp:417-418`, `'D'`). De haak is `BWebPage::SetDownloadListener`. De browser haalt de bytes niet zelf met een tweede HTTP-client.

Verder het gedrag dat de schil al aan de view koppelt: titel en favicon op de tab, voortgang, statusregel, terug en vooruit en stop aan of uit via `NavigationCapabilitiesChanged` (`:1781-1789`, drie bools: achteruit, vooruit, stop), zoomen, pagina opslaan als MHTML via `GetContentsAsMHTML` en een `BFilePanel` (`:849-866`), bron bekijken via `WebPage()->SendPageSource` (`:1043`). Geschiedenis is `BrowsingHistory`, gekopieerd, MIT-header intact. Het menu wordt gevuld zoals `_UpdateHistoryMenu` (`:2293`): het bestand `WebPositive/BrowsingHistory`, platgedrukt `BMessage`, items per dag. Het bestand ligt per `find_directory`-gebruiker.

Sneltoetsen die de bron zet, als `BMenuItem`-shortcut (op Haiku is dat Command): nieuw venster `'N'` (`:400`), nieuwe tab `'T'` (`:404`), sluit venster `'W'` met Shift (`:411`), sluit tab `'W'` (`:413`), zoek `'F'` (`:445`), herladen `'R'` (`:456`), zoom `'+'` `'-'` `'0'` (`:463-468`), bron `'U'` (`:479`), tab 1 tot 9 met Command (`:666-671`), Command-Tab wisselt (`:675`). F5 herlaadt, F11 is volledig scherm (`:720-725`).

`NewWindowRequested` post `NEW_TAB` met `select` gelijk aan `primaryAction` (`:1499-1502`). Een aanvraag die niet primair is, opent de tab en selecteert hem niet. Middenklik op de tab zelf sluit hem (`TabManager.cpp:560-562`). Of een middenklik op een link `primaryAction` false geeft, staat in de ontbrekende header; de app-kant is deze post.

### Wat vervangen wordt

- `BrowserWindow` als product. De basis blijft `BWebWindow` tot de header een gewone `BWindow` toelaat. De rest van het venster is nieuw en MIT.
- `URLInputGroup`, `BIconButton` met resource 201–206, `BookmarkBar`, `BookmarkMenu`.
- `AuthenticationPanel`. Wegens HTTP-auth: een klein venster volgens `docs/STYLE.md`. Paneelkleur, label rechts, veld links, één regel hoog. Het wachtwoord is `BAppearanceKit::BPasswordControl` (`docs/APPEARANCE-KIT.md`, `docs/LIBR5.md`). Geen `BCard` achter een formulier. Glas met `set_window_glass`, zoals `WiFiConnectWindow`. Onthouden schrijft naar `CredentialsStorage` (het MIT-bestand) of naar een nieuw bestand met hetzelfde pad `WebPositive/CredentialsStorage`. Niets in de log, niets in argv, niets in What’s new. `AuthenticationChallenge` leest eerst de bewaarde gegevens, anders het venster (`BrowserWindow.cpp:1808-1849`).
- Het instellingenvenster van de browser. Zelfde formulierregels: startpagina, zoek-URL, home-knop aan of uit, zoom alleen tekst (`"zoom text only"`, `BrowserWindow.cpp:371`), bladwijzerbalk aan of uit (`"show bookmarks bar"`, `SettingsKeys.cpp:42`). Geen nieuw item in de Settings-lijst. Die lijst en de vijf beschrijvingen blijven zoals `AGENTS.md` ze vastlegt. Dit venster is het antwoord op #67 voor het nieuwe programma. `webpositive.hpkg` wordt niet herbouwd om die paden te repareren. `FontSelectionView` gaat niet mee.
- Cookievenster en scriptconsole. Later, met de MIT-bestanden als de headers er zijn. Ze horen niet bij de eerste schil die vijf pagina’s moet openen.
- `scripts/apply-webpositive.py` en de aanroep in `scripts/apply-overlay.sh`, in de stap waarin de image omgaat. Tot die stap blijft de overlay, omdat de sonde de oude schil nog gebruikt.
- Harde `/boot/home`-paden. Alles via `find_directory` / `getpwuid`. Een standaardaccount schrijft onder de eigen home (`docs/MULTIUSER.md`). De mapnaam `WebPositive` blijft de bladwijzer- en geschiedenisnaam, als letterlijke string, niet als home-pad.

`libr5ui` levert wat al bestaat: `BPasswordControl`, `set_window_glass`, en verder niets voor deze schil. Geen tweede widgetset. Geen tab- of URL-klasse in de bibliotheek voor één programma. Geen nieuwe aanroep in `libbe`. Iconen via `libr5icons.so`: `r5_icon_bitmap` voor terug, vooruit en home. Het moersleutel-overlay is alleen voor Settings (`r5_settings_apply_prefs_overlay`). De browser krijgt die niet. Een ander plaatje is een vectorbuffer of een pad naar `r5_vector_icon_bitmap` / `r5_file_icon_bitmap`, geen Tracker-resource.

## Netwerk en de Wi-Fi Kit

De sessie gebruikt de Wi-Fi Kit om te verbinden en te onthouden. Daarna is het een gewone interface. De browser moet dan pagina’s laden zonder een eigen stack, een eigen DNS of een eigen aanroep naar `wpa_supplicant`.

Dat wordt pas bewezen als `WiFiController` een netwerk heeft verbonden en de nieuwe schil daar één `https`-pagina laadt. QEMU usernet telt niet. VirtualBox bridged is een LAN-proef, geen Wi-Fi-proef. De 5625U-kaart staat in `docs/ROADMAP.md` punt 3 nog als niet op het metaal getoond. Tot die boot blijft “werkt op de Wi-Fi Kit” onbewezen. Faalt alleen de browser terwijl `ping` of een ander programma dezelfde route haalt, dan is de fix in de WebKit-poort (curl, proxy, DNS), in een volgende kleine stap. Faalt de hele machine, dan is het de stack of de kit, en dit plan stopt bij dat feit.

Captive portal en VPN-browserlogin blijven buiten deze stappen. Een `r5vpn:`-handler komt pas als een aanmeldpagina op de nieuwe schil het formulier toont.

## GPU

Geen wijziging aan de GFX9-ring, de workload, compute, de kernelstage, de interrupt, GART, de reset of de stop bij afsluiten. Die contracten in `AGENTS.md` en `docs/GRAPHICS.md` blijven.

De browser opent `/dev/gpu/radeon_hd/<n>` niet. Tekenen is app_server. Stick 57 en stick 51 blijven limieten voor elke `BWebView` die tekst en bitmaps tekent. WebGL en een Vulkan-pad in WebKit zijn geen stap in dit plan. Als de poort `libgl` laadt, is dat Zink op RADV of llvmpipe, zoals `docs/GPU-ARCHITECTURE.md`. Lavapipe blijft geïnstalleerd en krijgt geen werk vanuit de browser.

## Licentie bij elke stap

- Nieuwe bestanden onder `distro/apps/browser`: MIT, SPDX, copyright 2026 R5OS Contributors.
- Gekopieerde WebPositive-bestanden: de header uit de tabel, ongewijzigd. Een R5-wijziging in zo’n bestand krijgt een SPDX-snippet, copyright 2026 R5OS Contributors, MIT. De overlay op `LoadNegotiating` heeft die snippet nog niet; die snippet hoort bij een latere landing, niet bij dit plan.
- `libWebKitLegacy`: eigen pakket, dynamisch. `BuildFeatures:310` noemt dat bestand. LGPL zoals de bron per bestand is, plus de BSD- en MIT-delen, blijft de uitspraak van het onderzoek van 19 november 2026; het hpkg is hier niet uitgepakt. Niet in de R5-bron, niet in `libbe`, niet in `libr5ui`.
- `BrowserWindow.cpp` in `distro/apps/webpositive` blijft het Haiku-bestand met de BSD-header zolang de overlay bestaat. Geen MIT-header eroverheen.
- `SettingsMessage.h`, als het meegaat: Be Sample Code, niet MIT.
- Bij de stap die de image omzet: `THIRD_PARTY_NOTICES.md` noemt het pakket `haikuwebkit-1.10.0-2`, de licentie, de copyrightregel uit de recipe en waar de bron staat. De tekst van de LGPL blijft in `LICENSES/` als die er al staat, en wordt niet herschreven.
- Geen combinatie met GPL-2.0-only of met de GPL-3-artwork.

## Stappen

Elke stap is één landing. De volgende begint pas als de vorige opgeschreven is, inclusief wat er niet geboot is. Stap 2 start niet vanuit dit plan. Stap 1 is deels gelezen: de pin staat in git.

1. **Versie van de image.** De AMD64-pin is `haikuwebkit-1.10.0-2` en `haikuwebkit_devel-1.10.0-2` (`haiku/build/jam/repositories/HaikuPorts/x86_64:108-109`). Een gebouwde image bevestigen met `package info` blijft nuttig en is deze sessie niet gedaan. Geen code, geen jam. WebKit 625.1.25 is niet deze pin; dat nummer staat niet in de repositorylijst.

2. **Vijf pagina’s op de oude schil, als sonde.** Zelfde image, QEMU mag. De lijst hierboven, inclusief het vijfde adres van Nick en minstens het aanmeldformulier. Per pagina: opent, formulier zichtbaar, scriptfout, crash, of de balk de URL toont. Geen claim over de 5625U. De oude schil wordt hier niet verbeterd, behalve als de sonde niet eens start. Dit is niet het product.

3. **Revisie, alleen als een image niet 1.10.0-2 heeft.** De recipe-revisie uit het onderzoek in de image brengen en stap 2 opnieuw. Wijkt de pin af van wat HaikuPorts bouwt, dan hoort het versienummer in `CHANGELOG.md` en in `THIRD_PARTY_NOTICES.md`. Geen WebKit2 in deze stap.

4. **Contract van `BWebView`.** Het devel-pakket uitpakken en de headers lezen die git niet heeft. Vastleggen wat de aanroepen hierboven in de header zijn, inclusief de basis van `BWebView`, de velden van `B_DOWNLOAD_ADDED`, en of een gewone `BWindow` de view mag houden. Geen code in de browser. Wijkt de header af van een aanroep die dit plan noemt, dan wijzigt het plan op dat punt voordat de schil die aanroep verzint.

5. **Leeg programma.** `distro/apps/browser`: `BApplication`, één `BWebWindow`, één `BWebView`, één `file://`-pagina. MIT, behalve een gekopieerd bestand uit de tabel. Linken zoals `Jamfile:73-76`: `libWebKitLegacy.so`, `libnetservices.a`, `be`, `network`, en de rest die de view nodig heeft. `tracker` alleen als een gekopieerd bestand dat nog eist; de nieuwe bladwijzers linken Tracker niet voor `BNavMenu`. Nog niet in `OptionalPackages`. De overlay blijft. Klaar als het venster die ene lokale pagina toont. QEMU is genoeg. De 5625U niet.

6. **URL-balk.** `BTextControl`, Go, Return, Escape, `MarkAsInvalid` bij lege invoer, slim adres, achtergrondgedrag ook al is er nog één tab (de tak voor “niet de huidige view” staat er meteen, zodat tabs hem niet later breken). Klaar als example.com of de lokale pagina via de balk laadt en Escape de vorige URL terugzet.

7. **Menu’s en navigatie.** `BMenuBar` met Window, Edit, View, History. Terug, vooruit, home via `r5_icon_bitmap`. Stop en herladen als tekst (`StopLoading` en `Reload`, `BrowserWindow.cpp:881` en `:823`). Knippen, kopiëren en plakken via `B_EDITING_CAPABILITIES_RESULT` (`:1089-1108`, bools `"can cut"`, `"can copy"`, `"can paste"`). Klaar als terug na een tweede pagina de eerste toont.

8. **Bladwijzers.** Schrijven en het menu vullen vanuit `WebPositive/Bookmarks`. Beheren opent Tracker. Klaar als een bladwijzer die WebPositive al schreef in het menu staat en een nieuwe in diezelfde map belandt, onder de home van de aangemelde persoon. Modus van een nieuwe map: de bron gebruikt `0777` (`:1935`). Gekozen: dezelfde modus, zodat een map die WebPositive aanmaakte en een map die de nieuwe app aanmaakt niet uit elkaar lopen.

9. **Bladwijzerbalk.** Rij knoppen uit de submap `Bookmark bar`, zonder `BookmarkBar.cpp`. View-menu zet hem aan en uit. Klaar als één bestand in die submap een knop is die laadt.

10. **Downloads.** `DownloadWindow` en `DownloadProgressView` kopiëren, MIT-header intact, `SetDownloadListener` zoals `BrowserApp.cpp:243`. Doelmap via `find_directory(B_DESKTOP_DIRECTORY)` als er nog geen instelling is. Klaar als één bestand van `https://example.com` (of een ander klein bestand op die sonde) op schijf staat en in de lijst zichtbaar is. Ontbreekt de header op het include-pad: stap overslaan en dat in `CHANGELOG.md` zetten. Geen tweede client.

11. **HTTP-auth.** Venster met `BPasswordControl` en `docs/STYLE.md`. `AuthenticationChallenge` zoals de oude schil: eerst opgeslagen gegevens, anders het venster (`:1808-1849`). Wachtwoord niet loggen. Klaar als een lokale testpagina om een naam vraagt en het venster verschijnt. Een echte site telt pas in stap 2-herhaling op de nieuwe schil.

12. **Zoeken, zoom, status.** Zoekbalk onder de pagina (`BTextControl`, `FindString` op `:1050`), `BStatusBar`, zoom in het View-menu, `IncreaseZoomFactor` / `DecreaseZoomFactor` met de bool zoom-alleen-tekst. Klaar op de lange tekstpagina uit de sonde.

13. **Tabs.** De gekopieerde `TabManager`. Sneltoetsen zoals nu (Command-T, Command-W, Command-1 tot 9, Command-Tab). Een tab op de achtergrond laat de URL-balk met rust. Middenklik op de tab sluit hem (`TabManager.cpp:560-562`). Een niet-primaire `NewWindowRequested` opent een tab zonder hem te selecteren (`BrowserWindow.cpp:1502`). Klaar als die aanvraag de voorgrond niet vervangt.

14. **Instellingen.** Formuliervenster, `find_directory`, startpagina, zoek-URL, home-knop, zoom alleen tekst, bladwijzerbalk. Geen Settings-item. Geen `FontSelectionView`. Klaar als twee accounts op één image verschillende startpagina’s houden. Virtuele machine is genoeg. Geen fast user switching: uitloggen, andere persoon, opnieuw (`docs/MULTIUSER.md`).

15. **Wi-Fi.** Op een machine waar de Wi-Fi Kit een netwerk heeft verbonden: één `https`-pagina in de nieuwe schil. De browser linkt `libr5wifi.so` niet. Opschrijven welk apparaat en dat het niet QEMU usernet was. Zonder die machine blijft de stap open en gaat stap 16 niet naar een 5625U-claim over Wi-Fi.

16. **Image omzetten.** Vijf pagina’s opnieuw, nu op de nieuwe schil, waarvan minstens de aanmeldpagina. Daarna pas: URL-types `http`, `https`, `file` en `ftp` naar de nieuwe signatuur (`WebPositive.rdef:25-28`: `application/x-vnd.Be.URL.file`, `.ftp`, `.http`, `.https`). `webpositive.hpkg` uit `OptionalPackages`, `apply-webpositive.py` uit `apply-overlay.sh`. De welkomstpagina moet de nieuwe app openen. `CHANGELOG.md` en, als de ticketstatus wijzigt, `python3 scripts/tracker-report.py` met `tracker/r5-status.tsv` en `tracker/STATUS.md`. #67 dicht als de nieuwe app de paden heeft en het oude pakket weg is. Blijft het oude pakket ernaast, dan blijft #67 open.

17. **WebKit2.** Pas na stap 16. Softwarecompositor, bitmaps die app_server toont, content-proces. GL alleen via Zink of llvmpipe. Lavapipe onaangeroerd. Geen ring- of resetwijziging. Eerste bewijs is dezelfde vijf pagina’s, plus een notitie of het UI-proces blijft hangen. Een crash zoals het forum die in mei 2026 beschreef, stopt deze stap. De schil uit stap 5–14 blijft het product. MiniBrowser wordt niet de GUI.

Stap 16 is het moment waarop roadmap-punt 4 een huidige WebKit in de image heeft, op de kits. Snelheid, WebGL, sandbox en de 5625U blijven dan nog open als ze niet in die boot zaten.

## Onbewezen

- Of een bepaald imagebestand op schijf dezelfde pin `haikuwebkit-1.10.0-2` heeft. De repositorylijst en `generated/boot-5625u-10/syslog:2044` zeggen dat wel voor de boom en voor die ene eerste start.
- Of 1.10.0-2 een aanmeldpagina tekent.
- De vijfde pagina, tot Nick die noemt.
- De tekst van `WebView.h` en `WebDownload.h`: basis van de view, en de waarden van de downloadberichten. De aanroepen zelf staan in de app.
- Of `set_window_glass` de pagina met rust laat.
- Wi-Fi: geen boot met de kit en een pagina.
- Stick 57 en stick 51: de lettercrash en de volle VRAM-tabel zijn niet opnieuw geboot.
- WebKit2 op R5. `--no-webkit2` en `libJavaScriptCore` zijn in deze boom niet nagelezen.
- Paginatijd, HTTP/2 in de curl van het pakket, WebGL.
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

Niet gelezen, en dus niet als header-feit gebruikt: `WebView.h`, `WebPage.h`, `WebDownload.h`, `WebWindow.h`, `WebViewConstants.h`. Het pad van het devel-pakket op de WSL-boom van Nick stond niet op deze machine.

Buiten de boom, uit het onderzoek van 19 november 2026, niet opnieuw geopend: de HaikuPorts-recipe inhoudelijk (`--no-webkit2`, `libJavaScriptCore`, WebKit 625.1.25), het forum over MiniBrowser, webkit.org over LGPL. `docs/WEBKIT.md` op `origin/cursor/webkit-plan-f12d` is het vorige plan, met SPDX en zonder het store-pad. Deze herziening vervangt het plan in de store. De pull request is niet gewijzigd.
