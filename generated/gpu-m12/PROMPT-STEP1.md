Opdracht voor M12 stap 1. Geef hem pas als de M11-stick geslaagd is (de brief: "Start after the M11 stick has passed"), of als jij zegt dat hij eerder mag. Hij bouwt in WSL, maar jamt de gedeelde Haiku-boom pas na jouw ja. Kopieer alles onder de streep.

---

Opdracht: M12 stap 1 uit `generated/gpu-m12/BRIEF.md`: het Skia-recept. Eén stap, niets anders.

Eerst lezen: `AGENTS.md` helemaal; de M12-brief (de "Rules for the agent", besluit 2 en 3, stap 1); in `docs/APP-SERVER-GPU-DRAWING.md` deel 1, 2, 3 en 10 en "Decided after step 0"; in `docs/GPU-ARCHITECTURE.md` "Release mechanism"; `distro/mesa/build-radv.sh` en `distro/mesa/COMPAT.md` als voorbeeld van een R5-recept. Zet je plan en de regels die gelden (hoogstens vijf regels) bovenaan je eerste antwoord; niet wachten op "ga". Tak `claude/m12-skia` vanaf `origin/main`.

**Bouwen:**
1. **Het recept** in `distro/skia/` (MIT), zoals het Mesa-recept: Skia `chrome/m158`, commit `707e33875fb45f2997a4d781128bec122fb777b4` (controleer eerst op Chrome's releasepagina dat m158 de stabiele tak is; zo niet: stop en vraag). De `gn`-argumenten uit deel 1. Cross-compiler `x86_64-unknown-haiku-gcc` 13.3.0, dezelfde als app_server. R5's eigen FreeType. Uitvoer: `libskia.a` met de headers, als pakket dat alleen bij het bouwen gebruikt wordt (deel 3). Bouwmap in WSL buiten de Haiku-boom.
2. **De hosttest:** een klein programma (MIT) dat met Skia en Graphite op Lavapipe één typisch frame tekent (deel 7: 800×600, tab, 30 regels tekst, 10 rechthoeken, een verloop, twee bitmaps, een rij ronde knoppen) en `skia: <version> ok lavapipe frame <n> us` print. Draaien in QEMU of VirtualBox.
3. **Twee controles voor later:** of Lavapipe (Mesa 25.3.6) `VK_EXT_external_memory_host` heeft (nodig voor stap 6b), en wat RADV en Lavapipe melden voor de Vulkan-versie en `dualSrcBlend`. Alleen opschrijven.
4. **Meten:** de grootte van `libskia.a` en hoeveel app_server zou groeien.

**Documentatie in dezelfde PR:** de Skia-regel en "Updating Skia, step by step" in `docs/GPU-ARCHITECTURE.md`, de Licensing-zin daar, het Skia-blok in `THIRD_PARTY_NOTICES.md` en in `distro/about/licenses.txt`, en de licentieteksten van Skia, skcms en VMA erbij. Alles zoals voorgesteld in deel 10. `CHANGELOG.md` met wat er gebouwd en gemeten is.

**Algemeen:**
- Geen Skia-header buiten `distro/skia/` en de hosttest. Nog niets in app_server; dat is stap 2–5.
- arm64 en riscv64 bouw je niet; schrijf op dat ze niet gebouwd zijn.
- Lukt Skia niet met GCC 13.3: stop en meld wat er misgaat, met de foutregels. Geen andere compiler zonder mijn ja.
- Geen `git` vanuit WSL op de checkout. Vraag vóór je de gedeelde WSL Haiku-boom jamt.
- Commit per onderdeel, lokaal. Niet: pushen, een PR openen, mergen.
- Eindig met: Veranderd / Getest (met de regels letterlijk) / Onbewezen / Documentatie, en "Gezien, niet gedaan".
