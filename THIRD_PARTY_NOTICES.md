# Third-party notices

Readable list for users and release builds. The rules are `docs/LICENSING.md`. This is not a complete bill of materials for the Haiku tree or for every package inside an R5 image. Components that have not been inspected are unresolved, not MIT.

## Haiku

R5 is an independent system based on Haiku. Haiku’s own copyright notices and licenses stay on Haiku’s files. Much of Haiku is MIT; the tree is not all MIT. This repository does not relicense it.

The preferences wrench on Settings page icons and home cards is Haiku’s unmodified `data/artwork/icons/Overlay_prefs`, copied to `distro/settings/overlay-prefs.hvif`. `data/artwork/ReadMe` releases the icons under the MIT license. About R5 does not use that wrench.

The seven boot-progress icons are unmodified Haiku drawings, MIT, copyright Haiku, Inc. and its contributors: Kernel, Harddisk_Development, Devices, Harddisk, Virtual_Memory, Config_Folder, and Window. The SVGs and the list are in `distro/artwork/boot_splash/icons-src/`. `SOURCES.md` names each file. The 308×80 strip is built from renders of those SVGs.

The Back, Forward, Up, and Home pictures in `libr5icons.so` are unmodified HVIF from Haiku’s `src/kits/tracker/TrackerIcons.rdef` (`R_ResBackNav`, `R_ResForwNav`, `R_ResUpNav`, `R_HomeDirIcon`). The bytes live in `distro/icons/standard_icons.inc`. The Open Tracker notice is `distro/icons/TRACKER_ICON_NOTICE.txt`. The open and closed locks are unmodified HVIF of Haiku’s `Action_Login` and `Action_Logout` artwork. The bytes live in `distro/icons/lock_icons.inc`. The MIT notice is `distro/icons/HAIKU_ICON_NOTICE.txt`. The C++ around them is MIT (`docs/LICENSING.md`), so any application can link the library. The picture bytes keep their own notices. Those pictures do not get the preferences wrench. Settings does not read Tracker’s private resources at run time. `r5_button_set_lock()` is the public call that puts one of those locks on a button.

## Identified in this repository

### wpa_supplicant

Location: `distro/wpa_supplicant` (the recipe) and `distro/packages/x86_64/wpa_supplicant-2.11.r5.6-1-x86_64.hpkg` (the program). The source is not in git. The license text is `LICENSES/wpa_supplicant-BSD-3-Clause.txt`.

wpa_supplicant 2.11, from haiku/wpa_supplicant commit `bdf3144`. Copyright 2002-2022 Jouni Malinen and contributors. License: BSD-3-Clause (the BSD license with the advertising clause removed). The Haiku entry point `wpa_gui-haiku/main_haiku.cpp` is GPL-2.0-only OR BSD, copyright 2011 Michael Lotz. `src/drivers/driver_haiku_events.cpp` is the Haiku file, copyright 2010 Axel Dörfler, GPL-2.0-only OR BSD. R5's changes are applied at build time by `patch-src.py` to `src/drivers/driver_bsd.c` and `wpa_gui-haiku/main_haiku.cpp`. That script is MIT and holds the match strings only. It does not contain those source files. The program is distributed under the BSD alternative. It links OpenSSL 3 from the HaikuPorts package already in the image. It is not part of the kernel.

### USB audio and MIDI behavior from RenkuOS

Location: `distro/media/` `usb_midi`, `multi_audio`, `usb_audio`, the media server, and the media kit.

The files stay Haiku's, MIT, copyright the authors named in each file. The added behavior is ported from RenkuOS commits `fe2de4aa60`, `cb61494f02`, `7405980fad`, `303b85eae5`, `bd571637d2`, `cf885a005b`, `8db4b0d1b4`, `8d3c445d68`, `428e08cd07`, `4b157fb134`, `bc165f5613`, `d1028141c2`, `cf12459a32`, `917e0e9e30`, `2aee4b6c3c`, `e95a210256`, `a842555ed6`, and `b3dc1212e4`. Authors: David Beckman, rations, and atomozero. The Roland cable table is facts about the hardware. No ALSA code is included. The product-name read and the playback pacing from an explicit feedback endpoint are the same ideas as the Media-OS `usb_audio` driver and are written on our file. The Haiku MIT headers stay. The R5 snippets in these files are MIT.

### PCI segments

The x86 PCI host files are Haiku's. Copyright 2022 Haiku, Inc., and the earlier authors named in `pci.cpp`. License: MIT. The code that keeps a firmware segment other than segment 0 is copyright 2026 R5OS Contributors. Those snippets are MIT. The Haiku headers stay.

### Fault signal

Location: `distro/kernel/src/system/kernel/signal.cpp`, `distro/kernel/headers/private/kernel/ksignal.h`, and the page-fault call in `distro/kernel/src/system/kernel/vm/vm.cpp`.

The files are Haiku's. Copyright the Haiku authors named in each file. License: MIT, and NewOS for `vm.cpp`. The added blocks are haiku-pi `6e517bb52c`: a blocked or ignored fault signal is reset to the default and unblocked before it is sent. Copyright 2026 Witold Bołt. The snippets are MIT, like the file. The Haiku headers stay. ARM64 `arch_int.cpp` is not included.

### Exited thread stack

Location: `distro/kernel/src/system/kernel/thread.cpp`.

The file is Haiku's `thread.cpp`. Copyright the Haiku authors named in the file. License: MIT and NewOS. One block is haiku-pi `e09cfa1d9e`: an exited thread's stack stays while its team goes down. Copyright 2026 Witold Bołt. The snippet is MIT, like the file. The Haiku header stays.

### BFS I/O vnode release

Location: `distro/kernel/src/add-ons/kernel/file_systems/bfs/kernel_interface.cpp`.

The file is Haiku's BFS interface. Copyright the Haiku authors named in the file. License: MIT. One block is haiku-pi `44cc183c0c` (star row 57): the last reference from a finished I/O is released through `vfs_put_vnode()`. Copyright 2026 Witold Bołt. The snippet is MIT, like the file. The Haiku header stays.
### USB controller

Location: `distro/kernel/src/add-ons/kernel/busses/usb/xhci.cpp`, `xhci.h`, `xhci_hardware.h`, `ehci.cpp`, `ehci.h`, `ehci_hardware.h`, `uhci.cpp`, `uhci.h`, `ohci.cpp`, `ohci.h`, `distro/kernel/src/add-ons/kernel/bus_managers/usb/Device.cpp`, `Hub.cpp`, `usb_private.h`, `Object.cpp`, `BusManager.cpp`, `Stack.cpp`, and `distro/kernel/src/add-ons/kernel/drivers/network/ether/usb_ecm/ECMDevice.cpp`.

The files are Haiku's. Copyright the Haiku authors named in each file. License: MIT. The headers stay. The added blocks stay MIT. They are not relicensed.

haiku-pi `c65ac5a2e6a6383be3e6630ded05b1a29d506166` clears a finished transfer on a VIA VL805 and left four empty slots after every ring. Copyright 2026 Witold Bołt. RenkuOS `942e51e6d6` and `dc8872b982` replace that pad.

air/OS (airos-works/haiku), copyright jmgasper, unless another author is named:

- `846abe02798c06eac1cba01c7a43b5eb293d9400` resets a control pipe as soon as it halts.
- `bdad03ac62bdf47da071448828b463fc887ebff8` resets a stalled bulk or interrupt pipe. The idea is RenkuOS `5b6210a165` by David Beckman.
- `01e2036af28ef692585487434a6b247d9d332922` and `2ae6e2c26b60f6840fb7f9bbf5368f8cdccc1f8a` warm-reset a SuperSpeed link that is inactive. The warm-reset bit is bit 31.
- `39515d87e198923306f404d02b7f05a2586aeb64` builds the route from the hubs' own ports. The guest test in that commit was not taken.
- `11775b1e9079ac101886941651d4ef1cb458efc4` returns when a transfer event names a slot past the device list. Taken from RenkuOS `a9bba0ca79` by Mas Ahmad Muhammad. The Bay Trail part of that commit was not taken.
- `e04ebcb285d3c54d8d7db73a34423c8bcb7173bd` fails control requests of a device that is going away, and a USB Ethernet adapter only clears a halt after a stall.
- `fa1ebc8a74050bba0c373d95f6307b8b46784e67` resets a built-in device that takes no address, through ACPI, on x86.
- `6d877da0572106eb647c47bd3ef568aa939c9251` lets a high-bandwidth isochronous endpoint send more than one packet per microframe. Co-Authored-By: Claude Fable 5.1.

`df92a69cdb633ebf70dcdd08d68ca8bed9f6a37c` and `c707b89ff80be4fdfa780637da5cf1d38167a46f` are copyright David Beckman. The first waits half a second for a USB object to go idle and panics only when it is still busy. The second writes a repeated transfer error at most once every tenth of a second. The two-second wait in the original note was not taken.

RenkuOS (RenkuOS/Source), copyright rainygirl:

- `64d38d0286e6ae25d125bc6124bde04ba6e71e3f` stops retrying a port that keeps failing to set up, and power-cycles it only when the hub switches power per port.
- `14576fdf3c5bc1e59739126b7e5539ab5bba8a45` lets an unplug clear that ignore, and drops the address-0 descriptor read.
- `03c6ae617edf54bd5b6d8662755042138144fa5d` recovers a UHCI controller from a halt.
- `9c3906bb98ea605152f2cd0ac37f9756162ab670` does not finish a transfer that was already cancelled while that recovery runs.

RenkuOS, copyright David Beckman:

- `9abfa5e06e0c9f381f1c1e532e6d82003c3a1858` enables that port cap on 64-bit x86.
- `42ea6ba8c967dd79c4c3196fbf02f7304d9aba2d` looks again as soon as a high-speed controller hands a port to its companion.
- `cdd493103a956a90295deb22ace647ab9df1f019` explores a controller that starts after the boot volume is mounted from the explore thread.
- `3f4ee2d0facb1d8cd0a04a3938f4185358408d26` puts the packets of one isochronous transfer in a single buffer, so a short packet does not shift the next one.
- `8d6303031ecf2800fdcef718646644ec1b674b67` starts an isochronous chain again when the frame it asked for has already passed, on xHCI and on OHCI.
- `698d1c3159e09ea0245c6756e5c987f12baa6390` gives each EHCI and UHCI controller its own interrupt lock.
- `eae629d59a211e6161eeefd6c4f6bace3e3f4f06` runs an isochronous callback with the lock released.
- `c43a4a2f562fc274c7c50949e57117dac95e56f8` transmits an isochronous OUT payload. `0c200ef849a202bac121c5777aaae29cb01aba10` maps the caller's buffers first.
- `6bdbffb6a85400294f7b8bae64aca459c420d86f` keeps the isochronous chaining anchor per pipe. `d57fdde9ded70c0e6599e0d9b28805fdfb92f99a` places packets per service interval. `f69ee0331005449c26d361aecebb3777469cf8fb` starts the next transfer in the next free frame.
- `0473f845745184b09b126f065bb8ae3e57a5c0c0` reports what an isochronous OUT transfer actually did. `4512333a5d9ef34bd38c5316f5d9f1474c380bb0` uses the caller's length per packet. `c15045f9749b91b736b82fc94c8a2d8e2f0a7d22` reclaims an isochronous transfer on cancel.
- `676e5cffa0e27e20aad2aba5e9e4e2d22848bfcf` detaches a descriptor that is still linked before freeing it.
- `599f04372363519958501400c5649624f1df15bc` survives a UHCI halt instead of panicking. `bb6e0a2b4d807d031835e876c80384f65a00f82b` reschedules UHCI isochronous transfers. `bba308447a8da5fedb587d2df3d03d3fff5434dd` restarts the finisher at the stream's first frame after idle.
- `d38c595a49ecfd4a6b0482ee8e9c40d9ee4eee32` uses the OHCI length per packet and reports the start frame. `6011739554cbf747d50f0a9aa9db5a2740d97f00` frees an OHCI descriptor at the size it was allocated. `5ed8a3e4c59b9060ef4ad416dbc22849156f7189` unlinks an OHCI isochronous transfer before walking its descriptors.

RenkuOS, copyright atomozero:

- `e28b91f5e8663bbf56ae6c35759ee8e46b777fca` keeps the data toggle of an EHCI transfer that failed. A stall still resets it.
- `925977a925a7378abb0acf65f434db5f7772f85a` refuses an EHCI transfer with no data instead of queueing a NULL descriptor.

RenkuOS, copyright Bryan Irvine:

- `942e51e6d6295bf24c28ec0c36806a5256509ebf` leaves 32 unused TRBs after a transfer and after an endpoint ring, so a controller that reads ahead does not act on a TRB that gets rewritten.
- `dc8872b9823656b7b323821cffc4d998970735d2` keeps that space only on the VIA VL805 (PCI 1106:3483). The kernel debugger's allocation size was not changed. The ACPI `_CCA` path in that commit was not added: this driver always has the PCI id.

`scripts/apply-kernel-bugs.py` copies these files.

### USB webcam

Location: `distro/media/src/add-ons/media/media-add-ons/usb_webcam/`.

The files are Haiku's webcam add-on. Copyright the Haiku authors named in each file, including François Revol. License: MIT. The headers stay. The air/OS rewrite stays MIT. It is not relicensed. `scripts/apply-media.py` copies the files and removes the old `USB_video.h` and `UVCDeframer` sources from the build tree.

air/OS (airos-works/haiku), copyright jmgasper:

- `a314c835a947baedb3a04fa46eec4dc4e74b2eb9` rewrites the USB Video Class path. YUY2, UYVY, NV12, I420 and Motion-JPEG become `B_RGB32`. Frames go out as the camera sends them, at the size the consumer asks for. Camera controls are parameters. One node per camera. The active USB configuration is left as it is, so the microphone stays with `usb_audio`. Co-Authored-By: Claude Fable 5.1.
- `d0df427f165f0dc6ef45c5d675d841200f242ee0` frees the camera when its consumer dies. Co-Authored-By: Claude Fable 5.1. `Producer.cpp` and `Producer.h` only.
- `0af4c0889e` keeps several isochronous transfers queued, in `usb_raw` and the USB kit, so a packet is not lost between two reads. The webcam add-on reads that stream.

`distro/media/src/bin/camtest/` is R5's own test. License: MIT. It is not from air/OS and it is not camgrab.

### Touchpads and TrackPoint

The image built `i2c_hid` and did not install it. RenkuOS `b37a078925` adds it next to `i2c_elan`.

Location: `distro/kernel/src/add-ons/kernel/drivers/input/i2c_hid/`, `distro/kernel/src/add-ons/kernel/drivers/input/i2c_elan/`, `distro/kernel/src/add-ons/kernel/bus_managers/ps2/`, `distro/rest/headers/os/interface/Input.h`, and `distro/rest/src/add-ons/input_server/devices/mouse/`.

The copied files are Haiku's. Copyright the Haiku authors named in each file. License: MIT. The headers stay. `InputReadyLine.cpp` and `InputReadyLine.h` are new in RenkuOS and keep the header that commit wrote: Copyright 2026, Haiku, Inc., MIT. The RenkuOS changes stay MIT. They are not relicensed. No name was added to `elan_iic_devs`. Goodix (`4f32e08ca6`) was not taken. Each commit below is signed off by its author (DCO).

RenkuOS (RenkuOS/Source):

- `5153bac8073787ba8bedc7ac126de57c646fb946`, duncanmcqueen: read an I2C HID report only while the input-ready line is asserted. Intel Sunrise Point-LP (`INT344B`).
- `b37a078925df647ead39f487159acf61234cd6e0`, duncanmcqueen: install `i2c_hid`, and return 0.5 for a compatible-ID match so a hardware-ID driver comes first.
- `dd62b72f0fc8e91179c6c76a9283b55020ed0d5c`, idumych: read that line on AMD GPIO `AMD0030` and `AMDI0030`.
- `29e452e51dfe669b42251c54623125752e948e9f`, Felipe De Bene: collect Synaptics pass-through replies in the interrupt handler.
- `e126f76b753ead750a0387d2250777b99bf1a356`, Felipe De Bene: keep TrackPoint buttons held, and report them through the stick when a pass-through device is published. The finger-count bitmask in that commit was already in this tree. The branch that cleared those buttons when a packet had no change flag is removed here.
- `04fdaf98e4ecdd4ce976b01481736dd3de023e96`, Felipe De Bene: the middle button of a trackpoint scrolls.
- `a9f23126d3e6d0a19d5caa6de856ce916e47cf37`, idumych: the Elan touchpad speaks its own I2C protocol and uses the same input-ready line.
- `cb22fd5cfecf93f5f52e188d2cb985e37fddd8ea`, idumych: a right or middle click on an Elan clickpad.
- `17bb2a0c3e7842996acf3602c55c56f9e76c64a6`, idumych: follow the fingers that stay down, so a drag or a scroll does not jump.

air/OS (airos-works/haiku), copyright jmgasper:

- `a063548811` publishes a precision touchpad as one `input/touchpad` device and switches it to reporting contacts. `TouchpadProtocolHandler.cpp` and `TouchpadProtocolHandler.h`.
- `94736a44f1` starts the movement again when the finger count changes, so the pointer does not jump.
- `1d69faffef` does not treat the finger left after a gesture as a tap.
- `bf91f88318` reports the scroll between whole wheel steps, and whether the gesture began, changed, or ended.

`scripts/apply-kernel-bugs.py` copies the kernel files and adds `i2c_hid` to the package. `scripts/apply-rest-native.py` copies the mouse add-on and `Input.h`.

### USB disk unplug

Location: `distro/kernel/src/add-ons/kernel/drivers/disk/usb/usb_disk/usb_disk.cpp`.

The file is Haiku's USB disk driver. Copyright the Haiku authors named in the file. License: MIT. One block is haiku-pi `d3cc8e3fd1` (star row 54): the I/O schedulers are deleted before the device lock is taken, and a removed disk's queued transfer returns at once. Copyright 2026 Witold Bołt. The snippet is MIT, like the file. The Haiku header stays.
### BFS mount cleanup

Location: `distro/kernel/src/add-ons/kernel/file_systems/bfs/Volume.cpp`.

The file is Haiku's BFS volume. Copyright the Haiku authors named in the file. License: MIT. One block is haiku-pi `9799738757` (star row 50): a failed mount deletes the journal before the block cache. Copyright 2026 Witold Bołt. The snippet is MIT, like the file. The Haiku header stays.
### Profiler timer

Location: `distro/kernel/src/system/kernel/debug/user_debugger.cpp` and `distro/kernel/src/system/kernel/scheduler/scheduler.cpp`.

The files are Haiku's. Copyright the Haiku authors named in the files. License: MIT, and NewOS for the scheduler. The blocks are haiku-pi `5180e96119` (star row 23): a profiling timer is cancelled when its thread exits or is switched away, and it is unlinked before it is armed again. Copyright 2026 Witold Bołt. The snippets are MIT, like the file. The Haiku headers stay.
### I/O request iteration

Location: `distro/kernel/src/system/kernel/device_manager/IORequest.cpp` and `IORequest.h`.

The files are Haiku's. Copyright the Haiku authors named in the files. License: MIT. One block is haiku-pi `90fa3bc401` (star row 19): a fragmented file is walked in a loop instead of by recursion. Copyright 2026 Witold Bołt. The snippet is MIT, like the file. The Haiku headers stay.
### Page writer flush after a timeout

Location: `distro/kernel/src/system/kernel/vm/vm_page_writer.cpp`.

The file is Haiku's page writer. Copyright the Haiku authors named in the file. License: MIT and NewOS. One block is haiku-pi `1539d29eee` (star row 18): when the flush wait times out, the modified pages already queued are still written. Copyright 2026 Witold Bołt. The snippet is MIT, like the file. The Haiku header stays.

### Wired range shrink

Location: `distro/kernel/src/system/kernel/vm/vm.cpp`.

The file is Haiku's `vm.cpp`. Copyright the Haiku authors named in the file. License: MIT and NewOS. One block is haiku-pi `6f810ed2d7`: a partial wire shrinks to the pages that were wired. Another block is haiku-pi `894c4abe7f` (star row 21): a protection change does not insert a copy-on-write cache above a file. Copyright 2026 Witold Bołt. The snippets are MIT, like the file. The Haiku header stays.

### Runtime-loader TLS generation

Location: `distro/rest/src/system/runtime_loader/elf_tls.cpp`.

The file is Haiku's `elf_tls.cpp`. Copyright 2014 Paweł Dziepak. License: MIT. One block is the haiku-pi change `5db923abc6`: a new thread's TLS vector starts at the current generation. Copyright 2026 Witold Bołt. The startup images' TLS template bytes are also placed at a fixed offset from the thread pointer. That placement is copyright 2026 R5OS Contributors. The snippets are MIT, like the file. The Haiku header stays.

### Teapot control points

Location: `distro/apps/vulkanteapot/fg_teapot_data.h`.

The file is the freeglut 3.8.0 header `src/fg_teapot_data.h`, with the copyright line the permission text requires written above that text. The data are Martin Newell's patches as redistributed by freeglut. Copyright 1999-2000 Pawel W. Olszta. License: MIT (the X11 permission grant in the file). The control points stay under that notice. The Haiku `glut_teapot.c` file that says GPL licensing is not permitted is not used.

### AirPlay receiver

Location: `distro/airplay`.

| Piece | Project | License |
|---|---|---|
| RAOP / classic AirPlay flow | Shairport (James Laird), Shairport Sync (Mike Brady) | MIT |
| ALAC decoder | David Hammerton | MIT |
| AES-128-CBC | kokke tiny-AES-c | Unlicense |
| RSA and Apple-Challenge | OpenSSL 3 from HaikuPorts (`libcrypto`) | OpenSSL 3’s license (Apache-2.0 for OpenSSL 3) |

Copyright, URLs, and what was not copied (AirPlay 2 pairing, NQPTP, FairPlay) are in `distro/airplay/THIRD_PARTY.md`.

### SMB client library

Location: `distro/smb/src/libs/libsmb2`, public headers `distro/smb/headers/libs/libsmb2`.

libsmb2 6.2 (tag `libsmb2-6.2`, `d67e213`), from https://github.com/sahlberg/libsmb2, as brought into air/OS (`github.com/airos-works/haiku`). Copyright 2016 Ronnie Sahlberg. Portions of the sources are copyright 2017 Primary Data Inc. License: LGPL-2.1-or-later. The license text is `LICENSES/LGPL-2.1.txt` and `distro/smb/src/libs/libsmb2/LICENCE-LGPL-2.1.txt`. The Haiku notes are `README.haiku`. The Kerberos wrapper is not included. The library is linked statically into the smbfs add-on. The source of libsmb2 and the object files of smbfs are available so that add-on can be linked again. The source is in this repository at `distro/smb/src/libs/libsmb2/` and `distro/smb/headers/libs/libsmb2/`. The object files are those of the smbfs add-on built from `distro/smb/src/add-ons/kernel/file_systems/smbfs/`.

### SMB file system

Location: `distro/smb/src/add-ons/kernel/file_systems/smbfs`.

A userlandfs add-on from air/OS (`github.com/airos-works/haiku`). Copyright 2026 Haiku, Inc. License: MIT. It links `libsmb2.a`, so the add-on as shipped combines that MIT code with LGPL-2.1-or-later. The notices stay on each part.

The userlandfs files that add-on runs in are copied from Haiku into `distro/smb/src/add-ons/kernel/file_systems/userlandfs/`. They keep the Haiku MIT header. The changes are from air/OS, by jmgasper <jmgasper@gmail.com>: `00e1291e8095e94362e211d2b8dfaed38127a2cd`, `08a75db478698cd9c1e35649b30bf57f08e6d4d8`, `5b3aae72a780e69adb4ae4e010a986e696711fb7`, and `6b3fed7c57ba230d356ada64e394ca02e3d4310b`. `scripts/apply-smb.py` copies them with the rest of `distro/smb`.

The mount server side is `distro/smb/src/servers/mount` and `distro/smb/headers/private/mount/MountServer.h`. Same origin, MIT. Passwords are stored with `BKeyStore` under `smb://server/share`. The server retries a share that failed because the network was not up, and posts a notification when the failure is not that kind.

Tracker preferences gain the Network shares page, and the Mount menu lists shares that are not mounted. Those files are `distro/desktop/src/kits/tracker/NetworkSharesSettings.cpp`, `NetworkSharesSettings.h`, `MountMenu.cpp`, `TrackerSettingsWindow.cpp`, and `TrackerSettingsWindow.h`. MIT, from the same air/OS import. The message handling added to `Tracker.cpp` is an R5 snippet.

The MIT text used for covered files is `LICENSES/MIT.txt`. The Unlicense text is `LICENSES/Unlicense.txt`. The Apache-2.0 text is not in `LICENSES/` yet.

R5-owned source in this tree is MIT where the file is ours as a whole (since 12 Nov 2026). The R5 artwork stays GPL-3.0-or-later. The list of those paths, and the Haiku files that stay on their own license, is in `docs/LICENSING.md`.

### CPU topology, scheduler and deep idle

The files are Haiku's x86 CPU code, CPU header, scheduler, and ACPI C-state module: `arch_cpu.cpp`, `cpu.h`, `low_latency.cpp`, `power_saving.cpp`, `scheduler.cpp`, `scheduler_cpu.cpp`, `scheduler_cpu.h`, `scheduler_thread.cpp`, `scheduler_thread.h`, `acpi_cpuidle.cpp`, `apic.cpp`, and `ioapic.cpp`. Copyright the Haiku authors named in each file. License: MIT. The headers stay. The changes below stay MIT and are not relicensed. `scripts/apply-kernel-bugs.py` copies them and adds `x86_acpi_cstates` to the image.

air/OS (airos-works/haiku), copyright jmgasper:

- `49eda6ac001a7f1e7d32ab9d65f7ee2c39582277` reads the SMT threads of an AMD Zen core from CPUID `0x8000001E`, and marks Zen 5c cores as slower. The cache sharing is no longer counted twice.
- `7abf76e98596d58cf3b796f9cd92f9016b199c9c` gives each CPU a speed (`cpu_ent::capacity`), set for Intel hybrid cores, and the scheduler prefers the faster cores. The arm64 and device-tree parts of that commit were not taken.
- `1376a518e2accbf222e1c69bc7528f7af03bf314`, `71eadca3af904d365bcc4ccd5448d44bbb928db6`, `a4ddc48124a93dc20169128216f9f7965580d488`, `f100985748f98d988411799151f9fdf0cd0ab673`, `3fbd41c840a4cfaf93532b44c1e9e5f243440157`, `1aa18e8aa529e5a7de1c7af1c4fb3361488b55ea`, `10c5b544a8d3b35eabd4e972a9769a8407f5b4b1`, `f1d93470320dec9f7e3aeb2d56dcb7abe5914c63`, `417666ae7744b470bb18621bac7cde9a346ed023`, and `c3de7f3ac6bbc8fb3ff485ea063ef493516fb4be` place threads on idle, SMT and faster cores in both scheduler modes, and fix the cases those changes found.
- `2c48d1ae0ca03c267b2bd278aef39567f8d80ce2`, `32a4200329c6e4b9fc8830e9af63e5f5b212fb8f`, and `e24004dc25f370061be94abbc1009a88e0a71433` let the ACPI C-state module run on AMD and newer firmware, and choose the state by how long a CPU stays idle.
- `3683cfbe47` (air/OS, jmgasper) chooses the cpuidle and cpufreq module before installing it (`cpu.cpp`).
- `30da1eeda2b5e7d5d49d28b7ebcd95305b54e5df` takes the ACPI processor ids only from enabled MADT entries (`apic.cpp`, `ioapic.cpp`), so the ACPI C-state module can match every CPU.

The line that prints the raw CPUID words of the topology is R5's own, MIT, in SPDX-Snippet tags.

### Wi-Fi, Bluetooth and the Wi-Fi Kit

The MediaTek MT7921/MT7922 Wi-Fi driver `mt7922wifi` (`distro/network/wlan/mt7922wifi`), the Wi-Fi panel, state machine and glyphs in `libr5wifi.so` (`distro/wifi/lib`), the WiFi program (`distro/wifi/app`), and the MediaTek Bluetooth loader `h2mediatek` in Haiku's `h2generic` (`distro/kernel/src/add-ons/kernel/drivers/bluetooth/h2/h2generic`) are from air/OS (`github.com/airos-works/haiku`), copyright 2026 Haiku, Inc., written by jmgasper. License: MIT. They are not relicensed. The driver is the state of air/OS `e3c4cc898c3ecb8f1f05aff79987e3f9a5f8f8a9`; the Wi-Fi Kit is the WiFi preferences, `WirelessNetworkList.h` and `BNetworkRoster::AddPersistentNetwork(network, password)` of `295ea77d36e80c0ee65d23f7e277292eac0a5ff4` (itself `4ba582512b3ab95535530ceaa08c8e4291993dc6`) with the icons of `206f54fad9be69bb3fe2325de808d2b720ee7b8d`; the Bluetooth loader is `70bebdce2ed7da0353d3dd3b72874b884cc8f320`, `d970346c43b51ef7a9b6f0feac04bb866a97d626`, the h2generic part of `295ea77d36`, and `63151e84e14ab9773ee03a530e7c9558d618fb54` (`44e20014ac289d130f263fe5acbb23e3ca93aeed` is taken and reverted, as air/OS did in `5f37ae22f74b131311732d0fcde1b42be09235ad`). The HE media types and the transmit-rate ioctl header are the matching hunks of `295ea77d36`.

R5 changes, all MIT: the firmware file names per PCI part in the driver (an SPDX snippet), the panel as `WiFiView` in a shared library with `R5WiFi` header names, and `_reserved` fields.

The FreeBSD network and wireless layers Haiku runs these drivers on (`src/libs/compat/freebsd_network`, `src/libs/compat/freebsd_wlan`) keep their own licenses (Haiku's MIT, FreeBSD's BSD). The fixes taken into them are air/OS commits: `db88e0c67ce4f7be26c2aaec5f825c21c6514322`, `01d5a4d53a4941b1baf96a4f05a7c3e04bf2f1c9`, `bdc484fbe61ad70f338c05b877829d9d6957bf19`, `291ad13a8aa04883e92e2ce63246a113c8a454f5`, `4797f2fb9ce4e3b4a9851a9beee991aa2c7928cb` and `efd664942774869d6790b0305486e4618a427943` by jmgasper; `2307520ffbb1b75fb44dd1768cbe0da1044f5992`, `d341df45ce30a91c83ebf20c3c68c148334a2b23`, `e6027d523b3265c4bfe1364b697e47982787fab3` and `a9899f43add1b82d87bbe4169fc94a331c7ec638` by Felipe De Bene; `db80c70462b2842adff39e8b2ec3e2e6de448703`, `5b2e7b33b001202d3bebdfe2d015f13236a4bf8a`, `69fac4c61ddb5674a84cbc36c9cfa1203fe4bf7d`, `745966932b6dab32906d7ac9c0c20968327cffab` and `88320aa9666f4d8b646115a3add80129624ca5e2` by Ian. `scripts/apply-kernel-bugs.py` and `scripts/apply-wireless.py` copy them.

`memcpy` for x86_64 (`distro/kernel/src/system/libroot/posix/string/arch/x86_64/memcpy.cpp`) and its test are Haiku's, MIT, with air/OS `6178b691d287eac6f53f4726589c29327012ee8c` by jmgasper (MIT): no `rep movsb` when source and destination are less than 16 bytes apart modulo the page size. R5 widened that window to 32 bytes after measuring the 5625U (MIT).

### Game controllers

The controller mapping database is `gamecontrollerdb.txt` of SDL_GameControllerDB (https://github.com/mdqinc/SDL_GameControllerDB), commit `22de0d7ef9fe1e6f852a50a9403a7a48f9be30ad` (16 Nov 2026), unmodified, in `distro/game/data` and on the regular image in `/boot/system/non-packaged/data/game_controllers`, with its `LICENSE` beside it. License: zlib (`Zlib`). Copyright (C) 1997-2025 Sam Lantinga. The entries are written by many contributors; they are facts about the hardware. sha256 of the file: `97be227812c9ae6f7918aee5d1e7bc6dccbc944c25cf00c93c78d01baadd61f6`. `libr5game.so` reads it the way SDL documents the format; no SDL code is in R5.

Rumble, trigger effects and motion of the DualSense and DualShock 4 (`libr5game.so`, `GameControllerReports.cpp`, and hid_shared's `R5HIDReports.h`, both MIT) are written from facts about the controllers' reports read in SDL's `SDL_hidapi_ps4.c`, `SDL_hidapi_ps5.c` and `test/testcontroller.c` (https://github.com/libsdl-org/SDL; zlib, Copyright (C) 1997-2026 Sam Lantinga). No SDL code is copied. HID PID force feedback is written from the USB-IF "Device Class Definition for Physical Interface Devices (PID)" 1.0.

Phase 2, the Switch Pro Controller, the Joy-Cons and the Xbox controllers over Bluetooth and USB (`libr5game.so`, hid_shared's `R5HIDReports.h` and usb_hid's R5 parts, all MIT), is written from facts read in SDL's `SDL_hidapi_switch.c` and `SDL_hidapi_xboxone.c` (the same project, license and copyright, commit `4cc7adcaee482b63b4e1a0de77f52e200aa87b90`) and Microsoft's description of the Gaming Input Protocol over USB ([MS-GIPUSB]). No SDL code is copied.

The controller pictures are the Gamepad Line Art Pack by gidedin (https://gidedin.itch.io/gamepad-line-art-pack), `gamepad-line-art-pack.zip` (3761733 bytes), downloaded 17 Nov 2026. License: CC0 1.0 Universal (`CC0-1.0`), a public domain dedication; no credit is required, R5 gives it anyway. The SVGs, `LICENSE.txt` and `README.md` are kept unmodified in `distro/game/art/gamepad-line-art-pack`; `scripts/gen-controller-pictures.py` turns the regular drawings into the outlines `libr5game.so` draws (`distro/game/lib/GameControllerPictureData.h`). The dedication grants no trademark rights: Xbox, PlayStation, DualShock, DualSense, Nintendo Switch and Joy-Con are trademarks of their respective owners. The drawings carry no logos, the names only say which controller a drawing shows, and neither the pack nor R5 is affiliated with, sponsored by or endorsed by Microsoft, Sony Interactive Entertainment or Nintendo. The Sega Genesis / 8BitDo M30 picture is R5's own drawing (`distro/game/art/r5/genesis-m30.svg`, MIT).

Which picture a controller gets comes from the vendor and product IDs and controller types in SDL's `src/joystick/controller_list.h` (https://github.com/libsdl-org/SDL, commit `b2de7a19ad3c0d5063a5fb8e4cf5deeeb9ce4560`, 16 Nov 2026; zlib, `Zlib`, Copyright (C) Valve Corporation), and from the Sega Genesis style entries of the database above. Only these facts are written into the generated table; no SDL code or file is in R5.

Two joystick fixes are from RenkuOS (RenkuOS/Source), copyright atomozero, MIT, signed off by the author (DCO): `244bf4329209480d11f504217a34fef22c874cc5` frees the data of a joystick opened before (`src/kits/device/Joystick.cpp`, in `distro/game/kit`), and `823649eb6e7366c12908fd2637e6d0e0557296c1` waits for the joystick update thread and starts a joystick at zero (`hid_shared/JoystickProtocolHandler.cpp` and `.h` in `distro/kernel`, and `headers/private/device/joystick_driver.h` in `distro/game/headers`). The files keep their Haiku headers: MIT (Michael Lotz; Marcus Overhagen, Stefano Ceccherini, Fredrik Modéen) and, for `joystick_driver.h`, the Be Sample Code License (Be Incorporated, 1999). The RenkuOS lines are SPDX snippets with atomozero's copyright; R5's own lines in the same files are MIT snippets. `scripts/apply-game.py` copies them.

### Device Manager

Device Manager is MIT. It builds Haiku's Devices code unchanged from `src/apps/devices` (`dm_wrapper.c`, `Device*.cpp`, `DriverUtils.cpp`): MIT, copyright 2006-2026 Haiku, Inc., authors Pieter Panman and Leo Rouleau as named in those files. Its icon is the Devices icon, MIT, copyright Haiku, Inc. Hardware names come from the lists in Haiku's `src/data/ids`, the same ones Devices uses. The PCI ID list (Martin Mares and Albert Pool, pci-ids.ucw.cz) may be distributed under GPL-2.0-or-later or BSD-3-Clause; Device Manager uses it under BSD-3-Clause. The USB ID list (maintained by Stephen J. Gowdy, linux-usb.org) states no license in the file; it is not yet reviewed beyond that. ACPI and PnP names come from the UEFI registry files Haiku ships in the same folder. The R5 parts are copyright 2026 R5OS Contributors, MIT.

### Power service

`distro/power/server/` is Haiku's `power_daemon` (`src/servers/power/`), as air/OS (airos-works/haiku) changed it. Copyright the Haiku authors named in each file (Jérôme Duval, Rene Gollent, Nathan Whitehorn and others). License: MIT. The headers stay.

air/OS's `power_policy.cpp`, `power_policy.h` and its changes to the AC, lid and power-button monitors and to `power_daemon.cpp` are from the air/OS tree as R5 has it (`a4ddc48124a9`, one squashed commit of jmgasper's work; the commits it squashes are not in that clone). Copyright jmgasper. The policy files carry air/OS's own header, "Copyright 2026, Haiku, Inc.", which stays. License: MIT. R5 kept the settings file, the profile per power source and the lid handling at start, and left out the sleep, the deep sleep, the wired-network handling, the app_server display layout and the thermal trip monitor (step 3 of `docs/POWER.md`). R5's changes are MIT `SPDX-Snippet` blocks, copyright 2026 R5OS Contributors.

`distro/power/lib/PowerBattery.cpp` and `PowerBattery.h` are PowerStatus's `ACPIDriverInterface` (Haiku `src/apps/powerstatus`), moved into `libr5power.so`. Copyright 2009-2015 Haiku, Inc., Axel Dörfler and Clemens Zeidler. License: MIT. The headers stay.

### Skia (in app_server)

Skia, branch `chrome/m158`, commit `707e33875fb45f2997a4d781128bec122fb777b4` (13 Nov 2026), built from R5's recipe `distro/skia` into the build-only package `skia_devel-158.0-1` (`libskia.a` and its headers). M12 links it statically into `/system/servers/app_server`; until that step app_server does not contain it, and the package itself is never installed in the image. Built with its Graphite Vulkan backend only; no codecs, no ICU, no HarfBuzz.

| Piece | Project | License |
|---|---|---|
| Skia core and Graphite | skia.googlesource.com/skia ("Copyright (c) 2011 Google Inc." in `LICENSE`; files by Google Inc., Google LLC, The Android Open Source Project and others, see `AUTHORS`) | BSD-3-Clause |
| skcms | `modules/skcms` in Skia ("Copyright (c) 2018 Google Inc.") | BSD-3-Clause |
| Vulkan Memory Allocator 3.4.0 | GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator ("Copyright (c) 2017-2026 Advanced Micro Devices, Inc.") | MIT |
| Vulkan headers (compile time only) | Khronos Group, in Skia's `include/third_party/vulkan` | Apache-2.0 |

R5 patch: `distro/skia/patches/gcc13-to-array.patch` removes `const` from the two members of one constant table in `src/ports/SkFontHost_FreeType.cpp`, because GCC 13.3's `std::to_array` cannot build it. Nothing else of Skia is changed. The licence texts, unmodified: `LICENSES/Skia-BSD-3-Clause.txt`, `LICENSES/skcms-BSD-3-Clause.txt`, `LICENSES/VulkanMemoryAllocator-MIT.txt`; the package also carries them and `AUTHORS` in `documentation/skia`. A release ships the three texts with this list and on the Licenses tab of About. No source offer is required. Skia's licence does not allow the names of Google or the contributors to be used to endorse or promote R5.

### Realtek card readers (OpenBSD rtsx)

Location: `distro/kernel/src/add-ons/kernel/busses/mmc/rtsx`, installed as `busses/mmc/rtsx` in the system package.

`rtsx.cpp`, `rtsxreg.h` and `rtsxvar.h` are from OpenBSD rtsx(4) (`sys/dev/ic/rtsx.c` 1.22, `rtsxreg.h` 1.5, `rtsxvar.h` 1.6, and the device list and flags of `sys/dev/pci/rtsx_pci.c` 1.16). Copyright (c) 2006 Uwe Stuehler, Copyright (c) 2012 Stefan Sperling. License: ISC, the notice is kept in each file:

> Permission to use, copy, modify, and distribute this software for any purpose with or without fee is hereby granted, provided that the above copyright notice and this permission notice appear in all copies.
>
> THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

The R5 lines in those files, and the RTS5228 part and Haiku attachment beside them, are copyright 2026 R5OS Contributors, MIT. The RTS5228 part contains no Linux code (clean room, `docs/LICENSING.md`).

### NTFS file system (ntfs-3g)

Location: `haiku/src/add-ons/kernel/file_systems/ntfs`, installed as the kernel add-on `file_systems/ntfs` in the regular and the minimum image (Haiku's `minimum` image definition, `SYSTEM_ADD_ONS_FILE_SYSTEMS`). It came with Haiku; R5 has not changed it.

`libntfs/` and `utils/` (`utils.c`, `mkntfs.c`, `attrdef.c`, `boot.c`, `sd.c`) and `lowntfs.c` are from the NTFS-3G project (originally Linux-NTFS), https://github.com/tuxera/ntfs-3g. License: GPL-2.0-or-later ("either version 2 of the License, or (at your option) any later version", in each file header). Copyright holders named in those headers include Anton Altaparmakov, Szabolcs Szakacsits, Richard Russon, Yura Pakhuchiy, Yuval Fledel, Erik Larsson, Lode Leroy, Erik Sornes and Jean-Pierre Andre; each file lists its own. Haiku's `kernel_interface.cpp` is MIT (Haiku, Inc., 2021), but it is linked with `libntfs.a` and `utils.c` into one add-on, so the add-on as shipped is GPL-2.0-or-later. The disk system add-on `disk_systems/ntfs` (`NTFSAddOn.cpp`, `InitializeParameterEditor.cpp`, MIT) does not link that library.

The image already ships the unmodified GNU GPL version 2 text as `GNU GPL v2` in the Haiku package's `data/licenses` (`haiku/data/system/data/licenses/GNU GPL v2`). A release must also offer the corresponding source of this add-on for that exact build; it is in this repository under the path above. Not a file-by-file audit of the add-on.

### rclone (Online Storage, translated)

Online Storage (`distro/apps/onlinestorage`) translates parts of rclone into C++. rclone is MIT, Copyright (C) 2012 by Nick Craig-Wood, https://github.com/rclone/rclone. The commit read is `c0cef5e872f937727c89bfab71d6be11480f4d40` (23 Nov 2026). The translated files name the rclone file they come from and keep that copyright. R5's own files in that directory are MIT, Copyright 2026 R5OS Contributors.

Translated: `lib/oauthutil/oauthutil.go`, `lib/rest/rest.go` (redirects), `fs/fserrors/error.go` (retry), `backend/onedrive/onedrive.go`, `backend/onedrive/api/types.go`, `backend/dropbox/dropbox.go`, `backend/dropbox/batcher.go`. No Go source and no Go runtime are copied. rclone's client id and client secret are not copied. The app is not in the image. The MIT text is `LICENSES/MIT.txt`.

## Built outside this repository, not in the image

### VirtualBox Guest Additions 7.2.22

Oracle VirtualBox 7.2.22 Haiku additions. License: GPL-3.0-only. Some IPRT files are GPL-3.0-only OR CDDL-1.0; we take the GPL-3.0-only choice. The Oracle tarball is not in git. The R5 build fixes are `distro/vbox-guest/virtualbox-7.2.22-r5os.patch`. The binaries are not in git. The daily image installs them, plus the unmodified GNU GPL version 3 and that source, at `/boot/system/non-packaged/data/licenses/virtualbox`. They are not MIT.

The “or later” name on `LICENSES/GPL-3.0-or-later.txt` is the grant for the R5 artwork. It is not added to the VirtualBox sources. Haiku’s MIT notices stay on the Haiku files. See `docs/LICENSING.md` and `distro/vbox-guest/README.md`.

### OpenGL stack on the image

Location: `distro/packages/x86_64`. Copied onto the image after jam. Mesa 22.0.5 is removed from that image.

| Piece | Project | License |
|---|---|---|
| `libglvnd` 1.7.0 (`libGL.so.1`) | NVIDIA, 2013 | MIT |
| Mesa 25.3.6 llvmpipe (`libEGL_mesa.so.0`) and Lavapipe | Brian Paul and others | MIT |
| `libLLVM.so.21.1` only | University of Illinois at Urbana-Champaign, 2003-2025 | Apache-2.0 with LLVM exception |
| Vulkan loader 1.4.367 | The Khronos Group Inc. R5 builds revision 2 and adds the view-surface lines. | Apache-2.0. `vulkan_r5.h` in the same package is MIT (R5OS Contributors, 2026). |
| vulkaninfo 1.4.367 (`vulkan_tools`) | The Khronos Group Inc., Valve Corporation, LunarG, Inc., RasterGrid Kft. | Apache-2.0 |

The LLVM license text shipped inside the runtime package is `LICENSES/Apache-2.0-with-LLVM-exception.txt`. These packages were not audited file by file. This is CPU drawing, not the graphics chip.

The view-surface file in Lavapipe, `wsi_common_haiku.cpp`, is R5's own code, MIT, copyright 2026 R5OS Contributors. The same Mesa 25.3.6 build also ships RADV (`libvulkan_radeon.so`), Lavapipe and Zink. `src/amd/vulkan/radv_device.c` is SPDX MIT, copyright 2016 Red Hat and Bas Nieuwenhuizen, and says it is based in part on ANV, copyright 2015 Intel Corporation. `src/gallium/frontends/lavapipe/lvp_device.c` is MIT, copyright 2019 Red Hat. `src/gallium/drivers/zink/zink_context.c` is MIT, copyright 2018 Collabora Ltd. Mesa's `docs/license.rst` says the core is MIT and that individual files can differ. These three files were read. The packages were not audited file by file.

libdrm 2.4.124 is built in this tree and installed as `libdrm.so.2` and `libdrm_amdgpu.so.1`. `xf86drm.c` is MIT, copyright 1999 Precision Insight, Inc. and 2000 VA Linux Systems, Inc. `amdgpu/amdgpu.h` is MIT, copyright 2014 Advanced Micro Devices, Inc. `data/amdgpu.ids` ships unmodified. Its first lines are a syntax comment and `1.0.0`. They name no copyright holder. The libdrm tree was not audited file by file.

### Manrope

Location: `distro/fonts/manrope`. Installed on the image at `data/settings/fonts` and loaded by Settings only.

| Piece | Project | License |
|---|---|---|
| `manrope-regular.ttf`, `manrope-bold.ttf` | Manrope, Mikhail Sharanda, 2018. Mirko Velimirovic and Mikhail Sharanda made the variable family in 2019. These two files are the unmodified static fonts from `googlefonts/manrope`. | OFL-1.1 |

Copyright line in the font's license file: Copyright 2018 The Manrope Project Authors (https://github.com/googlefonts/manrope). No Reserved Font Name is declared after that line. The files are not modified, so the name stays Manrope. `OFL.txt` and `FONTLOG.txt` ship next to the fonts. The SPDX text is `LICENSES/OFL-1.1.txt`. The same files are installed under `data/fonts/ttfonts`. app_server uses Manrope Regular and Bold as the default plain and bold fonts when the user has not saved a font choice. The fixed font stays Noto Sans Mono. Noto Sans remains the fallback if Manrope is missing.

### MediaTek MT7921 firmware

Location: `distro/firmware/mediatek`. Installed at `system/non-packaged/data/firmware/mt7922wifi/` (Wi-Fi) and `system/non-packaged/data/firmware/h2generic/` (Bluetooth), each with `LICENCE.mediatek`.

| Piece | Project | License |
|---|---|---|
| `WIFI_MT7961_patch_mcu_1_2_hdr.bin` (version 20260224110909a), `WIFI_RAM_CODE_MT7961_1.bin` (20260224110949), `BT_RAM_CODE_MT7961_1_2_hdr.bin` (20260224111243) | MediaTek Inc., via linux-firmware (git.kernel.org, 12 Nov 2026) | `LICENCE.mediatek` (`LicenseRef-LICENCE.mediatek`) |

The blobs are unmodified. sha256 starts: `WIFI_MT7961_patch_mcu_1_2_hdr.bin` `a276c06c2b772adb`, `WIFI_RAM_CODE_MT7961_1.bin` `b94217a951518a9c`, `BT_RAM_CODE_MT7961_1_2_hdr.bin` `7a440a0f7404c73a`. MediaTek allows use and redistribution with devices that contain MediaTek chipsets, but not in a form that would require the files themselves to be covered by the GPL or LGPL. R5 ships them as separate files that the drivers read at run time, the same way as the AMD firmware. This is not an open-source license. The text is `LICENSES/LICENCE.mediatek`. MediaTek does not endorse R5.

### Cirrus CS35L41 speaker firmware

Location: `distro/firmware/cirrus`, installed unmodified in `system/non-packaged/data/firmware/cirrus` on the regular image together with `LICENSE.cirrus`, plus a symlink `cs35l41-dsp1-spk-prot-103c8a29.wmfw` to the program. The cs35l41 driver loads them only when its settings say `cs35l41_firmware true`.

| Piece | Project | License |
|---|---|---|
| `halo_cspl_RAM_revB2_29.63.1.wmfw` (playback 6.61.1, release 29.63.1), four `cs35l41-dsp1-spk-prot-103c8a29-spkid*.bin`, four `cs35l41-dsp1-spk-cali-103c8a29-spkid*.bin` | Cirrus Logic, via linux-firmware (`gitlab.com/kernel-firmware/linux-firmware`). The wmfw and the prot bins are on that tree's main. The cali bins are from commit `9f47713653` (HP consumer laptops) and are not separate files on that main. | Cirrus Logic firmware agreement (`LICENSE.cirrus`) |

The blobs are unmodified. On this ENVY (ACPI `CSC3551`, subsystem `103C8A31`, Windows config `CONF_0252`) they are the same bytes as the Windows driver `csaudioext`: the wmfw is `halo_cspl_RAM_revB2_29.63.1.wmfw`, and the `103C8A29_211103` tunings match spkid 0 or 1, left (`A0`) or right (`A1`), with `_cal` for the calibration bin. sha256: wmfw `c11cb3e25825c673a955a3519370ad77fbcd8bcf68808e8e34140a70caefcec4`, prot spkid0 l0 `119725a44103865c9f83283b6d7f82d7979db5a2ea943fe95b3efa58d7adec0b`, r0 `59c2e439ff12c78b355da6bfe91ce36ce4be9d5144e2e06af7e0108ac5a8796b`, spkid1 l0 `f99008ec0a9c50470e7fedcf31786a31664171b4ccc1a3e2bdd7bb0d6bf56e2d`, r0 `18636548a651ddf39e74c65bba738b9cd0c71c19c9fce6468c6cf094b286f934`, cali spkid0 l0 `5b927b28e587f42774f5949582b53f5f2399af3899ddc0fbf059ebeba62b9c91`, r0 `37a472da1dbcece7c9938cc09a2e8c186e2ae8080c7d31d394682b0586f701c5`, spkid1 l0 `0af356009cf503159e34ecbcfba28ce305afb040068c7ca5741cb53993148e9a`, r0 `a17badce983bfdce7f4773ddb14471224342452a22ef3ab64f9ef826ecfbdd5a`. Cirrus allows redistribution only complete and unmodified, only for use with Cirrus Logic semiconductor devices, and only with this agreement beside the files. This is not an open-source license. There is no source offer. The text is `distro/firmware/cirrus/LICENSE.cirrus`. The Windows diagnostic wmfw and `103C8A29_211103_diag.bin` are not in that publication and are not included. Cirrus Logic does not endorse R5.

### Realtek RTL8761CU Bluetooth firmware

Location: `distro/firmware/realtek`. Installed at `system/non-packaged/data/firmware/h2generic/` with `LICENCE.rtlwifi_firmware.txt`, in the regular image. It is the patch for the ASUS USB-BT600 dongle (USB `0b05:1d70`).

| Piece | Project | License |
|---|---|---|
| `rtl8761cu_fw.bin` (10296 bytes, file version `0x4618d3f3`), `rtl8761cu_config.bin` (11 bytes) | Realtek Semiconductor Corporation, via linux-firmware (git.kernel.org, commit `35e542439e8aaca81fa1527fe3887afbb68a6779`, "rtl_bt: Add firmware and config files for RTL8761CUV"; fetched 17 Nov 2026) | `LICENCE.rtlwifi_firmware.txt` (`LicenseRef-LICENCE.rtlwifi_firmware.txt`) |

The blobs are unmodified. sha256: `rtl8761cu_fw.bin` `525bfa5ab9a3ed74b788f9ac494467c522a436c1bc5290ebcb05c7f920533c09`, `rtl8761cu_config.bin` `3142c2732470e4edcdb68edf58cc505b581e38d56b3ec09c64eddcb1d5099507`. Copyright (c) 2010, Realtek Semiconductor Corporation. Realtek allows redistribution and use in binary form without modification, if the copyright notice and the disclaimer are reproduced in the documentation, and forbids using its name to endorse a product and any reverse engineering, decompilation or disassembly. R5 only reads the file headers (signature, patch table, trailer), never the code inside. The licence also grants a limited patent licence for use with an operating system under an OSI-approved licence. This is not an open-source licence. The text is `LICENSES/LICENCE.rtlwifi_firmware.txt`. Realtek does not endorse R5.

### FreeBSD rtlbtfw

The Realtek loader in `h2generic` (`h2realtek.cpp`, `h2rtkfw.h`) is R5's own MIT code, written from the protocol and file format of FreeBSD's `usr.sbin/bluetooth/rtlbtfw`. Two of its tables, the chip table and the project ids in `h2rtkfw.h`, are FreeBSD's data and are marked as a `BSD-2-Clause` snippet. Copyright (c) 2013 Adrian Chadd, (c) 2019 Vladimir Kondratyev, (c) 2023 Future Crew LLC. License: BSD-2-Clause, text in `LICENSES/rtlbtfw-BSD-2-Clause.txt`. No GPL source (Linux `btrtl`, `btusb`) was read.

### AMD CPU microcode

Location: `distro/firmware/amd-ucode`. Installed on the boot volume at `system/non-packaged/data/firmware/amd-ucode/`.

| Piece | Project | License |
|---|---|---|
| `microcode_amd_fam15h.bin`, `microcode_amd_fam16h.bin`, `microcode_amd_fam17h.bin`, `microcode_amd_fam19h.bin`, `microcode_amd_fam1ah.bin` | Advanced Micro Devices, Inc., 2010-2025, via linux-firmware tag `20261019` | AMD microcode license |

The blobs are unmodified. sha256: `microcode_amd_fam15h.bin` `9d4a668410e72a4bdb86dc23e4261eca04daa83456ada02504115223f356981a`, `microcode_amd_fam16h.bin` `e02ad653b39c975d6c52674b50f23727bb6706bab7b4e5b391a4ce229e7ff121`, `microcode_amd_fam17h.bin` `966e4b796ec689c618868d08f8a37f347b0e7bfce4ae9df793e08471d363b7d0`, `microcode_amd_fam19h.bin` `c614d6db8056c5c67a9189b225124127d56990a190305bcb3927d50e132de7dd`, `microcode_amd_fam1ah.bin` `a6b22566f5aac770bdeae5610d417ee35ffd4af13eeaa624a2995f57e2d5b274`. `LICENSE.amd-ucode` is the unmodified AMD text and is installed beside the blobs. Redistribution of the binaries is allowed when that notice stays with them. This is not an open-source license. AMD does not endorse R5.

### Intel CPU microcode

Location: `distro/firmware/intel-ucode`. Installed on the boot volume at `system/non-packaged/data/firmware/intel-ucode/`.

| Piece | Project | License |
|---|---|---|
| `06-1a-04` to `06-dd-01` (97 files, family 06 from Nehalem, Atom from Silvermont) | Intel Corporation, 2018-2021, via `intel/Intel-Linux-Processor-Microcode-Data-Files` release `microcode-20261110` | Intel microcode license |

The files are unmodified (18,821,120 bytes). `LICENSE.intel-ucode` is the unmodified Intel text and is installed beside them; it is also in `LICENSES/`. Redistribution is allowed in binary form without modification, with the copyright notice and disclaimer reproduced. Reverse engineering is not allowed. This is not an open-source license. Intel does not endorse R5.

### AMD Renoir graphics firmware

Location: `distro/firmware/amdgpu`. Installed at `system/non-packaged/data/firmware/amdgpu/`.

| Piece | Project | License |
|---|---|---|
| `renoir_pfp.bin`, `renoir_me.bin`, `renoir_ce.bin`, `renoir_rlc.bin`, `renoir_mec.bin`, `green_sardine_pfp.bin`, `green_sardine_me.bin`, `green_sardine_ce.bin`, `green_sardine_rlc.bin`, `green_sardine_mec.bin`, `green_sardine_mec2.bin` | Advanced Micro Devices, Inc., 2024, via linux-firmware | AMD GPU microcode license |

The blobs are unmodified. sha256: `renoir_pfp.bin` `0c815572cc466d6c1e608e1d36ab16f35660179e19f0feeabc448b85e8158af4`, `renoir_me.bin` `38574ad3c583f3965cf1c4be84f9d0c3818511781c03733b28f07683061b1189`, `renoir_ce.bin` `b4ea0bf5d589d82ccef52e54536221f094b66b648514b14f03cf4ad50f9a4f72`, `green_sardine_pfp.bin` `94e1474b3c8d422e35661d2d584297d891464c173ebf4ad45536277bdf7bdcdc`, `green_sardine_me.bin` `671af908c8d631e659895d62db2e6bffc1d2c87d6db4c08246be0216f468fb9c`, `green_sardine_ce.bin` `3bba10cff48ec51eb63e78078456596258c9fea2e8b38d91d02be66c0aca70fd`. `renoir_rlc.bin` and `green_sardine_rlc.bin` are the same file, `66f4397c2e9d19af245f9da290c8678eb1a51cc815fa7864a9383c2cd0db319d`. `renoir_mec.bin`, `green_sardine_mec.bin` and `green_sardine_mec2.bin` are the same file, `0e4c6712f75af494c3fd70386bc07739cab3aafd77c4fe8efb46c0bc1ae4eba6`. `LICENSE.amdgpu` is the unmodified AMD text and is installed beside them. Redistribution of the binaries is allowed when that notice stays with them. This is not an open-source license. AMD does not endorse R5. Linux loads these through the PSP unless the load type is direct. RLC is header 2.1, ucode `0x3c`, 16896 bytes. PFP, ME, and CE stay header 1.0.

### AMD GFX9 clear-state table

Location: `distro/graphics/src/add-ons/accelerants/radeon_hd/clearstate_defs.h` and `clearstate_gfx9.h`. Compiled into the `radeon_hd` accelerant. Not installed as a separate file.

| Piece | Project | License |
|---|---|---|
| `clearstate_defs.h`, `clearstate_gfx9.h` | Advanced Micro Devices, Inc., 2012 and 2017, via Linux v6.12 `drivers/gpu/drm/amd/amdgpu/` | MIT |

The files are unmodified. sha256 `70eb9f9a57d2cacb2ee2d7b9f17485384019ac03c845ce4bea941acdae137285` and `e885c7f6475efd74e7729688a70992ff9543979363eb029c628bd95627fb34b5`. The permission text stays in each file. AMD does not endorse R5.

### Mesa packet definitions

Location: `distro/graphics/third_party/mesa-25.3.6/src/amd/common/sid.h`. Read by `scripts/check-pm4.py` on the host. Not compiled into the image.

| Piece | Project | License |
|---|---|---|
| `sid.h` | Advanced Micro Devices, Inc., 2011, Mesa 25.3.6 `src/amd/common/sid.h` | MIT |

The file is unmodified. sha256 `ab108b90f8d4443eaf6742f1bf6f4454fc43bd5c9f3373cb1e02f1f0cf5308d3`. The permission text stays in the file. AMD does not endorse R5.

### Shader compiler (build tool)

`glslangValidator` from glslang 16.6.0 (Khronos Group) compiles the compositor's GLSL to SPIR-V on the build host (`scripts/gen-compositor-shaders.sh`). Nothing of glslang is in the image: only SPIR-V generated from R5's own MIT shader source.

| Piece | Project | License |
|---|---|---|
| `glslangValidator` 16.6.0 | KhronosGroup/glslang (Google, Inc., The Khronos Group Inc., and others) | BSD-3-Clause, BSD-2-Clause, MIT, Apache-2.0, NVIDIA and Khronos licences per file; the generated parser `glslang_tab.cpp` is GPL-3.0-or-later WITH Bison-exception-2.2 |

## Not included

The Linux `amdgpu` driver is not imported. The clear-state table above is the only file taken from that tree.

## Not done

Credits & Licenses inside About R5 is not built. It must be generated from this inventory once the image actually contains the components it names. A full scan of `haiku/`, firmware, fonts, and package dependencies is still open.
