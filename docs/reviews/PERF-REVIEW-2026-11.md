<!--
  SPDX-FileCopyrightText: 2026 R5OS Contributors
  SPDX-License-Identifier: MIT
-->

# Prestatie- en bugreview van R5 Operating System (21 nov 2026)

Alleen gelezen. Bron is `origin/main` op `ec81d57163` (20 nov 2026, 14:37 Europe/Amsterdam, merge van #4481, M13 dicht). Logs: `generated/boot-5625u-60` tot en met `68` op de referentie-laptop (HP ENVY x360 15, Ryzen 5 5625U). Stick 69 is er niet. Geen jam, geen `apply-overlay.sh`, geen QEMU, geen stick, geen wijziging aan code of aan de gedeelde WSL-boom. Die boom is alleen gelezen voor de compilerregels in `build/jam/ArchitectureRules` en `BuildSetup`.

Regels die hier gelden, uit `AGENTS.md`: de softwareweg (Painter, llvmpipe, Lavapipe) blijft; de GFX9-ring, PSP, paginatabellen, reset en stop bij afsluiten veranderen niet zonder een gevraagde stick-proef; de Settings-indeling blijft; `libbe` en de Haiku-ABI blijven; Mesa en Skia houden hun eigen licentie; een nieuwe R5-helper hoort in een `libr5*`-bibliotheek en blijft MIT. M13 is dicht (`docs/M13-PLAN.md`, 20 nov 2026). De Lavapipe-opwarming is het open punt, geen reden om M13 opnieuw open te trekken. Skia is de standaard, ook op Lavapipe, en Painter alleen in veilige modus (eigenaar, 19 nov 2026). Op Lavapipe is Skia nog trager dan Painter. Die keuze blijft staan.

De stick-sessies draaien met `gpu_draw`, `gpu_debug` en `gpu_selftest` aan. Dat is de proefstick, niet de stille standaard. Cijfers uit die sessies tellen de debug-log mee.

## 1. Samenvatting

De desktop op de 5625U met de GPU aan is na de eerste twee seconden al vlot. Op stick 68 duurt de Skia-opwarming 2032 ms, de volgende start van dezelfde stick 72 ms, en het samenstellen van het scherm zit daarna rond een halve tot anderhalve milliseconde. Daar hoeft de ring niet meer open.

Het trage gevoel zit op drie andere plekken.

De softwareweg is de grote wachttijd. Met `gpu false` op stick 65 (20 nov 2026, 11:20) warmt Lavapipe in 27943 ms op. Het langste stuk is 16383 ms. In QEMU staat in het changelog een run van 42246 ms. Tijdens die minuut is de tekenthread verzadigd: 12 seconden CPU per 10 seconden. Het rustige samenstellen is dan al goedkoop (onder de 2 ms). De pijn is het compileren van shaders, niet een trage compose in rust. Slepen is in deze logs niet apart gemeten.

Afsluiten is niet afgemaakt in het spoor. Op sticks 64, 65, 67 en 68 staat bij de volgende start alleen `r5 previous shutdown: enter reboot=1`. `sync_done` komt nergens voor. `sync()` zit meteen na die eerste regel. Een herstart die de schijf niet haalt, of een herstart die in `sync()` blijft hangen, ziet er hetzelfde uit. Bluetooth kan daar los van tot 30 seconden bij de afsluiter houden als SDP op een stille client in `recv` zit. De debug-log van app_server op stick 68 is 12,4 MB en wordt ongebufferd achteraan het bestand geplakt, op de USB-stick die daarna gesynchroniseerd moet worden.

Elke verse stick mist het activatiebestand van de pakketten en doet first-boot van 137 pakketten (stick 68: van 7,55 s naar 9,08 s kerneltijd). Bij de tweede start van stick 68 ontbreekt dat bestand nog steeds.

WebPositive is het pakket `haikuwebkit-1.10.0-2` (WebKit rond 625). Dat is de dagelijkse traagheid in de browser. Aomi is het plan in `docs/AOMI.md`. Geen kleine patch.

Eerst doen, in deze volgorde:

1. De stderr-log van app_server niet meer ongebufferd en eindeloos laten groeien, en nakijken dat een geslaagde ioctl alleen met `gpu_trace` op stderr komt. Daarna één getimede herstart, met en zonder die log.
2. De Lavapipe-shadercache meten: wat er na een `gpu false`-start in `/boot/system/cache/app_server/mesa` staat, en of een tweede start van dezelfde image onder de 28 seconden duikt. De ring, de PSP en de paginatabellen blijven dicht.
3. Het afsluitspoor moet `sync_done` en `hooks_done` laten zien. Pas daarna Bluetooth inkorten.
4. WebKit en `-march=x86-64-v3` voor de hele boom niet nu. De boom is al `-O2` en `x86-64-v2`. De kernel hoort geen AVX te draaien.

## 2. Ranglijst

Gesorteerd op winst per moeite. Winst is een schatting. Zekerheid staat erbij. Moeite is in AI-sessies, niet in kalenderdagen. Architectuur is AMD64, tenzij de rij iets anders zegt.

| # | Soort | Gebied | Winst | Zeker | Moeite | Risico | Eerst? |
|---|---|---|---|---|---|---|---|
| 1 | bug en prestatie | app_server-log | seconden CPU per minuut debug, en een zware `sync()` op USB | hoog voor de 12 MB, middel voor het aandeel in afsluiten | 1 | laag | ja |
| 2 | prestatie | Lavapipe-opwarming | tot ongeveer 25 s op een softwarestart | hoog dat hij 28 s is, middel dat de cache hem onder de 5 s brengt | 2–3 | middel | ja |
| 3 | bug | afsluiten, `sync()` | onbekend, het spoor stopt | middel | 1–2 | middel | ja |
| 4 | bug | Bluetooth SDP | tot 30 s als er een stille client is | hoog in de code, niet gemeten op de stick | 1 | laag | na 3 |
| 5 | bug en prestatie | pakketactivatie | ongeveer 1,5 s scripts, plus een volle directoryscan, elke verse stick, en de scan ook bij start 2 | hoog | 1–2 | middel | ja, klein |
| 6 | prestatie | Lavapipe MSAA en threads | een deel van de tekentijd na de opwarming; slepen niet gemeten | laag tot middel | 1 | middel, het vergelijk moet blijven kloppen | na 2 |
| 7 | prestatie | browser | de dagelijkse app | hoog dat 1.10 traag is, laag hoeveel een nieuwere engine wint | veel | hoog, licentie en pakket | later, `docs/AOMI.md` |
| 8 | prestatie | vermogen, EPP | accu, niet snelheid; boost werkt al | middel | 1 | middel, boost niet weghalen op netstroom | meten eerst |
| 9 | meting | C-toestanden | onbekend tot er residutijd is | laag | 1 | laag als het alleen een teller is | meten eerst |
| 10 | prestatie | USB-opstart tot BFS | de eerste 5,1 s | hoog dat de stick dat is | n.v.t. | — | niet optimaliseren op de stick |
| 11 | prestatie | LTO of x86-64-v3 voor alles | klein tegenover 2 | middel dat v3 voor de kernel fout is | veel | hoog | niet doen |
| 12 | bug | luidsprekers CS35L41 | geen geluid, geen snelheid | hoog, al beschreven | apart project | licentie GPL-2.0 van het Linux-stuurprogramma | niet in deze ronde |

## 3. In één sessie

- Stderr van app_server: niet `append` zonder plafond, niet `_IONBF` op het submit-pad. Geslaagde ioctls blijven achter `gpu_trace`. Fouten blijven, met een plafond.
- Eén herstart op de 5625U met het afsluitspoor in de volgende syslog: de regels `sync_done`, `hooks_start`, `hooks_done` moeten er staan, met de wandklok erbij. Eén keer met Bluetooth aan, één keer uit. Geen wijziging aan de GFX9-stop.
- Nakijken waarom `activated-packages` bij de tweede start van stick 68 nog ontbreekt.
- Een teller voor C-residutijd, uit, tot de eigenaar hem aan zet. Geen wijziging aan de C-toestand zelf in die sessie.

## 4. Bevindingen

### 4.1 Software tekenen: Lavapipe en Skia

**Wat.** Stick 65, vierde start, `gpu false`, 20 nov 2026 11:20:56 Europe/Amsterdam: `gpu draw warm up 27943 ms`, `warm-up parts 8, longest 16383 ms`. De compose-medianen in die minuut zijn 330–1707 µs. De tekenthread verbruikt 11886 ms, 12611 ms, 5283 ms, 6152 ms en 5719 ms CPU per 10 seconden, en daarna 972 ms. De mediane tekentijd zakt van 204 ms naar ongeveer 0,6 ms. Het changelog noemt op 19 nov 2026 een QEMU-run van `gpu draw warm up 42246 ms`.

Op RADV, stick 68, 21 nov 2026: eerste start 2032 ms, tweede start 72 ms. Stick 65 boot 2 en 3: 65 ms en 64 ms. De schijfcache werkt daar.

**Waarom.** De opwarming tekent twee keer vier tekenmodi, en op Lavapipe met een eigen `Finish()` per stuk, elk met een limiet van 30 s (`distro/app_server/skia/SkiaOffscreen.cpp`, `skia_warm_up`, rond de regels 125–223). Die draad start op `B_LOW_PRIORITY` (`Compositor.cpp` rond 1008). Lavapipe krijgt standaard 2 threads (`Compositor.cpp` 871–873). De logregel op stick 65 is `lavapipe threads 2 (default)`, ook op de RADV-starts, vóór de keuze van het apparaat. MSAA blijft 4 tenzij de instelling anders zegt (`SkiaGpu.cpp` 652–653, `Compositor.cpp` 989). Mesa's shadercache staat in `/boot/system/cache/app_server/mesa` (`Compositor.cpp` 1267–1279). Op een USB-stick is dat een trage schijf. M13 zegt dat Skia's eigen pipeline-cache op Lavapipe alleen de Vulkan-kop is (312 bytes). De tweede start wint dan alleen wat Mesa zelf bewaart. Dat is op RADV gemeten (2 s naar 70 ms). Op Lavapipe is een tweede start van dezelfde image in deze sticks niet gelogd.

Een eerdere QEMU-meting, in de broncommentaar: slepen 18,5 ms per frame met 2 threads, 57 ms met 4, ongeveer 170 ms met één thread per CPU (`Compositor.cpp` 860–863). Die 2 is een bewuste keuze tegen onderlinge verdringing. Hem op de 5625U naar 6 of 12 zetten zonder een nieuwe meting maakt de softwareweg makkelijk trager.

**Winst.** 15–25 seconden op een koude softwarestart als de cache de JIT overslaat. Zekerheid middel: RADV bewijst dat een cache kan, Lavapipe bewijst het in deze logs niet. MSAA 1 wint rastertijd na de opwarming, niet de eerste LLVM-compile. Zekerheid laag tot de vergelijkset het toelaat.

**Voorstel.** Eerst de inhoud en de grootte van de Mesa-cache na een `gpu false`-start, en de opwarming van de tweede start. Daarna pas MSAA of threads, als instelling, niet als nieuwe standaard, en alleen binnen de vergelijkset. Geen fragment-shader voor compose tot een sleepmeting die de compute-weg als de grote post laat zien. De rustige compose in stick 65 is dat niet.

**Regels.** Lavapipe blijft geïnstalleerd. Skia blijft de standaard. De vergelijkset en de look ten opzichte van RADV blijven. Geen GFX9.

### 4.2 De GPU-weg op de 5625U

**Wat.** Stick 66, 67 en 68: `gpu compositor on radv`, scanout 1920×1080, opwarming rond 1,8–2,1 s, daarna onder de 80 ms. Compose-mediaan op stick 68: 402–1453 µs, 50–260 ms CPU per 10 s. De eerste 10 seconden tekenen kosten 2731 ms CPU, daarna honderden milliseconden. Write-combined GTT is aan sinds stick 67. Grote pagina's staan aan: `R5 VM: amd64_large_pages enabled` op 5,799 s, en de framebuffer is `128 x 2MB`.

**Waarom dit geen werk is.** M13 heeft de CPU-teruglees, de tweede buffer en de scanout-kopie van het kritieke pad gehaald. De ringcontracten in `AGENTS.md` en `docs/GRAPHICS.md` blijven. De userland-EOP-pagina is in M13 bewust niet gebouwd. Dat is een kleine ioctl minder op `FENCE_WAIT` met timeout 0, geen 28 seconden.

**Winst van nog een ring- of PTE-wijziging.** Niet voorstellen.

**Regels.** GFX9-ring, PSP, PTE-vlaggen, invalidate, reset en de stopvolgorde bij afsluiten: alleen met toestemming en een stick die scratch `0xDEADBEEF` schrijft.

### 4.3 De 12 MB stderr

**Wat.** `boot-5625u-68/app_server-stderr.log` is 12412066 bytes, 265878 regels. 221596 regels zijn `r5 libdrm: ioctl linux` zonder `errno`. 24966 regels bevatten `errno`. Daarnaast duizenden `DRM_IOCTL_AMDGPU_CS` en honderden `GEM_*`. Stick 62 heeft al 3,5 MB stderr en een `syslog.1` van 4,2 MB. De instellingen op de stick zijn `gpu_debug true`, niet `gpu_trace`.

In de huidige bron print een geslaagde ioctl alleen als `gpu_trace` aan staat (`distro/graphics/libdrm-r5/r5_translate.c` 122–135). Een mislukte ioctl print altijd. `kernel bo domain` print alleen met trace (regel 443). Stick 68 heeft ook die `kernel bo domain`-regels. Dan stond trace aan in het gedraaide beeld, of dat beeld logde geslaagde ioctls nog onder debug. De bewaarde `radeon_hd` in de homemap heeft geen `gpu_trace`.

De omleiding staat in `Compositor.cpp` 842–852: met debug gaat stderr naar `app_server-stderr.log` in modus `a`, en `setvbuf(..., _IONBF, 0)`. Elke regel is een write. Het bestand krimpt niet. Op een USB-stick blijft die schrijfstroom in de cache tot `sync()`.

**Winst.** Op een debug-stick: de ioctl-stroom van het submit-pad af, en minder te synchroniseren bytes bij afsluiten. In de stille standaard (debug uit) is de winst nul. De sticks van de eigenaar draaien met debug aan, dus hij voelt het wel.

**Voorstel.** Geslaagde ioctls blijven achter trace. Het logbestand krijgt een plafond of wordt per sessie vervangen, en een buffer. Fouten blijven zichtbaar, met een maximum per soort. Geen `gpu_debug` uit de stick-proef halen: die proef heeft de tellers nodig.

**Risico.** Laag. **Architectuur.** AMD64 nu; de log zit in app_server en libdrm, dus ook ARM64 en RISC-V zodra die de compositor draaien.

### 4.4 Opstarten

Stick 68, eerste start, kerneltijd. De syslog begint midden in PCI (`<DROP>`), dus de seconden vóór 0,35 s zitten er niet in.

| Tijd | Gebeurtenis |
|---|---|
| 0,47–0,63 s | eerste xHCI omhoog, na een BIOS-wachttijd |
| 1,58–1,73 s | dezelfde xHCI-parameters nog een keer |
| 2,01 s | AHCI |
| 5,11 s | BFS van de USB-stick gemount (`/dev/disk/usb/0/0/0`) |
| 5,12–5,62 s | packagefs, tweemaal geen activatiebestand |
| 5,78–5,80 s | C-toestanden, grote pagina's, vorige afsluiting |
| 7,16 s | `mt7922wifi` reserveert het PCI-apparaat |
| 7,55–9,08 s | first-boot van pakket 0 tot en met 136 |
| 09:51:43 wandklok | app_server leest de instellingen |
| 09:51:44 | Bluetooth: `started, 0 radios`, daarna `hci124` |
| 09:51:45 | mixer naar uitgang |
| 09:51:46 | opwarming 2032 ms klaar |
| 09:51:54 | eerste compose-mediaan |

De vijf seconden tot de root zijn de USB-stick. Een installatie op de NVMe van de laptop is niet gemeten. Die vijf seconden niet wegoptimaliseren op basis van een stick.

De tweede xHCI-start met dezelfde structurele parameters kost ongeveer een seconde. Het kan een tweede controller zijn of een dubbele init. Open vraag, zie sectie 6.

First-boot van 137 pakketten is 1,5 s zodra de daemon loopt. Het activatiebestand ontbreekt: `Failed to open packages activation file ... activated-packages` op 5,12 s en opnieuw bij de tweede start (syslogregel 5827, wandklok 09:59). De scripts lijken maar één keer te lopen (één regel `Starting First Boot Processing`). De directoryscan loopt wel opnieuw. Dat hoort na de eerste start niet meer.

**Voorstel.** Uitzoeken waarom het bestand niet blijft bestaan op de beschrijfbare stick. Als de transactie hem wel schrijft en de tweede start hem niet ziet, is dat een bug in packagefs of in de stick-indeling. Winst ongeveer een seconde plus minder log. Risico middel: een verkeerde activatie laat pakketten weg.

**Regels.** Geen nieuwe package-daemon. Het bestaande pad afmaken.

### 4.5 Afsluiten en Bluetooth

**Wat.** `system_shutdown` schrijft `enter reboot=1` of `enter reboot=0` en roept dan `sync()` aan (`distro/kernel/src/system/kernel/shutdown.cpp` 168–188). Pas daarna komen `hooks_start`, de afsluithaken (daaronder de GFX9-stop) en `hooks_done`. De volgende start van sticks 64 (vier keer), 65 (vier keer), 67 en 68 logt alleen `enter reboot=1`. Geen `sync_done`.

Dat bewijst niet dat `sync()` hangt. Een harde uitval tijdens `sync()` laat hetzelfde bestand achter, omdat de eerste regel het bestand leegmaakt. Het patroon op tien herstarts zonder één `sync_done` is wel de reden om het te meten. Het changelog van 19 nov 2026 beschrijft een herstart die in `sync()` bleef. De volgorde is daarna veranderd. Deze sticks zijn van na die wijziging en tonen het eind nog steeds niet.

**Bluetooth.** `BluetoothServer::QuitRequested` stopt SDP synchroon (`BluetoothServer.cpp` 199–200). `SDPServer::Stop` sluit de luistersocket en `wait_for_thread` (`SDPServer.cpp` 62–79). De client-`recv` heeft een time-out van 30 seconden (`SDPServer.cpp` 172–179). `Stop` sluit de luistersocket, niet de client. Een stille client houdt de luisterdraad in `recv`, en de afsluiter wacht die draad af. Zonder client keert `accept` terug door de `close`. De eigenaar noemt Bluetooth als oorzaak van traag afsluiten. Deze code kan dat, tot 30 seconden, alleen met een open SDP-client. Zonder headset is dit pad niet de verklaring. Het is ook niet het kern-`sync()`: de gebruikersstop zit vóór `system_shutdown`.

**Winst.** Tot 30 seconden in het SDP-geval, zekerheid hoog voor de code en laag voor de stick. De `sync()`-tijd is onbekend.

**Voorstel.** De luisterdraad bij stop ook de client laten vallen, of de time-out bij `fIsShuttingDown` niet uitzitten. Geen wijziging aan de GFX9-stopvolgorde. Eerst het spoor meten, zodat een Bluetooth-fix niet een hangende `sync()` verbergt.

**Architectuur.** De SDP-wachttijd is in de server, dus op elk doel dat Bluetooth start. De `sync()` op USB is AMD64-stick.

### 4.6 CPU, compiler, vermogen

**Compiler.** `ArchitectureRules` zet voor `x86_64` `-march=x86-64-v2` op gebruikersruimte en kernel. De bootloaders halen die vlag eraf en definiëren `R5_REQUIRE_X86_64_V2`, zodat de CPU-controle zelf geen `#UD` doet. `BuildSetup` heeft `OPTIM ?= -O2`. Geen LTO in die twee bestanden. Skia blijft op v2 als basis en kiest AVX2 in `Init_ml3` als de CPU het heeft (`distro/skia/build-skia.sh`). Mesa is `buildtype=release` (`distro/mesa/build-radv.sh`). Het changelog noemt LLVM 21.1.8 in de Lavapipe-regel.

De kernel naar `x86-64-v3` tillen zou AVX in kernelcode toelaten. Dat hoort niet. Libroot kiest memcpy, strings en libm al bij het draaien (changelog 18–19 nov 2026). Een hele boom op v3 herbouwen wint weinig tegenover de opwarming en vernauwt de CPU-vloer. Niet doen zonder een aparte vraag.

**Boost.** `r5-cpu.txt` van stick 68: CPPC hoogste 181, nominaal 87, EPP 0, `no msr write`. Over de bezige seconde is aperf/mperf ongeveer 1,87 tegen een TSC van 2296 MHz, dus rond 4,3 GHz op die draad. De boost werkt. EPP 0 is de prestatievoorkeur. De kernel schrijft hem niet, dus de firmware laat hem daar. Op accu is een evenwichtige EPP (ergens rond 128, zoals air/OS op een andere Zen) een vermogenswinst, geen snelheidswinst. Alleen met een meting, en niet op netstroom als de desktop de boost gebruikt.

**C-toestanden.** Stick 68, 5,796 s: ACPI vindt C1 (FFH, latentie 1), C2 (IO, 18) en C3 (IO, 350). `_OSC` en `_PDC` falen. Daarna `using x86 ACPI idle` en de module `x86_acpi_cstates`. Er is geen residutijd. Of de kernen C3 halen, en of de mislukte `_OSC` dat blokkeert, staat niet in de log. C3 via een I/O-poort met 350 µs latentie kan het gevoel bij een tik ook slechter maken als hij te gretig is. Eerst tellen, dan pas wijzigen.

**Grote pagina's.** Aan, en de framebuffer gebruikt ze. Geen voorstel.

**SMT.** De review van 12 nov 2026 is achterhaald voor de topologie: latere builds hebben de Zen-bladwijzer. Stick 68 zegt `core load tracking: true` en twaalf logische CPU's. De regel met `smt` per CPU zit niet in deze syslog (die begint laat). Geen nieuwe SMT-wijziging voorstellen op deze log alleen.

**Architectuur.** v2-vlag en C-toestanden zijn AMD64. ARM64 en RISC-V hebben hun eigen `-march` (`armv8-a+crc`, `rv64gc`) en doen niet mee aan EPP of ACPI-C3.

### 4.7 Browser, lettertypen, geluid

**WebPositive.** Het image-pakket is `haikuwebkit-1.10.0-2`. `docs/AOMI.md` zet de WebKit-revisie op 625.1.25, naast 626 op nieuwere takken, en laat de app in het pakket tot Aomi hem vervangt. De bron van de schil ligt in de Haiku-boom, met één R5-bestand voor de adresbalk. Een snellere browser is een nieuwere engine of Aomi, niet een vlag in app_server. Licentie van WebKit blijft van dat project. Niet in deze ronde beginnen.

**Lettertypen.** Stick 57 liep vast in FreeType omdat twee draden dezelfde face liepen, vanuit WebPositive. De bron heeft nu één slot `sCharIndexLock` om elke tekenindex (`FontCacheEntry.cpp` 286–302). Latere sticks in deze reeks noemen die fout niet. Beschouwen als gerepareerd in de bron, niet opnieuw als open bug, tot een stick het tegendeel laat zien.

**Geluid.** De luidsprekers blijven stil tot er een CS35L41-stuurprogramma is. Het changelog van 19 nov 2026 zegt dat het Linux-stuurprogramma GPL-2.0 is en niet als bron is gebruikt. De firmware ligt er, ongeladen. Dat is een bug voor de eigenaar en geen prestatiepunt. De hoofdtelefoon is het pad zonder die versterkers.

**Media.** Stick 68 koppelt de mixer om 09:51:45, één seconde na app_server. Geen wachttijd die de opstart domineert.

### 4.8 Wat al gedaan is en niet opnieuw open moet

- Skia deelt zijn beeld met de compositor. De CPU-teruglees van M12 is van het normale pad af (M13, dicht 20 nov 2026).
- Scanout en de GPU-kopie staan op de 5625U aan (stick 66).
- Write-combined GTT zonder write-back-alias staat aan (stick 67).
- `DrawString` legt ongedraaide tekst één keer uit (`SkiaDrawingEngine.cpp` 1292–1309). Gedraaide tekst nog wel twee keer. Klein.
- `gpu_debug` telt submits in de syslog in plaats van een regel per submit (19 nov 2026). De stderr van libdrm is het gat dat overblijft.
- Grote pagina's en de ACPI-idle-module zijn gebouwd en op stick 68 geladen.

## 5. Metingen die er nog niet zijn

De sticks hebben kerneltijd tot de root, een opwarming in milliseconden, en elke 10 seconden een mediaan. Ze hebben geen:

- een tijdlijn van `launch_daemon` per dienst, met de wachtreden;
- een afsluittijdlijn met wandklok: verzoek, Bluetooth weg, `sync()` begint, `sync_done`, haken, stroom eraf;
- residutijd per C-toestand per kern, en het vermogen of in elk geval de pakket-C-toestand;
- frame-tijd tijdens slepen, scrollen en schalen, apart van de 10-secondenmediaan in rust;
- de fasen van de Lavapipe-opwarming: wachten op het eerste venster, per tekenmodus, LLVM, schijfcache-treffer of -mis;
- de grootte van `/boot/system/cache/app_server/mesa` na start 1 en de opwarming van start 2 op dezelfde image, RADV en Lavapipe apart;
- bytes geschreven naar de stick per sessie (stderr, syslog, packagefs);
- een geïnstalleerde start op de interne schijf, naast de USB-stick;
- een paginalading in WebPositive met een vaste lokale pagina, zodat een latere engine te vergelijken is.

`m13-measure` leest een deel hiervan al uit de syslog. De afsluiting, de C-toestand en de pakketscan zitten er niet in.

## 6. Open vragen

Een QEMU-start is voor deze review niet gedraaid. De 42 seconden komen uit het changelog van 19 nov 2026.

| Vraag | Wat hem beantwoordt |
|---|---|
| Hangt `sync()` of wordt de laptop eerder uitgezet? | Eén herstart laten uitlopen. De volgende syslog moet `sync_done` bevatten. De wandklok van het verzoek en van het donkere scherm erbij. |
| Maakt Bluetooth dat lang zonder headset? | Dezelfde herstart met de radio aan en uit, zonder gekoppeld apparaat, en één keer met een headset die SDP open laat. |
| Waarom 221596 geslaagde ioctls terwijl de instellingen geen `gpu_trace` hebben? | De libdrm van dat stick-beeld naast `r5_translate.c` van `ec81d571`. Als het beeld ouder is, is de 12 MB de oude log. De `append` blijft dan nog steeds een bug. |
| Blijft de Mesa-cache op de stick, en daalt de Lavapipe-opwarming bij start 2? | `gpu false` twee keer op dezelfde stick, zonder het cachebestand te wissen. Grootte van de cache na beide starts. |
| Is de tweede xHCI een tweede controller? | `listdev` in `r5-hardware.txt` van stick 68 naast de twee starten op 0,47 s en 1,58 s. |
| Gaan de kernen C3 in na de mislukte `_OSC`? | Een residuteller, of een bestaande ACPI-teller, over 30 s nietsdoen. |
| Hoe traag is WebPositive in seconden? | Eén lokale pagina, koude en warme start. Geen enginewissel in die meting. |
| Hoe snel is een start van de interne schijf? | Eén installatie, dezelfde tijdlijn als sectie 4.4. De stick van 5,1 s tot BFS geldt dan niet. |

Niet doen naar aanleiding van deze review: de GFX9-ring, de PSP, de PTE-vlaggen, de reset, de stop bij afsluiten, de Settings-indeling, de Haiku-ABI, of Skia uit als standaard.
