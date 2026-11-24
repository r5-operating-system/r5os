# CS35L41 Hardware and Firmware Specification — Round 2 (Subsystem `103c:8a31`)

This document supplements `CS35L41-SPEC.md` with the OTP trim layout, `.wmfw` / `.bin` firmware formats, firmware state and calibration locations, and ACPI mute override behavior for the dual Cirrus Logic CS35L41 amplifiers on the HP ENVY x360 15 (`103c:8a31`).

## 1. OTP trim map

OTP memory consists of 32 32-bit words at registers `0x00000400` through `0x0000047C`. Register `0x00000010` holds the OTP map ID. Supported IDs are `0x01`, `0x02`, `0x03`, `0x06`, and `0x08`:
- IDs `0x01` and `0x08` use Map A (96 entries).
- IDs `0x02`, `0x03`, and `0x06` use Map B (97 entries; identical to Map A except entry 91 writes `0x00004000` bit 11 instead of being skipped).

### Unpacking rules

- Unpacking starts at **word 2** (`0x00000408`), **bit 16** (0-indexed LSB within the 32-bit word), with the test key unlocked.
- Fields are packed LSB-first sequentially across word boundaries:
  - If `bit_offset + width - 1 < 32`, extract bits `[bit_offset + width - 1 : bit_offset]` from the current word and advance `bit_offset` by `width`. If `bit_offset` reaches `32`, reset `bit_offset` to `0` and advance `word_offset` by `1`.
  - If `bit_offset + width - 1 >= 32`, take the low `32 - bit_offset` bits from bits `[31 : bit_offset]` of the current word, take the remaining `bit_offset + width - 32` bits from bits `[bit_offset + width - 33 : 0]` of `word_offset + 1`, combine them (`(next_bits << (32 - bit_offset)) | cur_bits`), advance `word_offset` by `1`, and set `bit_offset = bit_offset + width - 32`.
- If the target register is `0x00000000` ("Skipped"), the bits are consumed to advance the cursor, and no register write is performed. Otherwise, the extracted value is written via read-modify-write into bits `[target_bit + width - 1 : target_bit]` of the target register.

### Field table (Map A: OTP IDs `0x01`, `0x08`; Map B: OTP IDs `0x02`, `0x03`, `0x06`)

| # | Width (bits) | Target Register | Target Bits (`[msb:lsb]`) | Notes |
| --- | --- | --- | --- | --- |
| 1 | 4 | `0x00002030` | `[3:0]` | |
| 2 | 1 | `0x00002030` | `[7:7]` | |
| 3 | 6 | `0x0000208C` | `[29:24]` | |
| 4 | 4 | `0x00002090` | `[17:14]` | |
| 5 | 4 | `0x00002090` | `[13:10]` | |
| 6 | 4 | `0x0000300C` | `[14:11]` | |
| 7 | 2 | `0x0000394C` | `[24:23]` | |
| 8 | 7 | `0x00003950` | `[6:0]` | |
| 9 | 7 | `0x00003950` | `[14:8]` | |
| 10 | 8 | `0x00003950` | `[23:16]` | |
| 11 | 8 | `0x00003950` | `[31:24]` | |
| 12 | 7 | `0x00003954` | `[6:0]` | |
| 13 | 7 | `0x00003954` | `[14:8]` | |
| 14 | 8 | `0x00003954` | `[23:16]` | |
| 15 | 8 | `0x00003954` | `[31:24]` | |
| 16 | 7 | `0x00003958` | `[6:0]` | |
| 17 | 7 | `0x00003958` | `[14:8]` | |
| 18 | 8 | `0x00003958` | `[23:16]` | |
| 19 | 8 | `0x00003958` | `[31:24]` | |
| 20 | 7 | `0x0000395C` | `[6:0]` | |
| 21 | 7 | `0x0000395C` | `[14:8]` | |
| 22 | 8 | `0x0000395C` | `[23:16]` | |
| 23 | 8 | `0x0000395C` | `[31:24]` | |
| 24 | 8 | `0x0000416C` | `[7:0]` | |
| 25 | 7 | `0x00004160` | `[6:0]` | |
| 26 | 8 | `0x0000416C` | `[15:8]` | |
| 27 | 10 | `0x00004160` | `[25:16]` | |
| 28 | 12 | `0x0000416C` | `[27:16]` | |
| 29 | 1 | `0x0000416C` | `[28:28]` | |
| 30 | 6 | `0x00004170` | `[5:0]` | |
| 31 | 1 | `0x00004170` | `[6:6]` | |
| 32 | 6 | `0x00004170` | `[13:8]` | |
| 33 | 1 | `0x00004170` | `[14:14]` | |
| 34 | 9 | `0x00004170` | `[24:16]` | |
| 35 | 5 | `0x00004360` | `[4:0]` | |
| 36 | 9 | `0x00004360` | `[14:6]` | |
| 37 | 8 | `0x00004448` | `[7:0]` | |
| 38 | 8 | `0x00004448` | `[15:8]` | |
| 39 | 8 | `0x00004448` | `[23:16]` | |
| 40 | 8 | `0x00004448` | `[31:24]` | |
| 41 | 3 | `0x0000444C` | `[2:0]` | |
| 42 | 5 | `0x00006E30` | `[4:0]` | |
| 43 | 5 | `0x00006E30` | `[12:8]` | |
| 44 | 5 | `0x00006E30` | `[20:16]` | |
| 45 | 5 | `0x00006E30` | `[28:24]` | |
| 46 | 5 | `0x00006E34` | `[4:0]` | |
| 47 | 5 | `0x00006E34` | `[12:8]` | |
| 48 | 5 | `0x00006E34` | `[20:16]` | |
| 49 | 5 | `0x00006E34` | `[28:24]` | |
| 50 | 5 | `0x00006E38` | `[4:0]` | |
| 51 | 5 | `0x00006E38` | `[12:8]` | |
| 52 | 5 | `0x00006E38` | `[20:16]` | |
| 53 | 5 | `0x00006E38` | `[28:24]` | |
| 54 | 5 | `0x00006E3C` | `[4:0]` | |
| 55 | 5 | `0x00006E3C` | `[12:8]` | |
| 56 | 5 | `0x00006E3C` | `[20:16]` | |
| 57 | 5 | `0x00006E3C` | `[28:24]` | |
| 58 | 5 | `0x00006E40` | `[4:0]` | |
| 59 | 5 | `0x00006E40` | `[12:8]` | |
| 60 | 5 | `0x00006E40` | `[20:16]` | |
| 61 | 5 | `0x00006E40` | `[28:24]` | |
| 62 | 5 | `0x00006E44` | `[4:0]` | |
| 63 | 10 | `0x00006E48` | `[9:0]` | |
| 64 | 10 | `0x00006E48` | `[19:10]` | |
| 65 | 10 | `0x00006E48` | `[29:20]` | |
| 66 | 10 | `0x00006E4C` | `[9:0]` | |
| 67 | 10 | `0x00006E4C` | `[19:10]` | |
| 68 | 10 | `0x00006E4C` | `[29:20]` | |
| 69 | 10 | `0x00006E50` | `[9:0]` | |
| 70 | 10 | `0x00006E50` | `[19:10]` | |
| 71 | 10 | `0x00006E50` | `[29:20]` | |
| 72 | 10 | `0x00006E54` | `[9:0]` | |
| 73 | 10 | `0x00006E54` | `[19:10]` | |
| 74 | 10 | `0x00006E54` | `[29:20]` | |
| 75 | 10 | `0x00006E58` | `[9:0]` | |
| 76 | 10 | `0x00006E58` | `[19:10]` | |
| 77 | 10 | `0x00006E58` | `[29:20]` | |
| 78 | 10 | `0x00006E5C` | `[9:0]` | |
| 79 | 10 | `0x00006E5C` | `[19:10]` | |
| 80 | 10 | `0x00006E5C` | `[29:20]` | |
| 81 | 10 | `0x00006E60` | `[9:0]` | |
| 82 | 10 | `0x00006E60` | `[19:10]` | |
| 83 | 10 | `0x00006E60` | `[29:20]` | |
| 84 | 10 | `0x00006E64` | `[9:0]` | |
| 85 | 5 | `0x00007418` | `[11:7]` | |
| 86 | 5 | `0x0000741C` | `[4:0]` | |
| 87 | 4 | `0x0000741C` | `[14:11]` | |
| 88 | 4 | `0x0000741C` | `[22:19]` | |
| 89 | 1 | `0x00007434` | `[17:17]` | |
| 90 | 7 | `0x00007434` | `[24:18]` | |
| 91 | 9 | `0x00007068` | `[8:0]` | |
| 92 | 1 | `0x0000410C` | `[7:7]` | |
| 93 | 7 | `0x0000400C` | `[6:0]` | |
| 94 | 1 | Map A: Skipped (`0x00000000`) / Map B: `0x00004000` | Map A: — / Map B: `[11:11]` | Only difference between Map A and Map B |
| 95 | 8 | `0x00017040` | `[7:0]` | Die status 1 (X coordinate) |
| 96 | 8 | `0x00017040` | `[15:8]` | Die status 1 (Y coordinate) |
| 97 | 8 | `0x00017040` | `[23:16]` | Die status 1 (Wafer ID) |
| 98 | 8 | `0x00017040` | `[31:24]` | Die status 1 (DVS) |
| 99 | 24 | `0x00017044` | `[23:0]` | Die status 2 (Lot number) |

## 2. Firmware formats

### `.wmfw` format (Halo DSP, version 3)

- **File header (12 bytes, at offset `0`)**:
  - Bytes `0..3`: ASCII magic `"WMFW"`
  - Bytes `4..7`: `le32` total pre-block header length (must equal `40` for Halo)
  - Bytes `8..9`: `le16` revision
  - Byte `10`: `u8` core type (`4` = Halo)
  - Byte `11`: `u8` file format version (`3` for Halo)
- **Memory sizes (16 bytes, at offset `12`)**:
  - Four `le32` values: XM size, YM size, PM size, ZM size.
- **Footer (12 bytes, at offset `28`)**:
  - Bytes `28..35`: `le64` timestamp
  - Bytes `36..39`: `le32` checksum
- **Block header (8 bytes, starting at offset `40`, repeated until EOF)**:
  - Bytes `0..3`: Dual-interpreted 32-bit field:
    - `offset` = `le32(word) & 0x00FFFFFF` (bytes `0..2`, 24-bit word offset)
    - `type` = `be32(word) & 0xFF` (byte `3`)
  - Bytes `4..7`: `le32` payload length `len` in bytes. Next block starts at `pos + 8 + len` (no alignment padding).
- **Block types and CS35L41 target address calculation**:

| Type | Meaning | Base Address | Target Register Formula |
| --- | --- | --- | --- |
| `0x05` | XM unpacked (24-bit in 32-bit words) | `0x02800000` | `base + (offset * 4)` |
| `0x06` | YM unpacked (24-bit in 32-bit words) | `0x03400000` | `base + (offset * 4)` |
| `0x10` | Halo PM packed | `0x03800000` | `base + (offset * 5)` |
| `0x11` | Halo XM packed | `0x02000000` | `(base + (offset * 3)) & ~0x3` |
| `0x12` | Halo YM packed | `0x02C00000` | `(base + (offset * 3)) & ~0x3` |
| `0xF0` | Absolute register write | — | `offset` |
| `0xF2` | Algorithm & control descriptors | — | Not written to DSP memory |
| `0xFC` | Metadata | — | Ignored |
| `0xFE` | Name text | — | Informational text |
| `0xFF` | Info text | — | Informational text |

- **Algorithm information blocks**:
  - **`.wmfw` `0xF2` block (format version 3)**:
    - Algorithm header: `le32` algorithm ID; 1-byte-length-prefixed name string padded to a 4-byte boundary (`((L + 1) + 3) & ~3`); `le16`-length-prefixed description string padded to a 4-byte boundary; `le32` count of coefficient descriptors (`ncoeff`).
    - Followed by `ncoeff` coefficient descriptors, each consisting of: `le16` word offset within the algorithm's region, `le16` memory region type, `le32` descriptor payload size `blk_len` (next descriptor is at `cur + 8 + blk_len`); 1-byte-length-prefixed name (4-byte aligned); 1-byte-length-prefixed short name (4-byte aligned); `le16`-length-prefixed description (4-byte aligned); `le16` control type (`0x0004` byte control, `0x1000` acked, `0x1001` host event, `0x1002` host buffer, `0x1004` firmware event); `le16` flags (`0x0001` readable, `0x0002` writeable, `0x0004` volatile, `0x8000` system); `le32` data length in bytes.
  - **Runtime algorithm table in DSP XM (`0x02800000`)**:
    - After `.wmfw` is loaded, read a 40-byte Halo ID header (10 `be32` words) from `0x02800000`: `core_id`, `block_rev`, `vendor_id`, `fw_id`, `fw_ver`, `xm_base`, `xm_size`, `ym_base`, `ym_size`, `n_algs`.
    - Immediately following at word offset `10` (`0x02800028`) are `n_algs` 24-byte entries (6 `be32` words each): `alg_id`, `alg_ver`, `xm_base`, `xm_size`, `ym_base`, `ym_size`.
    - Each `xm_base` / `ym_base` is a word offset within XM / YM for that algorithm.

### `.bin` (coefficients) format

- **File header (16 bytes, at offset `0`)**:
  - Bytes `0..3`: ASCII magic `"WMDR"`
  - Bytes `4..7`: `le32` header length (offset of first block)
  - Bytes `8..11`: `be32` where byte 3 (`& 0xFF`) is revision (`1`, `2`, or `3`) / `le32` version
  - Bytes `12..15`: `be32` / `le32` core version
- **Block header (20 bytes)**:
  - Bytes `0..1`: `le16` `offset` (16-bit offset)
  - Bytes `2..3`: `le16` `type`
  - Bytes `4..7`: `le32` `id` (algorithm ID)
  - Bytes `8..11`: `le32` `ver` (`ver >> 8` is 24-bit algorithm version)
  - Bytes `12..15`: `le32` `offset32` (32-bit offset for extended types)
  - Bytes `16..19`: `le32` `len` (payload length in bytes). Next block starts at `(pos + 20 + len + 3) & ~0x03` (4-byte aligned).
- **Block types and target resolution**:
  - `0xFE00`: Name text; `0xFF00` / `0xFC00`: Info text / Metadata (skipped).
  - `0xF000`: Absolute register write (`reg = offset`, or global region base if `id == fw_id && offset == 0`).
  - `0x0005`, `0x0006`, `0x0010`, `0x0011`, `0x0012`: Use 16-bit `offset`.
  - `0xF405` (XM unpacked), `0xF406` (YM unpacked), `0xF411` (XM packed), `0xF412` (YM packed): Use 32-bit `offset32` and strip the `0xF400` prefix (`type & 0xFF`).
  - For memory region blocks, look up the algorithm's base word offset `(type, id)` from the runtime algorithm table, convert it to a register address using the region's formula above, and add `offset`: `reg = region_to_reg(mem, alg_base) + offset`.

### Packing 24-bit DSP words into I²C writes

- Block payloads from `.wmfw` and `.bin` are written as raw byte streams to the computed 32-bit big-endian register address.
- **Unpacked 24-bit memory (`0x05`, `0x06`, 4-byte register stride)**: Each 24-bit DSP word is stored as a 4-byte big-endian word with the most significant byte padded to `0x00`, followed by the 3 data bytes (MSB to LSB).
- **Packed memory (`0x11`, `0x12` at 3 bytes/word; `0x10` at 5 bytes/word)**: Data bytes are packed consecutively without per-word `0x00` padding.

### Firmware state

- Located via the `0xF2` descriptor table for algorithm ID `262308` (`0x400A4`), control name `"HALO_STATE"`, region type `0x05` (XM unpacked).
- Address: `0x02800000 + (alg_xm_base + ctl_offset) * 4`.
- Read as a 32-bit big-endian integer. (Note: the numeric constant representing the running state is defined in a header outside the files read in this round.)
- Before starting the DSP core: configure the Halo MPU (`0x02BC3140` = `0x5555`, then `0xAAAA`; write `0xFFFFFFFF` to XMEM/YMEM access registers `0x02BC3000`–`0x02BC304C` and `0xFFFFFFFF` to WINDOW/XREG/YREG access registers `0x02BC3008`–`0x02BC305C`; then `0x02BC3140` = `0`). Then start the core via `0x02BC1000` (set `0x00000201`, then clear bit 9 `0x00000200` leaving bit 0 `0x00000001` set).

### Calibration block

- Located via the `0xF2` descriptor table for algorithm ID `205` (`0x000000CD`), region type `0x05` (XM unpacked), with control names `"CAL_AMBIENT"`, `"CAL_R"`, `"CAL_STATUS"`, and `"CAL_CHECKSUM"`.
- The 64-bit silicon unique ID is read from `0x00017044` (bits 63:32) and `0x00017040` (bits 31:0) and used to index the factory EFI calibration data. (Note: the EFI variable name, GUID, and binary layout are implemented in the shared amplifier library outside the files read in this round.)

## 3. ACPI mute override

- Read from the `CSC3551` ACPI device using `_DSM` with:
  - **UUID**: `50d90cdc-3de4-4f18-b528-c7fe3b71f40d`
  - **Revision**: `0`
  - **Function index**: `5` (returns an ACPI Buffer; byte 0 is the mute state).
- ACPI Notify event `0x91` on the `CSC3551` device indicates the mute state changed; re-evaluate `_DSM` function `5` and apply immediately.
- **Value meanings**:
  - `0`: Normal operation (unmuting during playback is permitted).
  - `> 0`: Force-muted. Immediately mute the amplifier (`0x00006C04` = `0x00000000`, `0x00006000` = `0x0000A678`) and keep it muted even when playback unmute occurs.

## 4. Other details missed in round 1

- **Initial mute**: During probe, after reading calibration and before applying GPIO/boost properties, mute the amplifier (`0x00006C04` = `0x00000000`, `0x00006000` = `0x0000A678`).
- **Speaker ID GPIO indexing**: On `103c:8a31` (2 amps), `_CRS` GPIO index `1` is used for speaker ID. If `N` speaker-ID GPIOs are present, each amplifier reads `N / 2` pins starting at `amp_index * (N / 2)` and packs them LSB-first into its speaker ID.
- **Stopping the Halo DSP core**: Clear bit 0 in `0x02BC7000` (disable watchdog), clear bit 0 in `0x02BC1000` (disable core), and set bit 0 in `0x02B80010` (core soft reset).
- **Tuned gain unmute**: When DSP firmware is running and unmuting, `0x00006C04` is written with `(tuning_gain << 5) | default_pdm_gain`, where `tuning_gain` is read from the optional `.bincfg` file (signature `0x109A4A35`, version `1`, entry type `0`) or falls back to the default PCM gain.

---

Linux version read: `v7.3.5`
Exact file paths:
- `sound/soc/codecs/cs35l41-lib.c`
- `include/sound/cs35l41.h`
- `drivers/firmware/cirrus/cs_dsp.c`
- `include/linux/firmware/cirrus/cs_dsp.h`
- `include/linux/firmware/cirrus/wmfw.h`
- `sound/hda/codecs/side-codecs/cs35l41_hda.c`
- `sound/hda/codecs/side-codecs/cs35l41_hda_property.c`

Written by the reader; contains no Linux code.
