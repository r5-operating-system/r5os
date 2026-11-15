Opdracht voor een onafhankelijke review van de hele M11-stack, alleen lezen. Kopieer alles onder de streep.

---

Je reviewt code van R5 Operating System (R5), een onafhankelijk besturingssysteem dat op Haiku is gebaseerd. Je mag alleen lezen en reviewen. Je schrijft niets.

## Wat je niet doet

- Geen bestanden wijzigen, aanmaken of verwijderen. Ook geen notities of rapporten in de repository.
- Geen git-commando dat iets verandert: geen commit, checkout, switch, stash, reset, branch, merge, rebase, fetch of push. Lezen mag wel: `git log`, `git show`, `git diff`, `git blame` en `git grep`, vanuit Windows (PowerShell of Git Bash). Gebruik nooit git vanuit WSL op deze checkout.
- Niets bouwen of draaien: geen jam, geen scripts uit `scripts/`, geen QEMU of VirtualBox. Schrijf geen USB-stick.
- Geen GitHub-acties: geen issues, comments of PR's.

Je antwoord komt alleen in de chat.

## Eerst lezen

1. `AGENTS.md` in de root. Daar staan de regels van het project: licenties en SPDX-Snippet-tags, de libbe-ABI, de GFX9-volgorde die niet mag veranderen, SMAP en de stick-regels.
2. `docs/GPU-ARCHITECTURE.md` en `docs/APP-SERVER-COMPOSITOR.md`. Daarin staat wat de code hoort te doen.
3. `generated/gpu-m11/BRIEF.md`. Dat is de opdracht van M11.
4. `generated/gpu-m11/STICK-CARD.md`. Dat is het testplan per boot op de Ryzen 5 5625U.

Lees de eerdere reviewlijsten (`generated/gpu-m11/REVIEW-STACK*.md`) pas **na** je eigen review, en dan alleen om dubbele punten te markeren. Neem niets over uit commitberichten, `CHANGELOG.md` of die lijsten zonder het zelf in de code na te gaan. Als daar staat dat iets is opgelost of getest, controleer dat dan; geloof het niet op hun woord.

## De stack

- Repository: `C:\Users\nickr\Projects\R5OS`, branch `claude/m11-stick`.
- Te reviewen commit: `5b0b19bb03`. Commits erna wijzigen alleen documentatie en het opnieuw ingepakte `mesa_radv-25.3.6-9`-pakket.
- Basis: `7144867f45`, de commit vóór M11 stap 0 (`c25f47408a`). Stap 0 tot en met 5 zitten in `origin/main` (laatste merge `9729b942c2`, #4261). Stap 6 en de fixes erna staan op `claude/m11-stick` (`git log --oneline 9729b942c2..5b0b19bb03`).
- De wijzigingen zie je met:
  `git diff 7144867f45 5b0b19bb03 -- <pad>`
- Paden, per deel:
  1. **Compositor in app_server:** `distro/app_server/` (Compositor, WindowBuffer, WindowHWInterface, GPUCopy, AccelerantHWInterface, `compositor/*.comp`).
  2. **Delen van Vulkan-vensterbeelden zonder kopie:**
     - `distro/graphics/headers/private/r5gpu/r5_share.h`
     - `distro/mesa/wsi/wsi_common_haiku.cpp`
     - `distro/rest/src/servers/app/ServerApp.cpp`
     - `distro/rest/headers/private/app/ServerProtocol.h`
     - `distro/graphics/src/bin/r5-share-check/`
     - `scripts/test-r5-share.cpp`
  3. **Kernel:**
     - `distro/graphics/src/add-ons/kernel/drivers/graphics/r5gpu/`
     - `distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/` (vooral `r5_gpu.cpp`, `driver.cpp` en `gfx9_k12.cpp`)
     - `distro/graphics/headers/private/r5gpu/`
     - `distro/kernel/src/system/kernel/vm/vm.cpp`
     - de user-kant in `distro/graphics/libdrm-r5/`
  4. **Hoe app_server de compositor gebruikt:**
     - `distro/desktop/src/servers/app/`: Desktop, Window, ServerWindow, View, DesktopSettings, `WindowEffectLevels.h` en `decorator/`
     - `distro/desktop/src/add-ons/decorators/`
  5. **Vensterinstellingen (UI):**
     - `distro/ui/WindowEffects.*`
     - `distro/settings/WindowEffectsBox.*` en `distro/settings/LookAndFeelSettingsView.*`
     - `distro/desktop/src/preferences/appearance/`
     - `distro/desktop/src/kits/interface/DecorInfo.cpp`
  6. **Test-apps:**
     - `distro/apps/vulkanteapot/`
     - `distro/graphics/src/bin/r5-effects-test/`
     - `distro/graphics/src/bin/r5-gpu-selftest/`
  7. **Stick-pad:**
     - `distro/graphics/boot/` (`check`, `check-user`, `boot-next`, `m11-scroll`, `m11-vulkan`, `radeon_hd`)
     - `scripts/plant-m10-stick.sh`, `scripts/check-pre-stick.py`, `scripts/read-stick-logs.py`, `scripts/write-stick.py`
     - `scripts/pin-mesa-packages.sh`, `scripts/install-mesa-packages.sh`
     - `distro/mesa/pack-radv.py`
- Ter vergelijking staat het originele Haiku onder `haiku/`. Als iets daar niet staat, gebruik dan `git show 7144867f45:<pad>`. R5-regels in Haiku-bestanden horen tussen SPDX-Snippet-tags te staan.

## Waar je naar kijkt

Zoek echte fouten. Stijl en wensen tellen niet. Bijvoorbeeld:

- races en lockvolgorde tussen threads, use-after-free en lekken (Vulkan-objecten, GPU-geheugen, areas en semaforen);
- verkeerde toestand na een pauze, een hervatting, een GPU-reset, een moduswissel of het sluiten van een venster of programma;
- invoer van een niet-vertrouwd programma: ioctl's, berichtvelden, groottes en overflow;
- SMAP (user-pointers alleen via `user_memcpy`) en wat een programma met zijn eigen areas kan doen (fork, clone, resize, delete, transfer);
- foutpaden die iets half laten staan;
- de libbe-ABI en de ABI van decorator-add-ons;
- licentie- en SPDX-fouten;
- in het stick-pad: een stap op de kaart die zo niet kan werken, een controle die ten onrechte slaagt of faalt, een resultaat van een eerdere boot, en aannames over Haiku's shell en tools (bash, coreutils, geen `/proc`, syslog-rotatie).

Stel geen wijziging voor aan de GFX9-ringvolgorde, de registers, de PSP, de firmware of het hangbeleid. Zie je daar iets mis, meld het dan als "beleidsgebied".

## Wat je oplevert

Maximaal 25 punten, het ernstigste eerst. Per punt geef je:

- `bestand:regel`;
- wat er misgaat, in een concreet scenario;
- **GECONTROLEERD** (je hebt het hele pad gelezen) of **AANNEMELIJK** (je redenering, niet helemaal nagegaan);
- de ernst: crash of corruptie, fout gedrag, lek, of klein;
- een fix van één regel.

Daarna, kort:

- welke delen je niet of maar half hebt gelezen;
- welke punten na je eigen review al in `REVIEW-STACK*.md` bleken te staan, en of de fix daar volgens jou klopt.

Je hebt niets gebouwd, gedraaid of geboot. Noem dus niets "getest". QEMU heeft geen radeon_hd. Wat alleen op de 5625U te zien is, blijft aannemelijk.
