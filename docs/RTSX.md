# Realtek card readers (rtsx)

`busses/mmc/rtsx` drives the Realtek PCIe SD card readers. It publishes Haiku's
`mmc_bus_interface`, so `mmc_bus` finds the card and `mmc_disk` publishes it
under `/dev/disk/mmc/`, as with `sdhci`.

Source: `distro/kernel/src/add-ons/kernel/busses/mmc/rtsx`, copied into the
tree by `scripts/apply-kernel-bugs.py` (`install_rtsx`). Installed in the
system package, so both the regular and the minimum image have it, and as a
boot module next to `sdhci`.

## Parts

| File | License | What |
|---|---|---|
| `rtsx.cpp`, `rtsxreg.h`, `rtsxvar.h` | ISC (OpenBSD), R5 snippets MIT | OpenBSD rtsx(4) chip logic: registers, host command queue, ADMA, power, clock, interrupt |
| `rtsx_5228.cpp`, `rtsx_5228.h` | MIT | RTS5228: init, slot power, clock and SSC, signal voltage, overcurrent, shutdown. Clean room from `RTS5228-SPEC.md` (`docs/LICENSING.md`) |
| `rtsx_haiku.cpp`, `rtsx.h` | MIT | The device table, the device_manager modules, `mmc_bus_interface`, the card thread, the shutdown hook |

The node layout follows `sdhci`: PCI node, controller node
(`busses/mmc/rtsx/driver_v1`), one slot node (`busses/mmc/rtsx/device/v1`)
with `mmc_bus` as fixed child.

Realtek readers use PCI class `0xff`. `device_manager.cpp` (a copy in
`distro/kernel`, R5 snippets) probes a class `0xff` PCI device when
`/dev/disk` is scanned (`device_node::Probe("disk")`; a PCI node is only
probed on demand, and only for the classes listed there) and adds
`busses/mmc` to its search path. Stick 74 had only the second part: the
reader was never probed. The driver only claims class `0xff`, as OpenBSD does,
so a reader that also shows as a standard SD host stays with `sdhci`.

## Chips

One table, `kChips` in `rtsx_haiku.cpp`. The OpenBSD chips are the list of
OpenBSD `rtsx_pci.c` 1.16, with its flags and BARs. The driver logs the chip
name once at attach: `rtsx: RTS5228 (10ec:5228), …` for the tested chip,
`rtsx: RTS5229 (experimental) (10ec:5229), …` for the OpenBSD chips.

| PCI id | Chip | Source | Level on R5 |
|---|---|---|---|
| `10ec:5228` | RTS5228 | clean-room spec | buildable; the 5625U boots are still to do |
| `10ec:5209` | RTS5209 | OpenBSD | experimental: buildable, not tested on R5 |
| `10ec:5227` | RTS5227 | OpenBSD | experimental: buildable, not tested on R5 |
| `10ec:5229` | RTS5229 | OpenBSD | experimental: buildable, not tested on R5 |
| `10ec:522a` | RTS522A | OpenBSD | experimental: buildable, not tested on R5 |
| `10ec:5249` | RTS5249 | OpenBSD | experimental: buildable, not tested on R5 |
| `10ec:525a` | RTS525A | OpenBSD (registers in BAR 1) | experimental: buildable, not tested on R5 |
| `10ec:5286` | RTL8402 | OpenBSD | experimental: buildable, not tested on R5 |
| `10ec:5287` | RTL8411B | OpenBSD | experimental: buildable, not tested on R5 |
| `10ec:5289` | RTL8411 | OpenBSD | experimental: buildable, not tested on R5 |

Possible later work, not in the driver: RTS5208, RTS5260, RTS5261, RTS5264
and the RTS5765DL. Neither OpenBSD's driver nor the spec covers them.

## Behaviour

- Speeds: 400 kHz while `mmc_bus` identifies the card, then 25 MHz default
  speed, 4-bit. No high speed, no UHS-I (1.8 V), no tuning, no SDIO.
- Data goes by ADMA straight to the I/O scheduler's physical vectors: up to
  128 pieces of at most 512 KiB, 512 KiB per transfer, below 4 GiB. The
  RTS5228 uses its own descriptor length layout.
- `mmc_disk` always uses the multiple-block commands. The chip's transfer
  modes leave CMD12 to the driver, so `do_io` sends it after each transfer.
- Card detect: the interrupt hands the change to a thread, which waits 200 ms,
  reads the card-present bit again, powers the slot and sets 400 kHz, and then
  wakes `mmc_bus` once per card. A removal is seen in the interrupt at once:
  waiting commands stop, later ones return `B_DEV_NO_MEDIA`.
- RTS5228: ASPM stays forced off (the reader is always in the active state),
  LTR active latency 60 µs when LTR is on, overcurrent protection on (930 mA,
  current limit 1500 mA). An overcurrent powers the slot off until the card
  is taken out. At restart and power off an R5 shutdown hook powers the card
  off and puts the chip in S1, as the spec describes.

## Limits

- A card that is taken out is removed at once: rtsx wakes the `mmc_bus`
  scan, which finds the slot empty and removes the card's node. A volume on
  it is not unmounted. A disk that is still open (a mounted volume) stays
  until it is closed, returns `B_DEV_NO_MEDIA`, and its I/O does not reach a
  new card; the new card gets a new number. Unmount before removing the
  card: what was not written yet is lost.
- The write-protect switch is not read; a card is always writable.
- Default speed only (25 MHz, 4 bit); no high speed or UHS-I.
- No suspend or runtime power management (Haiku has neither here).
- The RTS5228 bit the spec leaves open: after the 74 init clocks the driver
  stops the clock toggle again.

## SD 2.0 timing on the RTS5228

The RTS5228 power-on sequence (spec section 3.2, step 11) leaves the SD
engine in SD 3.0 mode with the CRC clock from variable-phase clock 0, which
only SDR50 and SDR104 set up. Stick 75 showed what that does: CMD0 (no
answer) completed, CMD8 ended with no interrupt at all. Every clock change
on the RTS5228 now first sets OpenBSD's SD 2.0 timing, as
`rtsx_switch_sd_clock()` does for the other chips (spec section 7.9: the SD
path is the same): SD 2.0 mode, clock source `0x24`, RX on the rising edge,
TX on the falling edge.

## Diagnostics

- A failed command is logged with its argument and one line of chip state
  (`bipr`, `bier`, the SD configuration, status and transfer registers, the
  clock registers, and on the RTS5228 the LDO and overcurrent status): every
  failure up to 32, then one in 256. A removed card is not logged.
- "cannot power the card slot" says whether the power or the clock step
  failed, with the same state line, and every RTS5228 step that fails names
  its line in `rtsx_5228.cpp`.

## mmc_bus

R5 has its own copy of `mmc_bus.cpp` (`distro/kernel/src/add-ons/kernel/
bus_managers/mmc/`, Haiku MIT, R5 snippets). Every scan sends CMD0 first, so
a card put in later is reset too. CMD1 and ACMD41 stop after 20 tries (two
seconds; the SD specification gives the card one), and a failed CMD55 or
ACMD41 ends the scan; until now the loop went on with an old OCR, also after
the card was gone (stick 75: 489 tries). A failed scan, a card outside the
voltage range, and a card that does not enter the data state now wait for
the next card instead of ending the scan thread. This also applies to sdhci
readers.

CMD0 puts every card in the idle state: none is selected, and the RCA and the
4 bit bus are gone. So a scan first forgets the selected card (no CMD7 to
deselect it, which a card put in again does not answer) and removes the node
it published for the last card. rtsx also wakes the scan when a card is
taken out; CMD0 then returns `B_DEV_NO_MEDIA`, and the scan waits for the
next card with the old node already removed. The card found now gets a new node, and
`mmc_disk` reads its size and sets the 4 bit bus again. `mmc_disk` has its own
copy too (`distro/kernel/src/add-ons/kernel/drivers/disk/mmc/`): after its
node is removed it fails every I/O and trim with `B_DEV_NO_MEDIA` and says so
for `B_GET_MEDIA_STATUS`, and a trim ends at its last block (CMD33 takes the
last block; `offset + length` also erased the block after the range).

In rtsx, CMD7 with RCA 0 has no response (SD physical layer; OpenBSD sdmmc
sends it without one), an erase (CMD38) may stay busy for 10 seconds instead
of one, and a transfer end that arrives after its waiter gave up is cleared
before the next command starts.
