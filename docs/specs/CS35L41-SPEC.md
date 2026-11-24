# CS35L41 Hardware Specification (HP ENVY x360 15, Subsystem `103c:8a31`)

This document describes the hardware interface and programming sequences for the dual Cirrus Logic CS35L41 speaker amplifiers on the HP ENVY x360 15 (AMD Ryzen 5 5625U, HD Audio controller PCI `1002:1637`, subsystem `103c:8a31`).

## 1. Devices

The Realtek HD Audio codec quirk for subsystem `103c:8a31` selects a two-amplifier CS35L41 configuration on I²C (alongside HP mute LED control on the Realtek codec). Both amplifiers appear under the ACPI device `CSC3551`.

| Item | Value / Source | Origin |
| --- | --- | --- |
| Control bus | I²C, 32-bit big-endian register addresses, 32-bit big-endian values, 4-byte stride | Hardware / I²C |
| Amplifier count | 2 | `103c:8a31` table |
| Left amplifier (index 0) | I²C address `0x40` | ACPI `_CRS` |
| Right amplifier (index 1) | Second I²C address from ACPI (typically `0x41` or `0x42`) | ACPI `_CRS` |
| Channel assignment | Index 0 = Left (`l0`), Index 1 = Right (`r0`) | `103c:8a31` table |
| I²S/TDM RX slot | Left = slot 0, Right = slot 1 (register `0x00004820`, bits 5:0) | `103c:8a31` table |
| Reset GPIO | `_CRS` GPIO index `0`, active-low (shared reset line; 0 = reset asserted, 1 = released) | `103c:8a31` table + ACPI `_CRS` |
| Speaker ID GPIO | `_CRS` GPIO index `1`, input (read to form speaker ID for firmware selection) | `103c:8a31` table + ACPI `_CRS` |
| Chip-select GPIO | None (`-1`, not used on I²C) | `103c:8a31` table |
| Interrupt | I²C IRQ from ACPI, wired to chip GPIO2 (active-low open-drain) | ACPI `_CRS` |
| On-chip GPIO1 | Unused (internal boost is used; no external speaker-power switch) | `103c:8a31` table |
| On-chip GPIO2 | Interrupt output, open-drain, active-low | `103c:8a31` table |

## 2. Power-up

Perform the following sequence for each amplifier on probe/power-up. For `103c:8a31`, the boost converter is **internal** with: inductance = **1000 nH** (1.0 µH), peak current = **4100 mA**, capacitance = **24 µF**.

1. **Hardware reset**: Drive the reset GPIO low, wait 2 ms, drive the reset GPIO high, wait 2 ms.
2. **Software reset**: Write register `0x00000020` = `0x5A000000`, then wait 2 ms.
3. **Wait for OTP boot**: Read register `0x0001001C` every 1 ms (up to 100 ms) until bit 1 (`0x00000002`) is `1`. Then read register `0x00010018` and verify bit 31 (`0x80000000`) is `0`.
4. **Verify chip ID**:
   - Read register `0x00000000` (Device ID) and register `0x00000004` (Revision ID).
   - Check the low 4 bits (`0x0F`) of Revision ID: if odd, expected Device ID is `0x35B40`; if even, expected Device ID is `0x35A40`.
   - Known silicon revision values in `0x00000004` are `0xA0`, `0xB0`, and `0xB2`.
5. **Unlock test key**: Write `0x00000040` = `0x00000055`, then write `0x00000040` = `0x000000AA`.
6. **Apply silicon errata** (with test key unlocked):
   - If Revision ID is `0xA0`, write:
     - `0x00003854` = `0x05180240`, `0x00004100` = `0x00000000`, `0x00004310` = `0x00000000`, `0x00004400` = `0x00000000`
     - `0x00007418` = `0x9091A1C8`, `0x00003014` = `0x0200EE0E`, `0x0000381C` = `0x00000051`, `0x00000054` = `0x00000004`
     - `0x00010318` = `0x00000000`, `0x00010B18` = `0x00000000`, `0x02BC20E0` = `0x00000000`, `0x02BC2020` = `0x00000000`
     - `0x00002018` = `0x00000000`, `0x00006C04` = `0x00000000`, `0x00004C28` = `0x00000000`, `0x00004C2C` = `0x00000000`
   - If Revision ID is `0xB0` or `0xB2`, write:
     - `0x00004100` = `0x00000000`, `0x00004310` = `0x00000000`, `0x00004400` = `0x00000000`, `0x0000381C` = `0x00000051`
     - `0x02BC20E0` = `0x00000000`, `0x02BC2020` = `0x00000000`, `0x00002018` = `0x00000000`, `0x00006C04` = `0x00000000`
     - `0x00004C28` = `0x00000000`, `0x00004C2C` = `0x00000000`
   - Write `0x02BC1000` = `0x00000000` (clear DSP core clock control).
7. **Unpack OTP trim** (with test key unlocked):
   - Read register `0x00000010` (OTP ID; valid IDs are `0x01`, `0x02`, `0x03`, `0x06`, `0x08`).
   - Read 32 32-bit words from `0x00000400` through `0x0000047C` and unpack the bit-fields (starting at word 2, bit 16) into their target trim registers.
8. **Lock test key**: Write `0x00000040` = `0x000000CC`, then write `0x00000040` = `0x00000033`.
9. **Configure internal boost** (for 1000 nH, 24 µF, 4100 mA):
   - Update `0x00003810` bits 15:0 = `0x4924` (bits 7:0 = `0x24`, bits 15:8 = `0x49`).
   - Update `0x00003814`: bits 1:0 = `0x0` (1.0 µH), bits 15:8 = `0x75` (boost slope).
   - Update `0x00003808` bits 6:0 = `0x42` (`((4100 - 1600) / 50) + 0x10`).
   - Update `0x00002018` bits 5:4 = `0x2` (enable internal boost).
10. **Configure GPIOs and TDM slot**:
    - Update `0x00011008` (GPIO1): bit 12 = `0` (normal polarity), bit 31 = `1` (not output).
    - Update `0x0001100C` (GPIO2): bit 12 = `0` (active-low), bit 31 = `1` (not output).
    - Update `0x0000242C`: bits 17:16 = `0` (GPIO1 unused), bits 26:24 = `2` (GPIO2 open-drain interrupt).
    - Update `0x00004820` bits 5:0 = `0` for Left, `1` for Right.

## 3. Firmware

### File naming for `103c:8a31`

All filenames are lowercase under `cirrus/` (with non-alphanumeric characters other than `.` and `/` replaced by `-`). Default firmware type is `spk-prot` on `dsp1`. With speaker ID `N` from GPIO and amp suffix `l0` (left) or `r0` (right), the driver searches in order for a `.wmfw` firmware image and matching `.bin` coefficient file:

1. `cirrus/cs35l41-dsp1-spk-prot-103c8a31-spkid<N>-<amp>.wmfw` and `.bin`
2. `cirrus/cs35l41-dsp1-spk-prot-103c8a31-<amp>.wmfw` and `...-spkid<N>-<amp>.bin`
3. `cirrus/cs35l41-dsp1-spk-prot-103c8a31-spkid<N>.wmfw` and `...-spkid<N>-<amp>.bin` (or `...-spkid<N>.bin`)
4. `cirrus/cs35l41-dsp1-spk-prot-103c8a31.wmfw` and `...-spkid<N>-<amp>.bin` (or `...-spkid<N>.bin`)
5. Fallback: `cirrus/cs35l41-dsp1-spk-prot.wmfw` and `cirrus/cs35l41-dsp1-spk-prot.bin`

An optional tuning configuration file `<coeff_filename>cfg` (header signature `0x109A4A35`, version `1`) may accompany the `.bin` file to override the default PCM gain.

### DSP memory ranges and load sequence

| Region | Address Range |
| --- | --- |
| DSP1 control base / system info base | `0x02B80000` / `0x025E0000` |
| Program memory (PMEM packed) | `0x03800000` – `0x03804FE8` |
| X data memory (packed / 24-bit unpacked) | `0x02000000` – `0x02002FF0` / `0x02800000` – `0x02803FF4` |
| Y data memory (packed / 24-bit unpacked) | `0x02C00000` – `0x02C017F0` / `0x03400000` – `0x03401FF4` |

1. **Load**: Write the `.wmfw` blocks and `.bin` coefficient blocks into the DSP1 memory regions above, then write any factory speaker calibration coefficients into the calibration control block.
2. **Pre-start**: Write `0x00000001` to all 16 DSP1 stream rate registers: `0x02B80080`, `0x02B80088`, `0x02B80090`, `0x02B80098`, `0x02B800A0`, `0x02B800A8`, `0x02B800B0`, `0x02B800B8` (RX1–RX8) and `0x02B80280`, `0x02B80288`, `0x02B80290`, `0x02B80298`, `0x02B802A0`, `0x02B802A8`, `0x02B802B0`, `0x02B802B8` (TX1–TX8).
3. **Start & verify**: Enable the DSP1 core, then poll the firmware state control in DSP memory every 1 ms (up to 15 ms) until it indicates the running state. Read mailbox status `0x00013004` and verify it is `0` (running) or `1` (paused).
4. **Post-start pause**: Write `1` (pause) to mailbox `0x00013020`, then poll `0x00013004` up to 5 times (1 ms apart) until it reads `1` (paused).

## 4. Playback start / stop

Playback is driven by HD Audio PCM lifecycle events:

- **Stream Open**: Wake the amplifiers from runtime suspend.
- **Stream Prepare (before codec/controller start)**:
  1. Configure clocks, I²S format, and common routing:
     - `0x00002C04` = `0x00000430` (PLL reference = 3.072 MHz BCLK, reference clock enabled)
     - `0x00002C08` = `0x00000003` (DSP clock enabled)
     - `0x00002C0C` = `0x00000003` (48 kHz global sample rate)
     - `0x00004804` = `0x00000021` (ASP BCLK = 3.072 MHz)
     - `0x00004808` = `0x20200200` (32-bit RX/TX slots, I²S, clock consumer)
     - `0x00004830` = `0x00000018` (24-bit TX word length)
     - `0x00004840` = `0x00000018` (24-bit RX word length)
     - `0x00004C20` = `0x00000018` (ASP TX1 = VMON), `0x00004C24` = `0x00000019` (ASP TX2 = IMON)
     - `0x00004C48` = `0x00000018` (DSP1 RX3 = VMON), `0x00004C4C` = `0x00000019` (DSP1 RX4 = IMON)
  2. If DSP firmware is running:
     - `0x0000480C` = `0x00000003`, `0x00004C00` = `0x00000032` (DAC = DSP1 TX1)
     - `0x00004C28` = `0x00000028` (ASP TX3 = VPMON), `0x00004C2C` = `0x00000029` (ASP TX4 = VBSTMON)
     - `0x00004C50` = `0x00000028` (DSP1 RX5 = VPMON for internal boost), `0x00004C54` = `0x00000029` (DSP1 RX6 = VBSTMON)
     - `0x00004800` = `0x00010001` (enable ASP RX1 and ASP TX1)
     - If DSP is bypassed (no firmware): `0x0000480C` = `0x00000002`, `0x00004C00` = `0x00000008` (DAC = ASP RX1), `0x00004C28` = `0x00000000`, `0x00004C2C` = `0x00000000`, `0x00004C50` = `0x00000020`, `0x00004C54` = `0x00000021`, `0x00004800` = `0x00010000` (enable ASP RX1).
  3. Write `0x00004C40` = `0x00000008` and `0x00004C44` = `0x00000008`.
  4. If DSP firmware is running: set bits 12 and 13 (`0x00003000`) in `0x00002018` (enable VMON and IMON), then write `2` (resume) to `0x00013020` and poll `0x00013004` until it reads `0`.
  5. Set bit 0 (`0x00000001`) in `0x00002018` (enable amplifier).
- **Stream Prepare (after codec/controller start)**:
  1. Set bit 0 (`0x00000001`) in `0x00002014` (global enable).
  2. Poll `0x00010010` every 1 ms (up to 100 ms) until bit 24 (`0x01000000`, power-up done) is `1`, then write `0x01000000` to `0x00010010` to clear it.
  3. Unmute (unless ACPI mute override is active): write `0x00006000` = `0x00008000` (HPF on, 0.0 dB), and write `0x00006C04` = `0x00000084` (4.5 dB PCM gain when DSP is bypassed, or the tuned gain in bits 9:5 when DSP is running).
- **Stream Cleanup (pre-cleanup, before stopping HDA stream)**:
  1. Mute: write `0x00006C04` = `0x00000000`, then write `0x00006000` = `0x0000A678`.
  2. Clear bit 0 (`0x00000000`) in `0x00002014` (global disable).
  3. Poll `0x00010010` every 1 ms (up to 100 ms) until bit 23 (`0x00800000`, power-down done) is `1`, then write `0x00800000` to `0x00010010` to clear it.
- **Stream Cleanup (post-cleanup)**:
  1. Clear bit 0 in `0x00002018` (disable amplifier).
  2. If DSP firmware is running: write `1` (pause) to `0x00013020` and poll `0x00013004` until it reads `1`, then clear bits 12 and 13 (`0x00003000`) in `0x00002018` (disable VMON and IMON).
  3. Release any latched protection errors (see Section 5).
- **Stream Close**: Allow runtime suspend (autosuspend delay 3000 ms).

## 5. Errors

Interrupt status registers `0x00010010`–`0x0001001C` are write-1-to-clear; corresponding masks are at `0x00010110`–`0x0001011C`.

| Register & Bit | Meaning | Release Bit in `0x00002034` |
| --- | --- | --- |
| `0x00010010` bit 6 (`0x00000040`) | Boost overvoltage error | Bit 3 (`0x08`) |
| `0x00010010` bit 7 (`0x00000080`) | Boost DCM undervoltage error | Bit 4 (`0x10`) |
| `0x00010010` bit 8 (`0x00000100`) | Boost inductor short error | Bit 2 (`0x04`) |
| `0x00010010` bit 15 (`0x00008000`) | Over-temperature warning | Bit 5 (`0x20`) |
| `0x00010010` bit 17 (`0x00020000`) | Over-temperature error | Bit 6 (`0x40`) |
| `0x00010010` bit 31 (`0x80000000`) | Amplifier output short error | Bit 1 (`0x02`) |
| `0x00010018` bit 31 (`0x80000000`) | OTP boot error | — |
| `0x0001001C` bit 1 (`0x00000002`) | OTP boot complete | — |
| `0x00013004` = `0xFFFFFFFF` or `0x00FFFFFF` | DSP firmware mailbox error | — |

When any of the six protection faults in `0x00010010` fires, the amplifier latches into Safe-Mode. To exit Safe-Mode during stream stop (after `0x00002014` bit 0 has been cleared to `0`): write `0x00002034` = `0`, set the accumulated release bits in `0x00002034`, and then clear those bits in `0x00002034`.

## 6. Not covered

- System suspend/resume and runtime suspend/resume (DSP hibernate entry and wake).
- EFI speaker calibration data retrieval and formatting.

---

Linux version read: `v7.3.5`
Exact file paths:
- `sound/hda/codecs/side-codecs/cs35l41_hda.c`
- `sound/hda/codecs/side-codecs/cs35l41_hda_i2c.c`
- `sound/hda/codecs/side-codecs/cs35l41_hda_property.c`
- `sound/soc/codecs/cs35l41-lib.c`
- `include/sound/cs35l41.h`
- `sound/hda/codecs/realtek/alc269.c`

Written by the reader; contains no Linux code.
