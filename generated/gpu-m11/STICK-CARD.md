# M11-stick, kaart per boot (5625U)

Vier boots. Elke boot: start van de stick, Sign in als `user`. Typen doe je in Terminal (Deskbar → Applications → Terminal). Na elke opdracht: Enter.

Commando's: `check wait`, `m11-scroll`, `m11-vulkan`, `check`, `boot-next`.

---

## M11-boot1 (GPU, alles aan)

1. Sign in. Open Terminal, typ `check wait`. Wacht tot er staat: *start the scenario now*.
2. **Slepen, 20 s:** sleep een Tracker-venster in rondjes over andere vensters.
3. **Scrollen, 20 s:** typ `m11-scroll`. Scroll de lijst 20 s op en neer.
4. **Vulkan, 20 s:** typ `m11-vulkan`. Zet de theepot en de kubus naast elkaar. Na 20 s beide vensters sluiten.
5. **Effecten:** Settings → Appearance → Look and feel.
   - Zet **Shadow** uit en **Frosted glass** uit.
   - Sleep 20 s een venster rond.
   - Zet **Shadow** aan en **Frosted glass** aan.
6. **Animaties:** open en sluit Terminal 5 keer. Wissel 5 keer van werkblad (Alt+F1, Alt+F2). Zet **Animations** uit en weer aan.
7. **Chart:** Deskbar → Demos → Chart. Zet **Display** (bovenaan) op **DirectWindow**. 10 s laten lopen, dan sluiten.
8. **Foto** van het scherm (GPU-modus, schaduw en glas zichtbaar).
9. In Terminal: typ `check`. Duurt ca. 5 minuten; er gaan twee keer vanzelf theepot-vensters open en dicht. Niets aanraken. Noteer de regels met FAIL.
10. Typ `boot-next`, dan `y`.

## M11-boot2 (geforceerde fout → CPU-modus)

1. Sign in. Typ `check wait`.
2. Stap 2 t/m 7 van boot 1 (zelfde scenario).
3. **Foto** van het scherm (CPU-modus: R5-frame, geen schaduw).
4. Typ `check`. Noteer FAIL-regels. Klopte het: de desktop bleef werken?
5. Typ `boot-next`, dan `y`.

## M11-boot3 (Lavapipe op de 5625U)

1. Sign in. Typ `check wait`.
2. Stap 2 t/m 7 van boot 1.
3. Typ `check` (één theepot-venster gaat vanzelf open en dicht). Noteer FAIL-regels.
4. Typ `boot-next`, dan `y`.

## M11-boot4 (een GPU-hang tijdens het werk)

1. Sign in. **Niet** `check wait`: begin meteen.
2. Stap 2 t/m 7 van boot 1. De hang komt pas als de self-test klaar is (kan 15 minuten duren). Het beeld staat dan tot 10 s stil. Was er nog geen stilstand na stap 7? Blijf een venster rondslepen tot het beeld één keer stilstond en weer verder ging, en sleep dan nog 20 s door. Na 15 minuten zonder stilstand: ga door naar stap 3. Let op: **bleef de aanwijzer bewegen?**
3. Typ `check`. Noteer FAIL-regels en of de aanwijzer bleef bewegen.
4. Typ `boot-next`, dan `y`. Daarna staat er geen testinstelling meer op de machine.

---

Na boot 4: stick terug in de pc, dan `py scripts/read-stick-logs.py`. De bestanden staan op de stick in `~/m11/boot1` … `boot4`.

Faalt iets dat de desktop laat vastlopen (aanwijzer staat stil)? Houd de aan-knop ingedrukt. Start opnieuw (dezelfde boot), typ `check`, dan `boot-next` en `y`. Bij boot 1: typ na het opnieuw starten eerst `check wait` en doe het scenario nog een keer, dan pas `check`. Schrijf op bij welke stap het vastliep. Heb je `check` zelf afgebroken (Ctrl+C of Terminal dicht)? Doe dan hetzelfde: `check wait`, het scenario, `check`.
