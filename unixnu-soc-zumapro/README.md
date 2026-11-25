# UniXNU-zumapro-common

Shared support for the Google Tensor G4 (`zumapro`), the Exynos-derived chip
used in the Pixel 9 and Pixel 10a families.

## Scope

`dts/zumapro.dtsi` describes what is true of the chip rather than of any one
phone: the core layout and their affinity values, the timer rate and the
interrupt controller. A board device tree includes it and adds its own memory
range, framebuffer and peripherals.

## Where the values came from

Every value was read from a Pixel 10a running stock Android, either from its
own flattened device tree at `/sys/firmware/fdt` or from the chip's registers
by firmware running on the phone. Two Tensor phones differ in the places that
matter most, so nothing here was copied from a similar board.

| Value | Source |
| --- | --- |
| Eight cores, four efficiency and four performance | the phone's `/cpus`, split by the capacities it reports |
| Core affinity values 0x000 to 0x700 | the same nodes' `reg` properties |
| 24.576 MHz timer | the phone's `timer` node, `clock-frequency = <0x1770000>` |
| Interrupt controller at 0x10400000 and 0x10440000 | the phone's `interrupt-controller@10400000` |
| 256 KiB redistributor stride | the 2 MiB redistributor window divided by eight cores |

The timer rate is worth repeating: Qualcomm boards run at 19.2 MHz, and most
of this ecosystem is Qualcomm. A kernel told the wrong rate gets every
timeout wrong by a quarter without ever reporting an error.
