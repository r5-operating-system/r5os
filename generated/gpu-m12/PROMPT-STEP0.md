Opdracht voor M12 stap 0. Geef hem pas als #4263 (de M12-brief) in `main` zit. Hij mag naast de M11-stick lopen: geen jam, geen QEMU. Kopieer alles onder de streep.

---

Opdracht: M12 stap 0 uit `generated/gpu-m12/BRIEF.md`, het ontwerp op papier. Alleen documentatie, geen code. Wacht niet op mij, behalve bij "Niet" hieronder.

Eerst lezen: `AGENTS.md` helemaal; de M12-brief helemaal (de "Rules for the agent", besluit 1–10, de stappen en de stick); `generated/gpu-m12/SAFE-MODE.md`; `docs/APP-SERVER-COMPOSITOR.md`; in `docs/GPU-ARCHITECTURE.md` "2D in app_server", "Default and fallback" en "Release mechanism"; `docs/LICENSING.md`. Zet je plan en de regels die gelden bovenaan je rapport; niet wachten op "ga". Tak `claude/m12-design` vanaf `origin/main`.

**Schrijf `docs/APP-SERVER-GPU-DRAWING.md` met deze delen:**
1. **Skia, het rapport (besluit 2):** project, SPDX, copyright, de exacte versie (een vaste release-tak, met de reden), welke meegeleverde bibliotheken we bouwen en welke niet, of ICU nodig is, wat we moeten doen voor de broncode, en wat er samengaat met GPL-3.0 en MIT. Geen copyrighthouders verzinnen: alleen wat in Skia's eigen bestanden staat, met het pad.
2. **Ganesh of Graphite:** een keuze met de reden. Kijk naar de Vulkan-eisen van elk tegen wat RADV en Lavapipe op R5 bieden, hoe stabiel ze zijn in die versie, en hoe ze delen met de VkDevice en de wachtrij van de compositor (besluit 4).
3. **Statisch of een eigen verborgen bibliotheek (besluit 3),** met de reden.
4. **De vertaaltabel:** elke aanroep van `DrawingEngine` en elke functie van Painter, één regel per stuk, met de Skia-tegenhanger. Lees ze uit de bron (`haiku/src/servers/app/drawing/` en de R5-kopieën in `distro/`), niet uit het geheugen. Waar Skia er geen heeft, de runtime effect of blender die het wordt. Een gat dat je niet kunt vullen, zet je als open vraag; nooit "op de CPU".
5. **De toleranties per soort (besluit 6),** met getallen.
6. **Tekst (besluit 5)** en **offscreen-bitmaps en `Sync()` (besluit 7),** met de testprogramma's die bitmaps lezen zonder `Sync()`. Zoek eerst bestaande programma's in de Haiku-boom.
7. **De vergelijkingsset en de meting:** wat `r5-draw-compare` tekent, en het scenario voor besluit 9.
8. **Safe mode (besluit 10):**
   - hoe app_server de bootloader laat weten dat de start mislukte: het merkteken, waar, wie het weghaalt;
   - het bootmenu met Safe mode gekozen;
   - de overstap van RADV naar Lavapipe midden in een sessie, met de lock-volgorde uit M11;
   - wat Safe mode uitzet.
   Lees de Haiku-bootloader en zijn safe mode-opties in de bron. Zoek eerst wat Haiku al heeft (bestaand eerst).
9. **Of `BDirectWindow` en `BWindowScreen` een eigen buffer kunnen krijgen (besluit 10):** wel of niet eenvoudig, met de reden. Alleen een advies; dit wordt niet gebouwd zonder mijn ja.
10. **De wijzigingen** in `docs/GPU-ARCHITECTURE.md` en `THIRD_PARTY_NOTICES.md`, en de voorgestelde tekst voor `AGENTS.md` (Safe mode). `AGENTS.md` zelf pas je niet aan.

Eindig het document met **"Open questions for the user"**, elk met de opties en jouw advies, in gewone woorden.

**Algemeen:**
- Skia mag je lezen: de broncode van de gekozen versie, in een eigen map buiten de repository, alleen lezen, niet bouwen. Niets van Skia in de repository; dat is stap 1, na mijn ja.
- Ook air/OS mag je lezen, maar niets overnemen (de licentie van hun recept is onduidelijk).
- Geen `git` vanuit WSL op de checkout. Niet jammen: de M11-stick gebruikt de WSL-boom.
- Commit per deel, lokaal.
- Bij een conflict met de brief of `AGENTS.md`: schrijf het op als open vraag en ga door met de rest.
- Niet: pushen, een PR openen, mergen, of iets buiten de repository wijzigen (behalve de leesmap voor Skia).
- Eindig met: Veranderd / Getest / Onbewezen / Documentatie, de open vragen kort, en een lijst "keuzes die ik voor je maakte".
