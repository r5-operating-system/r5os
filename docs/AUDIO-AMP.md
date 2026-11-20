# Speaker amplifiers behind HD Audio

Some laptops do not drive their speakers from the HD Audio codec. The codec
sends the samples over I²S to separate amplifier chips on I²C or SPI, and
those chips must be powered, unmuted and stopped around every stream. R5 has
a small kernel interface for that, so the HD Audio driver needs no vendor
code.

- Header: `distro/media/headers/os/drivers/AudioAmp.h` (MIT).
- Module: `generic/audio_amp/v1`, source `distro/media/src/add-ons/kernel/generic/audio_amp` (MIT).
- First user: the CS35L41 driver, `distro/media/src/add-ons/kernel/drivers/audio/cs35l41` (MIT), installed as `drivers/audio/hmulti/cs35l41` on x86_64.
- The HD Audio side: SPDX-Snippet blocks in the R5 copy of `hda` (`hda_codec.cpp`, `hda_multi_audio.cpp`).

It is a kernel module in the Haiku way (`module_info`, `std_ops`, `get_module()`), like the ACPI module; not part of a LibR5 library, and not part of `libbe`.

## Contract

An amplifier driver calls `register_amp()` with an `audio_amp_info` and an `audio_amp_hooks` table, and `unregister_amp()` before it goes away. `unregister_amp()` waits for a hook that is running.

- `subsystem`: the HD Audio codec subsystem the amplifier belongs to, as the codec reports it (`0x103c8a31`). `0` means any codec with an analog output.
- `channel`: 0 left, 1 right. Calls go to the amplifiers in channel order.
- `rates`: the `B_SR_*` rates the amplifier accepts. The HD Audio driver offers only those rates while amplifiers are registered.
- `max_gain`: the hard limit in hundredths of a decibel. `set_gain()` never passes more.

The HD Audio driver calls, for a codec with an analog output:

| When | Call |
|---|---|
| before the playback stream runs | `prepare(subsystem, format)` |
| after it runs | `start(subsystem)` |
| before it stops | `stop(subsystem)` |
| after it stopped | `unprepare(subsystem)` |
| headphone jack sense changes (speakers muted while a jack is in) | `set_mute(subsystem, mute)` |
| codec closes | `stop()` and `unprepare()` |

The module keeps the last `set_mute()` per subsystem, so an amplifier that registers later starts in the same state. `set_gain()`, `suspend()` and `resume()` exist for later phases; the HD Audio driver does not call them yet.

Every hook may sleep. They run in thread context, never with interrupts off or a spinlock held. A hook that fails only affects its own amplifier; the codec keeps playing.

Rules: calls and fields are only added at the end of these structures (both carry a `size`). A call that shipped does not change.

## CS35L41, phase 1

The HP ENVY x360 15 (Ryzen 5 5625U, HDA subsystem `103c:8a31`) has two CS35L41 amplifiers under one ACPI device, `CSC3551`.

- Haiku's I²C controller registers a node for the first `I2cSerialBus` of an ACPI device only. The driver reads every address from `_CRS` and talks to each amplifier through `i2c_bus_interface::exec_command()` with that address.
- `_CRS` GPIO 0 is the shared reset line, GPIO 1 the speaker ID, both through the GPIO module (`docs/GPIO.md`). The reset is pulsed low for 2 ms unless the firmware already drives it high; its state is logged once either way. The software reset of each amplifier always follows.
- `_SUB` gives the subsystem. The boost parts and TDM slots are not in ACPI, so they come from a machine table in `Driver.cpp`. An unknown machine leaves the amplifiers off.
- Power-up, errata, OTP trim, boost, pads and the DSP bypass playback path follow the R5 clean-room specification (see `docs/LICENSING.md`).
- The ACPI mute override (`_DSM` function 5, Notify `0x91`) mutes both amplifiers while it is set.
- Protection faults are polled every 100 ms while playing. A fault mutes that amplifier until the next stream; three faults turn it off until restart.

**Hard limit.** Without the DSP firmware there is no speaker protection. The PCM gain field of `REG_AMP_GAIN` (bits 9:5) is written as 0, its lowest step; the bypass default would be 4 (4.5 dB). The size of a step is not in the specification, so no dB figure is claimed. Nothing raises it in phase 1.

## CS35L41, phase 2: speaker protection firmware

Off by default until a boot proves it. A driver settings file `cs35l41` (in `~/config/settings/kernel/drivers` or `/boot/system/settings/kernel/drivers`) with the line

```
cs35l41_firmware true
```

turns it on. Without it, or when anything below fails, the amplifiers run in DSP bypass with the hard limit above.

- Files: `system/non-packaged/data/firmware/cirrus` (installed by `scripts/apply-cirrus-fw.py`, unmodified, sha256 checked). The search order is the specification's, with the machine's firmware ID: `103c8a29` for subsystem `103c:8a31`, the files Windows uses on this laptop. The program is also reachable as `cs35l41-dsp1-spk-prot-103c8a29.wmfw`, a symlink to `halo_cspl_RAM_revB2_29.63.1.wmfw`. The tuning is `...-spkid<N>-l0.bin` or `-r0.bin`, with N read from the speaker ID GPIO.
- Load at start: program blocks, the algorithm table from DSP memory, tuning blocks, the 16 stream rates, memory protection, core start, then the firmware state must read 2 (running) within 15 ms and the mailbox must report running or paused; then the firmware is paused.
- Playback: DSP routing, VMON/IMON on, mailbox resume before the stream runs; pause and VMON/IMON off after it stopped. The gain with the firmware running is the default PCM gain (4, 4.5 dB): no tuning configuration file is installed.
- Any failure (missing file, bad format, state not 2, mailbox error, no resume or pause) stops the DSP core and falls back to bypass with the cap, with a log line.
- Factory calibration: read from the EFI variable `SmartAmpCalibrationData` (or `CirrusSmartAmpCalibrationData`) through the EFI module `generic/efi/v1` (`docs/EFI.md`). The entry is chosen by the amplifier's silicon ID (`0x00017044`/`0x00017040`) or its index, and written to `CAL_AMBIENT`, `CAL_R`, `CAL_STATUS` and `CAL_CHECKSUM` before the core starts. That module is not implemented yet, so today the firmware runs with its default calibration and the driver logs that once. When the module lands, the driver needs no change.
- Suspend and resume are not built.
