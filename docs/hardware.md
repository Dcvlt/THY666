# Hardware target

THY666 targets the TheoryBoard THY333 application with an STM32F405-class MCU,
96 pressure pads, 43 capacitive controls, a 128 × 64 OLED, pad RGB lights,
center-control backlights, USB and DIN MIDI OUT. The working board model is
STM32F405VGT6 / THY333 Echo Summit with 16 MiB serial NOR. Exact chip markings
and compatibility across other PCB revisions remain to be independently checked.

The present instrument has been exercised on one owner's controller. A working
path on that board does not establish compatibility with every hardware revision
or modification. Sensitivity profiles accommodate modified pads but cannot
repair a mechanical fault.

## Runtime resources

| Region | Allocation |
| --- | --- |
| Existing boot region | Below `0x08040000`; outside application ownership |
| Application flash | `0x08040000..0x080c0000`, 512 KiB |
| Settings journal | `0x080c0000..0x08100000`, 256 KiB |
| Ordinary SRAM | 128 KiB including the reserved 8 KiB stack |
| CCM | 64 KiB CPU-only storage; not DMA-accessible |
| Serial NOR | 16 MiB device; not currently an expandable song collection |

The linker enforces partition, CCM and stack boundaries. Startup explicitly
enables CCM access and initializes the instrument state; it does not assume the
existing bootloader left peripherals in application-ready state.

## Device interfaces

- Six multiplexed ADC banks acquire 96 pads. An explicit electrical/logical map
  binds samples to column-major pad identities and physical lights.
- Four CY8CMBR3116-family touch banks provide navigation, function controls,
  left root keys and right shortcuts on I2C1. Center and upper polling have
  independent intervals and bounded retry behavior.
- SPI3 serves the SSD1306-compatible page display and serial-NOR interface.
  OLED control uses PD14 CS, PD11 DC and PD12 reset.
- SPI2/DMA encodes RGB pad lighting. Center backlights use a separate serial
  clock/data/latch stream, with logical indicator mapping independent of pads.
- DIN MIDI OUT uses its own bounded transmit service at MIDI serial speed.
  USB exposes MIDI and the application's diagnostic/companion service.

Authoritative pin selections and peripheral setup are in
`firmware/diagnostic/board.c`, shared by the instrument adapter. Current driver
behavior is described in [Display](display.md) and
[Center response](center-touch-response.md).

No working DIN receive path is established. Pedal integration, low-power
lifecycle and interrupted-update recovery remain separate work. The held-pad
update mode belongs to the existing bootloader; see [Installation](installation.md).
