# MicroPython Heap Probe Archive

`micropython-heap-probes.patch` preserves the temporary C-level allocation
diagnostics used during the BadgeBot heap investigation. It is an investigation
artifact only and must not be applied by `scripts/firstTime.sh` or included in a
production firmware build.

## Provenance

- MicroPython base: v1.28.0, `e0e9fbb17ed6fd06bb76e266ae554784c9c80804`.
- Nested micropython-lib base: v1.28.0, `8380c7bb8f9e5e5260e9539156742925e00366b2`.
- Badge firmware: `f18381f7ebd715ede392eeb879ffa56e61527aa7` on
  `memory_churn_investigations`.
- Board/build configuration: `BOARD=tildagon`, `TARGET=esp32s3`, ESP-IDF 5.5.1.
- Local MicroPython probe commit: `0ce1c660231611cddfc6f8745290d42e01c98dc5`
  on the local-only `heap-diagnostics` branch.

The patch contains only the GC/allocation instrumentation changes in the
ESP32 port and MicroPython core. It excludes the separate micropython-lib LED
optimizations and other local MicroPython changes.

## Reproduction

Start from the MicroPython base above, ensure its nested micropython-lib
submodule is at the recorded base, then run `git apply --check` followed by
`git apply patches/investigation/micropython-heap-probes.patch`. This archive
does not modify the production patch stack.