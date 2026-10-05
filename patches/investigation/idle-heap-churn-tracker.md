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
- `GC_ALLOC_FAIL` is the deliberate trigger used to run/report the GC probe.
  Its free-heap figures are observed after collection; do not treat the line
  as an application allocation failure or evidence of fragmentation.
- The baseline below came from idle badge operation without BadgeBot. The
  2026-10-05 follow-up is three consecutive GC-triggered reports with a visible
  menu and no user interaction. Repeated counts are per-GC-interval values,
  not the sum across all three intervals.

## Addressed targets

The per-interval savings are potential requested frame bytes, based on the
baseline counts. In the 2026-10-05 menu-idle run, none of the targeted H1-H9
allocations are reported in three consecutive GC intervals. Treat these as
verified absent for that tested scenario; other workloads and startup paths
are not covered by this result.

| ID | Baseline hot site | Baseline calls / requested bytes | Baseline prelude / frame | Playground change | Current maximum in hot path | Status |
|---|---|---:|---:|---|---:|---|
| H1 | `IOQueue.wait_io_event` in [core.py](../../micropython/extmod/asyncio/core.py) | 4,238 / 322,088 B | `(13, 0)` / 76 B | Move ready-event processing into `_process_io_event`; the polling loop keeps only one event local. | 40 B (`wait_io_event`); helper 36 B | Verified absent in three menu-idle GC intervals |
| H2 | `set_color` in [tokens.py](../../modules/app_components/tokens.py) | 1,485 / 112,860 B | `(10, 1)` / 76 B | Separate callable handling and RGB application/fallback while retaining tuple indexing and generic color fallback. | 44 B | Verified absent in three menu-idle GC intervals |
| H3 | `_draw_app` in [scheduler](../../modules/system/scheduler/__init__.py) | 594 / 71,280 B | `(15, 3)` / 120 B | Keep profiling and save/restore in `_draw_app`; move app exception handling to `_draw_app_safely` and crash notification to a small helper. | 44 B | Verified absent in three menu-idle GC intervals |
| H4 | `Menu.draw` in [menu.py](../../modules/app_components/menu.py) | 297 / 43,956 B | `(25, 2)` / 148 B | Split info, focus, and neighbor drawing; cache per-frame animation state. When idle, assign the precomputed focused font size directly instead of interpolating. | 44 B | Verified absent in three menu-idle GC intervals |
| H5 | `_Background.draw` in [background.py](../../modules/app_components/background.py) | 297 / 30,888 B | `(14, 2)` / 104 B | Make the no-runner path an early return and move runner drawing/error recovery to `_draw_runner`. | 44 B | Verified absent in three menu-idle GC intervals |
| H6 | `NotificationService.update` in [app.py](../../modules/system/notification/app.py) | 297 / 29,700 B | `(13, 2)` / 100 B | Keep settled slots on the existing skip path; isolate active-slot updates and their failure log. | 44 B | Verified absent in three menu-idle GC intervals |
| H7 | `PrintA11y.get_deduped_strings` in [printer.py](../../modules/system/a11y/printer.py) | 297 / 22,572 B | `(13, 0)` / 76 B | Keep the unchanged-data test small and split changed-string processing into focused helpers. `finalise_frame` remains async. | 44 B | Verified absent in three menu-idle GC intervals |
| H8 | `TwentyTwentySix.background_task` in [twentysix.py](../../modules/frontboards/twentysix.py) | 298 dict views / 3,576 B; 149 floats / 1,192 B | Python allocations, not code-state frames | Iterate dictionaries directly instead of through `.keys()` views; replace `asyncio.sleep(0.1)` with the existing portable `sleep_ms(100)` helper. | Not applicable | Verified absent in three menu-idle GC intervals |
| H9 | `Menu.draw` idle interpolation in [menu.py](../../modules/app_components/menu.py) | 891 floats / 7,128 B | Three floats per draw | Use the precomputed focused size on the idle path; interpolation remains for animation. | Not applicable | Verified absent in three menu-idle GC intervals |

H1-H7 account for 7,505 calls and 633,344 requested bytes in the baseline
interval.

For H1-H7, no corresponding `GC_FRAME_SITE` entries appear in any of the three
reports. For H8-H9, no frontboard dict-view or float allocations appear. Close
these as resolved for the tested menu-idle scenario, but retain coverage for
other workloads and cold paths as separate questions.

## New probe evidence (2026-10-05)

The supplied output covers three consecutive GC-triggered reports while the
badge showed a menu with no user interaction. The same per-interval counters
are printed for each report; do not interpret them as a single three-interval
total.

| Observation | New log evidence | Conclusion / next action |
|---|---|---|
| H1-H7 recurring frame sites | No matching `GC_FRAME_SITE` lines in any of the three intervals. | Resolved for the tested menu-idle scenario; validate separately under other workloads if they are in scope. |
| H8 frontboard dictionary views / sleep floats | No matching frontboard `dict_view` or float allocations in any interval. | Resolved for the tested menu-idle scenario; validate separately under other workloads if they are in scope. |
| H9 menu idle interpolation floats | No float allocations in any interval. | Resolved for the tested menu-idle scenario; validate separately during menu animation if needed. |
| F1 `_render_task` generator | 842 allocations / 45,468 B in each GC interval (`GC_PY_ALLOC_SITE`). | Still recurring in all three intervals; investigate a semantics-preserving alternative. |
| F2 `app.run` generator | 421 allocations / 23,576 B in each GC interval (`GC_PY_ALLOC_SITE`). | Still recurring in all three intervals; investigate a semantics-preserving alternative. |
| ESP-NOW `_apply_power_management` | 3 calls / 300 B (`GC_FRAME_SITE`) in each GC interval; earlier baseline was 2 / 200 B. | Still a heap-backed frame site in all three intervals. |
| ESP-NOW `_has_listeners` | 3 calls / 240 B in frames, 6 dict views / 72 B, and 3 tuples / 48 B (`GC_PY_ALLOC_SITE`) in each interval. | Recurring frame and Python-object churn. The implementation iterates over a tuple of two registries and calls `registry.values()`; assess a small allocation/frame reduction if worth optimizing. |
| GC probe trigger | `GC_ALLOC_FAIL request=56B ...` appears once in each of the three intervals. | Expected: this failed allocation triggers the GC/probe report, and the free-heap figures are post-collection. Excluded from application churn findings. |
| Periodic Wi-Fi power configuration | `wifi:Set ps type: 1, coexist: 0` appears about every 10 seconds; `_apply_power_management` reports 3 calls per GC interval. | Likely the 10-second `_reconcile_power` loop re-applies the unchanged setting. Verify the call path; consider skipping redundant config calls while preserving initial/reset and listener-transition behavior. |
| Unresolved allocator callers | `0x42089339`: 1,272 / 69,164 B (heap 74,240 B); `0x420049b4`: 421 / 6,736 B; `0x4208bf56`: 6 / 540 B (heap 576 B). | Symbolicate against the exact firmware ELF/map and correlate to probe kinds before assigning owners. Do not add these totals to `GC_PY_ALLOC_SITE` or frame totals; instrumentation views may overlap. |

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
| F1 | Generator allocations attributed to `_render_task`: 842 / 45,468 B per GC interval, repeated in all three menu-idle intervals. Likely from `render_needed.wait()` and async accessibility finalization. | These awaits perform synchronization; changing `finalise_frame` to synchronous would break its tested API contract. | Profile generator call sites before considering an awaitable/API redesign. | Open; confirmed recurring |
| F2 | Generator allocations attributed to `app.run`: 421 / 23,576 B per GC interval, repeated in all three menu-idle intervals. Likely the `mark_update_finished` coroutine callback. | It delays rendering and waits for foreground focus; changing it could alter scheduling behavior. | Confirm attribution with a call-site probe and measure a semantics-preserving alternative. | Open; confirmed recurring |
| F3 | `mp_ctx_from_ctx` allocates a context wrapper for `display.get_ctx()`. | Reusing the wrapper could affect identity, retained references, or the per-frame `a11y` field. | Check callers and identity/lifetime expectations, then measure wrapper allocations. | Open |
| F4 | The menu's initial focused-font-size calculation and its exception-only diagnostic frame exceed 44 B. | They are cold paths rather than recurring idle-frame churn. | Revisit only if startup/menu-open probe intervals show material churn. | Open |
| F5 | Search found 15 firmware-executable `asyncio.sleep()` calls with decimal-literal delays, plus one simulator-only occurrence. | Not all are idle paths; some run only in app-specific, retry, or threaded helper flows. | Candidate locations and durations are listed below; convert deliberate delays to `sleep_ms` and test timing/API behavior. | Search complete; replacements not applied |
| F6 | `system.espnow.service._apply_power_management`: baseline 2 / 200 B; new result 3 / 300 B per GC interval in all three reports. | Low volume beside F1/F2, but it is confirmed recurring. The Wi-Fi log also shows the setting being applied about every 10 seconds. | Verify whether the periodic loop can skip unchanged settings without breaking setup/reset behavior. | Deferred; confirmed recurring |
| F7 | `system.espnow.service._has_listeners`: 3 / 240 B in frames, 6 dict views / 72 B, and 3 tuples / 48 B per GC interval, repeated in all three reports. | Lower volume than F1/F2 but confirmed recurring. | Consider simplifying registry traversal, then verify with another three-interval probe. | Open |
| F8 | Three `GC_ALLOC_SITE` caller addresses are not yet symbolicated. | Caller totals may overlap frame/Python allocator instrumentation and must not be summed. | Resolve against the exact firmware ELF/map and correlate with allocation-kind records. | Open |
| F9 | Wi-Fi power mode is configured about every 10 seconds while idle. | The repeated `Set ps type` log and `_apply_power_management` count suggest redundant periodic work, but the exact cause is not proven. | Confirm `service.py` call path and test whether unchanged PM values can safely be skipped. | Open |

### Float sleep replacement candidates

`modules/async_helpers.py:sleep_ms` uses MicroPython's native
`asyncio.sleep_ms(delay_ms)` when available. The MicroPython implementation
returns a `SingletonGenerator` specifically to avoid heap allocation. Its
CPython fallback converts milliseconds to seconds for simulator compatibility,
so device-side float-allocation benefits do not apply to that fallback.

The firmware-side search found 15 decimal-delay `asyncio.sleep()` call sites.
These are candidates, not claims of observed idle churn:

| File | Firmware call sites | Delay | Notes |
|---|---:|---:|---|
| [async_helpers.py](../../modules/async_helpers.py) | 1 | 100 ms | `unblock` periodic callback. Its `Message.wait` call at line 30 is in the CPython-only simulator implementation and is not a device candidate. |
| [dialog.py](../../modules/app_components/dialog.py) | 2 | 50 ms | Dialog polling/wait paths. |
| [twentyfour.py](../../modules/frontboards/twentyfour.py) | 1 | 100 ms | Frontboard background loop; analogous to the converted TwentyTwentySix loop. |
| [hexpansionfw.py](../../modules/firmware_apps/hexpansionfw.py) | 6 | 100 ms | Firmware-app waits and polling paths. |
| [patterninhibit.py](../../modules/firmware_apps/patterninhibit.py) | 1 | 500 ms | Pattern timing loop. |
| [tick_app.py](../../modules/firmware_apps/tick_app.py) | 1 | 100 ms | Firmware-app polling loop. |
| [app.py](../../modules/system/hexpansion/app.py) | 1 | 100 ms | EEPROM detection retry. |
| [app.py](../../modules/system/backleds/app.py) | 2 | 50 ms | Back-LED update loops. |

The search also found integer-delay `asyncio.sleep()` calls and zero-delay
cooperative yields; those are not decimal-float candidates and are not listed
for replacement here. Synchronous `time.sleep(0.5)` calls in configuration or
simulator code are not asyncio sleep sites.

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
- **On-device probe:** three consecutive GC-triggered intervals were supplied
  on 2026-10-05 for a menu-idle, no-interaction scenario. H1-H9 are absent in
  each interval; F1/F2 and ESP-NOW churn recur in each. The `GC_ALLOC_FAIL`
  lines are the expected probe trigger, with free-heap values reported after
  collection, not application allocation failures.
- **Commits:** none created. No changes from this task were made to the
  `memory_churn_reduction_standalone` worktree; it has separate pre-existing
  local changes that were preserved.
