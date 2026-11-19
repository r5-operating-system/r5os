# AMD64 Review (#4435, #4436, #4437, #4439, #4443, #4444, #4445, #4446, #4447, #4448)

Reviewed against `origin/main` (`1a465ee64c`) and the 5625U stick logs (`generated/boot-5625u-52` in `claude/amd64-stick-tests`). Findings are ordered most severe first.

## 1. First restart hang + CMOS corruption on 5625U (`#4443`, `#4445`)
- **Location**: `distro/kernel/src/system/kernel/shutdown.cpp:88-115`, `distro/kernel/src/system/kernel/arch/x86/arch_cpu.cpp:2017-2043`, `distro/kernel/src/system/kernel/cpu.cpp:367-380`, `distro/kernel/src/add-ons/kernel/power/cpuidle/x86_acpi_cstates/acpi_cpuidle.cpp:225-239`, `distro/kernel/src/system/kernel/scheduler/scheduler.cpp:146-152`, `distro/kernel/src/system/kernel/arch/x86/timers/x86_apic.cpp:2189-2230`, `distro/kernel/build/config_headers/kernel_debug_config.h:161-185`.
- **What goes wrong**: On the 5625U stick (`boot-5625u-52`), the first restart hung and left a CMOS corruption error; `syslog` ends before shutdown because `kill_team()` terminates `syslogd` before `sync()`, `call_shutdown_hooks()`, and `arch_cpu_shutdown()`, and `KDEBUG_ENABLE_DEBUG_SYSLOG` is `0` when `KDEBUG_LEVEL` is `0`.
- **Scenario**: Rebooting on 5625U hardware (`acpi_cpuidle` active, MWAIT IPI skip active, single-write APIC one-shot, `XSAVEC`, `KDEBUG_LEVEL 0`).
- **How sure**: Unproven by inspection alone — no unconditional shutdown/reboot hang was found in `cpu_idle` reschedule, MWAIT IPI skip, APIC one-shot, `XSAVEC`, `time_seq`, or `KDEBUG_LEVEL 0` (and `r5_fpu_test`, `r5_usercopy_test`, `r5_timer_test`, and `r5_idle_pingpong` all passed).
- **Fix / Status**: Not blindly modified; added the smallest `dprintf` trail in `distro/kernel/src/system/kernel/shutdown.cpp` (`enter`, `teams killed`, `sync done`, `hooks done`) and `distro/kernel/src/system/kernel/arch/x86/arch_cpu.cpp` (`acpi_shutdown`, `kbc 0xfe`, `x86_reboot`) without touching the GFX9 stop contract. A restart fix is only proven on the 5625U.

## 2. GLTeapot on RADV shows frame by frame (300 fps reported) while llvmpipe is smooth (700 fps)
- **Location**: **Not found** in `#4435..#4448`; `distro/mesa/` was not modified per constraints.
- **What goes wrong**: GLTeapot on RADV (`zink Vulkan 1.4(AMD Radeon Graphics (RADV RENOIR))`, `BDirectWindow` active) reports 300 fps yet presents frame by frame, whereas `llvmpipe` is smooth at 700 fps.
- **What was ruled out**:
  - **GTT `memcpy` from uncached memory** (`distro/kernel/src/system/libroot/posix/string/arch/x86_64/memcpy.cpp:204-256`): `gfx9_wc_gtt` is off, so GTT buffers are mapped cached; additionally, both RADV and `llvmpipe` use `memcpy` to copy rows to `BDirectWindow` framebuffer memory, and `llvmpipe` is smooth at 700 fps.
  - **`Compositor.cpp` / `r5_span_avx.h` / `Painter.cpp` / `DrawBitmapBilinear.h` / `ColorConversion.cpp`** (`#4446`): `syslog` shows `app_server: compositor paused: direct window` while GLTeapot runs, so `Compositor` is paused and not drawing GLTeapot frames.
  - **MWAIT IPI skip / `cpu_idle` reschedule / APIC one-shot timer / `XSAVEC`** (`#4445`): `r5_idle_pingpong` (`20000` rounds in `69685` us), `r5_timer_test`, and `r5_fpu_test` all passed on 5625U in `boot-5625u-52`, and `llvmpipe` (multi-threaded) runs smoothly.
  - **`pthread_mutex.cpp` / `pthread_rwlock.cpp` / `mutex.cpp` / `malloc.c`** (`#4446`): Inspected; no spin/wait bug found on x86-64, and `llvmpipe` shares these primitives.
  - **Large pages (`#4435`, `#4436`)**: `2MB pages: off` in `boot-5625u-52/r5-amd64-check.txt`.
- **Scenario**: Running GLTeapot on 5625U with RADV enabled.
- **How sure**: Not found by code inspection — QEMU has no 5625U GPU and cannot run RADV.
- **Fix / Status**: Not fixed (requires 5625U hardware trace).

## 3. Data race on one-time dispatch in `memcpy.cpp` and `memset.cpp` (`#4439`)
- **Location**: `distro/kernel/src/system/libroot/posix/string/arch/x86_64/memcpy.cpp:216-232`, `distro/kernel/src/system/libroot/posix/string/arch/x86_64/memset.cpp:135-149`.
- **What goes wrong**: `chosen` was checked with `__atomic_load_n(&chosen, __ATOMIC_ACQUIRE) == 0`, then `memcpy_choose_bulk()` / `memset_choose_bulk()` wrote `sMemcpyStreaming` / `sMemcpyBulk` / `sMemcpyRepFrom` (and `sMemsetStreaming` / `sMemsetBulk`) non-atomically before storing `chosen = 1`. Two threads entering simultaneously race on writing/reading those globals (with `sMemcpyStreaming` starting at `0`).
- **Scenario**: Multi-threaded startup calling `memcpy` (>16 B) or `memset` (>=32 B) concurrently in user space or kernel.
- **How sure**: Sure (directly observed in source).
- **Fix / Status**: **Fixed** by computing the streaming cutoff, `rep movsb` cutoff, and bulk function pointer into locals, publishing them with `__atomic_store_n(..., __ATOMIC_RELAXED)`, and publishing `sMemcpyChosen` / `sMemsetChosen` last with `__ATOMIC_RELEASE` (readers load with `__ATOMIC_ACQUIRE`). No spinning/waiting, so signal handlers and interrupts cannot deadlock on the same CPU.

## 4. Non-temporal stores (`_mm_stream_si128`) in kernel `memcpy` / `memset` (`#4439`)
- **Location**: `distro/kernel/src/system/libroot/posix/string/arch/x86_64/memcpy.cpp:222-225`, `distro/kernel/src/system/libroot/posix/string/arch/x86_64/memset.cpp:141-144`.
- **What goes wrong**: `memcpy.cpp` and `memset.cpp` are linked into the kernel (`src/system/kernel/lib/arch/x86_64/Jamfile`). Copies or fills `>= 3/4 LLC` (12 MiB on 5625U) used `memcpy_nt` / `memset_nt` (`_mm_stream_si128` + `_mm_sfence`), which is unsafe/weakly-ordered if ever called on uncacheable (UC) MMIO/device mappings in the kernel.
- **Scenario**: Kernel `memcpy`/`memset` of >= 12 MiB touching UC memory.
- **How sure**: Sure of mechanism; low probability in practice due to 12 MiB threshold.
- **Fix / Status**: **Fixed** by guarding `memcpy_nt` / `memset_nt` out under `#ifndef _KERNEL_MODE`.

## 5. Data race on one-time dispatch in AVX2 string functions and `r5_span_blend` (`#4444`, `#4446`)
- **Location**: `distro/kernel/src/system/libroot/posix/musl/string/strlen.c:825-830`, `memchr.c:577-582`, `strchrnul.c:722-727`, `distro/kernel/src/system/libroot/posix/string/memcmp.c:989-994`, `strcmp.c:1175-1180`, `distro/app_server/r5_span_avx.h:83-86`.
- **What goes wrong**: String functions used a separate `s*Chosen` flag + non-atomic `s*Long` pointer store; `r5_span_blend` used a plain non-atomic `static int useAvx = -1`.
- **Scenario**: Concurrent first calls from multiple threads.
- **How sure**: Sure (directly observed in source).
- **Fix / Status**: **Fixed** by loading/storing the function pointer (`strlen`, `memchr`, `strchrnul`, `memcmp`, `strcmp`) with `__atomic_load_n(..., __ATOMIC_ACQUIRE)` / `__atomic_store_n(..., __ATOMIC_RELEASE)` (matching `fma.c`), and using `__atomic_load_n` / `__atomic_store_n` for `useAvx` in `r5_span_blend`.

## 6. `InformationModule.cpp` hardcoded software graphics detail (`#4447`)
- **Location**: `distro/settings/InformationModule.cpp:1170-1171`, `distro/settings/InformationModule.cpp:1420`.
- **What goes wrong**: The Graphics tile subtitle and Copy details text always reported `"Drawn by the processor (software)"` even when `radeon_hd` GPU acceleration is available.
- **Scenario**: Opening Settings -> System -> Information on a machine with a supported GPU (`radeon_hd: gpu available`).
- **How sure**: Sure (directly observed in source).
- **Fix / Status**: **Fixed** by adding `graphics_detail()` querying `/dev/gpu/radeon_hd/0` via `R5_GPU_GET_INFO` (`R5_GPU_CAP_AVAILABLE` and not `R5_GPU_CAP_LOST`), returning `"Drawn by the GPU (hardware)"` when available and `"Drawn by the processor (software)"` otherwise, without changing the layout or fixed descriptions.

## 7. `uint64` overflow in APIC one-shot timer tick calculation (`#4445`)
- **Location**: `distro/kernel/src/system/kernel/arch/x86/timers/x86_apic.cpp:118-124` (`apic_timer_set_hardware_timer`).
- **What goes wrong**: In non-TSC-deadline mode, `(relativeTimeout * sApicTicsPerSec) / 1000000` was computed in `uint64` before clamping to `UINT32_MAX` (unlike the TSC-deadline branch above it, which used `__uint128_t`). A large `relativeTimeout` (such as `B_INFINITE_TIMEOUT` or multi-hour timeouts at high APIC bus frequencies) overflows `uint64` and wraps to a small tick count, firing the timer early.
- **Scenario**: Long relative timeout on a CPU using APIC one-shot count mode (e.g., AMD 5625U).
- **How sure**: Sure (directly observed in source).
- **Fix / Status**: **Fixed** using `(__uint128_t)relativeTimeout * sApicTicsPerSec / 1000000` before clamping to `UINT32_MAX`.

## 8. `pthread_rwlock.cpp` lockless reader fast path memory ordering on non-x86 (`#4446`)
- **Location**: `distro/kernel/src/system/libroot/posix/pthread/pthread_rwlock.cpp:163-173`, `188-200`, `211-220`.
- **What goes wrong**: `LocalRWLock` is in a shared file without `#ifdef __x86_64__` and relies on `atomic_add` + `atomic_get` for store-load ordering between `reader_count` and `writer_count`. That holds on x86-64 TSO (`lock xadd`), but `atomic_get` has no barrier on weakly-ordered architectures (ARM64, RISC-V).
- **Scenario**: Concurrent `ReadLock` / `WriteLock` / `Unlock` on ARM64 or RISC-V.
- **How sure**: Sure for weak memory models; safe on x86-64.
- **Fix / Status**: Not fixed in this AMD64-focused pass; documented for ARM64/RISC-V bring-up (use `__ATOMIC_SEQ_CST` or guard).

## 8. ABI check: `Vision` `Could not resolve symbol 'general_info'` (`#4439` review)
- **Location**: `boot-5625u-52/syslog:2485`.
- **What goes wrong**: `runtime_loader` logged `Could not resolve symbol 'general_info'` for `/boot/system/apps/Vision/Vision`.
- **How sure**: Verified in source: `general_info` (`InterfaceDefs.cpp:71`) is a global variable not exported in `libbe_versions` in Haiku upstream; `#4439` only added `-fno-semantic-interposition` and `-Wl,-Bsymbolic-functions` (which do not hide exported symbols).
- **Fix / Status**: Not a `#4439` regression; no change needed.
