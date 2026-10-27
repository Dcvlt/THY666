# Startup presentation

The shared C renderer displays the supplied THY666 monochrome emblem during
application startup, then hands off to the configured Play view. Device and
WebAssembly use the same branding and lifecycle.

Readiness is explicit: the caller reports display, controls, acquisition and
MIDI milestones through `thy_studio_ui_boot_begin/progress/ready/skip`. Progress
does not invent a percentage from elapsed time. The panel remains hidden until
its initial image is transferred completely.

Input and MIDI service continue during presentation. A real musical/control
gesture can dismiss it, with that first gesture still taking effect. Readiness
does not wait for USB mounting or every pad to finish conditioning. Startup
never restores running notes or resets applied musical settings.

Animation settings/reduced motion disable movement. The renderer uses existing
buffers and integer arithmetic, without dynamic allocation. Replaying the
startup in the local preview shows presentation only; it does not simulate
device initialization timings or reboot the connected board.

The earlier bootloader logo belongs to the existing update/boot environment and
is outside this application renderer. See [Display](display.md) and
[Installation](installation.md).
