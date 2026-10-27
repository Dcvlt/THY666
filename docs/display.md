# Display and foreground scheduling

The shared C renderer produces a **128 × 64 monochrome framebuffer** of 1,024
bytes. Pixel `(x,y)` is byte `(y / 8) * 128 + x`, bit `1 << (y % 8)`. Fonts,
menus, musical names, views and branding are shared by the STM32 and WebAssembly
builds. Browser CSS presentation must preserve the same content.

## Panel updates

`firmware/instrument/display.c` owns the target image, sent-byte shadow and
transfer state. A prepared transfer freezes its payload; only successful bytes
update the shadow. Dirty spans use explicit page/column addressing rather than
rewriting the whole panel for every note change. New renders replace the latest
target without modifying an in-flight slice.

One foreground turn sends at most **16 data bytes plus three address bytes**.
Pad acquisition and MIDI service run between turns. OLED transfer defers while
pad acquisition or the conflicting LED transfer is active. Faults remain visible
in telemetry rather than turning failed writes into a completed frame.

Page changes use a visibility fence: hide the panel, transfer a complete coherent
scene, then reveal it. The device currently uses that clean handoff instead of
a moving side slide. UP/DOWN selection and ordinary value edits update in place.
Boot similarly keeps the display hidden until its initial frame is complete.
The bootloader's earlier logo is outside the application renderer.

## Musical feedback

Simple, Piano and Fifths retain admitted Arp input pitches between gates, dimmed
with stable dithering. Sounding ownership brightens the active pitches. The
renderer reads snapshots without taking MIDI events from their consumers or
advancing musical time. Generated output and releases keep priority over visual
refresh.

Counters distinguish render time, chunk time, deferred work and completed panel
updates. These software measurements do not measure optical panel scan rate.
There is no single claimed refresh-rate number covering renderer, SPI transfer,
OLED electronics and browser polling.

## Companion view

The OLED option copies the driver's sent buffer and visibility state. Web
presents that same image at higher scale with recognized text; it does not run a
second menu renderer. Companion polling adds latency independently of panel
refresh. The default 100 ms interval is configurable down to 50 ms; commands
remain serialized and foreground musical work retains priority.

`tools/build_oled_profile.py` creates `web/generated/oled-profile.json` using
independent glyphs from `core/thy_ui.c`. Its timing/geometry model supports the
preview, while `thy_studio_ui` remains the shared instrument UI implementation.
See [Views](modes-and-views.md) and [Startup](boot-animation.md).
