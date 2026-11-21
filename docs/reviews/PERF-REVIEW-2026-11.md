<!--
  SPDX-FileCopyrightText: 2026 R5OS Contributors
  SPDX-License-Identifier: MIT
-->

# Prestatie- en bugreview van R5 Operating System (21 nov 2026)

Alleen gelezen. Bron is `origin/main` op `ec81d57163` (20 nov 2026, 14:37 Europe/Amsterdam, merge van #4481, M13 dicht). Logs: `generated/boot-5625u-60` tot en met `68` op de referentie-laptop (HP ENVY x360 15, Ryzen 5 5625U), plus ter vergelijking `boot-5625u-69` en `70` (van de tak `r5/stick-70` met `cs35l41`). Geen jam, geen `apply-overlay.sh`, geen QEMU, geen stick, geen wijziging aan code of aan de gedeelde WSL-boom. Die boom en de reeds gebouwde pakketten/bronnen (`/home/nickr/r5os/haiku`, `/home/nickr/r5os/mesa-build/mesa-25.3.6`) zijn alleen gelezen: compilerregels in `build/jam/ArchitectureRules` en `BuildSetup`, `block_cache_create` en de lage-geheugenhandler in `src/system/kernel/cache/block_cache.cpp`, `src/servers/notification/NotificationServer.cpp`, `#define hz 1000LL` in `src/libs/compat/freebsd_network/compat/sys/kernel.h`, `src/add-ons/kernel/busses/scsi/ahci/ahci_port.cpp` en `ahci.c`, `src/add-ons/kernel/drivers/disk/usb/usb_disk/usb_disk.cpp`, `src/system/libroot/posix/malloc/openbsd/malloc.c`, Mesa's `lp_screen.c`, `lvp_pipeline_cache.c`, `lp_bld_init.c` en `lp_bld_passmgr.c`, `libdrm-2.4.124-4-x86_64.hpkg`, `haikuwebkit-1.10.0-2-x86_64.hpkg`, en een zoektocht naar `snooze` en `BMessageRunner` onder `src/servers`.

Regels die hier gelden, uit `AGENTS.md`: de softwareweg (Painter, llvmpipe, Lavapipe) blijft; de GFX9-ring, PSP, paginatabellen, reset en stop bij afsluiten veranderen niet zonder een gevraagde stick-proef; de Settings-indeling blijft; `libbe` en de Haiku-ABI blijven; Mesa en Skia houden hun eigen licentie; een nieuwe R5-helper hoort in een `libr5*`-bibliotheek en blijft MIT. M13 is dicht (`docs/M13-PLAN.md`, 20 nov 2026). De Lavapipe-opwarming is het open punt, geen reden om M13 opnieuw open te trekken. Skia is de standaard, ook op Lavapipe, en Painter alleen in veilige modus (eigenaar, 19 nov 2026). Op Lavapipe is Skia nog trager dan Painter. Die keuze blijft staan.

De stick-sessies draaien met `gpu_draw`, `gpu_debug` en `gpu_selftest` aan. Dat is de proefstick, niet de stille standaard. Cijfers uit die sessies tellen de debug-log mee.

De eerste versie van dit rapport leunde op `docs/M13-PLAN.md` en oudere reviews voor wat "al gedaan" was. Deze versie houdt alleen stand waar de bron of de syslog het zelf laat zien. Deskbar, de notificatieserver, Terminal, het Wi-Fi-stuurprogramma, de BFS-blokcache en de pictogramweg van Tracker staan in sectie 4.10. Wat daarna nog open is, staat in sectie 6 en is geen bevinding.

## 1. Samenvatting

De desktop op de 5625U met de GPU aan is na de eerste twee seconden al vlot. Op stick 68 duurt de Skia-opwarming 2032 ms, de volgende start van dezelfde stick 72 ms, en het samenstellen van het scherm zit daarna rond een halve tot anderhalve milliseconde. Daar hoeft de ring niet meer open.

Het trage gevoel zit op drie andere plekken.

De softwareweg is de grote wachttijd. Met `gpu false` op stick 65 (20 nov 2026, 11:20) warmt Lavapipe in 27943 ms op. Het langste stuk is 16383 ms. In QEMU staat in het changelog een run van 42246 ms. Tijdens die minuut is de tekenthread verzadigd: 12 seconden CPU per 10 seconden. Het rustige samenstellen is dan al goedkoop (onder de 2 ms). De pijn is het compileren van shaders, niet een trage compose in rust. Slepen is in deze logs niet apart gemeten.

Afsluiten is niet afgemaakt in het spoor. Op sticks 64, 65, 67 en 68 staat bij de volgende start alleen `r5 previous shutdown: enter reboot=1`. `sync_done` komt nergens voor. `sync()` zit meteen na die eerste regel. Een herstart die de schijf niet haalt, of een herstart die in `sync()` blijft hangen, ziet er hetzelfde uit. Bluetooth kan daar los van tot 30 seconden bij de afsluiter houden als SDP op een stille client in `recv` zit. De debug-log van app_server op stick 68 is 12,4 MB en wordt ongebufferd achteraan het bestand geplakt, op de USB-stick die daarna gesynchroniseerd moet worden.

Elke verse stick mist het activatiebestand van de pakketten en doet first-boot van 137 pakketten (stick 68: van 7,55 s naar 9,08 s kerneltijd). Bij de tweede start van stick 68 ontbreekt dat bestand nog steeds.

WebPositive is het pakket `haikuwebkit-1.10.0-2` (WebKit rond 625). Dat is de dagelijkse traagheid in de browser. Aomi is het plan in `docs/AOMI.md`. Geen kleine patch.

De rest van de desktop heeft geen tweede wachttijd van die orde. Deskbar tekent de klok één keer per seconde en alleen als de tekst verandert. De notificatieserver wacht op een bericht en zet daarna één timer voor het verdwijnen. Terminal tekent een scroll al samen per 100 ms zodra er meer dan een scherm aan regels binnenkomt. Wat wel blijft draaien: de vensterlijst van Deskbar elke 150 ms, en het Wi-Fi-stuurprogramma elke 10 ms zolang de interface open is. Geen van beide is een seconde op de opstart. De invoermethode van Terminal slaapt 100 ms per wijziging, met het commentaar `TODO: Debug only`.

Eerst doen, in deze volgorde:

1. **AHCI lege poorten (3,03 s winst op elke start, ook op NVMe!)**: de eerste 5,1 s tot BFS zijn *niet* vooral de USB-stick; 3,03 s daarvan zijn twee lege AMD AHCI-controllers (`1022:7901`) die elk 1,0 s + 0,5 s wachten in `AHCIPort::PortReset()`.
2. **`libdrm-2.4.124-4` opnieuw bouwen + app_server-stderr begrenzen**: het pakket in `generated.x86_64/download` bevat nog de oude `libdrm.so` zonder `gpu_trace`, daarom loggen sticks 68, 69 en 70 nog steeds miljoenen regels ongebufferd.
3. **`i2c_hid` touchpad-polling (1000 Hz) terugschroeven**: `HIDDevice.cpp` polt nu elke 1 ms voor altijd, wat op laptops diepe C-toestanden en accuduur blokkeert.
4. **Lavapipe-opwarming meten en versnellen**: `lvp_pipeline_cache.c` is een lege stub; alle schijfcache loopt via `llvmpipe`'s `lp_disk_cache_create`. Meten wat er na een `gpu false`-start in `/boot/system/cache/app_server/mesa` staat, plus een tweede start en eventueel `GALLIVM_PERF=no_opt` / MSAA 1 als test. De ring, PSP en paginatabellen blijven dicht.
5. **WC direct-map vrijgeven in één batch** (`r5_direct_map.cpp`): nu 3 SMP-IPI's per 32 pagina's (96 globale IPI's bij het vrijgeven van één 4 MiB GTT-buffer).
6. **Het afsluitspoor (`sync_done`, `hooks_done`) + `r5_pstate_log` (`burn_cpu` 1 s + 4× `fsync`)** achter debug of op verzoek zetten, en `PipelineStorage` per apparaat opslaan met checksum-check. Pas na het afsluitspoor Bluetooth SDP inkorten.
7. **WebKit en `-march=x86-64-v3` voor de hele boom niet nu.** `haikuwebkit-1.10.0-2` heeft JIT aan (6438 DFG/FTL/JIT-symbolen), de boom is al `-O2` en `x86-64-v2`, en de kernel hoort geen AVX te draaien.

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
| 10 | prestatie | AHCI lege poorten (opstart) | 2,8–3,0 s op elke start (USB én NVMe; 3,03 s van de 5,1 s tot BFS zijn twee lege AHCI-controllers) | hoog (stick 68/69/70 + `ahci_port.cpp`) | 1 | laag tot middel | ja |
| 11 | prestatie | LTO of x86-64-v3 voor alles | klein tegenover 2 | middel dat v3 voor de kernel fout is | veel | hoog | niet doen |
| 12 | prestatie | luidsprekers CS35L41 | 2,68 s synchrone I2C-firmwarelaadtijd bij opstart (stick 70); asynchroon laden haalt dat uit de opstart | hoog (stick 70) | 1 | laag; de MIT-driver (clean room) staat in #4482 | na #4482 |
| 13 | prestatie | `r5_pstate_log` | 1,0 s 100% CPU-spin (`burn_cpu`) + vier keer `fsync` van tot 1,2 MiB syslog plus hardwarelog, elke start | hoog | 1 | laag | ja |
| 14 | bug en prestatie | Skia-pipelinebestand | elke `store()` herschrijft en hasht alles (tot 804 KB), én één bestandsnaam wist RADV-cache na Lavapipe-start | hoog | 1 | laag | ja |
| 15 | geen snelheidswinst | scheduler | start altijd in low latency, quantum 1 ms | hoog dat het zo staat, laag dat power-saving de desktop sneller maakt | — | middel als je het omzet | niet voor snelheid |
| 16 | bug | Terminal, invoermethode | 100 ms `snooze` per wijziging van de invoermethode | hoog in de code, niet gemeten | 1 | laag | ja, klein |
| 17 | prestatie | Deskbar-vensterlijst | een draad op lage prioriteit, elke 150 ms, zolang de balk er is | hoog dat hij loopt, laag hoeveel CPU dat is | 2 | middel, titels mogen niet blijven hangen | meten eerst |
| 18 | prestatie | mt7922, lege ringen | een ontvangstdraad elke 10 ms zolang de interface open is; leeg is drie descriptoren | hoog dat hij loopt, laag dat het een procent is | 1–2 | middel, de ringen liepen vol toen de tik wegviel | meten eerst |
| 19 | geheugen | BFS-blokcache | geen vaste grootte; krimpt pas bij weinig geheugen | hoog voor het mechanisme, geen telling op de stick | 1 om te tellen | middel als je een plafond zet | meten eerst |
| 20 | prestatie en vermogen | `i2c_hid` / `i2c_elan` 1000 Hz poll | stopt 1000 timer-wakeups/s in rust; diepe C-toestanden en accuduur op laptops (5625U, X1) | hoog in de code (`HIDDevice.cpp:32,251`) | 1 (backoff) / 2–3 (GPIO IRQ) | laag | ja |
| 21 | prestatie | WC direct-map vrijgeven (`r5_direct_map.cpp`) | 96 globale SMP-IPI's → 2 IPI's bij vrijgeven van een 4 MiB WC GTT-buffer (AMD64) | hoog in de code (`batch[32]`) | 1 | middel (PAT/TLB-volgorde) | ja |
| 22 | bug en prestatie | `libdrm-2.4.124-4` pakket verouderd | neemt 221.596 ioctl-regels (12,4 MB) op debug-sticks weg; pakket mist `gpu_trace` uit `r5_translate.c` | hoog (bevestigd in `.hpkg`) | 1 | laag | ja, samen met 1 |
| 23 | prestatie | `Compositor::_ReadZeroCopySwitch()` | stopt elke 10 s schijf-open + stderr/syslog-log op de urgente compositordraad | hoog in de code (`Compositor.cpp:6442`) | 1 | laag | ja |
| 24 | prestatie | 32-bit DMA-limiet in `usb_disk` en `ahci` | vermijdt SCSI bounce-buffer kopieën boven 4 GiB RAM op 16 GiB-systemen (60+ hits in stick 68) | hoog in de code + syslog | 1–2 | middel (64-bit DMA-paden testen) | na 10 |
| 25 | meting-bug | `Compositor::NoteInput()` | voorkomt verkeerde `input latency` (60–200 ms mediaan / 248 ms p95 door ongerelateerde 250 ms cursor-blink) | hoog in de code + stick 68 | 1 | laag | ja |
| 26 | vermogen | 1 Hz wakeups in rust (`gpu_retire_thread`, `AirPlayServer`) | minder periodieke wakeups als GPU en AirPlay inactief zijn | hoog in de code, klein effect | 1 | laag | laagste prio |

## 3. In één sessie

- **AHCI lege poorten (`ahci_port.cpp:264-286`)**: sla de 1,0 s `TFD.BSY`-wachttijd over als `SStatus.DET == 0` en de controller geen staggered spin-up heeft, en verkort de lege-poort COMRESET-wachttijd. Scheelt ~3,0 s op elke start van de 5625U (USB én NVMe).
- **`libdrm-2.4.124-4` opnieuw verpakken + app_server-stderr begrenzen**: `libdrm.so.2.124.0` in het huidige `.hpkg` mist `gpu_trace` nog. Daarna `app_server-stderr.log` niet meer `append` zonder plafond en niet `_IONBF` op het submit-pad.
- **`i2c_hid` adaptieve poll-backoff (`HIDDevice.cpp:32,251`)**: van vast 1 ms (1000 Hz) naar 1 ms bij activiteit en 8–16 ms na 200 ms rust (tot GPIO-interrupts zijn aangesloten).
- **WC direct-map vrijgeven in één batch (`r5_direct_map.cpp:807-849`)**: één `UnmapPages`, één `arch_vm_direct_map_drain()` en één `r5_direct_shootdown()` per bereik in plaats van per 32 pagina's.
- **`r5_pstate_log` achter een instelling of op verzoek**: schakelt zowel de 1,0 s `burn_cpu()`-lus als de vier `fsync`-passen uit op normale starts; `KERN:` uit `line_matters()` halen.
- **`PipelineStorage::store()` (`SkiaGpu.cpp:291,325`)**: bestandsnaam per apparaat/driver (zodat Lavapipe de RADV-cache niet wist), niets wegschrijven op Lavapipe (waar `lvp_CreatePipelineCache` een stub is), en overslaan als de checksum gelijk is.
- **`Compositor::_ReadZeroCopySwitch()` (`Compositor.cpp:6442`)**: niet elke 10 s van schijf lezen en loggen als `gpu_debug` uit staat.
- **`Compositor::NoteInput()` (`Compositor.cpp:2650,3131`)**: alleen meten als een muisbeweging daadwerkelijk vensterschade veroorzaakt, niet bij een willekeurige 250 ms cursor-blink.
- Eén herstart op de 5625U met het afsluitspoor in de volgende syslog: de regels `sync_done`, `hooks_start`, `hooks_done` moeten er staan, met de wandklok erbij. Eén keer met Bluetooth aan, één keer uit. Geen wijziging aan de GFX9-stop.
- Nakijken waarom `activated-packages` bij de tweede start van stick 68 nog ontbreekt.
- Een teller voor C-residutijd, uit, tot de eigenaar hem aan zet. Geen wijziging aan de C-toestand zelf in die sessie.
- De `snooze(100000)` in `TermView::_HandleInputMethodChanged` weghalen. Het commentaar erboven zegt `TODO: Debug only`. Gewone toetsen en het scrollen gaan daar niet doorheen.

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

**Geluid.** De luidsprekers hebben een eigen MIT-stuurprogramma, clean room gebouwd uit een specificatie zonder Linux-code (#4482, nog niet gemerged; aangevuld door Jarvis op 21 nov 2026). In bypass speelt het op stick 68; de firmware met speakerbescherming laadt sinds stick 70 en wacht op de laatste stap. Het enige prestatiepunt is dat het laden van de firmware over I2C 2,68 s van de opstart kost (zie rij 12).

**Media.** Stick 68 koppelt de mixer om 09:51:45, één seconde na app_server. Geen wachttijd die de opstart domineert.

### 4.8 Wat al gedaan is en niet opnieuw open moet

- Skia deelt zijn beeld met de compositor. De CPU-teruglees van M12 is van het normale pad af (M13, dicht 20 nov 2026).
- Scanout en de GPU-kopie staan op de 5625U aan (stick 66).
- Write-combined GTT zonder write-back-alias staat aan (stick 67).
- `DrawString` legt ongedraaide tekst één keer uit (`SkiaDrawingEngine.cpp` 1292–1309). Gedraaide tekst nog wel twee keer. Klein.
- `gpu_debug` telt submits in de syslog in plaats van een regel per submit (19 nov 2026). De stderr van libdrm is het gat dat overblijft.
- Grote pagina's en de ACPI-idle-module zijn gebouwd en op stick 68 geladen.

### 4.9 Wat de code doet buiten de grafische weg

Dit zijn paden die voor deze aanvulling in de bron zijn gelezen, niet in een plan.

**Diagnostische log op elke start.** `data/launch/system` (de boom die de image bouwt) heeft `job x-vnd.R5OS-cpu-log`: `/system/bin/r5_pstate_log` op `initial_volumes_mounted`. `distro/cpu/r5_pstate_log.cpp` `main` (rond regel 266) schrijft meteen, en daarna nog drie keer, met tussenpozen van 15 s, 45 s en 120 s. Elke pas roept `write_boot_logs` aan. Die doet `fsync` op het CPU-rapport, daarna `write_hardware` (`listdev`, `listimage 1`, en syslogregels, ook met `fsync`), en `copy_tail` van de syslog tot 1200 × 1024 bytes, ook met `fsync` (regels 90–134 en 251–263). `line_matters()` (regel 184) houdt een regel als hij `KERN:` bevat. Bijna elke kernelregel in de syslog begint zo, en de functie stopt pas na 4000 bewaarde regels. Op stick 68 is `r5-hardware.txt` 319833 bytes. Vier passen op een USB-stick zijn vier keer die schrijf- en sync-stroom, terwijl de desktop opkomt. Dit staat niet achter `gpu_debug`.

**Pipelinebestand.** `PipelineStorage::store()` in `SkiaGpu.cpp` (regel 325) berekent een FNV-1a over elke byte (regel 361) en schrijft het hele bestand via een tijdelijk bestand en `rename`. Er is geen vergelijking met de checksum die `load()` net gelezen heeft. Stick 68, 21 nov 2026: 25 KB om 09:51:44, 529 KB om 09:51:54, 531 KB om 09:52:04, 682 KB om 09:52:40 en 09:52:54, 683 KB om 09:53:04 en 09:54:04, 804 KB om 09:54:45 en opnieuw om 09:57:45. De tweede start logt `pipeline cache loaded` en meteen daarna weer `stored 804 KB`. Dat is schijf op de opstartvolume, ook als de inhoud niet veranderd is. De hash van 800 KB op de CPU komt er elke keer bij.

**Scheduler.** `scheduler_init()` zet `SCHEDULER_MODE_LOW_LATENCY` (`scheduler.cpp` regel 892). De modus in `low_latency.cpp` (regel 322) heeft `base_quantum` 1000 µs, `minimal_quantum` 100 µs, vermenigvuldigers 2 en 5, `maximum_latency` 5000 µs. `gSchedulerPowerSavingMode` bestaat (2000 µs, 500 µs, maximum 20000 µs) en wordt bij het starten niet gekozen. `acpi_cpuidle.cpp` regel 705 zet de modus daarna nog eens op low latency. Een omschakeling naar power saving zou de desktop niet sneller maken. Het is een accuknop, en die is nu niet de startstand. Geen voorstel om dat zonder meting om te zetten.

**`launch_speedup` wordt gevraagd en niet gevonden.** `file_cache_init_post_boot_device()` in de kern (`src/system/kernel/cache/file_cache.cpp` rond regel 1250) doet `get_module("file_cache/launch_speedup/v1")` en print alleen bij succes `opened launch speedup`. Stick 68, twee starts, kerneltijd 5,625 s: `module: Search for file_cache/launch_speedup/v1 failed.` De succesregel staat er niet. De addon staat in de Jamfile van `src/add-ons/kernel/file_cache`. Waarom het bestand niet op de stick staat, is niet nagelopen: een `find` over `generated.x86_64` was niet af. Geen winst claimen tot dat bestand er is of aantoonbaar ontbreekt in het pakket.

**Kopiëren.** `_arch_cpu_user_memcpy` in `arch.S` (regel 135) zet een fault-handler en gebruikt `rep movsb` vanaf 64 bytes. Er zit daar geen `setjmp` op dat pad. `memcpy` in gebruikersruimte (`libroot/.../memcpy.cpp`) kiest ERMS als de CPU het heeft, en wijkt uit naar SSE bij een pagina-alias van minder dan 32 bytes. Dat is de huidige code. Geen open voorstel om die paden opnieuw te schrijven op basis van de review van 12 november.

**Geluid.** De mixerlog op stick 68 zegt `48000 Hz 2 ch fmt 0x4 buf 16384`. `buffer_size` in die regel (`DefaultManager.cpp` rond 1430) is het veld uit `media_raw_audio_format`. `default_buffer_length_for_rate()` in `hda_multi_audio.cpp` (regel 945) geeft bij 48000 het getal 2048, in frames. 2048 frames, twee kanalen, vier bytes, is 16384 bytes, ongeveer 43 ms. Dat is de bedoelde latentie naast 2048 frames op 44100 Hz, niet een buffer van 16384 frames.

**Pictogrammen.** De cache zelf staat in sectie 4.10. Het commentaar boven de includes klopt met de aanroep: een type dat nog niet in de cache zit haalt het pictogram via `BMimeType::GetIcon` in een `BBitmap` (`IconCache.cpp` regel 442).

## 4.10 Deskbar, meldingen, Terminal, Wi-Fi, BFS, Tracker

Dit zijn de paden die de vorige versie nog niet gelezen had. De periodieke wachten onder `src/servers` zijn in de toegepaste boom opgezocht. Alleen de functies hieronder zijn in de bron nagelopen.

**Deskbar.** `TReplicantTray::AttachedToWindow` zet de vensterpuls op 1000000 µs (`StatusView.cpp` regel 204). `TTimeView::Pulse` (regel 332) herlaadt de landinstelling alleen als de 24-uurskeuze of de conventie-id verandert, en tekent opnieuw als de tijdtekst anders is. De `snooze(25000)` in `TDragRegion::MouseDown` (regel 1629) zit in de lus die de muis volgt zolang de knop ingedrukt is. `InlineScrollView` slaapt 5 ms alleen tijdens een scrollanimatie. Wat wel blijft lopen is `TExpandoMenuBar::monitor_team_windows` (regel 1022): een draad op `B_LOW_PRIORITY`, gestart in `StartMonitoringWindows` (regel 1001), die de vensterlijst naloopt en daarna `snooze(150000)` (regel 1189). Dat is de staande kost van de balk, niet de klok.

**Meldingen.** `NotificationServer.cpp` is 128 regels. `MessageReceived` (regel 53) handelt een bericht af en geeft de rest aan `BApplication`. Het enige tijdmechanisme in die server is één `BMessageRunner` in `NotificationView.cpp` regel 140, met het timeout van die melding en een telling van 1. Geen poller.

**Terminal.** `kSyncUpdateGranularity` is 100000 µs (`TermView.cpp` regel 98). `_SynchronizeWithTextBuffer` (regel 2262) tekent meteen zolang er in dat venster niet meer dan `fRows` regels zijn verschoven. Pas daarboven start hij een `BMessageRunner` op die 100 ms, zodat een stortvloed niet per regel het scherm bijwerkt. Dat pad is al een rem. `_HandleInputMethodChanged` (regel 3130) doet daarna `Invalidate()` en `snooze(100000)`, met het commentaar `TODO: Debug only` (regels 3151–3153). Elke wijziging van de invoermethode wacht een tiende seconde op de draad van het venster. Een gewone letter gaat daar niet doorheen.

**Wi-Fi.** Zolang de interface open is zet `mtk_parent` een callout op `hz / 100` (`if_mtk.c` regel 972). De compat-laag definieert `hz` als 1000, dus 10 ms. `mtk_tick` (regel 1359) plant zichzelf opnieuw (regel 1395) en zet elke tik de ontvangsttaak in de wachtrij. Het commentaar zegt dat de ringen volliepen toen commando's elke tik meegingen, en dat legen goedkoop moet blijven. `mtk_rxwork` (regel 1167) neemt het slot en roept `mtk_receive` aan (`mtk_tx.c` regel 770). Op een lege ring leest die één descriptorwoord per ring en stopt (`mtk_tx.c` regels 794–799). De onderbrekingsweg `mtk_intr` (regel 1402) leest dezelfde ringen als de kaart iets meldt. De 10 ms blijft dus ook als er geen frame is. Geen voorstel om die tik te schrappen zonder een telling van `sc_beat` tegen `sc_received` op de 5625U: de vorige keer dat de wachtrij een tik miste, liepen de ringen vol.

**BFS.** `Volume.cpp` regel 231 geeft `opener.InitCache(NumBlocks(), fBlockSize)` het aantal blokken van het volume, niet een RAM-plafond. `DeviceOpener::InitCache` geeft dat door aan `block_cache_create` (`src/add-ons/kernel/file_systems/shared/DeviceOpener.cpp` regels 84–87). `block_cache_create` (`block_cache.cpp` regel 3727) bewaart dat aantal als `max_blocks`: het adresbereik, niet het aantal blokken in RAM. Een ongebruikt blok blijft in de lijst tot `_LowMemoryHandler` (regel 1864). Bij `B_NO_LOW_RESOURCE` geeft die meteen terug. Bij een notitie maakt hij een kwart vrij dat ouder is dan 120 s, bij een waarschuwing de helft ouder dan 10 s, bij kritiek alles behalve één. Op een laptop met vrij geheugen groeit de cache met wat gelezen is en krimpt hij niet. Er is geen syslogregel met `unused_block_count`. Geen plafond voorstellen tot die telling er is.

**Tracker.** `BPoseView::AddPoses` (regel 1396) vult een map op een eigen draad, in stukken van `kMaxAddPosesChunk` (50, regel 119). Een type dat de pictogramcache nog niet heeft, gaat via `GetIconFromMetaMime` naar `BMimeType::GetIcon` op een `BBitmap` (`IconCache.cpp` regel 442) en blijft daarna in `fSharedCache`. De eerste map betaalt die rondgang naar de registrar en app_server. Een tweede keer hetzelfde type niet. Hoeveel unieke types de bureaubladmap op de 5625U heeft, staat niet in de log.

**Andere diensten, alleen de wacht.** In de toegepaste boom: de invoerserver herhaalt een mislukte start na 2 s (`InputServer.cpp` 435). De registrar slaapt in de afleverherhaling en in de afsluitanimatie, niet in een vaste poller. `net_server` wacht tot 20 keer 50 ms tot de messenger van `wpa_supplicant` geldig is, alleen na een start van die helper (`NetServer.cpp` rond 1427). De media-addon-server heeft een puls van 1 s. De indexserver ook. Mail plant zijn eigen controle. Het Bluetooth-pictogram in de balk polt elke 5 s (`BluetoothDeskbar.cpp` regel 60). De Bluetooth-server controleert SMP- en ATT-timeouts elke 5 s (`BluetoothServer.cpp` regel 279). Geen van die wachten is een opstart van seconden.

### 4.11 Tweede doorloop: AHCI-opstart, I2C-touchpad, WC-shootdowns, DMA, Lavapipe/Mesa-details en opgeloste open vragen

Deze bevindingen komen uit directe inspectie van de kernel-, driver-, Mesa-, pakket- en stick-68/69/70-logs na de eerste versie van dit rapport.

**1. Twee lege AHCI-controllers kosten 3,03 s op elke start — ook op interne NVMe (Prestatie, Opstarten, AMD64/alle doelen met AHCI).**
- **Wat.** In `boot-5625u-68/syslog` (regels 475–555; zelfde patroon in sticks 69 en 70): de eerste 5,1 s tot BFS zijn *niet* grotendeels de USB-stick. AMD FCH exposeert twee lege AHCI-controllers (`1022:7901`, `5:0:0` en `5:0:1`), elk met 1 poort en `Supports Staggered Spin-up: no`. Controller 1 wacht van `[2.022]` tot `[3.026]` (1,004 s) en doet daarna `COMRESET` tot `[3.529]` (0,503 s). Controller 2 herhaalt dat exact van `[3.540]` tot `[4.544]` (1,004 s) en `[4.544]` tot `[5.047]` (0,503 s). Pas op `[5.056]` koppelt `nvme_disk`, en op `[5.11]` mount BFS.
- **Waarom.** `AHCIPort::PortReset()` (`src/add-ons/kernel/busses/scsi/ahci/ahci_port.cpp:264-286`) wacht eerst `wait_until_clear(&fRegs->tfd, ATA_STATUS_BUSY | ATA_STATUS_DATA_REQUEST, 1000000)` (1,0 s) op een lege poort waar `TFD` `0x7f`/`0xff` is, voert dan `COMRESET` uit en wacht vervolgens `wait_until_set(&fRegs->ssts, SSTS_PORT_DET_PRESENT, 500000)` (0,5 s). Dit gebeurt synchroon voor beide controllers vóórdat `nvme_disk` of `bfs` opkomt.
- **Winst.** **2,8–3,0 seconden snellere start op elke boot** (USB-stick én geïnstalleerde NVMe op de 5625U en vergelijkbare Ryzen-laptops). Zekerheid: **hoog**.
- **Voorstel.** Als `SStatus.DET == 0` vóór reset en de HBA geen staggered spin-up ondersteunt, sla de 1,0 s `TFD.BSY`-wacht over (direct naar COMRESET of korte detectie), en verkort de post-COMRESET wachttijd voor een lege poort (SATA PHY-detectie duurt ≤ 10 ms; Linux wacht 10–100 ms in plaats van 500 ms), of probeer AHCI-controllers parallel.
- **Moeite en risico.** 1 sessie; risico laag tot middel (echte SATA-schijf op oudere hardware moet blijven detecteren). **Regels.** Kopieer `ahci_port.cpp` naar `distro/` met originele MIT-header + SPDX-Snippet.

**2. `i2c_hid` (en `i2c_elan`) touchpad-stuurprogramma polt continu op 1000 Hz (Prestatie/Vermogen, Invoer, AMD64-laptops / ARM64 / RISC-V met I2C-HID).**
- **Wat.** `distro/kernel/src/add-ons/kernel/drivers/input/i2c_hid/HIDDevice.cpp:32` zet `kInputReadyPollInterval = 1000` (1 ms) en regels 245–257 lussen `while (!fInputReady->IsAsserted()) snooze_etc(kInputReadyPollInterval, ...)`. Hetzelfde geldt voor `distro/kernel/src/add-ons/kernel/drivers/input/i2c_elan/ELANDevice.cpp:68, 382`.
- **Waarom.** Zelfs wanneer de gebruiker het touchpad niet aanraakt, wekt een kerneldraad de CPU **1000 keer per seconde** om de GPIO-lijn uit te lezen. Op laptops (HP ENVY x360 15, Lenovo X1 Carbon Gen 8) verhindert een 1 kHz-timer dat de CPU-package diep in CC6/PC6 slaapt en kost dit merkbaar accuduur.
- **Winst.** Lager rustverbruik, hogere C-toestand-residutijd en minder timer-jitter op laptops. Zekerheid: **hoog** dat hij 1000 Hz polt, **middel** voor exacte watt-winst tot er gemeten wordt.
- **Voorstel.** Snelle winst (1 sessie): adaptieve backoff — 1 ms zolang er recent (bijv. laatste 200 ms) een rapport was, daarna opschalen naar 8–16 ms in rust. Structurele oplossing (2–3 sessies): GPIO-interrupt aansluiten zodat er 0 wakeups zijn in rust.
- **Moeite en risico.** 1 sessie (backoff) / 2–3 (GPIO IRQ); risico laag. **Regels.** MIT-code in `distro/`.

**3. `arch_vm_direct_map_release_range()` stuurt 3 SMP-IPI's per 32 pagina's (128 KiB) bij het vrijgeven van WC GTT-buffers (Prestatie, Kernel VM / GPU, AMD64).**
- **Wat.** `distro/kernel/src/system/kernel/arch/x86/r5_direct_map.cpp:807-849` loopt in blokken van `batch[32]` (128 KiB) door het bereik en roept *binnen elke iteratie* `map->UnmapPages(...)`, `arch_vm_direct_map_drain()` (SMP-IPI `sfence` op alle 12 kernen) en `r5_direct_shootdown()` (SMP-IPI TLB-shootdown op alle 12 kernen) aan.
- **Waarom.** Het vrijgeven van één 4 MiB write-combined GTT-buffer (1024 pagina's = 32 blokken) veroorzaakt **96 globale SMP-IPI-broadcasts** over alle 12 CPU-threads (en 384 IPI's voor een 16 MiB-buffer). Stick 67 logde `wc 175` in 10 s, dus WC-buffers worden actief aangemaakt en vrijgegeven.
- **Winst.** Merkbaar lagere kernel-CPU en minder IPI-verstoring op alle 12 kernen bij venster-resize, schalen en GPU-bufferverloop. Zekerheid: **hoog** voor het mechanisme, **middel** voor het percentage van de totale CPU.
- **Voorstel.** Unmap het hele bereik in één `map->UnmapPages(area, base, size, true)`, doe daarna één `arch_vm_direct_map_drain()`, loop vervolgens de pagina's af om de direct-map-referenties te verlagen (met een grotere stapel- of tijdelijke lijst), en sluit af met één `r5_direct_shootdown()` (2 IPI's in plaats van `3 × (pagina's / 32)`).
- **Moeite en risico.** 1 sessie; risico middel (PAT/WC-volgorde en `r5_wc_gtt`-zelftest moeten blijven slagen). **Regels.** AMD64-specifiek; GFX9-ring en PTE-vlaggen blijven onaangeraakt.

**4. Oorzaak van 221.596 `r5 libdrm:`-regels in `app_server-stderr.log` opgelost: `libdrm-2.4.124-4-x86_64.hpkg` is verouderd (Bug en prestatie, Pakketbouw, AMD64).**
- **Wat.** Uitpakken en inspecteren (`strings`) van `libdrm.so.2.124.0` in `/home/nickr/r5os/haiku/generated.x86_64/download/libdrm-2.4.124-4-x86_64.hpkg` laat zien dat het binaire bestand wél `gpu_debug` bevat, maar **geen `gpu_trace`**. Hoewel `distro/graphics/libdrm-r5/r5_translate.c` op 19 nov is aangepast om geslaagde ioctls achter `gpu_trace` te zetten, is het `.hpkg` nooit opnieuw gebouwd. Daarom schreven ook sticks 68 (12,4 MB), 69 (2,7 MB) en 70 (3,0 MB) nog elke ioctl ongebufferd naar stderr.
- **Winst.** Neemt direct megabytes ongebufferde schrijf-I/O van het submit-pad en uit `sync()` op debug-sticks weg. Zekerheid: **hoog**.
- **Voorstel.** `libdrm` opnieuw bouwen en als `-5` verpakken uit `distro/graphics/libdrm-r5`, samen met het begrenzen/bufferen van `app_server-stderr.log`.
- **Moeite en risico.** 1 sessie; risico laag. **Regels.** `libdrm-r5` blijft MIT; symboolcheck volgens `docs/GPU-ARCHITECTURE.md`.

**5. Lavapipe- en Mesa 25.3.6-diepteanalyse (Prestatie, Software-rendering, AMD64 en toekomstige doelen).**
- **Wat.** Direct uit `/home/nickr/r5os/mesa-build/mesa-25.3.6` en `build-radv/build.ninja`:
  - `src/gallium/frontends/lavapipe/lvp_pipeline_cache.c` is een volledige no-op stub: `VkPipelineCache` slaat niets op. Alle schijfcache voor Lavapipe komt dus uitsluitend van `llvmpipe`'s `lp_disk_cache_create()` (`lp_screen.c:845-864`). Die is wél meegecompileerd (`-DENABLE_SHADER_CACHE -DHAVE_DLADDR -DGALLIVM_USE_ORCJIT=0`) en sleutelt via `dladdr()` + `stat(dli_fname).st_mtime` (wat op Haiku via `dlfcn.c:78` ondersteund is).
  - `gallivm` (`lp_bld_passmgr.c:149-160`, `lp_bld_init.c:155-166`) draait standaard **twee** LLVM 21-pass-runs (`default<O0>` gevolgd door `sroa,early-cse,simplifycfg,reassociate,mem2reg,instsimplify,instcombine<no-verify-fixpoint>`) plus `CodeGenOpt::Default` (`-O2`) op *elke* shader-variant. Met `GALLIVM_PERF=no_opt` valt dat terug op alleen `mem2reg` + `CodeGenOpt::None`.
  - `SkiaOffscreen.cpp:176-209` compileert 80 pijplijncombinaties (2× opaque × 4 blend-modi × 10 primitieven) met 4× MSAA (`SkiaGpu.cpp:652-654`) **serieel op één draad** met `B_LOW_PRIORITY`.
  - `build-radv/build.ninja` bouwt Mesa zelf zonder `-march=x86-64-v2` (alleen baseline `x86-64` met handgeschreven `-DUSE_SSE41`-paden; de LLVM-JIT kiest wel AVX2 op runtime).
- **Voorstel.** (1) Eerst meten of `/boot/system/cache/app_server/mesa` na een `gpu false`-start daadwerkelijk gevuld is en of start 2 daalt. (2) Op Lavapipe `GALLIVM_PERF=no_opt` benchmarken (opwarmtijd vs. sleep-frametijd) naast `gpu_lavapipe_msaa 1`. (3) Overwegen om `-march=x86-64-v2` aan `distro/mesa/haiku-cross.txt` toe te voegen bij de volgende Mesa-rebuild (kleine winst, zelfde CPU-vloer als de rest van R5).
- **Regels.** Lavapipe en Skia blijven; Mesa houdt zijn eigen licentie.

**6. `r5_pstate_log` verbrandt 1,0 s lang 100% van één CPU-kern op elke start (Prestatie/Vermogen, Opstarten, AMD64).**
- **Wat.** Naast de vier `fsync`-passen uit sectie 4.9 draait `distro/cpu/r5_pstate_log.cpp:48-74` de functie `burn_cpu()`: `while (system_time() - start < 1000000) sink += 1;`. Dat verschijnt in `boot-5625u-68/syslog` als `R5 time check: busy thread wall=1000000 us user=999779 us ...`, tijdens het opkomen van de desktop, zonder `gpu_debug`.
- **Winst.** 1 volledige CPU-seconde minder belasting, warmte en accuverbruik bij elke boot (plus de 4× `fsync` uit #13). Zekerheid: **hoog**.
- **Voorstel.** `r5_pstate_log` alleen starten als een diagnostische instelling aan staat of op expliciet verzoek. Moeite: 1 sessie; risico laag.

**7. `PipelineStorage` gebruikt één bestandsnaam voor alle GPU-drivers, waardoor Lavapipe de RADV-cache wist (Bug en prestatie, app_server, alle doelen).**
- **Wat.** `distro/app_server/skia/SkiaGpu.cpp:291` gebruikt altijd `/boot/system/cache/app_server/skia-pipelines`. In `boot-5625u-65/syslog:10849` logde start 5 (RADV) `gpu draw pipeline cache ignored` direct nadat start 4 (`gpu false`, Lavapipe) het bestand had overschreven met Lavapipe's 312-byte kop.
- **Winst.** Voorkomt een koude RADV-opwarming (~2 s) na een enkele veilige/softwarestart, en voorkomt nutteloze schrijf- en hash-acties op Lavapipe. Zekerheid: **hoog**.
- **Voorstel.** Apparaat-/drivernaam of vendor/device-id in de bestandsnaam opnemen, `store()` overslaan als de checksum niet veranderd is, en op Lavapipe niet opslaan zolang `lvp_CreatePipelineCache` een stub is. Moeite: 1 sessie; risico laag.

**8. `Compositor::_ReadZeroCopySwitch()` opent elke 10 s het instellingenbestand en logt op de urgente compositordraad (Prestatie, app_server, alle doelen).**
- **Wat.** `distro/app_server/Compositor.cpp:6442-6454`, aangeroepen vanuit `_Report()` (regel 6540), roept elke 10 s `load_settings(options)` aan. Dat opent `/boot/home/config/settings/kernel/drivers/radeon_hd` van schijf en schrijft `app_server: radeon_hd settings ...` naar zowel `stderr` als `syslog` (`boot-5625u-65/syslog:11631`), óók als `gpu_debug` uit staat.
- **Voorstel.** Alleen uitvoeren als `fDebug` aan staat (of via `watch_node`), en alleen loggen bij verandering. Moeite: 1 sessie; risico laag.

**9. 32-bit DMA-limiet in `usb_disk` en `ahci` dwingt SCSI bounce-buffers af boven 4 GiB RAM (Prestatie, Opslag, AMD64/ARM64/RISC-V met > 4 GiB RAM).**
- **Wat.** `src/add-ons/kernel/drivers/disk/usb/usb_disk/usb_disk.cpp:999` zet hard `restrictions.high_address = UINT32_MAX;`. `src/add-ons/kernel/busses/scsi/ahci/ahci.c:280` zet hard `B_DMA_HIGH_ADDRESS = 0x100000000LL`, zelfs wanneer de AHCI-controller `Supports 64-bit Addressing: yes` meldt (`boot-5625u-68/syslog:502`). In `boot-5625u-68/syslog:523-565` leidt dit tot 60+ meldingen `SCSI -- is_sg_list_dma_safe: S/G-entry above high address @1001800c0`, en `usb_disk` krijgt `DMAResource: low/high 0/ffffffff` (terwijl `nvme_disk` wel 64-bit `0/ffffffffffffffff` gebruikt).
- **Winst.** Vermijdt kernel bounce-buffer-allocaties en extra `memcpy` voor USB- en SATA-I/O boven 4 GiB fysiek geheugen. Zekerheid: **hoog** dat het gebeurt, **laag tot middel** voor de totale doorvoersnelheidswinst op een USB-stick.
- **Voorstel.** In `ahci.c` 64-bit DMA aanzetten wanneer `CAP.S64A` gezet is; in `usb_disk` de 64-bit DMA-capaciteit van de USB-hostcontroller (xHCI `HCCPARAMS1.AC64`) opvragen. Moeite: 1–2 sessies; risico middel.

**10. Overige gecontroleerde punten (inclusief opgeloste open vragen):**
- **Twee fysieke xHCI-controllers:** `boot-5625u-68/r5-hardware.txt` (regels 65–67 en 85–87) bevestigt twee fysieke `1022:1639` Renoir/Cezanne xHCI-controllers. Controller 0 doet BIOS-handoff (`xhci.cpp:470`, `snooze(50000)`), start (`101 ms`) en somt zijn poorten synchroon op (`0,626 → 1,575 s`) vóór controller 1 start (`1,576 → 1,732 s`).
- **`launch_speedup`:** `src/add-ons/kernel/file_cache/Jamfile` definieert `KernelAddon launch_speedup`, maar geen enkele Jamfile onder `build/jam/` neemt hem op in het `haiku`-pakket (`generated.x86_64/objects/.../file_cache/` is leeg). Dat verklaart `Search for file_cache/launch_speedup/v1 failed` op elke start.
- **`Compositor::NoteInput()` meetfout (`Compositor.cpp:82-83, 2650-2652, 3131-3139`):** slaat een muisbeweging op die geen vensterschade veroorzaakt en schrijft daarna het eerstvolgende ongerelateerde frame (zoals een 250 ms cursor-blink) toe als invoerlatentie. Dat verklaart de vertekende waarden in stick 68 (`input latency median 60–200 ms, p95 ~248 ms`).
- **JavaScriptCore JIT & `libroot` malloc:** in `haikuwebkit-1.10.0-2` zijn 6438 DFG/FTL/JIT-symbolen aanwezig in `libJavaScriptCore.so.18.7.4` (JIT staat dus niet uit). In `src/system/libroot/posix/malloc/openbsd/malloc.c:550-564` heeft R5 `mopts.malloc_mutexes = r5_heap_pool_count()` al gezet en `def_malloc_junk` staat op Haiku al uit (`#ifndef __HAIKU__`).
- **Kleine 1 Hz-wakeups in rust:** `gpu_retire_thread` (`r5_gpu_core.cpp:635`) en `AirPlayServer.cpp:604` worden elke 1000 ms wakker, ook zonder lopende GPU-submits of AirPlay-sessies. `VBoxClip.cpp:76-80` probeert 30 × 200 ms (6 s) `pc_serial1` te openen en stopt daarna netjes op bare metal.
- **Vooruitblik stick 69/70 (`r5/stick-70`, `cs35l41`):** op stick 70 laadt `cs35l41` de firmware van amp 0 en amp 1 synchroon na elkaar over I2C van `[6.008]` tot `[8.692]` (**2,68 s extra opstarttijd**). Bij het landen van die driver loont het om het laden asynchroon (na boot-mount) of parallel te doen.

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
- een paginalading in WebPositive met een vaste lokale pagina, zodat een latere engine te vergelijken is;
- `unused_block_count` van de BFS-cache na een koude start en na een minuut bladermap;
- CPU van `Expando Window Watcher` en van de mt7922-ontvangstdraad over 30 s nietsdoen, met de interface open en dicht;
- een invoermethode-wijziging in Terminal, voor en na het weghalen van de 100 ms `snooze`;
- een zuivere `input latency`-meting (alleen muisbewegingen die direct schade veroorzaken, na het repareren van `Compositor::NoteInput()`);
- aantal timer-wakeups/s in rust (met `i2c_hid` op 1000 Hz vs. adaptieve backoff/IRQ);
- een vergelijking op Lavapipe met en zonder `GALLIVM_PERF=no_opt` (opwarmtijd vs. sleep-frametijd).

`m13-measure` leest een deel hiervan al uit de syslog. De afsluiting, de C-toestand en de pakketscan zitten er niet in.

## 6. Open vragen

Een QEMU-start is voor deze review niet gedraaid. De 42 seconden komen uit het changelog van 19 nov 2026, niet uit een start in deze sessie.

Opgelost in de tweede doorloop (sectie 4.11):
- **Waarom 221.596 geslaagde ioctls zonder `gpu_trace`:** `libdrm-2.4.124-4-x86_64.hpkg` bevat nog de oude `libdrm.so.2.124.0` zonder `gpu_trace`.
- **Tweede xHCI:** `r5-hardware.txt` bevestigt twee fysieke `1022:1639` xHCI-controllers.
- **`launch_speedup`:** staat wel in `src/add-ons/kernel/file_cache/Jamfile`, maar niet in een pakket-Jamfile onder `build/jam/`.

Nog niet nagelopen, en dus hier geen bevinding: de inhoud van WebKit zelf (buiten de controle dat JIT-symbolen aanwezig zijn), en elke berichtafhandelaar van de registrar buiten de wachten die in sectie 4.10 staan. Het framepad na de ring in `mtk_receive_frame` is niet regel voor regel gelezen; de lege-ringweg wel.

| Vraag | Wat hem beantwoordt |
|---|---|
| Hangt `sync()` of wordt de laptop eerder uitgezet? | Eén herstart laten uitlopen. De volgende syslog moet `sync_done` bevatten. De wandklok van het verzoek en van het donkere scherm erbij. |
| Maakt Bluetooth dat lang zonder headset? | Dezelfde herstart met de radio aan en uit, zonder gekoppeld apparaat, en één keer met een headset die SDP open laat. |
| Blijft de Mesa-cache op de stick, en daalt de Lavapipe-opwarming bij start 2? | `gpu false` twee keer op dezelfde stick, zonder het cachebestand te wissen. Grootte van de cache na beide starts. |
| Gaan de kernen C3 in na de mislukte `_OSC` (en na het temmen van `i2c_hid` 1000 Hz)? | Een residuteller, of een bestaande ACPI-teller, over 30 s nietsdoen. |
| Hoe traag is WebPositive in seconden? | Eén lokale pagina, koude en warme start. Geen enginewissel in die meting. |
| Hoe snel is een start van de interne schijf (met en zonder de AHCI-fix)? | Eén installatie, dezelfde tijdlijn als sectie 4.4. De 3,03 s van AHCI telt daar net zo hard mee. |

Niet doen naar aanleiding van deze review: de GFX9-ring, de PSP, de PTE-vlaggen, de reset, de stop bij afsluiten, de Settings-indeling, de Haiku-ABI, of Skia uit als standaard.
