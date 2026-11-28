# Display on DCN 2.1 (second screen over HDMI)

<!-- SPDX-License-Identifier: MIT -->

Status 27 Nov 2026: steps 1, 2a (read only), 2 (opt-in test pattern on HDMI) and 4 (opt-in, app_server with more than one screen) are written. Step 3 is a plan. Nothing in this document is tested on the 5625U yet.

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
5. Reads the EDID over that DDC pad with the existing Haiku DDC code (`ddc2_read_edid1`), bit-banged through `DC_GPIO_DDCn_MASK/A/EN/Y`. The three registers it writes are saved first and put back after. Deliberate difference: Linux reads the EDID with the DC_I2C hardware engine (`dce/dce_i2c_hw.c`); bit-banging the same pad is what Linux's own software engine (`dce/dce_i2c_sw.c`) does, and it leaves the DC_I2C arbitration alone, which the firmware or DMCUB may hold.
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

### Step 2a: what the firmware left (written, reads only)

Part of the step 1 probe (same switch). Logs, once: `DMCUB_CNTL` (is the DMCUB running for the panel), `DENTIST_DISPCLK_CNTL`; for OTG 0 to 3 the control word (on or off), total and active size, `OTGn_PIXEL_RATE_CNTL` and the frame counter; for HUBP 0 to 3 `DCHUBP_CNTL` and the primary surface address (the panel's must be the boot framebuffer); OPP and DPG control; `DIGn_DIG_FE_CNTL` and `DIG_BE_CNTL` for DIG 0 to 4; and which VBIOS display tables exist with their revision (`atom_parse_cmd_header`). Lines start with `radeon_hd: dcn head:`. Step 2 needs this boot's answers: which OTG the panel uses, which DIG drives eDP, and whether `setpixelclock` is v1.7, `digxencodercontrol` v1.5 and `dig1transmittercontrol` v1.6.

### Step 2: one HDMI head with a test pattern (written, opt-in)

Goal: colour squares on the HDMI monitor at its EDID mode, with the panel untouched. Opt-in: `dcn_hdmi_modeset true` (`dcn21_head.cpp`).

This is Linux DC's sequence for a stream **without a plane**: the stream is lit, and the OPP's display pattern generator (DPG) fills it. Linux does exactly this when a stream has no plane yet (`dcn20_blank_pixel_data()` keeps the DPG on). With `visual_confirm` on, Linux draws colour squares there instead of black; R5 uses that pattern so the owner can see the link works. No HUBP, DPP or MPC runs, so no watermark, DET or DISPCLK change reaches the panel's pipe.

The link runs in DVI (TMDS) mode: every HDMI monitor must accept DVI, it needs no infoframes, and single link carries up to 165 MHz (1920x1080 at 60 Hz is 148.5 MHz). A mode above 165 MHz is skipped and logged.

#### The sequence, side by side with Linux

Linux v6.12, `drivers/gpu/drm/amd/display/dc/`, for DCN 2.1 (`dcn21_init.c` names which `dcn20`/`dcn10` functions it uses). Left: what Linux calls, in order. Right: what R5 does.

| # | Linux (file, function) | R5 (`dcn21_head.cpp`) |
|---|---|---|
| 0 | `core/dc_resource.c` `resource_map_clock_resources()`: a TMDS stream gets the first free PLL; the panel (DP) uses the DP DTO | COMBO PHY PLL0 |
| 0 | `link/link_factory.c` `translate_encoder_to_transmitter()` | same mapping from the VBIOS encoder object |
| 1 | `hwss/dce110/dce110_hwseq.c` `dce110_apply_single_controller_ctx_to_hw()`: `disable_stream_gating` (`dcn20_disable_stream_gating`: DSC only) | nothing: no DSC |
| 2 | `opp/dcn10/dcn10_opp.c` `opp1_program_fmt()` with `resource_build_bit_depth_reduction_params()` (8 bpc, `DITHER_OPTION_SPATIAL8`) and clamping `CLAMPING_FULL_RANGE` | same fields and values |
| 3 | `opp1_set_dyn_expansion()` (DVI: reset only) | same |
| 4 | `hwss/dcn20/dcn20_hwseq.c` `dcn20_enable_stream_timing()`: `set_pixel_rate_div` | none in `dccg21_funcs` |
| 5 | `optc/dcn10/dcn10_optc.c` `optc1_enable_optc_clock(true)`, two `REG_WAIT` of 1 ms | same, same limit |
| 6 | `dce/dce_clock_source.c` `dcn20_program_pix_clk()` → `dce112_program_pix_clk()` → `bios/command_table2.c` `set_pixel_clock_v7()`; then `dce112_program_pixel_clk_resync()` | VBIOS `setpixelclock` v1.7 with the same parameters; `PHYPLLA_PIXCLK_RESYNC_CNTL` deep colour 0 |
| 7 | `optc1_program_timing()` (incl. `set_vtotal_min_max`, `program_global_sync`, `set_vtg_params`, data format, H timing) | same registers; global sync values differ, see below |
| 8 | `set_out_rate_control` | none in the DCN 2.0 MPC |
| 9 | `opp1_pipe_clock_control(true)`, `opp2_program_left_edge_extra_pixel()` (RGB: 0) | same |
| 10 | `dcn20_blank_pixel_data(true)` → `opp2_set_disp_pattern_generator()` | same, colour squares (Linux's `visual_confirm` value) |
| 11 | `optc/dcn20/dcn20_optc.c` `optc2_enable_crtc()` | same |
| 12 | `dcn20_wait_for_blank_complete()`: up to 1000 × 100 µs | same; a timeout is an error |
| 13 | `optc1_set_drr()` with zero → `optc2_setup_manual_trigger()`; `optc1_set_static_screen_control(0, 2)` | same |
| — | — | **added**: the frame counter must move within 100 ms, so a missing pixel clock stops here, before the PHY |
| 14 | `dcn20_setup_vupdate_interrupt()` with `dcn10_get_vupdate_offset_from_vsync()` | same position register; no interrupt handler |
| 15 | `set_drr`, `set_static_screen_control` again | same |
| 16 | `dio/dcn10/dcn10_stream_encoder.c` `enc1_dig_connect_to_otg()` | same |
| 17 | `link/link_dpms.c` `link_set_dpms_on()`: `dcn10_link_encoder_setup(DVI)` | same |
| 18 | `link/hwss/link_hwss_dio.c` `setup_dio_stream_attribute()`: `enc1_setup_stereo_sync()`, `enc1_stream_encoder_dvi_set_stream_attribute()` → `encoder_control_digx_v1_5()` | same; VBIOS `digxencodercontrol` v1.5 |
| 19 | `update_info_frame` | none for DVI |
| 20 | `enable_link_hdmi()`: `setup_dio_stream_encoder()` (`dcn10_link_encoder_connect_dig_be_to_fe`), `dcn10_link_encoder_enable_tmds_output()` → `transmitter_control_v1_6()` | same; VBIOS `dig1transmittercontrol` v1.6 |
| 21 | `dcn20_enable_stream()`: `setup_stream_encoder` again, info frame, `set_early_control` (empty on DCN 2.x) | the bit is already set |
| 22 | `unblank_stream` | DP only, not called for DVI |

#### Deliberate differences

- **DVI, not HDMI signalling.** Linux picks HDMI when the EDID has an HDMI block. DVI needs no AVI infoframe, no audio, no scrambling. Safe: every HDMI sink must accept DVI. HDMI signalling (infoframes, audio, above 165 MHz) is a later step.
- **No DISPCLK, DCFCLK or watermark update** (`clk_mgr/dcn21/rn_clk_mgr.c` `rn_update_clocks()`, `hubbub` watermarks). Linux runs these through DML for the planes. With no plane on this pipe nothing fetches memory, so watermarks have no effect; DISPCLK stays what the firmware set for the panel. Step 2a logs it. If the OTG's DIO FIFO reports errors (`OTGn_PIXEL_RATE_CNTL`, logged at the end), step 3 adds the clock update.
- **Global sync without DML.** VSTARTUP is the last blank line before active, VUPDATE offset 0 width 1, VREADY 1. These only time the HUBP fetch, which this pipe does not have. Step 3 takes them from DML.
- **No DMCUB.** On Renoir Linux sends the VBIOS tables through DMCUB (`dmub_command_table = true` in `resource/dcn21/dcn21_resource.c`); `bios/command_table2.c` runs the same tables through the interpreter when there is no DMCUB service, and R5 does that. R5 does not reset or load the DMCUB the GOP may run for the panel.
- **No `init_hw`.** Linux's `dcn10_init_hw()` re-initialises the display at driver load unless it keeps the firmware state (seamless boot). R5 keeps the firmware state for everything it does not start.
- **No PSR, no FreeSync (DRR), no DSC, no audio.** None of them is needed to show a picture; each is a later step.
- **EDID by bit-bang, not the DC_I2C engine** (step 1): see step 1.

Values that come from hardware: the PHY, DIG, HPD and DDC pad from the VBIOS records and `gpio_pin_lut`; the mode from the EDID; the free OTG and DIG from the registers; the table revisions from the VBIOS.

Test: one boot with `dcn_hdmi_modeset true` (`boot-next hdmi`, boot 2) and the monitor attached. Expect colour squares on HDMI, the panel unchanged, and:

```
radeon_hd: dcn head: connector 1 start (dcn_hdmi_modeset true)
radeon_hd: dcn head: connector 1 otgN digT phy T pll combophy0 hpdM mode 1920x1080 total 2200x1125 148500 kHz dvi
radeon_hd: dcn head: otgN clocks optc on otg on
radeon_hd: dcn head: setpixelclock: No error, pixel rate 0x........
radeon_hd: dcn head: otgN control 0x........ frames A -> B in 100 ms      (B > A, about 6 frames)
radeon_hd: dcn head: digxencodercontrol stream setup: No error
radeon_hd: dcn head: dig1transmittercontrol enable: No error
radeon_hd: dcn head: head on: otgN control ... digT fe ... be ... dpg ...
```

### Step 3: the HDMI pipe reads a buffer (not written yet)

Goal: the HDMI pipe from step 2 shows a buffer in VRAM instead of the pattern, while the panel keeps running; both stable for minutes. Opt-in: `dcn_hdmi_scanout true`.

- Power the HUBP/DPP pair of that pipe up (`DOMAINn_PG_CONFIG`, `hwss/dcn21/dcn21_hwseq.c`), program the HUBP for a linear ARGB8888 surface in the VRAM pool (`hubp/dcn21/dcn21_hubp.c`), DPP in bypass (`dpp/dcn20`), one MPCC from that DPP to the OPP (`mpc/dcn20`), and turn the DPG off.
- Watermarks, DET and DISPCLK/DPPCLK for two active pipes, computed from `dcn2_1_soc` and `dcn2_1_ip` (`resource/dcn21/dcn21_resource.c`) and the DML formulas for two linear surfaces, not guessed. DISPCLK goes up through the SMU mailbox `rn_clk_mgr_vbios_smu.c` uses (R5 already talks to SMU 12, `r5_smu12.cpp`) and is never lowered while the panel runs. VSTARTUP, VUPDATE and VREADY come from the same formulas.
- The panel's pipe stays the GOP's. Taking it over is a later step, not part of this track.
- Hotplug: the HPD interrupt (`DC_HPD_INT_CONTROL`) moves into the kernel, so unplug and replug turn the head off and on.

Why it is not written yet: it changes resources the panel shares (HUBBUB arbitration, DISPCLK). Its values depend on what step 2a and step 2 report on the 5625U (which pipe is free, the DISPCLK the GOP set, whether the VBIOS tables run under the Haiku interpreter). Writing it before that boot would mean guessing.

Risks: an underflow from wrong watermarks also hits the panel; raising DISPCLK can blank the panel for a frame; a bad surface address shows garbage (VMID 0 physical addressing, not a hang). Test: one boot, 10 minutes of desktop use on the panel with a moving picture on HDMI, one unplug and replug; the underflow bits stay 0.

### Step 4: app_server with more than one screen (written, opt-in)

Goal: an extended desktop. Switch: `multi_screen true` in `~/config/settings/kernel/drivers/radeon_hd` (off by default; with it off, or with one screen, nothing changes). `multi_screen_test_output <w>x<h>` adds a **test screen in memory** to the right of the main screen, so step 4 can be tested without a second monitor or step 3.

#### How it works

Haiku has the pieces for more than one screen and leaves TODOs where the second screen would go. Step 4 fills those, in Haiku's own classes:

| Haiku class | Haiku today | Step 4 (R5) |
|---|---|---|
| `ScreenManager::_ScanDrivers()` | one `AccelerantHWInterface` ("Eventually we will loop ... to support multiple monitors") | with `multi_screen true`, every further output is a `Screen` too: `R5OutputHWInterface` (`distro/app_server/R5OutputHWInterface.cpp`), a `HWInterface` with its own id, mode, mode list and monitor info |
| `VirtualScreen::AddScreen()` | "this works only for single screen configurations": the last screen's engine and interface | the first screen is the main screen and owns the drawing engine; each further screen sits right of the one before, top aligned |
| `VirtualScreen::UpdateFrame()` | "compute virtual size depending on the actual screen position!" | the desktop is the union of the screens at their places; the main interface gets the others (`HWInterface::R5SetOutputs()`) |
| `VirtualScreen::ScreenByID()` | for `B_MAIN_SCREEN_ID` the *last* screen | the exact id; `B_MAIN_SCREEN_ID` is the first |
| `Desktop::_DetermineScreenFor()` | "choose the screen depending on where most of the area is" | that, so `BScreen(window)` is the screen the window is on |
| `Desktop::GetScreenFrame()` | `Screen::Frame()` (always at 0, 0) | the screen's place on the desktop |
| `Desktop::_ScreenChanged()` | the drawable region and the input bounds are that one screen | with more than one screen, all of them |
| `ServerApp` `AS_VALID_SCREEN_ID`, `AS_GET_NEXT_SCREEN_ID`, `AS_GET_MONITOR_INFO` | only the main id; "for now, just say we're the last one"; the id is not used | every screen; the next one; that screen's monitor |
| input_server `fFrame` | the main screen | the desktop for relative pointers; a device with absolute coordinates (touch screen, tablet) maps onto the main screen |
| Display page (`DisplayScreens.cpp`) | the main screen | every screen through `BScreen::SetToNext()`; the screen picture shows them at their places with the monitor's name |

**One drawing buffer for the whole desktop.** app_server draws (Painter, Skia, the compositor) into the main interface's back buffer, which with more than one screen covers the whole desktop. Every write to a front buffer goes through one place, `HWInterface::_CopyToFront()`, in desktop coordinates: the copy after drawing, the software cursor, and the cursor-area restore. With outputs, it splits each rectangle between the main front buffer at (0, 0) and every output's front buffer at its place. With no outputs it is Haiku's transfer, unchanged. The compositor needs no change for this: it sizes its frame from the back buffer and composes the whole desktop.

#### Compared with Haiku and with Linux

- **Haiku** intends one `Screen` per output in one `VirtualScreen` (`ScreenManager`, `BScreen` ids, `screen_configuration` with a frame per screen). Step 4 uses exactly those; nothing new is added to the public API. Where Haiku has a TODO, the table above says what R5 does.
- **Linux** (DRM/KMS) gives each CRTC a framebuffer and an offset: the compositor (X, Wayland) draws one large desktop and each head scans out its part. R5 draws one large desktop too, but each screen gets a **copy** of its part, because the main screen today is the firmware framebuffer, which cannot scan out a part of another buffer. When step 3 gives a head a buffer of its own, that head can scan out its part directly instead of being copied (a later change in `R5OutputHWInterface`).

#### Deliberate differences and limits

- Screens sit side by side, top aligned, in `ScreenManager` order. Moving them on the Display page is not wired to app_server yet; the tiles do not move.
- The mode menus on the Display page set the main screen. The test screen has one mode.
- With more than one screen the compositor's direct GPU scanout and the GPU back-to-front copy are off (they cover one screen); the damaged rectangles are copied by the CPU. The hardware cursor is off (it lives on one head); the cursor is drawn in software on every screen.
- The R5 wallpaper and the Tracker desktop window cover the main screen; other screens show the desktop colour. The Deskbar stays on the main screen (`BScreen(B_MAIN_SCREEN_ID)`).
- With screens of different heights, the area below the shorter screen is not part of the desktop region, but the relative pointer can still enter the bounding rectangle there.
- A real second head is step 3: then `ScreenManager` adds an `R5OutputHWInterface` whose front buffer is that head's scanout buffer.

#### Test without the laptop

`multi_screen_test_output 1280x720` makes a 1280x720 screen in memory. Every 20 s for the first minute, then every minute, it writes `/tmp/r5-screen-<id>.ppm`. In QEMU the main screen is QEMU's own display (screendump); the test screen's file is read out of the image afterwards with `bfs_shell`. QEMU's virtio-gpu with two outputs was not chosen: Haiku's `virtio_gpu` driver drives one scanout, and teaching it a second would test a QEMU driver rather than app_server.

Result, QEMU/KVM 27 Nov 2026 (1280x800 main screen, 1280x720 test screen): `app_server: multi_screen: 1 more screen(s), desktop 2560x800, back buffer 2560x800: No error`; the main screen unchanged; the test screen's picture showed the desktop colour and the pointer cut at the seam, half on each screen. Not covered: a window on the second screen, the Display page with two screens, RADV.

On the 5625U: boot 3 of `boot-next hdmi` runs the same test on RADV with the real compositor.

## Code layout (owner's decision, 27 Nov 2026)

The display code lives inside `radeon_hd`, not in a separate add-on.

- **Mode setting is in the accelerant**, as Haiku does it for DCE: it has the VBIOS parse and the atom interpreter, and app_server talks to it through the accelerant hooks. The kernel side stays small (later: the hotplug notification and vblank, through the R5 interface below).
- **One set of files per DCN version**, in `distro/graphics/src/add-ons/accelerants/radeon_hd/`:
  - `dcn_display.cpp`, `dcn_display.h`: version-neutral. The version table, the crash marker, the DCN lock, the switch-off, the VBIOS `gpio_pin_lut` lookup, EDID helpers, the connector loop.
  - `dcn21_hotplug.cpp` (steps 1 and 2a), `dcn21_head.cpp` (step 2), `dcn21_regs.h`: DCN 2.1 only, behind one `dcn_ops` table (`gDcn21Ops`).
- **One table picks the version.** The radeon_hd device table (`driver.cpp`) already gives every PCI ID its display version (`RHD_DISPLAY_DCN, 2, 1` for `15e7`, `1636`, `1638`, `164c`). `kDcnVersions[]` in `dcn_display.cpp` maps that version to its `dcn_ops`. No PCI ID appears in the DCN code.
- **A later AMD generation** (for example DCN 3.x) is a new `dcnNN_*` set and one row in `kDcnVersions[]`. Nothing else changes.

## Failure isolation (owner's requirement, 27 Nov 2026)

When DCN fails, it must not take the rest of the driver with it: the GFX ring, RADV, the compositor and the panel on the firmware framebuffer go on.

1. **No unbounded wait, no panic.** Every register wait has Linux's limit (`REG_WAIT` 1 ms for the clocks, 1000 × 100 µs for the DPG). The DDC read uses Haiku's DDC code, which retries a fixed number of times with its own I2C timeouts. A VBIOS table that loops is stopped by the interpreter after `ATOM_OP_JMP_TIMEOUT` (5 s, `atom.cpp`). Every step returns a `status_t`; none calls `panic()` or `debugger()`. Step 1 and 2 run only in the accelerant; there is no kernel DCN code yet, so there is no kernel input to check. When the kernel calls of the R5 interface below come (step 3), each one checks the head number, the buffer handle and every size from the accelerant before it uses them.
2. **A failed step switches DCN off for this boot.** `dcn_switch_off()` logs why (`radeon_hd: dcn: off for this boot: ...`), step 2 first turns its own OTG and PHY off again, and no later DCN step runs. The panel's pipe was never touched, so it stays on the firmware picture. The GPU side does not know DCN exists.
3. **Crash marker.** Before each step that touches DCN registers (`pipes`, `hotplug`, `head`), `dcn_marker_arm()` writes `armed <step> <generation>` to `/boot/system/settings/radeon_hd_dcn_marker` (the user settings directory if that is not writable) and `fsync`s it; the step's return clears it. If app_server restarts in the same boot, or the next boot starts, and finds the marker armed, the last start died inside that step: the marker becomes `failed <step> <generation>`, DCN stays off, and the syslog says `radeon_hd: dcn: the last start did not finish step <step> (crash or hang)`. The generation is taken from the radeon_hd settings files (time and size): when the settings change, as `boot-next hdmi` does for every boot of the stick, DCN gets one new try. So a stick never ends in a crash loop: at most one failed start per settings change.
4. **Apart from the GPU code.** The DCN files share no state with the GFX9 ring, the GART or RADV. The DCN lock (`sDcnLock` in `dcn_display.cpp`) is taken only there, never while a GPU lock is held, and no DCN path calls into GPU code. The only shared things are the MMIO mapping and the VBIOS image, both read-only to the GPU code's view of DCN.
5. **What cannot be isolated.** The accelerant runs inside app_server: a crash in the DCN code ends app_server (launch_daemon starts it again; the marker then keeps DCN off). A DCN register write that hangs the GPU or the bus, or a VBIOS table that faults the machine, takes the whole system down; no software layer can stop that. The marker covers it on the next boot: that boot skips DCN and logs why. A kernel fault caused by DCN would be the same; the only DCN code in the kernel is the read-only probe below.
6. **No write before the registers answer** (added 28 Nov 2026, after stick 1). Before any step that writes, `check_access` (`dcn21_check_access()`) reads, and only reads:
   - `DC_PINSTRAPS`, the first DCN register Linux reads (`read_dce_straps()` in `resource/dcn21/dcn21_resource.c`); all ones is not a strap value;
   - every OTG: the firmware drives the panel, so one OTG must be on with a horizontal total a real mode has.

   If either fails, DCN is off for the boot (`radeon_hd: dcn: registers do not answer: ...`) and nothing is written. Every value that is logged or used is checked for `0xffffffff` too: the HPD sense and pin, the DMCUB state, and the DDC pad registers right before the bit-bang.

## Register access: stick 1 (28 Nov 2026)

The first 5625U stick of this PR (commit `2b017df33a`, syslog in `R5OS-hdmi/generated/boot-5625u-54`) failed:

- **What it showed.** With the HDMI monitor attached, every DCN register the accelerant read returned `0xffffffff`: DMCUB, DISPCLK, all four OTGs, HUBPs, DIGs, HPD and GPIO. Step 2a logged that and treated it as data (`dmcub ... (running)`, `sense 1 pin 1`). Step 1 then bit-banged the DDC pad, which **writes** DCN GPIO registers, and the machine froze.
- **The crash marker worked.** The next two boots logged `the last start did not finish step hotplug` and came up without DCN. After `boot-next hdmi` changed the settings, DCN tried again and froze again, as designed (one try per settings change).
- **What is not the cause.** The addresses are the ones Linux uses: `DMU_BASE` segment 2 is `0x34C0` (`renoir_ip_offset.h`, the same table whose `GC_BASE` the working GFX9 code uses), the offsets are `dcn_2_1_0_offset.h`, and the byte address is (base + offset) × 4, as in `amdgpu_device_rreg()`. The accelerant reads through a clone of the kernel's own register area (`registers_area`, BAR5 `0xd0500000`, 512 KiB, the area the GFX9 ring and the SMU mailbox use), and every DCN offset lies inside it. Linux reads these offsets directly too (below `rmmio_size`, no indirect access). The SMU mode-2 reset the GPU start sends on `15e7` is what Linux does at init on Renoir too (`soc15_need_reset_on_init()` in `amdgpu/soc15.c`: "CP hangs in IGT reloading test on RN, reset to WA").
- **What is open.** Why the DCN block answers all ones at the moment the accelerant reads it. Candidates are a display block left without a register path after the GPU start (Linux re-initialises the display after its reset: `amdgpu_dm` then `dc_hardware_init()`), or a display power state the firmware left. The next boot decides it: the kernel now logs `radeon_hd: dcn early: before gpu start: ...` and `... after gpu start: ...` (`dcn21_early.cpp`, reads only: `DC_PINSTRAPS`, `DMCUB_CNTL`, OTG 0 to 3, OTG 0's horizontal total). The accelerant's gate also logs the SMU mailbox response it reads through the same mapping, so a dead mapping and a silent DCN block can be told apart.
- **Isolation.** Until the registers answer, the gate keeps every DCN write away. Step 4 writes no DCN register (its second screen is memory), so the same cause cannot reach it.

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

Read for step 2, all MIT, all under `drivers/gpu/drm/amd/display/dc/`: `hwss/dcn20/dcn20_hwseq.c`, `hwss/dcn21/dcn21_init.c`, `hwss/dce110/dce110_hwseq.c`, `hwss/dcn10/dcn10_hwseq.c`, `optc/dcn10/dcn10_optc.c`, `optc/dcn20/dcn20_optc.c`, `opp/dcn10/dcn10_opp.c`, `opp/dcn20/dcn20_opp.c`, `mpc/dcn20/dcn20_mpc.c`, `dccg/dcn21/dcn21_dccg.c`, `dce/dce_clock_source.c`, `bios/command_table2.c`, `bios/dce112/command_table_helper2_dce112.c`, `dio/dcn10/dcn10_stream_encoder.c`, `dio/dcn10/dcn10_link_encoder.c`, `link/link_dpms.c`, `link/hwss/link_hwss_dio.c`, `link/link_factory.c`, `core/dc_resource.c`, `inc/hw/hw_shared.h`.

Also checked for steps 3 and 4: `amdgpu_dm/amdgpu_dm.c`, `dmub/src/dmub_srv.c`, `clk_mgr/dcn21/rn_clk_mgr.c`, `rn_clk_mgr_vbios_smu.c`, `optc/dcn20/dcn20_optc.c`, `hubp/dcn21/dcn21_hubp.c`, `dccg/dcn20/dcn20_dccg.c`, `dcn21/dcn21_link_encoder.c`: all MIT.

R5 writes its own code from these and cites them; it does not paste them. MIT-to-MIT would allow copying, but the R5 code is much smaller than DC and has its own structure. The register offsets are facts taken from the headers. The DMCUB firmware is not used; if a later step needs it, it is a firmware blob under AMD's firmware licence (`LICENSE.amdgpu` in linux-firmware), checked in `docs/LICENSING.md` like the GFX blobs.

## Test rules for this track

- QEMU has no DCN. A QEMU boot only shows that nothing else broke.
- Every step needs one 5625U boot with an HDMI monitor attached, and its syslog lines recorded in `CHANGELOG.md`. A step without that boot is "written", not "tested".
- Each step stays opt-in on the 5625U until its boot passes, and becomes the default only by the owner's decision, as in "From opt-in to default" in `GPU-ARCHITECTURE.md`.
