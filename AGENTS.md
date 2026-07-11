# AGENTS

These instructions apply to the whole repository.

## Project Identity

- Target platform: EMF Camp badge firmware on MicroPython v1.28 (ESP32-S3 board config in `tildagon/`).
- This repo includes both firmware/runtime code and simulator tooling.
- Build and runtime behavior must stay compatible with MicroPython constraints, not just desktop CPython.

## Read First

- Main firmware and build docs: [README.md](README.md)
- Simulator setup and usage: [sim/README.md](sim/README.md)
- Shared Python type-checking config: [pyrightconfig.json](pyrightconfig.json)
- Simulator lint/test config: [sim/apps/pyproject.toml](sim/apps/pyproject.toml)
- MicroPython style/contribution guidance: [micropython/CODECONVENTIONS.md](micropython/CODECONVENTIONS.md)
- BadgeBot app-specific instructions: [sim/apps/BadgeBot/.github/copilot-instructions.md](sim/apps/BadgeBot/.github/copilot-instructions.md)

## Repository Boundaries

- `modules/`: Shared Python modules that run on badge and are used by simulator.
- `sim/`: CPython simulator and app test environment.
- `drivers/`: C user modules and low-level device support.
- `tildagon/`: Board manifest/config used by firmware build.
- `micropython/`: Vendored MicroPython source tree.

Prefer keeping changes scoped to the smallest correct area.

## Canonical Commands

- Firmware build uses Docker flow from [README.md](README.md#building) and [scripts/build.sh](scripts/build.sh).
- Simulator run from `sim/`: `pipenv run python run.py` (see [sim/README.md](sim/README.md#running)).
- Simulator app tests from `sim/apps/`: `pytest` (see [sim/apps/pyproject.toml](sim/apps/pyproject.toml)).
- Repo pre-commit linting is scoped to `modules/` and uses Ruff (see [.pre-commit-config.yaml](.pre-commit-config.yaml)).

## MicroPython Performance Rules

- Optimize for runtime and bytecode size, not just lint cleanliness.
- Do not add helper code solely to satisfy lint/type tools if it increases runtime overhead or `.mpy` size.
- Prefer comments, docstrings, and config-level lint adjustments over adding runtime branches/indirections.
- Avoid new allocations or extra work inside `update` / `background_update` hot paths.
- Keep hardware paths and simulator paths compatible without introducing per-frame overhead.

## Timing Pattern

- For app loops that receive `delta`, use delta-accumulated timing patterns instead of repeated direct tick polling in inner logic.
- Reset accumulators/counters on state changes, completion, or abort paths.
- Reference baseline timing style in [modules/app.py](modules/app.py).

## Simulator vs Device

- Guard hardware-only imports/behavior when simulator compatibility is required.
- Keep simulator fakes aligned with runtime call signatures used by badge code.
- Do not assume CPython-only modules are available on-device.

## Change Hygiene

- Keep edits minimal and local.
- Link to existing docs instead of duplicating large procedural content.
- Update instruction files only with durable guidance; avoid volatile implementation snapshots.
