<!-- SPDX-License-Identifier: MIT -->
<!-- SPDX-FileCopyrightText: 2026 R5OS Contributors -->

# WebKit op R5

Plan, 19 november 2026. Alleen lezen. Geen poort, geen bouw, geen commit. De motorkeuze is gemaakt op 19 november 2026: de Haiku-WebKit-poort, een schil op `BView`. Dit plan is de volgorde daarna.

Nick heeft de schil gekozen. De systeembrowser krijgt een nieuwe GUI, gebaseerd op WebPositive, op de Interface Kit en op R5 UI. De oude `BrowserWindow` is niet het product. Hij blijft alleen de sonde in de eerste twee stappen, tot de nieuwe schil dezelfde pagina’s opent.

Officieel eerst AMD64. `origin/main` is de bron van waarheid.

## Keuzes waar het open lag

- Het programma komt nieuw onder `distro/apps/browser`. Werknaam in het venster: Browser. Signatuur, als het pakket die krijgt: `application/x-vnd.R5-Browser`. De zichtbare naam kan Nick nog wijzigen; de stappen hangen er niet aan. WebPositive blijft de naam van het Haiku-pakket tot dat pakket uit de image gaat.
- Bladwijzers blijven de bestanden die er al zijn: `B_USER_SETTINGS_DIRECTORY` / `WebPositive` / `Bookmarks`, type `application/x-vnd.Be-bookmark`, attributen `META:url` en `META:title`, submap `Bookmark bar`. Dat pad komt uit `BrowserWindow::_BookmarkPath`. Een tweede map zou bestaande bladwijzers laten liggen. De mapnaam `WebPositive` blijft, ook als het programma anders heet. Een nieuwe map maakt de code met modus `0755`. Een map die er al is, blijft zoals die is.
- Pagina’s lopen via de client die haikuwebkit al linkt. De HaikuPorts-recipe `haikuwebkit-1.10.0` noemt `libcurl`. De browser praat niet met `WiFiController` en linkt `libr5wifi.so` niet om te laden. `BHttpRequest` in `distro/network/src/kits/network/libnetservices/HttpRequest.cpp` zet `fHttpVersion(B_HTTP_11)` en is de client van Haiku-programma’s. WebPositive gebruikt die niet. Overstappen daarop zou HTTP/2 en de poort weggooien. De Wi-Fi Kit zet de interface op via `net_server`. De browser gebruikt die interface zoals elk ander programma: sockets, route, DNS.
- De download-haak van `BWebView` staat niet in deze git-boom. De nieuwe downloadlijst wacht op de header in de jam-boom. Ontbreekt de haak, dan stopt die stap. Er komt geen eigen downloader naast de poort.
- Glas: `BAppearanceKit::set_window_glass()` op het venster, zoals andere R5-vensters. De pagina zelf blijft een dekkende `BWebView`. Blijkt het glas de pagina te matten, dan gaat die aanroep in diezelfde stap weer uit. Dat is onbewezen tot een boot.
- Stop en herladen krijgen geen nieuw nummer in `r5_icon`. De enum in `distro/icons/R5Icons.h` eindigt bij `R5_ICON_LOCKED`. Terug, vooruit en home gebruiken `r5_icon_bitmap()`. Stop en herladen zijn tekstknoppen tot iemand een bestaand vectorplaatje aanwijst.
- De vijf sonde-pagina’s, tot Nick een andere lijst geeft: de welkomstpagina of de R5Book via `file://`; `https://example.com`; een lange tekstpagina (Wikipedia); `https://github.com/login` zonder account, alleen of het formulier verschijnt; en een vijfde adres dat Nick noemt als een pagina die hij nu stuk ziet. Zonder dat vijfde adres is de sonde niet klaar voor zijn gevallen. Geen wachtwoorden in het verslag.
- WebKit2 wacht tot de nieuwe schil die vijf pagina’s op Legacy opent. De softwarecompositor en een `BView` komen eerst. OpenGL alleen als Zink op RADV of als llvmpipe. Lavapipe blijft voor Vulkan en krijgt geen browserwerk.

## Wat er al is

WebPositive in de image is het pakket `webpositive.hpkg`. `distro/rest/build/jam/OptionalPackages` zet het erin als het optionele pakket WebPositive aan staat en de bouwfeature `webpositive` aan staat. Het pakket eist `haikuwebkit`. De engine staat niet in git. Deze checkout heeft geen `haiku/`-boom. Welke revisie de image vandaag meeneemt, staat nergens in git.

Het enige WebPositive-bestand in de boom is `distro/apps/webpositive/src/apps/webpositive/BrowserWindow.cpp`. `scripts/apply-webpositive.py` kopieert dat over `src/apps/webpositive/BrowserWindow.cpp` in de Haiku-boom. `scripts/apply-overlay.sh` roept dat script aan. De header is de BSD-tekst met de Apple-disclaimer en copyrights 2007–2015, onder meer Ryan Leavengood en Haiku, Inc. `CHANGELOG.md` meldt dat `jam -q WebPositive` op 3 november 2026 linkte (`BrowserWindow.o`, daarna `WebPositive`). Dat bewijst de schil op die dag, niet de engineversie, en er ging geen achtergrondtab open.

`BWebView` bestaat voor dit bestand. De header `WebView.h` zit niet in git; die komt met haikuwebkit. Wat `BrowserWindow.cpp` aanroept: `BWebView` in een `BWebWindow`, `LoadURL`, `MainFrameURL`, `MainFrameTitle`, `Reload`, `IncreaseZoomFactor`, `DecreaseZoomFactor`, `Shutdown`, `WebPage()`, `GetContentsAsMHTML`, `SetUserData`, `GetUserData`, `SetAutoHidePointer`. De schil krijgt terug: `NavigationRequested`, `NewWindowRequested`, `NewPageCreated`, `CloseWindowRequested`, `LoadNegotiating`, `LoadCommitted`, `LoadProgress`, `LoadFailed`, `LoadFinished`, `MainDocumentError`, `TitleChanged`, `IconReceived`, `ResizeRequested`, `AuthenticationChallenge`, `UpdateGlobalHistory`, `NavigationCapabilitiesChanged`, `StatusChanged`. Dat is de Legacy-poort: één proces. WebKit2 is upstream MiniBrowser, niet dit venster. De recipe `haikuwebkit-1.10.0` zet WebKit2 uit (`--no-webkit2`) en levert `libWebKitLegacy` en `libJavaScriptCore`.

De GUI die dat bestand bouwt, van boven naar onder:

- `BMenuBar`: Window, Edit, View, History, Bookmarks.
- Tabs via `TabManager` (die klasse zit niet in deze boom).
- Een navigatierij: terug, vooruit, stop, home (`BIconButton`, plaatjes 201, 202, 204, 206 uit de Haiku-resources), dan `URLInputGroup`.
- Een bladwijzerbalk (`BookmarkBar`) als de submap bestaat.
- De `BWebView`.
- Een zoekbalk (`BTextControl`, vorige, volgende, hoofdlettergevoelig).
- Een statusregel (`BStringView`) en een `BStatusBar`.

Downloads, instellingen, cookies en de scriptconsole zijn menu-items. `SHOW_DOWNLOAD_WINDOW`, `SHOW_SETTINGS_WINDOW`, `SHOW_CONSOLE_WINDOW` en `SHOW_COOKIE_WINDOW` gaan naar `be_app`. Die vensters staan niet in deze boom.

De URL-balk, zoals die in dit bestand staat na de R5-wijziging:

- Return in het veld haalt de Go-knop in (`DispatchMessage`). Een veld dat alleen de focus verliest, laadt niet.
- Escape zet de URL van de huidige pagina terug.
- Een lege Go markeert het veld ongeldig.
- `LoadNegotiating`: een tab die niet geselecteerd is, schrijft niet in de balk. De URL blijft op die tab (`PageUserData`). Dat is Haiku #20319, opgeschreven in `CHANGELOG.md` op 3 november 2026.
- `_SmartURLHandler`: `http`, `https`, `file`, `about`, `data` laadt het zelf. Een ander schema gaat naar `application/x-vnd.Be.URL.<schema>` via `BRoster`. Zonder schema wordt een domein `http://`, en de rest een zoekopdracht (`_VisitSearchEngine`).

Bladwijzers zijn gewone bestanden. `_CreateBookmark` schrijft `META:url` en `META:title` en het type `application/x-vnd.Be-bookmark`. Beheren opent de map in Tracker (`BRoster::Launch`). Slepen naar Tracker vult hetzelfde bestand (`B_COPY_TARGET`).

`docs/MULTIUSER.md` zegt dat WebPositive het image-pakket blijft en dat standaardpaden in dat pakket nog vast staan. `distro/TODO.md` wijst daarvoor naar GitHub #67. Een herbouw van `webpositive.hpkg` stond uitdrukkelijk buiten de multi-user-pas.

`docs/ROADMAP.md` punt 4: een huidige WebKit of een Qt-browser. WebPositive is de zwakke dagelijkse toepassing. Dit plan vult dat punt met WebKit en de kits. Qt valt af, zoals het onderzoek al koos.

`docs/VPN-KIT.md`: WebPositive doet veel moderne aanmeldpagina’s niet. Browser-aanmelden voor VPN (`r5vpn:`) wacht tot er een huidige browser in de image staat. Dat hoort niet bij de eerste stappen hier.

Netwerk eromheen, wel in de boom. De Wi-Fi Kit (`docs/LIBR5.md`, “Wi-Fi Kit”; `distro/wifi/lib/R5WiFi.h`) laat `WiFiController` het werk met de adapter en `net_server` op een eigen thread doen. `WiFiController.cpp` loopt langs `BNetworkRoster` en de interfaces. Een geslaagde verbinding is een interface die opstaat, met adres en route. De kit is geen HTTP-stack.

GPU, uit `docs/GPU-ARCHITECTURE.md` en `docs/ROADMAP.md`. De softwarepaden blijven: Painter, llvmpipe, Lavapipe. OpenGL is Zink op RADV als de GPU zijn checks haalt, anders llvmpipe. Vulkan is RADV of Lavapipe. Skia-tekenen is volgens dat GPU-document de standaard sinds 19 november 2026 (M13 stap 5): RADV als de GPU slaagt, anders Lavapipe; `gpu_draw false` zet het uit. De browser tekent niet zelf op de GPU. Een `BWebView` gaat door app_server. `CHANGELOG.md`: stick 57, app_server-crash in FreeType op de teken-thread van WebPositive; stick 51, GPU-tekenen ging uit terwijl WebPositive openging omdat de VRAM-paginatabel vol zat. Beide zijn app_server. Een nieuwe schil haalt ze niet weg.

Licentie, uit het onderzoek en `docs/LICENSING.md`. WebKit blijft een eigen bibliotheek, dynamisch gelinkt. Per bestand vooral `LGPL-2.1-or-later`, plus BSD- en MIT-delen. Copyright zoals de recipe: Apple en de andere namen daar. De nieuwe schil is MIT, met een SPDX-header, zoals alle nieuwe R5-code. Geen WebKit-bron in een MIT-bestand. Geen plakken in `libbe`. Een apart programma trekt de MIT-kernel niet de LGPL in, zolang de bibliotheek dynamisch blijft en de bron beschikbaar is. De recipe wijst naar Codeberg. De GPL-3-artwork in de image is een ander werk. `docs/LICENSING.md` zet LGPL-2.1 apart: de linkregels van die licentie gelden. Het voorbeeld in die tekst voor een LGPL-bibliotheek in de boom is libsmb2, statisch in smbfs. WebKit wordt niet zo behandeld. Die blijft een pakket.

## Wat ontbreekt

- Het versienummer van `haikuwebkit` in de image. Grootste gat. Stap 1 leest het. Die stap is niet gestart.
- Een sitelijst met een boot. Geen bewijs dat 1.10.0 of de image een aanmeldpagina haalt. `docs/VPN-KIT.md` zegt dat veel moderne aanmeldpagina’s falen, zonder adressen.
- De nieuwe schil. Er is één overlappend bestand, geen programma onder `distro/apps/browser`.
- De headers en de overige WebPositive-bron (`URLInputGroup`, `TabManager`, `BookmarkBar`, `DownloadWindow`, `AuthenticationPanel`, `SettingsMessage`). Zonder de jam-boom is het download- en cookiepad onbewezen.
- HTTP-auth, cookies en downloads als R5-vensters.
- Paden per aangemelde persoon. #67 staat open. Het oude pakket heeft de standaardpaden nog vast.
- Een proef dat een pagina de interface gebruikt die de Wi-Fi Kit heeft opgezet. QEMU usernet is geen Wi-Fi en geen LAN (`docs/ROADMAP.md`, `AGENTS.md`).
- WebKit2 op een `BView` met een softwarecompositor. Het forum van mei 2026, in het onderzoek, noemt MiniBrowser instabiel. Geen R5-boot.
- WebGL. In de Haiku-WebKit2-demo stond het uit. `libgl` kan Zink of llvmpipe zijn. Dat is later.
- Snelheid. Geen paginatijd in `CHANGELOG.md`.
- Multiprocess en een sandbox. Legacy heeft geen van beide. Haiku heeft teams en ports. seccomp staat niet in `distro/`. Dat gat blijft tot een latere stap. Het houdt de schil niet tegen.
- VPN-browserlogin en een captive portal. Roadmap-punt 5 noemt de portal apart. Allebei buiten dit plan tot de vijf pagina’s op de nieuwe schil slagen.

## De nieuwe GUI

De indeling volgt WebPositive, omdat die al een Haiku-venster is en omdat de R5-fix in de URL-balk daar zit. De klassen zelf gaan niet mee als product. Nieuwe code staat in `distro/apps/browser` en is MIT. Een functie die letterlijk uit `BrowserWindow.cpp` wordt gekopieerd, houdt de BSD-header van dat bestand. Liever het gedrag opnieuw schrijven, kort, zodat het programma MIT blijft. WebKit-bron wordt nergens gekopieerd.

### Wat mee gaat

De indeling. Menubalk, tabs, navigatierij (terug, vooruit, stop, home, adres), bladwijzerbalk als de submap er is, de pagina, zoekbalk, statusregel met voortgang. Het venster is een `BWindow` met een `BGroupLayout`. De pagina is een `BWebView`. Tabs zijn pas aan de beurt als één pagina laadt. Tot die tijd is één view genoeg. Een tab-widget die alleen de browser gebruikt, blijft in het programma. Hij gaat niet naar `libr5ui` en niet naar `libbe`. `BTabView` is de eerste kandidaat. Past die niet bij sluiten en bij een view per tab, dan een rij `BButton`s in een `BGroupView`. Dat is nog steeds de Interface Kit.

De URL-balk als gedrag, niet als klasse `URLInputGroup`. Een `BTextControl` en een Go-`BButton`. Return laadt. Focus verliezen laadt niet. Escape herstelt `MainFrameURL` van de geselecteerde tab. Leeg bij Go toont een korte fout (`BStringView`), omdat `BTextControl` geen `MarkAsInvalid` heeft. Een achtergrondtab laat de balk met rust en bewaart de URL op die tab, zoals `LoadNegotiating` dat nu doet. `_SmartURLHandler` komt terug als gedrag: bekende schema’s zelf, andere schema’s via `BRoster` en `application/x-vnd.Be.URL.*`, een domein als `http://`, anders zoeken. De zoek-URL is een instelling van de persoon, niet een vast adres in de bron.

De bladwijzers als bestanden en als idee. Zelfde map, zelfde type, zelfde attributen, submap `Bookmark bar`. Het menu is een `BMenu` dat de map leest. Geen afgeleide van `BNavMenu`: die klasse hoort bij Tracker, en Tracker-privéstukken blijven dicht (`AGENTS.md`). “Bookmark this page” schrijft het bestand. “Manage bookmarks” opent de map met `BRoster::Launch`, zoals `_ShowBookmarks`. Slepen naar Tracker kan mee zodra één bladwijzer schrijft. De 8-bit-icoonvergroting in `_CreateBookmark` gaat niet mee. Een icoon op het bestand is `r5_file_icon_bitmap` of het icoon dat `BWebView` al gaf, zonder een tweede tekenpad.

De downloads als lijst, niet als het ontbrekende Haiku-venster. Een `BWindow` met een `BListView` en een `BStatusBar`. Bestanden landen in een map van de aangemelde persoon via `find_directory`. Welke constante (`B_USER_DIRECTORY` plus een submap, of een downloadmap als de header die heeft) wordt in de jam-boom gelezen op het moment van die stap. De header staat hier niet. Het menu-item en de sneltoets blijven het idee van het Window-menu. De haak is de `BWebView`-aanroep die de header noemt. De browser haalt de bytes niet zelf met een tweede HTTP-client.

Verder het gedrag dat de schil al aan de view koppelt: titel en favicon op de tab, voortgang, statusregel, terug en vooruit aan of uit via `NavigationCapabilitiesChanged`, zoomen, pagina opslaan als MHTML via `GetContentsAsMHTML` en een `BFilePanel`, bron bekijken pas als de header dat aanbiedt. Geschiedenis is een `BMenu` met de bezochte URL’s. `BrowsingHistory` zit niet in deze boom. Het menu wordt nieuw. De opgeslagen geschiedenis van het oude pakket gaat mee als het bestand per gebruiker blijkt te liggen en het formaat in de header staat. Tot die lezing is overnemen van het oude geschiedenisbestand onbewezen.

### Wat vervangen wordt

- `BrowserWindow` als product, inclusief de basis `BWebWindow` zodra een gewone `BWindow` de view kan houden. Kan `BWebView` alleen in een `BWebWindow`, dan is die basis een dunne klasse in het nieuwe programma en blijft de rest nieuw. Dat blijkt uit de header, niet uit giswerk.
- `URLInputGroup`, `BIconButton` met resource 201–206, `BitmapButton`, `BookmarkBar`, `BookmarkMenu`.
- `TabManager`, cookievenster, scriptconsole en het instellingenvenster van WebPositive. Cookies en de console komen later, met `BListView` of `BColumnListView` als die publiek in de kits zit. Ze horen niet bij de eerste schil die vijf pagina’s moet openen.
- Het aanmeldvenster `AuthenticationPanel`. Wegens HTTP-auth: een klein venster volgens `docs/STYLE.md`. Paneelkleur, label rechts, veld links, één regel hoog. Het wachtwoord is `BAppearanceKit::BPasswordControl` (`docs/APPEARANCE-KIT.md`, `docs/LIBR5.md`). Geen `BCard` achter een formulier. Glas met `set_window_glass`, zoals `WiFiConnectWindow`. Onthouden schrijft naar de instellingen van die persoon. Niets in de log, niets in argv, niets in What’s new.
- Het instellingenvenster van de browser. Zelfde formulierregels: startpagina, zoek-URL, home-knop aan of uit, zoom alleen tekst, bladwijzerbalk aan of uit. Geen nieuw item in de Settings-lijst. Die lijst en de vijf beschrijvingen blijven zoals `AGENTS.md` ze vastlegt. Dit venster is het antwoord op #67 voor het nieuwe programma. `webpositive.hpkg` wordt niet herbouwd om die paden te repareren.
- `scripts/apply-webpositive.py` en de aanroep in `scripts/apply-overlay.sh`, in de stap waarin de image omgaat. Tot die stap blijft de overlay, omdat de sonde de oude schil nog gebruikt.
- Harde `/boot/home`-paden. Alles via `find_directory` / `getpwuid`. Een standaardaccount schrijft onder de eigen home (`docs/MULTIUSER.md`).

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
- `libWebKitLegacy` en `libJavaScriptCore`: eigen pakket, dynamisch, LGPL zoals de bron per bestand is, plus de BSD- en MIT-delen. Niet in de R5-bron, niet in `libbe`, niet in `libr5ui`.
- `BrowserWindow.cpp` blijft het Haiku-bestand met de BSD-header zolang de overlay bestaat. Geen MIT-header eroverheen.
- Bij de stap die de image omzet: `THIRD_PARTY_NOTICES.md` noemt het pakket, de licentie, de copyrightregel uit de recipe en waar de bron staat. De tekst van de LGPL blijft in `LICENSES/` als die er al staat, en wordt niet herschreven.
- Geen combinatie met GPL-2.0-only of met de GPL-3-artwork.

## Stappen

Elke stap is één landing. De volgende begint pas als de vorige opgeschreven is, inclusief wat er niet geboot is. Stap 1 en 2 starten niet vanuit dit plan.

1. **Versie van de image.** Op een al gebouwde AMD64-image de pakketinfo van `webpositive.hpkg` en van `haikuwebkit` uitlezen (`package list` / `package info`). Opschrijven: pakketversie, de eis op haikuwebkit, en of dat 1.10.0 is. Geen code, geen jam. Onbewezen tot die lezing: alles over “de image draait 625.1.25”.

2. **Vijf pagina’s op de oude schil, als sonde.** Zelfde image, QEMU mag. De lijst hierboven, inclusief het vijfde adres van Nick en minstens het aanmeldformulier. Per pagina: opent, formulier zichtbaar, scriptfout, crash, of de balk de URL toont. Geen claim over de 5625U. De oude schil wordt hier niet verbeterd, behalve als de sonde niet eens start. Dit is niet het product.

3. **Revisie, alleen als stap 1 niet 1.10.0 ziet.** De recipe-revisie uit het onderzoek in de image brengen en stap 2 opnieuw. Wijkt de pin af van wat HaikuPorts bouwt, dan hoort het versienummer in `CHANGELOG.md` en in `THIRD_PARTY_NOTICES.md`. Geen WebKit2 in deze stap.

4. **Contract van `BWebView`.** In de jam-boom de headers lezen en de lijst aanroepen en callbacks vastleggen die stappen 5–13 gebruiken, inclusief of een download-callback bestaat en of `BWebView` een `BWebWindow` eist. Geen code in de browser. Ontbreekt een aanroep die dit plan noemt, dan wijzigt het plan op dat punt voordat de schil die aanroep verzint.

5. **Leeg programma.** `distro/apps/browser`: `BApplication`, één `BWindow`, één `BWebView`, één `file://`-pagina. MIT. Linken tegen de libraries die het bestaande WebPositive-pakket linkt. Nog niet in `OptionalPackages`. De overlay blijft. Klaar als het venster die ene lokale pagina toont. QEMU is genoeg. De 5625U niet.

6. **URL-balk.** `BTextControl`, Go, Return, Escape, lege invoer, slim adres, achtergrondgedrag ook al is er nog één tab (de tak voor “niet de huidige view” staat er meteen, zodat tabs hem niet later breken). Klaar als example.com of de lokale pagina via de balk laadt en Escape de vorige URL terugzet.

7. **Menu’s en navigatie.** `BMenuBar` met Window, Edit, View, History. Terug, vooruit, home via `r5_icon_bitmap`. Stop en herladen als tekst. Knippen, kopiëren en plakken via de editing-capabilities die `BrowserWindow::MessageReceived` al afhandelt (`B_EDITING_CAPABILITIES_RESULT`). Klaar als terug na een tweede pagina de eerste toont.

8. **Bladwijzers.** Schrijven en het menu vullen vanuit de bestaande map. Beheren opent Tracker. Klaar als een bladwijzer die WebPositive al schreef in het menu staat en een nieuwe in diezelfde map belandt, onder de home van de aangemelde persoon.

9. **Bladwijzerbalk.** Rij knoppen uit de submap `Bookmark bar`. View-menu zet hem aan en uit. Klaar als één bestand in die submap een knop is die laadt.

10. **Downloads.** Alleen met de haak uit stap 4. Lijst, voortgang, doelmap via `find_directory`. Klaar als één bestand van `https://example.com` (of een ander klein bestand op die sonde) op schijf staat en in de lijst zichtbaar is. Zonder haak: stap overslaan en dat in `CHANGELOG.md` zetten. Geen tweede client.

11. **HTTP-auth.** Venster met `BPasswordControl` en `docs/STYLE.md`. `AuthenticationChallenge` zoals de oude schil: eerst opgeslagen gegevens, anders het venster. Wachtwoord niet loggen. Klaar als een lokale testpagina om een naam vraagt en het venster verschijnt. Een echte site telt pas in stap 2-herhaling op de nieuwe schil.

12. **Zoeken, zoom, status.** Zoekbalk onder de pagina, `BStatusBar`, zoom in het View-menu, `IncreaseZoomFactor` / `DecreaseZoomFactor`. Klaar op de lange tekstpagina uit de sonde.

13. **Tabs.** Tweede `BWebView`, sluiten, sneltoetsen zoals nu (commando-T, commando-W, commando-1 tot 9, commando-Tab). Een tab op de achtergrond laat de URL-balk met rust. Klaar als een middenklik of “new tab” de voorgrond niet vervangt. Of de view middenklik zelf meldt, komt uit stap 4. Zo niet, dan alleen het menu.

14. **Instellingen.** Formuliervenster, `find_directory`, startpagina, zoek-URL, home-knop, zoom alleen tekst, bladwijzerbalk. Geen Settings-item. Klaar als twee accounts op één image verschillende startpagina’s houden. Virtuele machine is genoeg. Geen fast user switching: uitloggen, andere persoon, opnieuw (`docs/MULTIUSER.md`).

15. **Wi-Fi.** Op een machine waar de Wi-Fi Kit een netwerk heeft verbonden: één `https`-pagina in de nieuwe schil. De browser linkt `libr5wifi.so` niet. Opschrijven welk apparaat en dat het niet QEMU usernet was. Zonder die machine blijft de stap open en gaat stap 16 niet naar een 5625U-claim over Wi-Fi.

16. **Image omzetten.** Vijf pagina’s opnieuw, nu op de nieuwe schil, waarvan minstens de aanmeldpagina. Daarna pas: URL-types `http`, `https`, `file` en `ftp` naar de nieuwe signatuur, `webpositive.hpkg` uit `OptionalPackages`, `apply-webpositive.py` uit `apply-overlay.sh`. De welkomstpagina moet de nieuwe app openen. `CHANGELOG.md` en, als de ticketstatus wijzigt, `python3 scripts/tracker-report.py` met `tracker/r5-status.tsv` en `tracker/STATUS.md`. #67 dicht als de nieuwe app de paden heeft en het oude pakket weg is. Blijft het oude pakket ernaast, dan blijft #67 open.

17. **WebKit2.** Pas na stap 16. Softwarecompositor, bitmaps die app_server toont, content-proces. GL alleen via Zink of llvmpipe. Lavapipe onaangeroerd. Geen ring- of resetwijziging. Eerste bewijs is dezelfde vijf pagina’s, plus een notitie of het UI-proces blijft hangen. Een crash zoals het forum die in mei 2026 beschreef, stopt deze stap. De schil uit stap 5–14 blijft het product. MiniBrowser wordt niet de GUI.

Stap 16 is het moment waarop roadmap-punt 4 een huidige WebKit in de image heeft, op de kits. Snelheid, WebGL, sandbox en de 5625U blijven dan nog open als ze niet in die boot zaten.

## Onbewezen

- Welke `haikuwebkit` de image nu heeft.
- Of 1.10.0 of de image een aanmeldpagina tekent.
- De vijfde pagina, tot Nick die noemt.
- De download-callback en de eis `BWebWindow`.
- Of bestaande geschiedenisbestanden meegaan.
- Of `set_window_glass` de pagina met rust laat.
- Wi-Fi: geen boot met de kit en een pagina.
- Stick 57 en stick 51: de lettercrash en de volle VRAM-tabel zijn niet opnieuw geboot.
- WebKit2 op R5.
- Paginatijd, HTTP/2 in de curl van de image, WebGL.
- Dat een standaardaccount de bladwijzermap niet deelt met de administrator. Uid 0 leest homes nog (`docs/MULTIUSER.md`). De nieuwe code vermijdt vaste paden. Dat is geen bewijs dat de ene persoon de cookies van de ander niet kan lezen zolang uid 0 dat kan.

## Bronnen die ik gelezen heb

In de boom:

- `AGENTS.md`
- `docs/ROADMAP.md` (punt 4 en de GPU-alinea)
- `docs/MULTIUSER.md` (bundled applications, WebPositive, #67 in de “not this pass”-regel)
- `docs/GPU-ARCHITECTURE.md` (softwarepad, Zink, RADV, Lavapipe, Skia als standaard op 19 november 2026)
- `docs/LIBR5.md` (Wi-Fi Kit, `set_window_glass`, `BPasswordControl`, geen verplaatsing naar `libbe`)
- `docs/APPEARANCE-KIT.md` (welke klasse waarvoor, formulier versus `BCard`)
- `docs/STYLE.md` (formulieren)
- `docs/LICENSING.md` (dynamische bibliotheken, LGPL-2.1)
- `docs/VPN-KIT.md` (browserlogin, WebPositive en aanmeldpagina’s)
- `docs/BUGFIX-RELEASE.md` alleen de regel dat WebPositive en de Web Kit laatst komen, via zoeken
- `distro/TODO.md` (GitHub #67)
- `distro/rest/build/jam/OptionalPackages` (het blok WebPositive)
- `distro/apps/webpositive/src/apps/webpositive/BrowserWindow.cpp` (menu’s, URL-balk, `LoadNegotiating`, bladwijzers, auth-aanroep, downloads als bericht naar `be_app`)
- `scripts/apply-webpositive.py` en de aanroep in `scripts/apply-overlay.sh`
- `distro/wifi/lib/R5WiFi.h` en het begin van `distro/wifi/lib/WiFiController.cpp` (`BNetworkRoster`)
- `distro/network/src/kits/network/libnetservices/HttpRequest.cpp` regel met `fHttpVersion(B_HTTP_11)`
- `distro/icons/R5Icons.h` (de zes iconen)
- `CHANGELOG.md`: 3 november 2026 (`jam -q WebPositive`, achtergrondtab), stick 57, stick 51, de welkomstpagina, de multi-user-zin dat WebPositive het hpkg blijft

Buiten de boom, overgenomen uit het onderzoek waarbij de motorkeuze op 19 november 2026 is gemaakt (niet opnieuw geopend deze sessie): HaikuPorts-recipe `haikuwebkit-1.10.0`, Codeberg HaikuWebKit 1.10.0 en WebKit 625.1.25, het forum over MiniBrowser, webkit.org over LGPL. Die feiten blijven aan dat onderzoek hangen. Deze sessie heeft die pagina’s niet opnieuw geladen.
