# M11 pre-stick-check op papier (14 nov 2026)

Tak `claude/m11-stick` vanaf `origin/main` `9729b942c2`. Elke boot, elke stap: welke instelling, welk programma, welke regel, en waar die regel ontstaat. `scripts/check-pre-stick.py --paper` controleert hetzelfde mechanisch (35 regels met een bron, de instellingen per boot in een zandbak).

## Instellingen per boot (`boot-next`)

| Boot | Bestand `~/config/settings/kernel/drivers/radeon_hd` | Hoe erin |
|---|---|---|
| 1 | `gpu_compositor true`, `gpu_debug true`, `gpu_selftest true` | geplant in het image (`distro/graphics/boot/radeon_hd`, `plant-m10-stick.sh`) |
| 2 | boot 1 + `gpu_compositor_fail true` | `boot-next` na boot 1 |
| 3 | boot 1 + `gpu_compositor_lavapipe true` | `boot-next` na boot 2 |
| 4 | boot 1 + `gpu_hang_test 1` | `boot-next` na boot 3 |
| daarna | geen bestand | `boot-next` na boot 4 |

`gpu_selftest true` staat niet in de brief (die zegt "alleen `gpu_compositor true` en `gpu_debug true`"), maar zonder dat start Login de self-test niet (`distro/login/LoginApp.cpp:615`), start `gpu_hang_test` nooit (boot 4, `r5_gpu_core.cpp:2770`) en print de kernel de `bo ... snooped`-regel niet (`r5_gpu_core.cpp:794`). Zie "keuzes" in het rapport.

De R5-decorator: een vers image heeft geen `decorator_settings`, dan laadt app_server R5 (`DecorManager`, CHANGELOG stap 1). Regel `decorator: r5 loaded` (`R5Decorator.cpp:391`).

## Boot 1 (RADV, alles aan)

| Stap | Programma | Regel | Bron |
|---|---|---|---|
| Sign in | Login, app_server | `radeon_hd: gpu available` | `r5_gpu_core.cpp` |
| | | `app_server: gpu compositor on radv (<naam>)` | `Compositor.cpp` `Create` |
| | | `decorator: r5 loaded` | `R5Decorator.cpp:391` |
| eerste venster | app_server | `app_server: window memory type <i> flags <hex>` (bit 0x8 = cached) | `Compositor.cpp` `_AllocateBuffer` |
| | | `app_server: window buffer write fill … malloc …` (nieuw) | `Compositor.cpp` `log_write_test` |
| self-test | r5-gpu-selftest | `selftest done`; kernel `radeon_hd: bo domain … snooped 1` | `r5-gpu-selftest.cpp:518`, `r5_gpu_core.cpp:794` |
| `check wait` | check | wacht op `selftest done` | `check` |
| slepen 20 s | Tracker | `compose median <n> us cpu <n> ms per 10 s` elke 10 s, `team cpu <n> ms per 10 s` (nieuw), `window memory <n> MB` | `Compositor.cpp` `_Report` |
| `m11-scroll` 20 s | Terminal | idem | |
| `m11-vulkan` 20 s | VulkanTeapot, vkcube | `app_server: window <id> zero-copy on` ×2 (nieuw) | `Compositor.cpp` `_ShareLogOn` |
| Schaduw en Glas uit, 20 s slepen, weer aan | Settings, Appearance | `app_server: shadow off/on`, `frosted off/on`; een median met `compose effects shadow off frosted off`; `compose compare ok` (alleen zonder effecten, elke 100e frame) | `ServerApp.cpp:144`, `Compositor.cpp` |
| Terminal 5× open/dicht, 5× werkblad, Animaties uit/aan | Terminal, Deskbar | `animation open/close/workspace <n> ms frames <n>`, `animations off/on` | `Compositor.cpp:3142…`, `ServerApp.cpp:144` |
| Chart starten en sluiten | Chart (`BDirectWindow`) | `compositor paused: direct window`, `compositor resumed` | `Compositor.cpp` |
| `check` | r5-share-check | de drie gevallen `…: pass`; `buffers <n>` voor en na een gestopte exporteur | `r5-share-check.cpp` |
| | VulkanTeapot `--seconds 30` (nieuw), zero-copy | `window fps <x> …`; `zero-copy on`; `bo_count` voor, tijdens, na | `VulkanWindow.cpp`, `r5-share-check count` |
| | check zet `gpu_compositor_zero_copy false` (nieuw) | `zero-copy setting off` binnen 10 s; VulkanTeapot opnieuw: `zero-copy off: setting`, FPS, `team cpu`; dan terug: `zero-copy setting on` | `Compositor.cpp` `_Report`, `ShareImages` |
| | | nergens `gpu compositor off`, `MISMATCH`, of `zero-copy off` anders dan `setting` | |

## Boot 2 (geforceerde fout)

`gpu_compositor_fail true`: de eerste submit faalt (`Compositor.cpp:2301`). Regels: `gpu compositor on radv`, daarna precies één `gpu compositor off: forced failure`. Hetzelfde scenario in de CPU-modus; R5-frame zonder schaduw. Met de hand: de desktop blijft werken.

## Boot 3 (Lavapipe op de 5625U)

`gpu_compositor_lavapipe true` (`Compositor.cpp:572`): `gpu compositor on lavapipe: forced`; `compose compare ok`, nooit `MISMATCH`, median ×3 met en zonder effecten; `zero-copy off: lavapipe`; geen `gpu compositor off`. `check` draait VulkanTeapot één keer voor de FPS op Lavapipe.

## Boot 4 (hang tijdens de compose)

`gpu_hang_test 1` start na de laatste oproep van de self-test (`r5_gpu_core.cpp:2770`), dus het scenario begint meteen na Sign in, zonder `check wait`. Regels: precies één `radeon_hd: gpu hang ring`, `gpu reset mode2 ok`, `gpu selftest after reset ok`, `app_server: gpu compositor restarted after reset`, geen `gpu compositor off`, geen `gpu off until reboot`. Met de hand: de aanwijzer bewoog de hele tijd.

## In het image (te controleren met `check-pre-stick.py <image>`)

`r5-share-check`, `r5-effects-test` (`bin`, haiku-pakket), Chart (`demos`), de R5-decorator (`add-ons/decorators`), VulkanTeapot (`vulkanteapot-1.0.0-5`), `vkcube` (`vulkan_tools-1.4.367-2`), `check`, `boot-next`, `check-user`, `m11-vulkan`, `m11-scroll` (`mesa_radv-25.3.6-9`, `/boot/system/bin`). Strings in app_server en VulkanTeapot tegen een oudere build.

## Niet periodiek, één keer

Geen sync-lus: `check` en `check-user` doen één `sync` aan het eind (zoals M10). `boot-next` alleen de hartslag na `y`, die M10 al had voor de afsluittijd. De self-test start één keer per boot (`LoginApp.cpp:612`).

## Stoppunten

- Geen. Wel open (niet in deze stick): de scanout via Vulkan (`gpu_compositor_scanout true`, de rij "the two scanout ways" in `docs/APP-SERVER-COMPOSITOR.md`) staat niet in de brief-boots en wordt dus niet gemeten.
