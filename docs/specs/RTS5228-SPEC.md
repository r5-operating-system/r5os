# Realtek RTS5228 Hardware Specification (Clean-Room)

Target hardware: Realtek RTS5228 PCIe SD card reader, PCI `10ec:5228`, revision `01`, class `0xff:00:00`, subsystem `103c:8a31` (HP ENVY x360 15, Ryzen 5 5625U).

Register access conventions used in this document:
- **MMIO registers** (`0x00`–`0x1C`): 32-bit little-endian accesses in PCI BAR 0.
- **Internal registers** (`0xFC00`–`0xFF7F`): 8-bit registers accessed either individually through MMIO `0x10` (bit 31 = start/busy, bit 30 = `1` write / `0` read, bits `[29:16]` = 14-bit register address `addr & 0x3FFF`, bits `[15:8]` = write bitmask, bits `[7:0]` = write data / read result; poll bit 31 until `0`, up to 1024 polls) or in batches through the DMA host command buffer pointed to by MMIO `0x00`.
- **PCIe PHY registers** (`0x00`–`0x1F`): 16-bit registers accessed via internal registers `0xFE3D` (data low byte), `0xFE3E` (data high byte), `0xFE3F` (5-bit PHY register address), and `0xFE3C` (control: write `0x81` to start a write, `0x80` to start a read; poll bit 7 until `0`, up to 100,000 polls; for reads, then read `0xFE3D` and `0xFE3E`).
- **PCI configuration space**: standard PCI / PCIe configuration registers on the device function.

---

## 1. Identification

- **PCI IDs and class:** Vendor ID `0x10EC`, Device ID `0x5228`, PCI base class `0xFF` (mask `0xFF0000`).
- **MMIO BAR:** PCI BAR 0 (not BAR 1).
- **DMA addressing:** 32-bit DMA addresses.
- **Slots:** Single slot (SD only; no Memory Stick slot).
- **Supported SD speed modes (hardware capabilities):** Default Speed, High Speed, UHS-I SDR50, and UHS-I SDR104. (DDR50 and SD Express are not advertised.)
- **IC version:** Read 8-bit internal register `0xFE90` and mask with `0x0F` (bits `[3:0]`):
  - `0x0` = version A
  - `0x1` = version B
  - `0x2` = version C
  - `0x3` = version D
- **Per-revision differences:** None. All IC versions of the RTS5228 use identical register sequences and parameters.

---

## 2. Initialisation at Probe

### 2.1 PCI and host buffer setup

1. Enable 32-bit DMA, enable PCI memory space and bus mastering, and map PCI BAR 0.
2. Allocate a 4096-byte DMA-coherent host buffer:
   - Bytes `0..1023`: host command buffer (base DMA address written to MMIO `0x00`).
   - Bytes `1024..4095`: scatter-gather DMA descriptor table (up to 384 entries of 8 bytes each; base DMA address written to MMIO `0x08` before DMA transfers).
3. Enable MSI if available (falling back to legacy INTx) and attach the interrupt handler.

### 2.2 Reading initial hardware and PCIe configuration

1. **IC version:** Read internal register `0xFE90` & `0x0F`.
2. **Initial ASPM state:** Read internal register `0xFE57`. If both bits 5 and 4 (`0x30`) are set, ASPM is currently disabled; otherwise ASPM is currently enabled.
3. **PCIe L1 PM Substates and LTR:**
   - Locate PCIe Extended Capability ID `0x001E` (L1 PM Substates).
   - If present:
     - Read 32-bit L1 PM Substates Control 1 at extended capability offset `+0x08`:
       - bit 3 (`0x08`): ASPM L1.1 enabled
       - bit 2 (`0x04`): ASPM L1.2 enabled
       - bit 1 (`0x02`): PCI-PM L1.1 enabled
       - bit 0 (`0x01`): PCI-PM L1.2 enabled
     - Read 16-bit PCIe Device Control 2 (PCIe capability offset `+0x28`):
       - bit 10 (`0x0400`): LTR mechanism enabled.
     - If any of the four L1 substate bits (`[3:0]`) is set, CLKREQ# is **not** forced low; otherwise CLKREQ# is forced low.
   - If Extended Capability `0x001E` is absent: LTR is disabled and CLKREQ# is forced low.
4. **Vendor / board settings in PCI config space** (defaults before reading: 1.8 V drive index = `3`, 3.3 V drive index = `3`, ASPM enable = `0x02` [L1], RTD3 = `0`, MMC allowed, reverse socket = `0`, CD invert = `0`, WP invert = `0`):
   - Read 32-bit PCI config register `0x724`:
     - **Bit 24 (`0x01000000`):** Validity flag. `0` = valid; `1` = invalid. If bit 24 is `1`, skip parsing both `0x724` and `0x814` and keep all defaults.
     - **Bits `[29:28]`:** ASPM enable mask (`0` = none, `1` = L0s, `2` = L1, `3` = L0s+L1).
     - **Bits `[27:26]`:** 1.8 V SD 3.0 pad drive-strength index (`0`..`3`).
   - Read 32-bit PCI config register `0x814` (only when `0x724` bit 24 is `0`):
     - **Bit 1 (`0x00000002`):** Runtime D3 (RTD3) enabled when `1`.
     - **Bit 4 (`0x00000010`):** MMC support disabled when `1`.
     - **Bits `[6:5]`:** 3.3 V SD 3.0 pad drive-strength index (`0`..`3`).
     - **Bit 14 (`0x00004000`):** Reverse socket when `1` (inverts both card-detect and write-protect polarity).
     - **Bit 22 (`0x00400000`):** Invert SD write-protect polarity when `1` (used if bit 14 is `0`).
     - **Bit 23 (`0x00800000`):** Invert SD card-detect polarity when `1` (used if bit 14 is `0`).

### 2.3 Numbered hardware initialisation steps

This sequence runs at probe and again on resume from system suspend or runtime suspend (after clearing the sleep state register `0xFE60` bits `[1:0]` to `0x00`).

1. **LDO1 slew-rate ramp time:** Write `0xFF73`, mask `0xC0`, value `0x80` (0.5 ms ramp time; encoding: `0x00` = 0.0 ms, `0x40` = 0.25 ms, `0x80` = 0.5 ms, `0xC0` = 1.0 ms).
2. **Host command buffer address:** Write 32-bit MMIO `0x00` with the 32-bit DMA address of the host command buffer.
3. **Enable bus interrupts:** Write 32-bit MMIO `0x18` with `0x32400000` (bit 29 transfer OK, bit 28 transfer fail, bit 25 SD card detect, bit 22 SD overcurrent).
4. **Power on SSC PLL:** Write `0xFC00`, mask `0x01`, value `0x00`.
5. **Wait:** Delay **200 µs** for SSC power to stabilize.
6. **Disable ASPM** (if currently enabled):
   - Clear bits `[1:0]` of PCIe Link Control (PCIe cap `+0x10`) to `0`.
   - Write `0xFE57`, mask `0x33`, value `0x30`.
   - Write `0xFDA0`, mask `0x10`, value `0x00` (allow SD async FIFO reset).
   - Delay **10 ms**.
7. **Optimize PCIe PHY:** Write 16-bit PHY register `0x07` = `0x8F40`.
8. **Core clock, link, and interrupt configuration** (may be sent as one command-buffer batch, 100 ms timeout):
   - `0xFC03`, mask `0x07`, value `0x07` (MCU clock count = 7)
   - `0xFE60`, mask `0x03`, value `0x00` (host sleep state = active)
   - `0xFD69`, mask `0x1E`, value `0x00` (disable card clocks)
   - `0xFE5B`, mask `0x0A`, value `0x00` (reset delink mode)
   - `0xFD53`, mask `0xFF`, value `0x41` (card drive select default: bits 6 and 0)
   - `0xFC11`, mask `0xFF`, value `0x50` (SSC 8x enable [bit 6] + 4 MHz reference select [bit 4])
   - `0xFC12`, mask `0xFF`, value `0x03` (SSC depth = 2M for RTS5228)
   - `0xFE5B`, mask `0x16`, value `0x10` (disable card-detect power save)
   - `0xFE21`, mask `0x20`, value `0x20` (clear link-ready interrupt status)
   - `0xFE5C`, mask `0xFF`, value `0x80` (PERST# glitch filter width)
   - `0xFC14`, mask `0x01`, value `0x00` (switch RC oscillator from 2 MHz reset default to 400 kHz)
   - `0xFE72`, mask `0x02`, value `0x00` (set interrupt flags to write-1-to-clear instead of read-to-clear)
9. **Clock power management:** Write `0xFE58`, mask `0x01`, value `0x01`.
10. **Overcurrent protection (OCP) initialisation** (see Section 3.4 for full details):
    - `0xFF72`, mask `0x12`, value `0x12` (enable LDO1 OCP and current limit)
    - `0xFF72`, mask `0xE0`, value `0x60` (930 mA OCP threshold)
    - `0xFF72`, mask `0x0C`, value `0x08` (1500 mA current-limit threshold)
    - Read back `0xFF72`
    - `0xFD6C`, mask `0x0F`, value `0x05` (800 µs OCP glitch filter)
    - `0xFD6A`, mask `0xFF`, value `0x0C` (enable SD OCP detect and interrupt)
    - `0xFF72`, mask `0x12`, value `0x12`
11. **Enable PCIe CLKREQ#:** Set bit 8 (`0x0100`) in PCIe Link Control (PCIe cap `+0x10`).
12. **Host TX idle L1 entry:** Write 8-bit value `0x5B` to PCI config offset `0x70F`.
13. **RTS5228-specific hardware setup:**
    - `0xFF7C`, mask `0xF0`, value `0xF0` (enable card-detect resume).
    - **OOBS polling:**
      - If any L1 PM substate (ASPM L1.1/L1.2 or PCI-PM L1.1/L1.2) is enabled, **disable** OOBS polling:
        - Read PHY register `0x01`, clear bit 9 (`0x0200`), write back PHY register `0x01`.
        - Write `0xFEA8`, mask `0xFF`, value `0x03`.
        - Write `0xFEA9`, mask `0xFF`, value `0x00`.
      - Otherwise, **enable** OOBS polling:
        - Read PHY register `0x01`, set bit 9 (`0x0200`), write back PHY register `0x01`.
        - Write `0xFEA6`, mask `0xFF`, value `0x32`.
        - Write `0xFEA7`, mask `0xFF`, value `0x05`.
        - Write `0xFEA8`, mask `0xFF`, value `0x83`.
        - Write `0xFEA9`, mask `0xFF`, value `0xDE`.
    - `0xFE57`, mask `0xFF`, value `0x00` (clear ASPM force control).
    - **Active LTR latency** (only if LTR is enabled in PCIe Device Control 2):
      - Write `0x883C` (60 µs) little-endian to `0xFE44`..`0xFE47`: `0xFE44` = `0x3C`, `0xFE45` = `0x88`, `0xFE46` = `0x00`, `0xFE47` = `0x00`.
      - Write `0xFE4A`, mask `0xC0`, value `0xC0` (enable LTR transmission [bit 7] and software latency mode [bit 6]).
    - `0xFE8D`, mask `0x01`, value `0x00` (L1 substate aux clock active select = MAC clock switch done).
    - `0xFE8F`, mask `0xFF`, value `0x00` (clear L1 substate power-gate config 3).
    - `0xFE59`, mask `0x02`, value `0x02` (force UPME transmit debug).
    - `0xFE55`, mask `0x20`, value `0x20` (PCLK mode select).
    - `0xFE71`, mask `0x08`, value `0x08` (PM event debug bit 3).
    - `0xFE58`, mask `0x01`, value `0x01` (clock PM enable).
    - `0xFC1E`, mask `0x0F`, value `0x02` (disable LED auto-blink, set initial blink period to 2).
    - Program 3.3 V pad drive strengths into `0xFD5A`, `0xFD5E`, `0xFD5F` (see Section 3.3).
    - **Card-detect / write-protect polarity** in `0xFF03`:
      - If reverse socket is set: write `0xFF03`, mask `0x30`, value `0x30`.
      - Otherwise: write `0xFF03`, mask `0x20`, value `0x20` if CD polarity is inverted (else `0x00`), and `0xFF03`, mask `0x10`, value `0x10` if WP polarity is inverted (else `0x00`).
    - **CLKREQ# force** in `0xFF03`:
      - If CLKREQ# is forced low (no L1 substates enabled): write `0xFF03`, mask `0x80`, value `0x80`.
      - Otherwise: write `0xFF03`, mask `0x80`, value `0x00`.
    - `0xFE76`, mask `0xFF`, value `0xFB` (power-down suspend enable).
    - **PME / RTD3 control:**
      - If RTD3 is enabled:
        - `0xFF7E`, mask `0x01`, value `0x01` (PM wake enable).
        - `0xFF78`, mask `0x30`, value `0x30` (force PM control = 1, force PM value = 1).
      - Else:
        - `0xFF7E`, mask `0x01`, value `0x00`.
        - `0xFF78`, mask `0x30`, value `0x20` (force PM control = 1, force PM value = 0).
    - `0xFF7E`, mask `0x10`, value `0x00` (clear D3 delink mode enable).
14. **Assert ASPM force bits:** Write `0xFE57`, mask `0x30`, value `0x30` (register-based ASPM control starts with ASPM forced off until idle).
15. **Initial card presence:** Read 32-bit MMIO `0x14` and test bit 16 (`0x00010000`) to detect a card already inserted at probe.

**LED control registers (optional):**
- LED on: `0xFC1F`, mask `0x02`, value `0x02`.
- LED off: `0xFC1F`, mask `0x02`, value `0x00`.
- Auto-blink enable: `0xFC1E`, mask `0x08`, value `0x08`.
- Auto-blink disable: `0xFC1E`, mask `0x08`, value `0x00`.

---

## 3. Card Power On and Off

### 3.1 Pull-up / pull-down tables

Internal registers `0xFD61` and `0xFD62` configure the SD bus pad resistors (2 bits per pin).

| Bus state | Register | Mask | Value | Pin resistor state |
| :--- | :--- | :--- | :--- | :--- |
| **Enabled** (card active) | `0xFD61` | `0xFF` | `0xAA` | `SD_DAT[3:0]` pull-up |
| | `0xFD62` | `0xFF` | `0xE9` | `SD_CD`, `SD_WP`, `SD_CMD` pull-up; `SD_CLK` pull-down |
| **Disabled** (card powered off) | `0xFD61` | `0xFF` | `0x55` | `SD_DAT[3:0]` pull-down |
| | `0xFD62` | `0xFF` | `0xD5` | `SD_CD` pull-up; `SD_WP`, `SD_CMD`, `SD_CLK` pull-down |

### 3.2 Card power on (3.3 V initial supply)

Before invoking the chip power-on sequence, the generic SD path waits 100 ms, selects the SD module (`0xFD5C` mask `0x07` = `0x02`), sets 48-pin SD share mode (`0xFD52` mask `0x0F` = `0x04`), enables the SD clock (`0xFD69` mask `0x04` = `0x04`), and applies the **Enabled** pull-up/pull-down table above.

Then perform the RTS5228 regulator power-on sequence in order:

1. **Enable OCP** (if OCP is enabled):
   - Write `0xFD6A`, mask `0xFF`, value `0x0C` (enable SD OCP detect and interrupt).
   - Write `0xFF72`, mask `0x12`, value `0x12` (enable LDO1 OCP and current limit).
2. **Auto power-down on card remove:** Write `0xFD71`, mask `0x01`, value `0x01`.
3. **Set LDO1 output to 3.3 V:** Write `0xFF73`, mask `0x0E` (bits `[3:1]`), value `0x0E` (`0x07 << 1`; `0x05 << 1` = `0x0A` is 1.8 V).
4. **LDO1 soft-start:** Write `0xFF70`, mask `0x03`, value `0x01`.
5. **Wait:** Delay **2 ms**.
6. **LDO1 full-on:** Write `0xFF70`, mask `0x03`, value `0x03`.
7. **Enable LDO3318 (I/O voltage regulator):** Write `0xFF70`, mask `0x08`, value `0x08`.
8. **Wait:** Sleep **20 ms** for card power rail stabilization.
9. **Enable SD pad output:** Write `0xFD55`, mask `0x04`, value `0x04`.
10. **Initialize SD controller registers:**
    - `0xFDA0`, mask `0xFF`, value `0x80` (clock divide-by-128, SD 2.0 mode, 1-bit bus width).
    - `0xFDA7`, mask `0xFF`, value `0x00` (SD 2.0 RX positive edge).
    - `0xFDA8`, mask `0xFF`, value `0x00` (SD 2.0 TX default).
    - `0xFD54`, mask `0x44`, value `0x44` (SD stop + SD clear error).
    - `0xFDA2`, mask `0x10`, value `0x00` (disable SD 3.0 clock-end).
    - `0xFDB8`, mask `0x07`, value `0x00` (clear SD 3.0 clock-stop config).
11. **Configure SD 3.0 sample/push clock sources** (since 5228 supports SDR50/SDR104):
    - `0xFDA0`, mask `0x1C`, value `0x18` (SD 3.0 mode + async FIFO not reset).
    - `0xFC02`, mask `0x01`, value `0x01` (low-frequency clock).
    - `0xFC2E`, mask `0xFF`, value `0x21` (CRC = variable clock 0, SD30 = fixed clock, sample = variable clock 1).
    - `0xFC02`, mask `0x01`, value `0x00` (normal clock).
12. After power-on, the generic SD path delays **1 ms**, ensures `0xFD55` bit 2 (`0x04`) is set, and starts clock toggling (`0xFDA5`, mask `0x80`, value `0x80`) for the 74-clock card initialization period.

### 3.3 Signal voltage switch (3.3 V / 1.8 V) and drive strength

When switching to 1.8 V, first stop SD clock toggling by writing `0xFDA5`, mask `0xFF`, value `0x40` (force-stop SD clock).

Then execute the RTS5228 voltage switch:
1. Write `0xFD50`, mask `0x20`, value `0x20` (enable pull-up/pull-down discharge control).
2. **For 3.3 V:**
   - Read 16-bit PHY register `0x08`, set bit 9 (`0x0200`), write back PHY register `0x08`.
   - Write `0xFF71`, mask `0x70` (bits `[6:4]`), value `0x70` (DV3318 = 3.3 V).
   - Write `0xFDA6`, mask `0x80`, value `0x00` (SD pads using 3.3 V).
3. **For 1.8 V:**
   - Read 16-bit PHY register `0x08`, clear bit 9 (`0x0200`), write back PHY register `0x08`.
   - Write `0xFF71`, mask `0x70` (bits `[6:4]`), value `0x20` (DV3318 = 1.8 V; other encodings in bits `[6:4]`: `0x00` = 1.7 V, `0x10` = 1.75 V, `0x20` = 1.8 V, `0x30` = 1.85 V, `0x40` = 1.9 V, `0x70` = 3.3 V).
   - Write `0xFDA6`, mask `0x80`, value `0x80` (SD pads using 1.8 V).
4. **Program pad drive strength** into `0xFD5A` (SD_CLK), `0xFD5E` (SD_CMD), and `0xFD5F` (SD_DAT), mask `0xFF` each, using the 2-bit index (`0`..`3`) from PCI config space (default index `3`):

| Index | 3.3 V: `0xFD5A` (CLK) | 3.3 V: `0xFD5E` (CMD) | 3.3 V: `0xFD5F` (DAT) | 1.8 V: `0xFD5A` (CLK) | 1.8 V: `0xFD5E` (CMD) | 1.8 V: `0xFD5F` (DAT) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `0` | `0x13` | `0x13` | `0x13` | `0x99` | `0x99` | `0x99` |
| `1` | `0x96` | `0x96` | `0x96` | `0xB5` | `0xB5` | `0xB5` |
| `2` | `0x7F` | `0x7F` | `0x7F` | `0xE6` | `0x7E` | `0xFE` |
| `3` (default) | `0x96` | `0x96` | `0x96` | `0x6B` | `0x6B` | `0x6B` |

### 3.4 Card power off

1. Disable SD clock and output enable: `0xFD69` mask `0x04` = `0x00`; `0xFD55` mask `0x04` = `0x00`.
2. **Stop commands and DMA** (RTS5228-specific reset register):
   - Write 32-bit MMIO `0x04` = `0x10000000` (bit 28: stop command).
   - Write 32-bit MMIO `0x0C` = `0x10000000` (bit 28: stop DMA).
   - Write `0xFEBF`, mask `0xC0`, value `0xC0` (bit 7 = DMA reset, bit 6 = ADMA3 reset).
   - Write `0xFE34`, mask `0x80`, value `0x80` (flush ring buffer).
3. Switch signal voltage back to 3.3 V (Section 3.3).
4. **Turn off LDO1 and LDO3318:** Write `0xFF70`, mask `0x0F`, value `0x00`.
5. **Disable auto power-down:** Write `0xFD71`, mask `0x01`, value `0x00`.
6. **Disable OCP** (if enabled):
   - Write `0xFD6A`, mask `0x0C`, value `0x00`.
   - Write `0xFF72`, mask `0x12`, value `0x00`.
7. Apply the **Disabled** pull-up/pull-down table (`0xFD61` = `0x55`, `0xFD62` = `0xD5`).

### 3.5 Overcurrent protection (OCP)

- **LDO1 OCP configuration register `0xFF72`:**
  - Bits `[7:5]`: OCP current threshold (`0x0` = 730 mA, `0x1` = 780 mA, `0x2` = 860 mA, **`0x3` = 930 mA [default]**, `0x4` = 1000 mA, `0x5` = 1070 mA, `0x6` = 1140 mA, `0x7` = 1220 mA).
  - Bit 4 (`0x10`): LDO1 OCP enable.
  - Bits `[3:2]`: Current-limit threshold (`0x0` = 450 mA, `0x1` = 1000 mA, **`0x2` = 1500 mA [default]**, `0x3` = 2000 mA).
  - Bit 1 (`0x02`): LDO1 current-limit enable.
- **Glitch filter register `0xFD6C`:** Bits `[3:0]` = `0x05` (800 µs).
- **Control register `0xFD6A`:** Bit 3 (`0x08`) = detect enable, bit 2 (`0x04`) = interrupt enable, bit 1 (`0x02`) = interrupt clear, bit 0 (`0x01`) = OC latch clear.
- **Status register `0xFD6E`:** Bit 2 (`0x04`) = OC active now, bit 1 (`0x02`) = OC occurred ever.
- **Handling an OCP event** (triggered by MMIO `0x14` bit 22 = `0x00400000`):
  1. Read `0xFD6E`. If bits 2 or 1 (`0x06`) are set:
  2. **Clear OCP status BEFORE powering off** (1 ms pulse):
     - Write `0xFD6A`, mask `0x03`, value `0x03`.
     - Delay **1000 µs** (1 ms).
     - Write `0xFD6A`, mask `0x03`, value `0x00`.
  3. Run the card power-off sequence (Section 3.4).
  4. Write `0xFD55`, mask `0x04`, value `0x00` (ensure SD output enable is off).

---

## 4. Clock

### 4.1 Divider and PLL `N` calculation

1. **Initial vs normal mode:**
   - If the requested SD clock is <= 1 MHz (initial identification mode):
     - Write `0xFDA0`, mask `0xC0`, value `0x80` (divide-by-128).
     - Substitute a base clock of `30 MHz` (producing ~234 kHz on `SD_CLK`).
   - Otherwise:
     - Write `0xFDA0`, mask `0xC0`, value `0x00` (divide-by-1).
2. **Internal SSC clock frequency ($f_{\text{MHz}}$):**
   - Divide the target clock in Hz by 1,000,000 (integer MHz).
   - If not in initial mode and double-clock mode is used (all modes **except** SDR50 and SDR104), multiply by 2.
   - If the resulting internal frequency equals the currently programmed internal frequency, return without touching the PLL.
3. **Range check and initial PLL multiplier $N$:**
   - Start with $N = f_{\text{MHz}} - 4$.
   - Valid range: $f_{\text{MHz}} > 4$ and $N \le 396$ (i.e., 5 MHz to 400 MHz).
4. **MCU clock count:**
   - Compute $\lfloor 125 / f_{\text{MHz}} \rfloor + 3$, clamped to a maximum of `15`.
5. **Post-divider loop:**
   - Start with divider code `1` (encoding in `0xFC03` bits `[7:4]`: `1` = /1, `2` = /2, `3` = /4, `4` = /8).
   - While $N < 76$ and divider code `< 4`:
     - Replace $N$ with $(N + 4) \times 2 - 4$ and increment the divider code by 1.
6. **Final RTS5228 $N$ scaling:**
   - Replace $N$ with $\lfloor N / 2 \rfloor - 1$. This 8-bit value is written to `0xFC0F`.

### 4.2 SSC depth encoding and per-speed settings

Register `0xFC12` bits `[2:0]` encode the spread-spectrum depth (shifted by +1 compared to older Realtek readers):

| Code (`0xFC12[2:0]`) | Depth |
| :--- | :--- |
| `0x00` | Disabled |
| `0x01` | 8M |
| `0x02` | 4M |
| `0x03` | 2M |
| `0x04` | 1M |
| `0x05` | 512K |
| `0x06` | 256K |
| `0x07` | 128K |

**Per-speed nominal parameters:**

| SD bus timing | Nominal SSC depth | Double-clock mode | Reset variable-phase clocks |
| :--- | :--- | :--- | :--- |
| SDR50, SDR104 | `0x03` (2M) | No | Yes |
| SDR25, DDR50, MMC DDR52 | `0x04` (1M) | Yes | No |
| Identification, Default Speed, High Speed | `0x05` (512K) | Yes | No |

**Depth adjustment rules:**
1. If double-clock mode is active and depth code `> 1`: decrement depth code by 1.
2. If depth code `!= 0`:
   - For divider code `2` (/2): subtract 1 from depth code, clamped to a minimum of `0x01` (8M).
   - For divider code `3` (/4): subtract 2 from depth code, clamped to a minimum of `0x01` (8M).
   - For divider code `4` (/8): subtract 3 from depth code, clamped to a minimum of `0x01` (8M).

### 4.3 Register programming sequence

Send steps 1–7 as a single command-buffer batch (2000 ms timeout):

1. `0xFC02`, mask `0x01`, value `0x01` (switch to low-frequency clock).
2. `0xFC03`, mask `0xFF`, value `(divider code << 4) | (MCU clock count)`.
3. `0xFC11`, mask `0x80`, value `0x00` (assert SSC PLL reset).
4. `0xFC12`, mask `0x07`, value = adjusted depth code.
5. `0xFC0F`, mask `0xFF`, value = final $N$.
6. `0xFC11`, mask `0x80`, value `0x80` (release SSC PLL reset).
7. **If variable-phase clocks are used (SDR50 / SDR104), reset BOTH variable-phase clocks:**
   - `0xFC2A`, mask `0x40`, value `0x00` (reset TX phase clock)
   - `0xFC2B`, mask `0x40`, value `0x00` (reset RX phase clock)
   - `0xFC2A`, mask `0x40`, value `0x40` (release TX phase clock)
   - `0xFC2B`, mask `0x40`, value `0x40` (release RX phase clock)
8. **Wait:** Delay **130 µs** for the SSC clock to stabilize.
9. Write `0xFC02`, mask `0x01`, value `0x00` (switch back to the programmed clock).

*(Note on initial UHS-I clock phases if tuning is added later: TX phases = SDR104: 28, SDR50: 27, DDR50: 11; RX phases = SDR104: 24, SDR50: 6, DDR50: 5.)*

---

## 5. Card Detect and Interrupts

### 5.1 MMIO interrupt registers

- **Interrupt enable register:** 32-bit MMIO `0x18`.
  - Enabled mask for RTS5228: `0x32400000`:
    - Bit 29 (`0x20000000`): Transfer OK interrupt enable.
    - Bit 28 (`0x10000000`): Transfer Fail interrupt enable.
    - Bit 25 (`0x02000000`): SD card-detect change interrupt enable.
    - Bit 22 (`0x00400000`): SD overcurrent interrupt enable.
- **Interrupt pending / live status register:** 32-bit MMIO `0x14`.
  - Bit 30 (`0x40000000`): Data done.
  - Bit 29 (`0x20000000`): Transfer OK.
  - Bit 28 (`0x10000000`): Transfer Fail.
  - Bit 25 (`0x02000000`): SD card-detect event latched.
  - Bit 24 (`0x01000000`): Delink interrupt.
  - Bit 22 (`0x00400000`): SD overcurrent event latched.
  - Bit 19 (`0x00080000`): SD write-protect live status (`1` = write-protected; polarity adjusted by `0xFF03` bit 4).
  - Bit 16 (`0x00010000`): SD card present live status (`1` = card inserted; polarity adjusted by `0xFF03` bit 5).

### 5.2 Interrupt handling and debounce

1. Read 32-bit MMIO `0x14`.
2. Immediately write the read value back to MMIO `0x14` to clear latched interrupt bits (write-1-to-clear, configured by `0xFE72` bit 1 = `0`).
3. If `(status & enabled_mask) == 0`, the interrupt is not from this device. If `status == 0xFFFFFFFF`, the device is inaccessible/gone; ignore.
4. Mask `status` with `(enabled_mask | 0x007FFFFF)` so live status bits `[22:0]` remain visible.
5. If bit 22 (`0x00400000`) is set: handle SD overcurrent (Section 3.5).
6. If bit 25 (`0x02000000`) is set:
   - If bit 16 (`0x00010000`) is `1`: record card inserted.
   - If bit 16 (`0x00010000`) is `0`: record card removed (and clear any pending inserted flag).
   - If bit 22 (overcurrent) is NOT set, schedule a **200 ms** debounce delay; after 200 ms, re-read MMIO `0x14` bit 16 to confirm insertion before notifying the SD stack.
7. If bits 30, 29, 28, or 24 (`0x71000000`) are set: complete the in-flight command/DMA transfer (fail on bit 28 or bit 24; success on bit 29).

---

## 6. Power Management

### 6.1 Active / idle transitions (ASPM, LTR, L1 substates)

- **ASPM control on RTS5228** uses BOTH the internal force register `0xFE57` and PCIe Link Control (PCIe cap `+0x10`, bits `[1:0]`):
  - **Enable ASPM (entering idle):**
    1. Write `0xFE57`, mask `0x33`, value `0x30 | (configured ASPM bits & 0x02)` (sets bits `[5:4]` = `11`, and bit 1 = L1 enable bit from vendor settings).
    2. Write PCIe Link Control (PCIe cap `+0x10`) bits `[1:0]` = configured ASPM bits.
  - **Disable ASPM (returning to active):**
    1. Clear PCIe Link Control (PCIe cap `+0x10`) bits `[1:0]` to `0`.
    2. Write `0xFE57`, mask `0x33`, value `0x30` (force ASPM off).
    3. Write `0xFDA0`, mask `0x10`, value `0x00` (allow SD async FIFO reset).
    4. Delay **10 ms**.
- **L1 substate power-gate register `0xFE8F`** (when L1 substates power gating is active):
  - **Active (D0 running, 60 µs latency):** If ASPM L1.1 is enabled, write `0xFE8F` = `0x78`; otherwise write `0x00`.
  - **Idle / L1-off (300 µs latency):** If ASPM L1.2 is enabled, write `0xFE8F` = `0x7F`; otherwise write `0x00`.
- **Entering active state (before SD operations):**
  1. Enable LED auto-blink (`0xFC1E`, mask `0x08`, value `0x08`).
  2. Disable ASPM (above).
  3. Sleep **1 ms**.
  4. If LTR is enabled: write active LTR latency `0x883C` (60 µs) to `0xFE44`..`0xFE47` and `0xFE4A` mask `0xC0` = `0xC0`.
  5. Update `0xFE8F` for active state (`0x78` if ASPM L1.1 enabled, else `0x00`).
- **Entering runtime idle:**
  1. Disable LED auto-blink (`0xFC1E`, mask `0x08`, value `0x00`) and turn LED off (`0xFC1F`, mask `0x02`, value `0x00`).
  2. If LTR is enabled: write L1-off LTR latency `0x9003` (3 ms) to `0xFE44`..`0xFE47` (`0x03`, `0x90`, `0x00`, `0x00`) and `0xFE4A` mask `0xC0` = `0xC0`.
  3. Update `0xFE8F` for idle state (`0x7F` if ASPM L1.2 enabled, else `0x00`).
  4. Enable ASPM (above).
  5. If RTD3 is enabled, runtime suspend may follow after an idle timeout (10 s).

### 6.2 System suspend, runtime suspend, and shutdown

All three call the power-off sequence with different parameters:
- **System suspend (S3):** target sleep state = `0x02` (S3), runtime = no.
- **Runtime suspend (RTD3):** target sleep state = `0x02` (S3), runtime = yes.
- **System shutdown:** target sleep state = `0x01` (S1), runtime = no (followed by disabling PCI device, freeing IRQ, and disabling MSI).

**Power-off register sequence:**
1. Turn off LED: `0xFC1F`, mask `0x02`, value `0x00`.
2. Disable all bus interrupts: write 32-bit MMIO `0x18` = `0x00000000`.
3. Write `0xFF03`, mask `0x08`, value `0x08`.
4. Write `0xFE60`, mask `0x03`, value = target sleep state (`0x02` for S3 / runtime suspend, `0x01` for S1 / shutdown).
5. Clear relink time to 0:
   - `0xFF01`, mask `0xFF`, value `0x00`.
   - `0xFF02`, mask `0xFF`, value `0x00`.
   - `0xFF03`, mask `0x01`, value `0x00`.
6. Enable D3 delink mode: `0xFF7E`, mask `0x10`, value `0x10`.
7. **Only on system suspend and shutdown (skipped on runtime suspend so card-insert wake stays armed):**
   - `0xFF7C`, mask `0xF0`, value `0x00` (disable card-detect resume).
   - `0xFF7E`, mask `0x01`, value `0x00` (disable PM wake).
   - `0xFF78`, mask `0x30`, value `0x20` (force PM control = 1, force PM value = 0).
8. Power down SSC PLL: `0xFC00`, mask `0x01`, value `0x01`.

### 6.3 Resume (from system suspend or runtime suspend)

1. Write `0xFE60`, mask `0x03`, value `0x00` (clear host sleep state).
2. Re-run the full hardware initialisation sequence (Section 2.3).
3. On runtime resume, re-check card presence and notify the SD stack.

### 6.4 Driver unload / removal

1. Wake the device if runtime-suspended.
2. Write 32-bit MMIO `0x18` = `0x00000000` (disable all bus interrupts).
3. Cancel pending card-detect work and tear down the SD child device.
4. Free the 4096-byte DMA buffer, free IRQ, disable MSI, unmap BAR 0, release PCI regions, and disable the PCI device.

---

## 7. Differences from the Older Chips

Summary of everything the RTS5228 does differently from the older Realtek PCIe readers (RTS5209, 5227, 5229, 522A, 5249, 525A):

1. **Scatter-gather DMA descriptor format (critical):**
   - Older chips place the 20-bit transfer length at bits `[31:12]` of the 64-bit little-endian descriptor (`(addr << 32) | (len << 12) | flags`).
   - **RTS5228** places the low 16 bits of length at bits `[31:16]` and any upper length bits (`len > 0xFFFF`) at bits `[15:6]`:
     - `bits[63:32]` = 32-bit DMA address
     - `bits[31:16]` = `len & 0xFFFF`
     - `bits[15:6]` = `len >> 16`
     - `bits[5:0]` = flags (`0x01` valid, `0x02` end, `0x20` transfer data)
2. **DMA reset register on command stop:**
   - Older chips write `0x80` to `0xFE2C`.
   - **RTS5228** writes mask `0xC0`, value `0xC0` (DMA reset + ADMA3 reset) to `0xFEBF` (followed by `0xFE34` bit 7 flush as usual).
3. **LDO / card power registers moved and redesigned:**
   - Card power is NOT controlled via `0xFD50` bits `[1:0]` or `0xFE75`.
   - **RTS5228** uses `0xFF70` (LDO1 soft-start `0x01` -> 2 ms -> full-on `0x03`, LDO3318 on `0x08`; off = `0x00`), `0xFF71` bits `[6:4]` (DV3318 voltage: `0x70` = 3.3 V, `0x20` = 1.8 V), `0xFF73` (bits `[7:6]` = `0x80` 0.5 ms ramp time at init; bits `[3:1]` = `0x0E` for 3.3 V), `0xFD71` bit 0 (auto power-down), and `0xFD50` bit 5 (`0x20` on voltage switch).
   - Signal voltage switch also toggles bit 9 (`0x0200`) of PCIe PHY register `0x08` (`1` for 3.3 V, `0` for 1.8 V) and `0xFDA6` bit 7 (`0` for 3.3 V, `1` for 1.8 V).
4. **Drive strength tables and registers:**
   - Uses three separate 8-bit registers `0xFD5A` (CLK), `0xFD5E` (CMD), and `0xFD5F` (DAT) with RTS5228-specific lookup tables (Section 3.3) rather than a single 2-bit drive-type register.
5. **Overcurrent protection (OCP) registers and order:**
   - Thresholds and enable bits live in `0xFF72` (not `0xFD6B`/`0xFD6D`, and `0xFC00` bit 1 is not used for OCP power-down).
   - Clearing OCP status (`0xFD6A` bits `[1:0]`) uses a **1000 µs** pulse (not 100 µs) and happens **before** powering off the card.
6. **Clock PLL formula, SSC depth encoding, and RX variable-phase reset:**
   - Initial `N = clk - 4` (not `clk - 2`), minimum loop threshold `N < 76` (not `80`), doubling step `(N + 4) * 2 - 4`, and final scaling `N = (N / 2) - 1` before writing `0xFC0F`.
   - `0xFC12` SSC depth codes are shifted by +1 (`0x01` = 8M, `0x02` = 4M, `0x03` = 2M, `0x04` = 1M, `0x05` = 512K, `0x06` = 256K, `0x07` = 128K); init writes `0x03` (2M) instead of `0x12`.
   - When variable-phase clocks are used (SDR50/SDR104), both `0xFC2A` and `0xFC2B` bit 6 are toggled low then high (older chips toggle only `0xFC2A`).
7. **Power management and autoload register addresses:**
   - Card-detect resume is at `0xFF7C` bits `[7:4]` (not `0xFF44`).
   - PM wake / D3 delink is at `0xFF7E` (not `0xFF46`).
   - PME force control is at `0xFF78` bits `[5:4]` (not `0xFE56`).
   - Power-down writes only `0xFC00` bit 0 (SSC power-down), not `0x03` (all power-down).
   - ASPM enable/disable writes BOTH `0xFE57` (mask `0x33`) and PCIe Link Control bits `[1:0]`; disabling ASPM also clears `0xFDA0` bit 4 and waits 10 ms.
   - L1 substate power-gate values in `0xFE8F` are `0x78` (active, L1.1) and `0x7F` (idle, L1.2), rather than `0xAC`/`0xAF` or `0xF8`/`0xFF`.
8. **PHY optimization and polarity bits:**
   - Single PHY optimization write at init: PHY register `0x07` = `0x8F40`.
   - Separate CD-invert (config `0x814` bit 23 -> `0xFF03` bit 5) and WP-invert (config `0x814` bit 22 -> `0xFF03` bit 4) in addition to reverse-socket (config `0x814` bit 14 -> `0xFF03` bits `[5:4]`).
   - `0xFE58` bit 0 (clock PM enable) is set during init.
9. **SD/MMC core path:**
   - No RTS5228-specific differences exist in the SD command, data transfer, or bus-width paths beyond the DMA descriptor format and the card power/clock/voltage hooks above.

---

## 8. Not Covered

- **UHS-I phase tuning** (CMD19 tuning loop and DCM phase sweeping on `0xFC2A`/`0xFC2B`/`0xFC2C`/`0xFC2D`).
- **SD Express** (not supported by the RTS5228).

---

**Source reference:** Linux `7.3.5`
- `drivers/misc/cardreader/rts5228.c`
- `drivers/misc/cardreader/rts5228.h`
- `drivers/misc/cardreader/rtsx_pcr.c`
- `drivers/misc/cardreader/rtsx_pcr.h`
- `drivers/mmc/host/rtsx_pci_sdmmc.c`
- `include/linux/rtsx_pci.h`

Written by the reader; contains no Linux code.
