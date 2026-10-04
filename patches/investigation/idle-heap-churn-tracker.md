# Idle heap churn tracker

Updated 2026-10-05. This is a playground record for `churn_reduction_test`; none
of these changes have been copied to `memory_churn_reduction_standalone`.

## Measurement notes

- The probe report is one interval between garbage collections, not a
  time-normalized rate or a lifetime total.
- On the ESP32, the frame payload is `4 * n_state + 12 * n_exc_stack` bytes.
  The probe's requested frame size adds a 24-byte code-state header. Frames
  with a payload of at most 44 bytes do not need a heap allocation.
- Do not add `GC_ALLOC_SITE` C allocator totals to the frame/Python allocator
  totals; those are overlapping views of the same allocations.
- The baseline below came from idle badge operation without BadgeBot. Re-run
  the same probe interval after flashing this playground build before claiming
  measured on-device savings.

## Addressed targets

The per-interval savings are potential requested frame bytes, based on the
baseline counts. The post-change compiler result confirms that the corresponding
hot functions and helpers are under the allocation cutoff; the hardware probe
has not yet been re-run.

| ID | Baseline hot site | Baseline calls / requested bytes | Baseline prelude / frame | Playground change | Current maximum in hot path | Status |
|---|---|---:|---:|---|---:|---|
| H1 | `IOQueue.wait_io_event` in [core.py](../../micropython/extmod/asyncio/core.py) | 4,238 / 322,088 B | `(13, 0)` / 76 B | Move ready-event processing into `_process_io_event`; the polling loop keeps only one event local. | 40 B (`wait_io_event`); helper 36 B | Implemented; bytecode-verified; probe pending |
| H2 | `set_color` in [tokens.py](../../modules/app_components/tokens.py) | 1,485 / 112,860 B | `(10, 1)` / 76 B | Separate callable handling and RGB application/fallback while retaining tuple indexing and generic color fallback. | 44 B | Implemented; bytecode-verified; probe pending |
| H3 | `_draw_app` in [scheduler](../../modules/system/scheduler/__init__.py) | 594 / 71,280 B | `(15, 3)` / 120 B | Keep profiling and save/restore in `_draw_app`; move app exception handling to `_draw_app_safely` and crash notification to a small helper. | 44 B | Implemented; bytecode-verified; probe pending |
| H4 | `Menu.draw` in [menu.py](../../modules/app_components/menu.py) | 297 / 43,956 B | `(25, 2)` / 148 B | Split info, focus, and neighbor drawing; cache per-frame animation state. When idle, assign the precomputed focused font size directly instead of interpolating. | 44 B | Implemented; bytecode-verified; probe pending |
| H5 | `_Background.draw` in [background.py](../../modules/app_components/background.py) | 297 / 30,888 B | `(14, 2)` / 104 B | Make the no-runner path an early return and move runner drawing/error recovery to `_draw_runner`. | 44 B | Implemented; bytecode-verified; probe pending |
| H6 | `NotificationService.update` in [app.py](../../modules/system/notification/app.py) | 297 / 29,700 B | `(13, 2)` / 100 B | Keep settled slots on the existing skip path; isolate active-slot updates and their failure log. | 44 B | Implemented; bytecode-verified; probe pending |
| H7 | `PrintA11y.get_deduped_strings` in [printer.py](../../modules/system/a11y/printer.py) | 297 / 22,572 B | `(13, 0)` / 76 B | Keep the unchanged-data test small and split changed-string processing into focused helpers. `finalise_frame` remains async. | 44 B | Implemented; bytecode-verified; probe pending |
| H8 | `TwentyTwentySix.background_task` in [twentysix.py](../../modules/frontboards/twentysix.py) | 298 dict views / 3,576 B; 149 floats / 1,192 B | Python allocations, not code-state frames | Iterate dictionaries directly instead of through `.keys()` views; replace `asyncio.sleep(0.1)` with the existing portable `sleep_ms(100)` helper. | Not applicable | Implemented; firmware-compiled; probe pending |
| H9 | `Menu.draw` idle interpolation in [menu.py](../../modules/app_components/menu.py) | 891 floats / 7,128 B | Three floats per draw | Use the precomputed focused size on the idle path; interpolation remains for animation. | Not applicable | Implemented; focused render test passed; probe pending |

H1-H7 account for 7,505 calls and 633,344 requested bytes in the baseline
interval. The report also contained a small ESP-NOW site (2 calls / 200 B);
that site is not changed here.

To keep recurring exception-bearing helpers within the 44-byte cutoff, the
app-draw, background-draw, and notification-update error paths now print a
short generic failure message rather than formatting/printing the exception
object or traceback. App stop/notification/emote behavior and background runner
recovery remain. Review this diagnostic-detail trade-off before transferring
these changes to a PR branch.

The menu font-size initialization and its diagnostic helper remain larger than
44 bytes (`_calculate_focused_item_sizes`: 60 B; error reporting: 56 B), but
they run only on first use or on the exceptional calculation-failure path,
not on every idle render. Keep this as a lower-priority follow-up unless a
future probe shows it is recurring.

## Bytecode verification

Compiled the changed modules with the branch-matched MicroPython v1.28.0
`mpy-cross`, using the firmware flags `-march=xtensawin -O2`, then inspected
them with `micropython/tools/mpy-tool.py -d`. For each prelude, compute the
payload with `4 * n_state + 12 * n_exc_stack`.

| Target | Before | After |
|---|---:|---:|
| `wait_io_event` | 76 B | 40 B |
| `set_color` | 76 B | 36 B |
| `_draw_app` | 120 B | 40 B |
| `Menu.draw` | 148 B | 28 B |
| `_Background.draw` | 104 B | 20 B |
| `NotificationService.update` | 100 B | 36 B |
| `PrintA11y.get_deduped_strings` | 76 B | 28 B |

Every recurring helper measured for these paths is at most 44 B. Several
exception-bearing helpers and drawing helpers are exactly at the limit, so
retain the Xtensa `mpy-cross` check when changing them.

## Open follow-ups

| ID | Observation | Why deferred | Next evidence needed | Status |
|---|---|---|---|---|
| F1 | 594 generator allocations attributed to `_render_task`, likely from `render_needed.wait()` and async accessibility finalization. | These awaits perform synchronization; changing `finalise_frame` to synchronous would break its tested API contract. | Profile generator call sites before considering an awaitable/API redesign. | Open |
| F2 | 297 generator allocations attributed to `app.run`, likely the `mark_update_finished` coroutine callback. | It delays rendering and waits for foreground focus; changing it could alter scheduling behavior. | Confirm attribution with a call-site probe and measure a semantics-preserving alternative. | Open |
| F3 | `mp_ctx_from_ctx` allocates a context wrapper for `display.get_ctx()`. | Reusing the wrapper could affect identity, retained references, or the per-frame `a11y` field. | Check callers and identity/lifetime expectations, then measure wrapper allocations. | Open |
| F4 | The menu's initial focused-font-size calculation and its exception-only diagnostic frame exceed 44 B. | They are cold paths rather than recurring idle-frame churn. | Revisit only if startup/menu-open probe intervals show material churn. | Open |
| F5 | Other `asyncio.sleep` float allocations may exist outside the measured frontboard loop. | The baseline attribution identified the frontboard's 100 ms loop; the remaining callers were not isolated. | Re-run Python allocation probes and identify any residual callers before changing them. | Open |
| F6 | `system.espnow.service._apply_power_management` had 2 calls / 200 B in the baseline interval. | Low impact beside the high-frequency render/poll sites. | Recheck its count after the next idle probe; only optimize if it becomes material. | Deferred |

## Validation record

- **Branch-matched bytecode:** `mpy-cross -march=xtensawin -O2` compiled the
  changed MicroPython and firmware Python modules; `mpy-tool.py -d` confirmed
  the frame sizes above.
- **Focused simulator tests:** 8 passed, covering accessibility deduplication
  and async finalization, color dispatch, menu labels/font size, background
  failure recovery, notification slot updates, and scheduler accessibility
  handling. The simulator's patched `os.stat` needed a temporary test-process
  compatibility wrapper; no repository test files were changed.
- **Ruff:** both configured hooks passed on the edited `modules/` files.
- **ESP32-S3 firmware build:** passed after formatting. `micropython.bin` was
  `0x2311f0` bytes,
  leaving 10% of the smallest app partition free.
- **On-device probe:** not yet run; keep all byte savings above labelled as
  expected until equivalent idle intervals are measured.
- **Commits:** none created. No changes from this task were made to the
  `memory_churn_reduction_standalone` worktree; it has separate pre-existing
  local changes that were preserved.
