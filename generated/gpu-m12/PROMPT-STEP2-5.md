Opdracht voor M12 stap 2 tot en met 5: de GPU tekent de vensters. Stap 1 (het Skia-recept, #4275) zit in `main`. Eén PR met een commit per stap. Mag als nachtklus. Kopieer alles onder de streep.

---

Opdracht: M12 stap 2, 3, 4 en 5 uit `generated/gpu-m12/BRIEF.md`. Ik ben er misschien niet; wacht niet op mij, behalve bij "Niet" hieronder.

Eerst lezen: `AGENTS.md` helemaal; de M12-brief (de "Rules for the agent", besluit 1–9, stap 2–5, de lessen); in `docs/APP-SERVER-GPU-DRAWING.md` deel 2 tot en met 7, "Decided after step 0" (vooral Q3, Q4, Q5, Q12 en Q13) en "Scaling later: what M12 must not block"; `distro/skia/COMPAT.md` en de stap 1-regel in `CHANGELOG.md`; `docs/APP-SERVER-COMPOSITOR.md` voor de vensterbuffers. Zet je plan en de regels die gelden (hoogstens vijf regels) bovenaan je eerste antwoord; niet wachten op "ga". Eigen worktree onder `.claude/worktrees/`, tak `claude/m12-gpu-draw` vanaf `origin/main`, met de LF-checkout van `distro` en `scripts`.

**Stap 2: de vergelijkingsset** `r5-draw-compare` (MIT), zoals deel 7: elke opdracht uit de vertaaltabel van deel 4, getekend door Painter en door `SkiaDrawingEngine`, vergeleken met de toleranties van deel 5. Eén regel per opdracht, `draw-compare <op> ok` of `FAIL` met de afwijking, en de samenvatting `draw-compare <n>/<n> ok`. Ook de optie `--scale <f>` uit "Scaling later" (geen slaagregel).

**Stap 3: `SkiaDrawingEngine` deel 1** achter `DrawingEngine`, met Graphite op dezelfde VkDevice en wachtrij als de compositor: vlakken, rechthoeken, lijnen, ronde rechthoeken, ellipsen, bogen, bitmaps (geschaald, gefilterd), clipping, alle `B_OP_*`. Gebogen vormen met 4× MSAA (Q3). De schaalfactor per venster uit "Scaling later" punt 1–4, altijd 1. Achter `gpu_draw true` (radeon_hd-instellingen, standaard uit): de compositor blijft standaard aan, de GPU-tekening niet, tot de stick.

**Stap 4: deel 2:** veelhoeken, Bézier-krommen, `BShape`, de vijf verlopen, `BAffineTransform`, `BAlphaMask`, en **tekst**: Skia's eigen subpixeltekst (Q5), vormen grijs (Q12), metrics van `ServerFont` (besluit 5). En Q13: de schuif "average weight" verdwijnt in `AntialiasingSettingsView.cpp` als Skia de tekst tekent, via een nieuwe aanroep aan het eind van `libr5ui`, beschreven in `docs/LIBR5.md`. Zonder `gpu_draw` blijft de schuif.

**Stap 5:** offscreen-`BBitmap`s met views en `Sync()` (besluit 7, de testprogramma's uit deel 6); de terugval van besluit 8 met `gpu_draw_fail true`: precies één `app_server: gpu draw off: forced failure`, het bureaublad werkt door. Forceer hem echt in QEMU; een terugval die nooit draaide, telt niet. En de meting van besluit 9, opnieuw en rustig (stap 1 mat één run op 2 virtuele kernen tijdens het opstarten): minstens drie runs, nadat het bureaublad klaar is, met de spreiding: `app_server: draw median <n> us cpu <n> ms per 10 s`, met en zonder `gpu_draw`, het M11-scenario 60 s, in `CHANGELOG.md`, met de opdrachten die trager zijn (voor M13).

**Testen in QEMU (Lavapipe)**, met `gpu_draw true` en `gpu_debug true`: `r5-draw-compare` helemaal `ok`; Tracker, Terminal, Settings (elk tabblad van Appearance), About R5 en WebPositive op een lange pagina zien er goed uit (screenshots via de monitor-socket); `compose compare ok` met effecten uit; een boot zonder `gpu_draw`: Painter zoals nu. RADV op de 5625U is "Onbewezen".

**Algemeen:**
- Nooit "deze ene opdracht op de CPU" (besluit 1). Een gat blokkeert de GPU-tekening: schrijf het op en ga door met de rest.
- Jammen en QEMU alleen als geen andere sessie in de WSL-boom jamt (`pgrep -x jam`); anders wachten en later opnieuw, nooit tegelijk. Jouw QEMU op een eigen kopie van het image; nooit `run-qemu-r5.sh` (die stopt elke QEMU). Geen `git` vanuit WSL op de checkout.
- Commit per stap, lokaal. Reviewpunten die je zelf vindt: meteen fixen, ook de kleine.
- Bij een conflict met de brief, het ontwerp of `AGENTS.md`, of een test die twee keer faalt: stop met dat deel, schrijf het op, ga door met wat er niet van afhangt.
- Niet: pushen, een PR openen, mergen, een stick schrijven, de standaard van `gpu_draw` aanzetten, `AGENTS.md` aanpassen, of iets buiten de repository wijzigen.
- Eindig met: Veranderd / Getest (de regels letterlijk) / Onbewezen / Documentatie, de meting, en een lijst "keuzes die ik voor je maakte".
