# VirtualBox run, Bluetooth step 1a (15 Nov 2026)

Image: branch `claude/bluetooth-step1a` at `24b8faed14` plus the smoke-test
radio check (`e248572620`), built with `scripts/build-locked.sh`
(`jam jobs=12`, `build_s=182`, 8570 targets, after `lock_wait_s=135`; the build
before it: `build_s=180`, 8560 targets) and copied under the build lock.
`bt-vbox.job` was planted in that copy. VM `R5OS`, started with
`run-vbox-r5.ps1 -RawImage ... -PassBluetooth -ForceConvert -NoClip`.

- `syslog`: `/system/var/log/syslog` from the VDI after power-off.
- `bt-vbox.log`: the output of the planted job.
- `bt-vbox.job`: the launch job.
- `usbhost-after.txt`: `VBoxManage list usbhost` for the MT7922 after the VM
  was off and the filter removed (`Busy`: Windows has it again).
- `../fuzz-hci.log`: `scripts/fuzz-bt.sh hci 60` of the same branch.

What it shows:
- While the VM ran, the host listed the MT7922 (`0x13d3:0x3567`) as
  `Captured`, but the guest xHCI saw one device only
  (`usb hub 2: port 0: new device connected`). `h2generic` loaded and found no
  radio: `bluetooth_server: started, 0 radios`, `bt_server_smoke_test: 0 radios`,
  `ok (0 crashes)`. No firmware load, no HCI command, no `controller ready`.
- The desktop stayed at the wallpaper. The syslog ends with
  `app_server: Application for user 0 does not support the current server
  protocol (0), team 147 (/boot/system/servers/input_server)`. Not looked into.
- The VM was powered off by hand about four minutes after the start.
