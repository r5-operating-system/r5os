# GPIO for kernel drivers

Haiku has no GPIO interface for drivers. R5 adds a small one, built the way Haiku's own kernel interfaces are built: a kernel module with `module_info` and `std_ops`, used through `get_module()` / `put_module()` (or a `module_dependencies` entry), with a C struct of calls in a header next to `ACPI.h`.

- Header: `distro/kernel/headers/os/drivers/GPIO.h`, installed as `headers/os/drivers/GPIO.h` (MIT).
- Module: `B_GPIO_MODULE_NAME` = `"generic/gpio/v1"`, source `distro/kernel/src/add-ons/kernel/generic/gpio` (MIT), packaged for x86_64.
- First user: the CS35L41 speaker amplifier driver (reset line, speaker ID). See `docs/AUDIO-AMP.md`.

Why a generic module and not a bus manager: a driver here needs one or two pins named by its own ACPI device. There is nothing to enumerate and no device node to publish, so a device-manager bus would only add nodes nobody probes. A generic module is what `dpc` and `tty` use for the same kind of shared kernel service.

## Calls

| Call | What it does |
|---|---|
| `get_acpi_pin(device, index, &pin)` | The `index`'th GpioIo or GpioInt resource in the `_CRS` of `device`, counted in `_CRS` order |
| `get_pin(controllerPath, pin, &pin)` | A pin by its controller's ACPI path (the resource's ResourceSource) and pin number |
| `put_pin(pin)` | Releases the pin (unmaps its register). `NULL` is allowed |
| `get_pin_info(pin, &info)` | Pin number, connection (`B_GPIO_CONNECTION_IO` or `_INTERRUPT`), the firmware's I/O restriction, active-low, whether the output is enabled now, and the controller's hardware ID. Set `info.size` first |
| `read_pin(pin, &high)` | The pin level |
| `write_pin(pin, high)` | The output level; does not change the output enable |
| `set_output_enabled(pin, enabled)` | Turns the output driver on or off |

`B_NOT_SUPPORTED` means no vendor part drives that controller. `write_pin()` and enabling the output return `B_NOT_ALLOWED` on a pin the firmware marks input-only. A driver writes the level before it enables the output, so the pin does not glitch.

Rules: calls are only added at the end of `gpio_module_info`; a call that shipped does not change. `gpio_pin_info` carries a `size` so it can grow.

## Vendor parts

| Controller | ACPI IDs | Register |
|---|---|---|
| AMD FCH | `AMD0030`, `AMDI0030` | One 32-bit register per pin at MMIO base + pin × 4: pin state bit 16, output value bit 22, output enable bit 23 |

R5's `i2c_hid` (`InputReadyLine.cpp`) still reads its interrupt line from the same AMD registers itself. It can move to this module later; it was left unchanged in this round. Intel (for the X1 Carbon) is a later vendor part; interrupts on GPIO pins are not part of this interface yet.
