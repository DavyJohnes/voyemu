# How Voyemu works

This section explains the concepts behind Voyemu: what a profile and a machine are, how preparation turns OEM inputs into a machine, how a session runs the guests and how automation drives it. Each concept has one page; other pages link to it.

## From a profile to a running machine

- [Profiles and machines](profiles-machines.md) — the authoring source and the prepared result.
- [Components and recipes](components.md) — adaptations of OEM inputs and their guards.
- [Preparation](preparation.md) — `deps`, `fetch` and `build`.
- [Local roots](local-roots.md) — where inputs, tools, outputs and session state live.

## Running

- [Launch and sessions](launch.md) — lifecycle commands, subsystems and what they print.
- [Guests and the interguest link](guests.md) — the Android and QNX guests and how they talk.
- [Embedded ADB](embedded-adb.md) — how the supervisor reaches the Android guest.
- [Vehicle model and CAN](vehicle.md) — simulated vehicle values and CAN observations.
- [The window](window.md) — screens, steering wheel and vehicle controls.

## Automation and diagnostics

- [Voyemu API](api.md) — requests with `schema: 1`, ownership and receipts.
- [Errors](errors.md) — error codes, outcomes and exit codes.
- [Logs](logs.md) — log records, levels and session log files.

The [decision records](../why/index.md) explain why these concepts look the way they do.
