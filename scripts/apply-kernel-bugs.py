#!/usr/bin/env python3
"""Copy R5OS kernel sources over the Haiku tree. Jam and launch hooks stay."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")


def install_kernel_sources() -> None:
    root = pathlib.Path(__file__).resolve().parents[1]
    files = (
        "build/config_headers/kernel_debug_config.h",
        "src/add-ons/kernel/network/stack/utility.cpp",
        "src/system/kernel/arch/x86/arch_timer.cpp",
        "src/system/kernel/arch/x86/timers/x86_apic.cpp",
        "src/system/kernel/arch/x86/arch_cpu.cpp",
        "src/system/kernel/arch/x86/arch_vm.cpp",
        "src/system/kernel/arch/x86/r5_direct_map.cpp",
        "src/system/kernel/arch/x86/paging/64bit/paging.h",
        "src/system/kernel/arch/x86/paging/64bit/X86PagingMethod64Bit.h",
        "src/system/kernel/arch/x86/paging/64bit/X86PagingMethod64Bit.cpp",
        "src/system/kernel/arch/x86/paging/64bit/X86VMTranslationMap64Bit.h",
        "src/system/kernel/arch/x86/paging/64bit/X86VMTranslationMap64Bit.cpp",
        "headers/private/kernel/vm/VMTranslationMap.h",
        "headers/private/kernel/ksignal.h",
        "headers/private/kernel/arch/x86/arch_altcodepatch.h",
        "headers/private/kernel/arch/user_memory.h",
        "headers/private/kernel/thread_types.h",
        "src/system/kernel/arch/x86/asm_offsets.cpp",
        "src/system/kernel/arch/x86/64/interrupts.S",
        "src/system/kernel/arch/x86/64/arch.S",
        "src/system/kernel/arch/x86/64/stubs.S",
        "src/system/kernel/arch/x86/64/thread.cpp",
        "src/system/kernel/signal.cpp",
        "src/system/kernel/team.cpp",
        "src/system/kernel/thread.cpp",
        "src/system/kernel/vm/vm.cpp",
        "headers/private/kernel/vm/vm.h",
        "headers/private/kernel/arch/vm.h",
        "src/system/runtime_loader/arch/x86_64/arch_relocate.cpp",
        "src/system/runtime_loader/export.cpp",
        "headers/private/runtime_loader/runtime_loader.h",
        "src/system/libroot/os/thread.c",
        "src/system/libroot/posix/pthread/pthread.cpp",
        "src/system/libroot/posix/time/timer_support.cpp",
        "src/add-ons/kernel/bus_managers/pci/pci.cpp",
        # R5: the AC state is read at start and on every read (stick 74)
        "src/add-ons/kernel/drivers/power/acpi_ac/acpi_ac.cpp",
        "src/add-ons/kernel/busses/pci/ecam/ECAMPCIControllerACPI.cpp",
        "src/add-ons/kernel/busses/pci/x86/X86PCIController.cpp",
        "src/add-ons/kernel/busses/pci/x86/X86PCIController.h",
        "src/add-ons/kernel/busses/pci/x86/kernel_interface.cpp",
        "src/system/kernel/vm/vm_page_writer.cpp",
        # R5: opt-in block_cache size stats (r5_bfs_cache_stats)
        "src/system/kernel/cache/block_cache.cpp",
        # R5: forced unmount ignores fs_sync() errors when media is gone
        "src/system/kernel/fs/vfs.cpp",
        "src/system/kernel/device_manager/IORequest.cpp",
        "src/system/kernel/device_manager/IORequest.h",
        "src/system/kernel/debug/user_debugger.cpp",
        "src/system/kernel/scheduler/scheduler.cpp",
        # air/OS: SMT topology on Zen, cores of several speeds, and the
        # scheduler placement that uses them (docs/KERNEL.md)
        "headers/private/kernel/cpu.h",
        "src/system/kernel/scheduler/low_latency.cpp",
        "src/system/kernel/scheduler/power_saving.cpp",
        "src/system/kernel/scheduler/scheduler_cpu.cpp",
        "src/system/kernel/scheduler/scheduler_cpu.h",
        "src/system/kernel/scheduler/scheduler_thread.cpp",
        "src/system/kernel/scheduler/scheduler_thread.h",
        # air/OS: deep idle through ACPI _CST, also on AMD, and the ACPI
        # processor ids it matches CPUs by (only enabled MADT entries)
        "src/add-ons/kernel/power/cpuidle/x86_acpi_cstates/acpi_cpuidle.cpp",
        "src/add-ons/kernel/power/cpuidle/x86_acpi_cstates/x86_mwait.h",
        "src/add-ons/kernel/power/cpuidle/x86_cstates/x86_cstates.cpp",
        "src/system/kernel/arch/x86/apic.cpp",
        "src/system/kernel/arch/x86/ioapic.cpp",
        # air/OS 3683cfbe47: choose the cpuidle module before using it
        "src/system/kernel/cpu.cpp",
        # R5: shutdown hooks, so radeon_hd stops the GPU before a restart
        "headers/private/kernel/r5_shutdown.h",
        "src/system/kernel/shutdown.cpp",
        # R5: va_copy in syslog() LOG_CONS fallback (fixes app_server crash at
        # shutdown after syslog_daemon exits) and EFI previous_syslog recovery
        "src/system/libroot/posix/syslog.cpp",
        "src/system/boot/platform/efi/efi_platform.h",
        "src/system/boot/platform/efi/mmu.cpp",
        "src/system/boot/platform/efi/debug.cpp",
        "src/system/boot/platform/efi/start.cpp",
        "src/system/kernel/debug/debug.cpp",
        # R5: AHCI fast empty-port reset and 64-bit DMA when CAP.S64A is set
        "src/add-ons/kernel/busses/scsi/ahci/ahci.c",
        "src/add-ons/kernel/busses/scsi/ahci/ahci_port.cpp",
        # R5: no USB modules in the BIOS CD floppy archive (size limit)
        "build/jam/images/FloppyBootImage",
        # R5: the AMD CPPC values stay per CPU (no shared globals)
        "src/add-ons/kernel/power/cpufreq/amd_pstates/amd_pstates.cpp",
        # R5: processor profiles (docs/POWER.md, section 4): the profile
        # syscalls, the cpufreq interface version 2, AMD (CPPC MSR and the
        # ACPI _CPC registers) and Intel (HWP and the P-state ratio)
        "headers/os/drivers/cpufreq.h",
        "headers/private/system/syscalls.h",
        "headers/private/system/r5_power_profile.h",
        "src/add-ons/kernel/power/cpufreq/amd_pstates/amd_cppc_acpi.cpp",
        "src/add-ons/kernel/power/cpufreq/amd_pstates/amd_cppc_acpi.h",
        "src/add-ons/kernel/power/cpufreq/amd_pstates/Jamfile",
        "src/add-ons/kernel/power/cpufreq/intel_pstates/intel_pstates.cpp",
        "src/add-ons/kernel/power/cpufreq/intel_pstates/Jamfile",
        # air/OS 6178b691d2: memcpy leaves rep movsb under 4K aliasing
        "src/system/libroot/posix/string/arch/x86_64/memcpy.cpp",
        "src/system/libroot/posix/string/arch/x86_64/memset.cpp",
        "src/system/libroot/posix/musl/string/memmove.c",
        "src/system/libroot/posix/musl/string/strlen.c",
        "src/system/libroot/posix/musl/string/strchr.c",
        "src/system/libroot/posix/musl/string/strchrnul.c",
        "src/system/libroot/posix/musl/string/memchr.c",
        "src/system/libroot/posix/string/strcmp.c",
        "src/system/libroot/posix/string/memcmp.c",
        "headers/private/system/r5_string_x86.h",
        # The string scans need the private header. The Jamfiles are copies,
        # so apply does not patch them.
        "src/system/libroot/posix/musl/string/Jamfile",
        "src/system/libroot/posix/string/Jamfile",
        "src/system/kernel/lib/Jamfile",
        "src/system/boot/Jamfile",
        "src/bin/Jamfile",
        # R5: heap pool count from the CPU count, adaptive heap mutexes
        "src/system/libroot/posix/malloc/openbsd/malloc.c",
        "src/system/libroot/posix/malloc/openbsd/wrapper.c",
        # R5: TSC-bounded pause before a mutex syscall, rwlock reader fast path
        "src/system/libroot/posix/pthread/pthread_mutex.cpp",
        "src/system/libroot/posix/pthread/pthread_rwlock.cpp",
        "src/system/libroot/os/locks/mutex.cpp",
        # R5: one CPUID/XCR0 read for user-space dispatch
        "headers/private/system/r5_x86_cpu.h",
        "headers/private/system/r5_pstate_info.h",
        # R5: FMA3/FMA4 fma and SSE4.1 roundsd/roundss
        "src/system/libroot/posix/musl/math/x86_64/Jamfile",
        "src/system/libroot/posix/musl/math/x86_64/fma.c",
        "src/system/libroot/posix/musl/math/x86_64/fmaf.c",
        "src/system/libroot/posix/musl/math/x86_64/r5_round.c",
        "src/tests/system/libroot/posix/string/Jamfile",
        "src/tests/system/libroot/posix/string/memcpy_test.cpp",
        # air/OS: the FreeBSD network and wireless layers the MediaTek Wi-Fi
        # driver and wpa_supplicant need (THIRD_PARTY_NOTICES.md)
        "src/libs/compat/freebsd_network/compat/net/if_media.h",
        "src/libs/compat/freebsd_wlan/net80211/ieee80211_ioctl.h",
        "src/libs/compat/freebsd_network/bus.cpp",
        "src/libs/compat/freebsd_network/condvar.cpp",
        "src/libs/compat/freebsd_network/device_hooks.c",
        "src/libs/compat/freebsd_network/fbsd_if_media.c",
        "src/libs/compat/freebsd_network/if.c",
        "src/libs/compat/freebsd_network/taskqueue.c",
        "src/libs/compat/freebsd_network/usb.cpp",
        "src/libs/compat/freebsd_wlan/net80211/ieee80211_haiku.cpp",
        "src/libs/compat/freebsd_wlan/net80211/ieee80211_ioctl.c",
        "src/libs/compat/freebsd_wlan/net80211/ieee80211_output.c",
        "src/libs/compat/freebsd_wlan/net80211/ieee80211_scan_sw.c",
        "src/libs/compat/freebsd_wlan/net80211/ieee80211_sta.c",
        # R5: Wi-Fi parsers drop a frame that does not fit (docs/WIFI.md step 1)
        "src/libs/compat/freebsd_wlan/net80211/ieee80211_hostap.c",
        "src/libs/compat/freebsd_wlan/net80211/ieee80211_crypto_bip.c",
        "src/libs/compat/freebsd_wlan/net80211/Jamfile",
        "src/libs/compat/freebsd_wlan/net80211/ieee80211_ht.c",
        "src/libs/compat/freebsd_wlan/net80211/ieee80211_input.c",
        "src/libs/compat/freebsd_wlan/net80211/ieee80211_input.h",
        "src/libs/compat/freebsd_wlan/net80211/ieee80211_node.c",
        "src/libs/compat/freebsd_wlan/net80211/ieee80211_vht.c",
        "src/libs/compat/freebsd_wlan/net80211/ieee80211_he.c",
        "src/libs/compat/freebsd_wlan/net80211/ieee80211_he.h",
        "src/libs/compat/freebsd_wlan/net80211/ieee80211_node.h",
        "src/libs/compat/freebsd_wlan/net80211/_ieee80211.h",
        # air/OS: MediaTek Bluetooth firmware loader in h2generic
        "src/add-ons/kernel/bluetooth/hci/bluetooth.cpp",
        "src/add-ons/kernel/bluetooth/hci/hci_assemble.h",
        "src/add-ons/kernel/drivers/bluetooth/h2/h2generic/Jamfile",
        "src/add-ons/kernel/drivers/bluetooth/h2/h2generic/h2command.h",
        "src/add-ons/kernel/drivers/bluetooth/h2/h2generic/h2generic.cpp",
        "src/add-ons/kernel/drivers/bluetooth/h2/h2generic/h2mediatek.cpp",
        "src/add-ons/kernel/drivers/bluetooth/h2/h2generic/h2mediatek.h",
        # R5: Realtek firmware loader, docs/BLUETOOTH-2.md step B0
        "src/add-ons/kernel/drivers/bluetooth/h2/h2generic/h2realtek.cpp",
        "src/add-ons/kernel/drivers/bluetooth/h2/h2generic/h2realtek.h",
        "src/add-ons/kernel/drivers/bluetooth/h2/h2generic/h2rtkfw.h",
        "src/add-ons/kernel/drivers/bluetooth/h2/h2generic/h2sco.h",
        "src/add-ons/kernel/drivers/bluetooth/h2/h2generic/h2transactions.cpp",
        "src/add-ons/kernel/drivers/bluetooth/h2/h2generic/h2voice.cpp",
        "src/add-ons/kernel/drivers/bluetooth/h2/h2generic/h2voice.h",
        # R5: L2CAP, docs/BLUETOOTH.md step 1b
        "src/add-ons/kernel/network/protocols/bluetooth/l2cap/Jamfile",
        "src/add-ons/kernel/network/protocols/bluetooth/l2cap/L2capEndpoint.cpp",
        "src/add-ons/kernel/network/protocols/bluetooth/l2cap/L2capEndpoint.h",
        "src/add-ons/kernel/network/protocols/bluetooth/l2cap/L2capEndpointManager.cpp",
        "src/add-ons/kernel/network/protocols/bluetooth/l2cap/L2capEndpointManager.h",
        "src/add-ons/kernel/network/protocols/bluetooth/l2cap/L2capEndpointLE.cpp",
        "src/add-ons/kernel/network/protocols/bluetooth/l2cap/l2cap_tx.cpp",
        # R5: LE links in btCoreData, docs/BLUETOOTH.md step 6
        "src/add-ons/kernel/bluetooth/btCoreData/BTCoreData.cpp",
        "src/add-ons/kernel/bluetooth/btCoreData/ConnectionInterface.cpp",
        "src/add-ons/kernel/network/protocols/bluetooth/l2cap/l2cap.cpp",
        "src/add-ons/kernel/network/protocols/bluetooth/l2cap/l2cap_command.cpp",
        "src/add-ons/kernel/network/protocols/bluetooth/l2cap/l2cap_command.h",
        "src/add-ons/kernel/network/protocols/bluetooth/l2cap/l2cap_internal.h",
        "src/add-ons/kernel/network/protocols/bluetooth/l2cap/l2cap_parse.h",
        "src/add-ons/kernel/network/protocols/bluetooth/l2cap/l2cap_signal.cpp",
        "src/add-ons/kernel/network/protocols/bluetooth/l2cap/l2cap_signal.h",
        "src/add-ons/kernel/file_systems/bfs/Volume.cpp",
        "src/add-ons/kernel/drivers/disk/usb/usb_disk/usb_disk.cpp",
        "src/add-ons/kernel/file_systems/bfs/kernel_interface.cpp",
        "src/add-ons/kernel/drivers/input/i2c_hid/Driver.cpp",
        "src/add-ons/kernel/drivers/input/i2c_hid/HIDDevice.cpp",
        "src/add-ons/kernel/drivers/input/i2c_hid/HIDDevice.h",
        "src/add-ons/kernel/drivers/input/i2c_hid/InputReadyLine.cpp",
        "src/add-ons/kernel/drivers/input/i2c_hid/InputReadyLine.h",
        "src/add-ons/kernel/drivers/input/i2c_hid/Jamfile",
        "src/add-ons/kernel/drivers/input/i2c_hid/ProtocolHandler.cpp",
        "src/add-ons/kernel/drivers/input/i2c_hid/TouchpadProtocolHandler.cpp",
        "src/add-ons/kernel/drivers/input/i2c_hid/TouchpadProtocolHandler.h",
        "src/add-ons/kernel/drivers/input/hid_shared/HIDReportItem.h",
        "src/add-ons/kernel/drivers/bus/usb/usb_raw.cpp",
        "src/add-ons/kernel/drivers/bus/usb/usb_raw.h",
        "src/add-ons/kernel/drivers/input/i2c_elan/Driver.cpp",
        "src/add-ons/kernel/drivers/input/i2c_elan/ELANDevice.cpp",
        "src/add-ons/kernel/drivers/input/i2c_elan/ELANDevice.h",
        "src/add-ons/kernel/drivers/input/i2c_elan/Jamfile",
        "src/add-ons/kernel/bus_managers/ps2/ps2_dev.cpp",
        "src/add-ons/kernel/bus_managers/ps2/ps2_dev.h",
        "src/add-ons/kernel/bus_managers/ps2/ps2_service.cpp",
        "src/add-ons/kernel/bus_managers/ps2/ps2_service.h",
        "src/add-ons/kernel/bus_managers/ps2/ps2_standard_mouse.cpp",
        "src/add-ons/kernel/bus_managers/ps2/ps2_synaptics.cpp",
        "src/add-ons/kernel/bus_managers/ps2/ps2_synaptics.h",
        "src/add-ons/kernel/bus_managers/ps2/ps2_trackpoint.cpp",
    )
    for relative in files:
        install_file(
            root / "distro" / "kernel" / relative,
            HAIKU / relative,
            relative,
        )


def install_smp_boot() -> None:
    src = pathlib.Path(__file__).resolve().parents[1] / "distro/boot/bios_ia32/smp.cpp"
    dst = HAIKU / "src/system/boot/platform/bios_ia32/smp.cpp"
    if not src.is_file():
        raise SystemExit(f"missing {src}")
    if not dst.parent.is_dir():
        raise SystemExit(f"missing {dst.parent}")
    data = src.read_bytes().replace(b"\r\n", b"\n")
    if dst.is_file() and dst.read_bytes().replace(b"\r\n", b"\n") == data:
        print(f"already {dst.name} (smp boot timeout)")
        return
    dst.write_bytes(data)
    print(f"copied {dst} (smp boot timeout)")


def insert_once(path: pathlib.Path, old: str, new: str, marker: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if marker in text:
        print(f"already {path.name} ({tag})")
        return
    if old not in text:
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path} ({tag})")


def install_file(src: pathlib.Path, dst: pathlib.Path, tag: str) -> None:
    if not src.is_file():
        raise SystemExit(f"missing {src}")
    if not dst.parent.is_dir():
        raise SystemExit(f"missing {dst.parent}")
    data = src.read_bytes().replace(b"\r\n", b"\n")
    if dst.is_file() and dst.read_bytes().replace(b"\r\n", b"\n") == data:
        print(f"already {dst.name} ({tag})")
        return
    dst.write_bytes(data)
    print(f"copied {dst} ({tag})")


def install_pstate() -> None:
    root = pathlib.Path(__file__).resolve().parents[1]
    install_file(
        root / "distro/kernel/x86/r5_pstate.cpp",
        HAIKU / "src/system/kernel/arch/x86/r5_pstate.cpp",
        "cpu readout",
    )
    install_file(
        root / "distro/kernel/x86/r5_pstate.h",
        HAIKU / "src/system/kernel/arch/x86/r5_pstate.h",
        "cpu readout",
    )
    install_file(
        root / "distro/cpu/r5_pstate_log.cpp",
        HAIKU / "src/bin/r5_pstate_log.cpp",
        "cpu log",
    )
    install_file(
        root / "distro/cpu/r5_cpu_flags.cpp",
        HAIKU / "src/bin/r5_cpu_flags.cpp",
        "cpu flags",
    )
    install_file(
        root / "distro/cpu/r5_mem_fuzz.cpp",
        HAIKU / "src/bin/r5_mem_fuzz.cpp",
        "memory fuzz",
    )
    install_file(
        root / "distro/cpu/r5_string_fuzz.cpp",
        HAIKU / "src/bin/r5_string_fuzz.cpp",
        "string fuzz",
    )
    for test in ("r5_fpu_test", "r5_timer_test", "r5_usercopy_test", "r5_wc_gtt"):
        install_file(
            root / f"distro/cpu/{test}.cpp",
            HAIKU / f"src/bin/{test}.cpp",
            test,
        )
    install_file(
        root / "distro/cpu/r5_idle_pingpong.cpp",
        HAIKU / "src/bin/r5_idle_pingpong.cpp",
        "idle pingpong test",
    )

    insert_once(
        HAIKU / "src/system/kernel/arch/x86/Jamfile",
        "\tarch_timer.cpp\n",
        "\tr5_pstate.cpp\n\tarch_timer.cpp\n",
        "\tr5_pstate.cpp\n",
        "kernel Jamfile",
    )
    insert_once(
        HAIKU / "src/system/kernel/arch/x86/Jamfile",
        "\tr5_pstate.cpp\n",
        "\tr5_pstate.cpp\n\tr5_direct_map.cpp\n",
        "\tr5_direct_map.cpp\n",
        "kernel Jamfile direct map",
    )
    insert_once(
        HAIKU / "src/bin/Jamfile",
        "\tsystem_time.cpp\n",
        "\tsystem_time.cpp\n\tr5_pstate_log.cpp\n",
        "\tr5_pstate_log.cpp\n",
        "bin Jamfile",
    )
    insert_once(
        HAIKU / "src/bin/Jamfile",
        "\tr5_pstate_log.cpp\n",
        "\tr5_pstate_log.cpp\n\tr5_cpu_flags.cpp\n",
        "\tr5_cpu_flags.cpp\n",
        "bin Jamfile cpu flags",
    )
    insert_once(
        HAIKU / "src/bin/Jamfile",
        "\tr5_cpu_flags.cpp\n",
        "\tr5_cpu_flags.cpp\n\tr5_mem_fuzz.cpp\n",
        "\tr5_mem_fuzz.cpp\n",
        "bin Jamfile memory fuzz",
    )
    insert_once(
        HAIKU / "src/bin/Jamfile",
        "\tr5_string_fuzz.cpp\n",
        "\tr5_string_fuzz.cpp\n\tr5_idle_pingpong.cpp\n",
        "\tr5_idle_pingpong.cpp\n",
        "bin Jamfile idle pingpong test",
    )
    insert_once(
        HAIKU / "build/jam/images/definitions/minimum",
        "\tsystem_time\n",
        "\tsystem_time\n\tr5_pstate_log\n",
        "\tr5_pstate_log\n",
        "image bins",
    )
    insert_once(
        HAIKU / "build/jam/images/definitions/minimum",
        "\tr5_pstate_log\n",
        "\tr5_pstate_log\n\tr5_cpu_flags\n",
        "\tr5_cpu_flags\n",
        "image bins cpu flags",
    )
    insert_once(
        HAIKU / "build/jam/images/definitions/minimum",
        "\tr5_cpu_flags\n",
        "\tr5_cpu_flags\n\tr5_mem_fuzz\n",
        "\tr5_mem_fuzz\n",
        "image bins memory fuzz",
    )
    insert_once(
        HAIKU / "build/jam/images/definitions/minimum",
        "\tr5_mem_fuzz\n",
        "\tr5_mem_fuzz\n\tr5_string_fuzz\n",
        "\tr5_string_fuzz\n",
        "image bins string fuzz",
    )
    insert_once(
        HAIKU / "build/jam/images/definitions/minimum",
        "\tr5_string_fuzz\n",
        "\tr5_string_fuzz\n\tr5_idle_pingpong\n",
        "\tr5_idle_pingpong\n",
        "image bins idle pingpong test",
    )
    insert_once(
        HAIKU / "build/jam/images/definitions/minimum",
        "\tr5_idle_pingpong\n",
        "\tr5_idle_pingpong\n\tr5_timer_test\n\tr5_usercopy_test\n"
        "\tx86_64 @{\n\t\tr5_fpu_test\n\t}@ # x86_64\n",
        "\tr5_usercopy_test\n",
        "image bins amd64 tests",
    )
    # R5: the shared-memory link to app_server: garbage, clone and fork checks
    install_file(
        root / "distro/cpu/r5_link_ring_test.cpp",
        HAIKU / "src/bin/r5_link_ring_test.cpp",
        "link ring test",
    )
    insert_once(
        HAIKU / "build/jam/images/definitions/minimum",
        "\tr5_usercopy_test\n",
        "\tr5_usercopy_test\n\tr5_link_ring_test\n",
        "\tr5_link_ring_test\n",
        "image bins link ring test",
    )
    insert_once(
        HAIKU / "src/bin/Jamfile",
        "\t\tr5_fpu_test.cpp\n",
        "\t\tr5_fpu_test.cpp\n\t\tr5_wc_gtt.cpp\n",
        "\tr5_wc_gtt.cpp\n",
        "bin Jamfile wc gtt",
    )
    insert_once(
        HAIKU / "build/jam/images/definitions/minimum",
        "\t\tr5_fpu_test\n",
        "\t\tr5_fpu_test\n\t\tr5_wc_gtt\n",
        "\tr5_wc_gtt\n",
        "image bins wc gtt",
    )
    insert_once(
        HAIKU / "data/launch/system",
        """service x-vnd.Haiku-SystemLogger {
	launch /system/servers/syslog_daemon
	port logger {
		capacity 256
	}
}
""",
        """service x-vnd.Haiku-SystemLogger {
	launch /system/servers/syslog_daemon
	port logger {
		capacity 256
	}
}

job x-vnd.R5OS-cpu-log {
	launch /system/bin/r5_pstate_log
	on initial_volumes_mounted
}
""",
        "job x-vnd.R5OS-cpu-log",
        "cpu log job",
    )


def install_usb_stability() -> None:
    root = pathlib.Path(__file__).resolve().parents[1]
    source_root = root / "distro" / "kernel"
    files = (
        "src/add-ons/kernel/busses/usb/ehci.cpp",
        "src/add-ons/kernel/busses/usb/ohci.h",
        "src/add-ons/kernel/busses/usb/ehci_hardware.h",
        "src/add-ons/kernel/busses/usb/ehci.h",
        "src/add-ons/kernel/busses/usb/uhci.h",
        "src/add-ons/kernel/busses/usb/uhci.cpp",
        "src/add-ons/kernel/busses/usb/ohci.cpp",
        "src/add-ons/kernel/busses/usb/xhci.cpp",
        "src/add-ons/kernel/busses/usb/xhci.h",
        "src/add-ons/kernel/busses/usb/xhci_hardware.h",
        "src/add-ons/kernel/bus_managers/usb/Device.cpp",
        "src/add-ons/kernel/bus_managers/usb/Hub.cpp",
        "src/add-ons/kernel/bus_managers/usb/usb_private.h",
        "src/add-ons/kernel/bus_managers/usb/Object.cpp",
        "src/add-ons/kernel/bus_managers/usb/BusManager.cpp",
        "src/add-ons/kernel/bus_managers/usb/Stack.cpp",
        "src/add-ons/kernel/bus_managers/usb/usb.cpp",
        "headers/os/drivers/usb/R5USBSpeed.h",
        "src/add-ons/kernel/drivers/network/ether/usb_ecm/ECMDevice.cpp",
    )
    for relative in files:
        source = source_root / relative
        if not source.is_file():
            raise SystemExit(f"missing {source}")
        destination = HAIKU / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        print(f"copied {relative}")


def install_gpio() -> None:
    """The GPIO module (generic/gpio/v1, MIT, docs/GPIO.md) and its header.
    The package line is in distro/desktop/build/jam/packages/Haiku."""
    root = pathlib.Path(__file__).resolve().parents[1]
    (HAIKU / "src/add-ons/kernel/generic/gpio").mkdir(exist_ok=True)
    for relative in (
        "headers/os/drivers/GPIO.h",
        "src/add-ons/kernel/generic/gpio/Jamfile",
        "src/add-ons/kernel/generic/gpio/gpio.cpp",
    ):
        install_file(root / "distro/kernel" / relative, HAIKU / relative,
            "gpio")
    jam = HAIKU / "src/add-ons/kernel/generic/Jamfile"
    anchor = "SubInclude HAIKU_TOP src add-ons kernel generic dpc ;\n"
    line = "SubInclude HAIKU_TOP src add-ons kernel generic gpio ;\n"
    insert_once(jam, anchor, anchor + line, line, "gpio jam")
    if jam.read_text(encoding="utf-8").count(line) != 1:
        raise SystemExit(f"{jam}: gpio SubInclude is not there exactly once")


def install_efi_runtime() -> None:
    """EFI runtime services (generic/efi/v1, MIT, docs/EFI-RUNTIME.md): the
    EFI.h header, the loader hand-over, the kernel boot item and the module.
    The package line is in distro/desktop/build/jam/packages/Haiku."""
    root = pathlib.Path(__file__).resolve().parents[1]
    (HAIKU / "src/add-ons/kernel/generic/efi").mkdir(exist_ok=True)
    for relative in (
        "headers/os/drivers/EFI.h",
        "headers/private/kernel/boot/r5_efi_runtime.h",
        "src/system/boot/platform/efi/Jamfile",
        "src/system/boot/platform/efi/cpu.cpp",
        "src/system/boot/platform/efi/r5_efi_runtime.cpp",
        "src/system/boot/platform/efi/r5_efi_runtime.h",
        "src/system/boot/platform/efi/arch/x86_64/arch_start.cpp",
        "src/system/boot/platform/efi/arch/x86_64/arch_mmu.cpp",
        "src/system/kernel/arch/x86/arch_platform.cpp",
        "src/system/kernel/arch/x86/arch_int.cpp",
        "src/add-ons/kernel/generic/efi/Jamfile",
        "src/add-ons/kernel/generic/efi/efi.cpp",
        "src/add-ons/kernel/generic/efi/efi_arch.h",
        "src/add-ons/kernel/generic/efi/arch_x86_64.cpp",
        "src/add-ons/kernel/generic/efi/efi_call_x86_64.S",
        "src/add-ons/kernel/generic/efi/arch_none.cpp",
        "src/add-ons/kernel/generic/efi/efi_test.cpp",
    ):
        install_file(root / "distro/kernel" / relative, HAIKU / relative,
            "efi")
    jam = HAIKU / "src/add-ons/kernel/generic/Jamfile"
    anchor = "SubInclude HAIKU_TOP src add-ons kernel generic dpc ;\n"
    line = "SubInclude HAIKU_TOP src add-ons kernel generic efi ;\n"
    insert_once(jam, anchor, anchor + line, line, "efi jam")
    if jam.read_text(encoding="utf-8").count(line) != 1:
        raise SystemExit(f"{jam}: efi SubInclude is not there exactly once")


def install_rtsx() -> None:
    """Realtek card readers (busses/mmc/rtsx: OpenBSD rtsx ISC, RTS5228 MIT,
    docs/RTSX.md) and the device_manager path for PCI class 0xff. The
    package lines are in distro/desktop/build/jam/packages/Haiku."""
    root = pathlib.Path(__file__).resolve().parents[1]
    (HAIKU / "src/add-ons/kernel/busses/mmc/rtsx").mkdir(exist_ok=True)
    for relative in (
        "src/system/kernel/device_manager/device_manager.cpp",
        # mmc_bus: a bounded card scan that waits for the next card
        "src/add-ons/kernel/bus_managers/mmc/mmc_bus.cpp",
        # mmc_bus.h: the published card node, removed at the next scan
        "src/add-ons/kernel/bus_managers/mmc/mmc_bus.h",
        # mmc_disk: the last block of a trim, no I/O after its node is removed
        "src/add-ons/kernel/drivers/disk/mmc/mmc_disk.cpp",
        "src/add-ons/kernel/busses/mmc/rtsx/Jamfile",
        "src/add-ons/kernel/busses/mmc/rtsx/rtsx.cpp",
        "src/add-ons/kernel/busses/mmc/rtsx/rtsx.h",
        "src/add-ons/kernel/busses/mmc/rtsx/rtsx_5228.cpp",
        "src/add-ons/kernel/busses/mmc/rtsx/rtsx_5228.h",
        "src/add-ons/kernel/busses/mmc/rtsx/rtsx_haiku.cpp",
        "src/add-ons/kernel/busses/mmc/rtsx/rtsxreg.h",
        "src/add-ons/kernel/busses/mmc/rtsx/rtsxvar.h",
    ):
        install_file(root / "distro/kernel" / relative, HAIKU / relative,
            "rtsx")
    jam = HAIKU / "src/add-ons/kernel/busses/mmc/Jamfile"
    anchor = "\tsdhci_pci.cpp\n;\n"
    line = "SubInclude HAIKU_TOP src add-ons kernel busses mmc rtsx ;\n"
    insert_once(jam, anchor, anchor + "\n" + line, line, "rtsx jam")
    if jam.read_text(encoding="utf-8").count(line) != 1:
        raise SystemExit(f"{jam}: rtsx SubInclude is not there exactly once")


def install_bfs_sticky() -> None:
    """Enforce POSIX S_ISVTX (sticky bit) on BFS unlink, rmdir, and rename
    for /boot/home/Shared (mode 1777)."""
    root = pathlib.Path(__file__).resolve().parents[1]
    # kernel_interface.cpp (the rename check) is already in the list of
    # install_kernel_sources().
    relative = "src/add-ons/kernel/file_systems/bfs/Inode.cpp"
    install_file(root / "distro/kernel" / relative, HAIKU / relative,
        "bfs sticky")


def install_kit_crypt() -> None:
    """Accounts step 5: crypt() hands an Argon2id PHC setting to the
    Security Kit (libr5security.so), so su, login and sshd accept the
    sign-in hashes R5 now writes. Older scrypt and DES settings are
    unchanged."""
    root = pathlib.Path(__file__).resolve().parents[1]
    relative = "src/system/libroot/posix/crypt/crypt.cpp"
    install_file(root / "distro/kernel" / relative, HAIKU / relative,
        "kit crypt")


def install_exfat() -> None:
    """Haiku's exFAT add-on, copied so R5 can write it. The host test stays
    out of the tree that jam compiles."""
    root = pathlib.Path(__file__).resolve().parents[1]
    source = root / "distro/kernel/src/add-ons/kernel/file_systems/exfat"
    dest = HAIKU / "src/add-ons/kernel/file_systems/exfat"
    dest.mkdir(parents=True, exist_ok=True)
    for path in sorted(source.iterdir()):
        if path.name == "exfat_host.cpp":
            continue
        if path.suffix in {".cpp", ".h"} or path.name == "Jamfile":
            install_file(path, dest / path.name, "exfat")


def main() -> int:
    install_smp_boot()
    install_kernel_sources()
    install_pstate()
    install_usb_stability()
    install_gpio()
    install_efi_runtime()
    install_rtsx()
    install_bfs_sticky()
    install_kit_crypt()
    install_exfat()
    # i2c_hid (RenkuOS b37a078925) and x86_acpi_cstates (air/OS 2c48d1ae0)
    # are listed in the native build/jam/packages/Haiku in distro/desktop.
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
