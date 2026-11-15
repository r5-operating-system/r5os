# Skia chrome/m158 cross build notes

Each row is a build error of `build-skia.sh` with `x86_64-unknown-haiku-gcc`
13.3.0, and the fix. Order: `fetch-skia.sh`, `build-skia.sh`, `pack-skia.sh`,
then `test/build-test.sh` and `test/run-qemu.sh`. arm64 and riscv64 are not
built.

| Error | Fix |
|---|---|
| `gn`'s `build/gen.py` calls `clang++`, which the build host does not have | `fetch-skia.sh` builds `gn` with the host `g++` (`CC=gcc CXX=g++`). `gn` runs on the build host only |
| `gn` `last_commit_position.h: No such file or directory` | A shallow clone has no history to count. `fetch-skia.sh` passes `--no-last-commit-position` and writes that header with the pinned commit |
| 155 objects failed with `limits.h: No such file or directory` and `pthread.h: No such file or directory`, and many follow-up errors (`uint32_t` unknown) | Not Skia: a jam in the shared Haiku tree was repacking `haiku_devel`, and the cross sysroot's `develop/headers` is a link into that package. Do not build Skia while a jam runs; after the jam the errors were gone |
| `SkSemaphore.cpp` `dispatch/dispatch.h`, `SkMemory_malloc.cpp` `malloc/malloc.h`, `src/utils/mac` `ApplicationServices/ApplicationServices.h` | `include/private/SkFeatures.h` knows no `__HAIKU__` and falls back to `SK_BUILD_FOR_MAC`. `build-skia.sh` passes `-DSK_BUILD_FOR_UNIX`; every user of the headers passes it too (`skia-defines.txt` in the package) |
| `SkFontHost_FreeType.cpp:2262` `uninitialized const member in 'struct ...CommonWeights'`, `use of deleted function 'std::array<...CommonWeights, 25>::array()'` | GCC 13.3's `std::to_array` default-constructs the array and assigns each element; a struct with const members has neither. `patches/gcc13-to-array.patch` drops the two `const`s (approved by the user, 15 Nov 2026). After that, `[703/703] link libskia.a` |
| Host test: `SkColorSpace.h` `modules/skcms/skcms.h: No such file or directory` | The package also carries `modules/skcms/skcms.h` and `modules/skcms/src/skcms_public.h` |
| Host test: `SkiaVulkan.h` `vulkan/vulkan_core.h: No such file or directory` | Skia uses its own Vulkan headers. A user of the package adds `-I headers/skia/include/third_party/vulkan` |
| Host test: `'kNo' is not a member of 'skgpu::ThreadSafe'` | `VulkanMemoryAllocatorPriv.h` only forward-declares `enum class ThreadSafe : bool`; the values are in another private header. The test passes `static_cast<skgpu::ThreadSafe>(false)` |
