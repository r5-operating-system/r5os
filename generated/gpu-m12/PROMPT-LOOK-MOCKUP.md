Opdracht voor de mockup van de look and feel-instellingen. Mag nu al, naast de M11-stick en naast M12 stap 1: alleen een mockup, geen code, geen jam, geen QEMU. Kopieer alles onder de streep.

---

Opdracht: een mockup voor het redesign van de look and feel-instellingen, in het Settings-venster én in het losse Appearance-programma (`docs/APP-SERVER-GPU-DRAWING.md`, "Follow-up: the look and feel settings"). Alleen de mockup; ik beoordeel hem voordat er code komt.

Eerst lezen: `AGENTS.md` helemaal (vooral "Settings window"); `docs/SETTINGS.md`; `docs/STYLE.md`; `docs/APPEARANCE-KIT.md`; `docs/design/settings-mockup/README.md` en de HTML daar (stijl en opbouw); in `docs/APP-SERVER-GPU-DRAWING.md` "Decided after step 0" (Q5, Q12, Q13) en "Scaling later"; de huidige bron in `distro/settings/` (`LookModule.cpp`, `FontsModule.cpp`, `LookAndFeelSettingsView.cpp`, `WindowEffectsBox.cpp`, `AntialiasingSettingsView.cpp`, `FontView.cpp`, `ColorsView.cpp`). Zet je plan en de regels die gelden (hoogstens vijf regels) bovenaan je eerste antwoord. Tak `claude/look-mockup` vanaf `origin/main`.

**Eerst één vraag aan mij, vóór je tekent:** `AGENTS.md` beschrijft één Appearance-pagina met de tabs Fonts, Colors, Look and feel en Antialiasing; `docs/SETTINGS.md` beschrijft twee pagina's, Fonts en Look and Feel, zonder tabs. Kijk in de bron wat er nu echt gebouwd is, en vraag mij welke van de twee de basis is. Wacht op mijn antwoord.

**De mockup** in `docs/design/look-mockup/` (HTML die zonder server opent, plus PNG's op 1190 bij 770, zoals de Settings-mockup), met een `README.md` die zegt wat elke pagina beslist:
- alles wat er nu is: lettertypen, kleuren, decorator, control look, pijlstijl, het vak "Window effects" (schaduw, frosted glass, sterkte, animaties);
- tekst na M12: Skia's subpixeltekst, en de schuif "average weight" die verdwijnt als de GPU de tekst tekent (Q13), dus twee versies van die plek;
- het losse Appearance-programma: dezelfde inhoud, in zijn eigen venster;
- scaling hoort volgens de Settings-mockup al op de Display-pagina ("Scale"); teken het niet opnieuw, verwijs ernaar.

**Algemeen:**
- De vaste Settings-indeling uit `AGENTS.md` (lijst, Search, onderste rij, de vijf beschrijvingen, volgorde) blijft zoals hij is.
- Echte Haiku-iconen, met de sleutel van `r5_settings_apply_prefs_overlay()` waar die hoort; geen zelfgetekende symbolen.
- Geen code in `distro/`. Wat de mockup nodig heeft van `libr5ui` of `libr5icons`, schrijf je in de README als voorstel.
- Commit lokaal. Niet: pushen, een PR openen, mergen.
- Eindig met: Veranderd / Onbewezen / Documentatie, en de keuzes die ik moet maken, elk met opties en jouw advies, in gewone woorden.
