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
`distro/kernel`, R5 snippet) adds `busses/mmc` to the search path of a
class `0xff` PCI device. The driver only claims class `0xff`, as OpenBSD does,
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

- `mmc_bus` does not unpublish a card that is gone. After a removal the old
  disk stays in `/dev/disk/mmc/` and only returns errors; a new card gets a
  new node. Unmount before removing the card.
- `mmc_bus` repeats ACMD41 until the card is ready. If the card is pulled out
  during that loop, the loop can keep running until a card is inserted again.
  That is in `mmc_bus`, not in this driver.
- No suspend or runtime power management (Haiku has neither here).
- The RTS5228 bits the spec leaves open: after the 74 init clocks the driver
  stops the clock toggle again; at 25 MHz it keeps the SD 3.0 clock sources
  of the power-on sequence and does not switch to OpenBSD's SD 2.0 setup.
  If CMD0 or the first read fails on the 5625U, those are the first two
  places to look.
