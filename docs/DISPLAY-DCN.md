# Display on DCN 2.1 (second screen over HDMI)

<!-- SPDX-License-Identifier: MIT -->

Status 27 Nov 2026: step 1 is written (hotplug and EDID, logged only). Steps 2 to 4 are a plan. Nothing in this document is tested on the 5625U yet.

This is the display track that `GPU-ARCHITECTURE.md` keeps apart ("Display stays a separate track") and `GRAPHICS.md` calls the second problem: modes, more than one monitor, vsync, and putting a frame on a screen. The GFX9 ring, RADV and the compositor are the other problem and stay as they are. None of the GFX9 contracts in `GRAPHICS.md` change for this track.

## Where we start

- Machine: Ryzen 5 5625U, PCI `1002:15e7`, HP subsystem `103c:8a31`. Display engine DCN 2.1 (Renoir/Barcelo). `radeon_hd` row: `RHD_DISPLAY_DCN, 2, 1`.
- The UEFI GOP lights the internal 1920x1080 eDP panel and leaves its framebuffer in VRAM. `radeon_hd` maps that framebuffer and never programs DCN: every DCE mode, PLL, encoder and DPMS path returns early on DCN (`mode.cpp`, `pll.cpp`, `display.cpp`, `accelerant.cpp`).
- The VBIOS is atomfirmware (master data table format 2). The accelerant already reads its display object table 1.4. On the 5625U (boot 55 syslog) it lists 6 paths: eDP (path 0, I2C id `0x90`, HPD pin `0x1`), **HDMI A (path 1, I2C id `0x91`, HPD pin `0x2`)**, and two DisplayPort paths (USB-C).
- Found while writing step 1: `gpio_general_populate()` reads the atomfirmware `gpio_pin_lut` with the old 4-byte `ATOM_GPIO_PIN_ASSIGNMENT`. The v2.1 entry is 8 bytes (`atom_gpio_pin_assignment`), so every pin comes out as `hwPin 0x0` and no connector finds its I2C or HPD pin. Step 1 does not change that function (the DCE paths use it); it reads the table itself. Step 2 fixes it.

## The steps

Each step is one PR and one 5625U boot. Step 1 only reads and is on by default on DCN 2.1, with a setting to turn it off. Steps 2 and later write display registers and stay opt-in until their boot passes, like the GFX9 stages. The software path and the firmware picture on the panel stay the fallback at every step.

### Step 1: hotplug and EDID (written, read-only for the picture)

What it does, once at accelerant start, for each HDMI and DVI connector in the VBIOS:

1. Takes the I2C id and HPD pin id from the display path records.
2. Looks both up in `gpio_pin_lut` the way `bios_parser2.c` does (`get_gpio_i2c_info()`, `bios_parser_get_gpio_pin_info()`): match `gpio_id`, take `data_a_reg_index` and `gpio_bitshift`.
3. Maps the A register to a DDC pad and the bit to an HPD number the way `hw_translate_dcn21.c` `offset_to_id()` does. An address that is not one of the DCN 2.1 pads below is not used.
4. Reads the hotplug state: `DC_GPIO_HPD_Y` (the pin, readable in every mode, `hw_gpio.c`), and `HPDn_DC_HPD_CONTROL` and `HPDn_DC_HPD_INT_STATUS` (the sense bits count only when `DC_HPD_EN` is set). Pure reads.
5. Reads the EDID over that DDC pad with the existing Haiku DDC code (`ddc2_read_edid1`), bit-banged through `DC_GPIO_DDCn_MASK/A/EN/Y`. The three registers it writes are saved first and put back after.
6. Logs connector, DDC pad, HPD number, raw HPD registers, connected yes or no, monitor name, vendor and product, and the preferred mode (first detailed timing).

It does not add a display to `gDisplay[]`, so app_server, the mode list and the Display page see exactly what they saw before. It is skipped on any GPU that is not DCN 2.1. `dcn_hdmi_probe false` in the radeon_hd settings turns it off. QEMU has no `radeon_hd` device, so the code never runs there.

Registers (absolute dword index = `DMU_BASE` segment 2 `0x34C0` + offset; byte address = index x 4):

| Register | Offset | Source |
|---|---|---|
| `DC_GPIO_DDC1_MASK/A/EN/Y` | `0x28d0`..`0x28d3` | `dcn_2_1_0_offset.h` |
| `DC_GPIO_DDC2`..`DDC5` | `0x28d4`, `0x28d8`, `0x28dc`, `0x28e0` (+0..+3) | same |
| `DC_GPIO_DDCVGA_*` | `0x28e8`..`0x28eb` | same |
| `DC_GPIO_HPD_A`, `DC_GPIO_HPD_Y` | `0x28f5`, `0x28f7` | same |
| `HPD0_DC_HPD_INT_STATUS`, `HPD0_DC_HPD_CONTROL` | `0x1f14`, `0x1f16`, stride 8 up to HPD4 | same |
| DDC CLK bit 0, DATA bit 8, `AUX_PADn_MODE` bit 16 | | `dcn_2_1_0_sh_mask.h` |
| `DC_HPD_SENSE` bit 1, `DC_HPD_SENSE_DELAYED` bit 4, `DC_HPD_EN` bit 28 | | same |
| HPD n uses block HPD(n-1) | | `hw_factory_dcn21.c` (`ONE_MORE_n`) |

Firmware: none. Risks: a wrong pad mapping would bit-bang another pad; the table check above refuses any address that is not a DDC pad, and the eDP connector is never probed. An empty port costs a few NACKed reads.

The 5625U boot must show (monitor on HDMI, from `/var/log/syslog`, at app_server start, after the `connector_probe_object_v14` lines):

```
radeon_hd: dcn hdmi: connector 1 HDMI A i2c 0x91 ddc DDCn (reg 0x....) hpd pin 0x02 HPDm (reg 0x5db5 bit b)
radeon_hd: dcn hdmi: connector 1 hpd control 0x........ status 0x........ gpio y 0x........ sense 0|1|off pin 0|1
radeon_hd: dcn hdmi: connector 1 connected yes (hpd high, edid yes)
radeon_hd: dcn hdmi: connector 1 monitor "<name>" vendor <PNP> product 0x.... edid 1.3|1.4
radeon_hd: dcn hdmi: connector 1 preferred mode 1920x1080 60.00 Hz pixel clock 148500 kHz
```

and the same boot without the monitor: `connected no (hpd low, edid no)`. The panel looks as before. Which DDC pad and HPD number HDMI uses comes from this VBIOS; the boot records them.

### Step 2: one HDMI head (modeset)

Goal: a test pattern on the HDMI monitor at its EDID preferred mode, with the panel untouched. Opt-in: `dcn_hdmi_modeset true`.

Before any write, a read-only sub-step (2a) logs which pipes the GOP uses: `OTGn_OTG_CONTROL` master enable, the eDP timing, `HUBPn` primary surface address (must equal the boot framebuffer), `DMCUB_CNTL` (is the DMCUB running for the panel), the current DISPCLK and DPPCLK. Step 2 then takes the first pipe the GOP left off. The pipe that drives the panel is never written.

The chain, in Linux DC terms (file names are the MIT sources to read, `drivers/gpu/drm/amd/display/dc/...`):

| Block | What R5 programs | Linux source |
|---|---|---|
| Clocks | DISPCLK and DPPCLK high enough for both heads, through the SMU mailbox the VBIOS uses (R5 already talks to SMU 12, `r5_smu12.cpp`). Never lowered while the panel runs. | `clk_mgr/dcn21/rn_clk_mgr.c`, `rn_clk_mgr_vbios_smu.c` |
| Pixel clock | PHY PLL through the VBIOS `SetPixelClock` table (v7), run by the accelerant's atom interpreter; DCCG pixel-rate source for the OTG | `bios/command_table2.c` `set_pixel_clock_v7`, `dccg/dcn20/dcn20_dccg.c`, `dcn21` DCCG |
| OTG | H/V total, sync, blank, then master enable | `optc/dcn20/dcn20_optc.c`, `optc/dcn10/dcn10_optc.c` `program_timing` |
| HUBP, DPP, MPC, OPP | Linear ARGB8888 surface in the VRAM pool, pitch, viewport; DPP bypass; one MPCC; OPP without dither | `hubp/dcn21/dcn21_hubp.c`, `dpp/dcn20`, `mpc/dcn20`, `opp/dcn20` |
| HUBBUB | Watermarks and DET for one more pipe, from the DCN 2.1 bounding box | `hubbub/dcn21/dcn21_hubbub.c`, `resource/dcn21/dcn21_resource.c` (`dcn2_1_soc`, `dcn2_1_ip`) |
| Stream encoder | DIG front end in HDMI/TMDS mode, AVI infoframe, no audio yet | `dio/dcn20/dcn20_stream_encoder.c` |
| Link encoder and PHY | TMDS enable through the VBIOS `DIGxTransmitterControl` table (v1.6), as `dcn10_link_encoder_enable_tmds_output()` does | `dio/dcn10/dcn10_link_encoder.c`, `dcn21/dcn21_link_encoder.c`, `bios/command_table2.c` `transmitter_control_v1_6` |

Firmware: DMCUB (`green_sardine_dmcub.bin` for `15e7`, `amdgpu_dm.c`) is **not** needed for an HDMI modeset. On Renoir Linux sends the VBIOS tables through DMCUB (`dmub_command_table = true` in `dcn21_resource.c`), but `command_table2.c` runs the same tables through the interpreter when there is no DMCUB service. R5 does not reset or load the DMCUB the GOP may be running for panel self-refresh.

Risks: an underflow from wrong watermarks also hits the panel (shared HUBBUB); raising DISPCLK can blank the panel for a frame; a bad surface address makes the HUBP read the wrong memory (picture garbage, not a hang, on VMID 0 physical addressing). Each write is logged, and a failed step disables the head and leaves the panel to the GOP state.

Test: one boot with `dcn_hdmi_modeset true` and the monitor attached. The boot must show colour bars on HDMI at the preferred mode, the panel unchanged, `OTG` and `HUBP` status logged, no underflow bit (`HUBP` underflow status) after 10 s. QEMU: the code must not run.

### Step 3: two pipes, panel and HDMI together

Goal: the desktop stays on the panel, a second scanout buffer is shown on HDMI, both stable for minutes.

- Watermarks and DISPCLK for two active pipes, computed from `dcn2_1_soc` and `dcn2_1_ip` (no guessed constants). Linux uses DML for this; R5 takes the bounding-box values and the formulas for two linear surfaces only.
- The panel's pipe stays the GOP's. Taking it over (vsync and modes on the panel) is a later step, not part of this track.
- Hotplug: the HPD interrupt (`DC_HPD_INT_CONTROL`, IH source for DCN HPD) moves into the kernel, so unplug and replug turn the head off and on.

Risks: the shared HUBBUB and DISPCLK are where both screens meet. Test: one boot, 10 minutes of desktop use on the panel with a moving pattern on HDMI, one unplug and replug; logged underflow counters stay 0.

### Step 4: app_server, compositor and the Display page

Goal: an extended desktop.

- Haiku's app_server already has a `VirtualScreen` that holds several `Screen` objects with frames, and `BScreen` already has screen ids. R5 uses that: one `Screen` per head, each with its own `HWInterface` on the same accelerant. The accelerant gets one R5 hook that selects a head; the existing hooks (`SET_DISPLAY_MODE`, `GET_EDID_INFO`, `GET_PREFERRED_DISPLAY_MODE`) then answer for that head. A Haiku program that knows only one screen keeps working: the main screen is the panel.
- The compositor composes per screen into that screen's scanout buffer (`R5_GPU_SCANOUT_BO` per head). Windows keep screen coordinates; a window across the seam is composed into both.
- The Display page fills `BScreenArrangement` from the real screens (name from the EDID, frame from `VirtualScreen`). The arrangement is saved per monitor (EDID vendor, product and serial).
- The CPU mode (Painter) and Lavapipe keep working with two screens; a second screen without a working modeset is not shown.

Test: one boot, drag a window from the panel to the HDMI screen, move the Deskbar, change the arrangement on the Display page.

## Kernel interface

The modeset stays in the accelerant, as for DCE: it owns the VBIOS parse and the atom interpreter, and the Haiku accelerant hooks are the API app_server uses. The kernel gets only what needs interrupts or must outlive app_server. These are new, additive calls in `radeon_hd`, MIT, vendor-neutral in shape (a head number, a buffer handle, a fence), not a port of `amdgpu_dm`:

| Call | Step | What |
|---|---|---|
| `R5_DISPLAY_GET_HEADS` | 3 | number of connectors, type, HPD state, cached EDID (128 bytes plus extensions) |
| `R5_DISPLAY_WAIT_HOTPLUG` | 3 | blocks until an HPD interrupt, returns which head changed |
| `R5_DISPLAY_FLIP` | 4 | head, scanout buffer handle, fence; the kernel writes the HUBP address at vblank |
| `R5_DISPLAY_WAIT_VBLANK` | 4 | head, returns the vblank count and time |

A call that shipped does not change. Step 1 needs none of these: it runs in the accelerant and only logs. Intel (step I0 in `GPU-ARCHITECTURE.md`) would answer the same calls from its own driver.

## Licensing

Checked 27 Nov 2026 on Linux `v6.12`: every file named in this document carries the AMD MIT (X11) permission notice ("Permission is hereby granted, free of charge...") and no GPL text. The files read for step 1:

- `drivers/gpu/drm/amd/include/renoir_ip_offset.h` (Copyright 2019 AMD)
- `drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_1_0_offset.h`, `dcn_2_1_0_sh_mask.h` (2019 AMD)
- `drivers/gpu/drm/amd/include/atomfirmware.h` (AMD)
- `drivers/gpu/drm/amd/display/dc/bios/bios_parser2.c` (2012-15 AMD)
- `drivers/gpu/drm/amd/display/dc/gpio/dcn21/hw_translate_dcn21.c`, `hw_factory_dcn21.c` (2018, 2013-15 AMD)
- `drivers/gpu/drm/amd/display/dc/gpio/hw_gpio.c`, `hw_ddc.c`, `hw_hpd.c`, `hpd_regs.h`, `ddc_regs.h` (2012-16 AMD)
- `drivers/gpu/drm/amd/display/dc/dce/dce_i2c_hw.c`, `dce_i2c_hw.h` (2018 AMD)
- `drivers/gpu/drm/amd/display/dc/resource/dcn21/dcn21_resource.c` (2018 AMD)
- `drivers/gpu/drm/amd/display/dc/link/link_detection.c` (2022 AMD)

Also checked for steps 2 to 4: `amdgpu_dm/amdgpu_dm.c`, `dmub/src/dmub_srv.c`, `clk_mgr/dcn21/rn_clk_mgr.c`, `rn_clk_mgr_vbios_smu.c`, `optc/dcn20/dcn20_optc.c`, `hubp/dcn21/dcn21_hubp.c`, `dccg/dcn20/dcn20_dccg.c`, `dcn21/dcn21_link_encoder.c`: all MIT.

R5 writes its own code from these and cites them; it does not paste them. MIT-to-MIT would allow copying, but the R5 code is much smaller than DC and has its own structure. The register offsets are facts taken from the headers. The DMCUB firmware is not used; if a later step needs it, it is a firmware blob under AMD's firmware licence (`LICENSE.amdgpu` in linux-firmware), checked in `docs/LICENSING.md` like the GFX blobs.

## Test rules for this track

- QEMU has no DCN. A QEMU boot only shows that nothing else broke.
- Every step needs one 5625U boot with an HDMI monitor attached, and its syslog lines recorded in `CHANGELOG.md`. A step without that boot is "written", not "tested".
- Each step stays opt-in on the 5625U until its boot passes, and becomes the default only by the owner's decision, as in "From opt-in to default" in `GPU-ARCHITECTURE.md`.
