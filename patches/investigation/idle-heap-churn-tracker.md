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

The probe's Python-allocation label is the currently executing bytecode frame
when the object header is allocated, not necessarily the function that defines
the generator. The MicroPython probe records `gc_alloc_diag_code_state` in
`mp_obj_fun_bc_alloc_diag_record_current`; generator-wrapper allocation occurs
at the call site. Inspection of the following await expressions explains the
reported counts:

- Each `_render_task` iteration awaits `render_needed.wait()` and, when the
  default accessibility handler is installed, `ctx.a11y.finalise_frame()`.
  Both produce a generator object. The measured 842 is exactly two per 421
  render iterations. `PrintA11y.finalise_frame` contains no internal await,
  but is still declared `async`.
- Each base `App.run` update cycle awaits the scheduler-provided
  `render_update` callback, which is the async `mark_update_finished` closure.
  That creates one generator per cycle, matching the 421 reported allocations.
- `asyncio.sleep_ms` returns its module-level `SingletonGenerator`; the awaits
  of `sleep_ms(0)` and `sleep_ms(50)` do not create a new generator each cycle.

Thus the source attribution is high confidence for this menu-idle workload;
the counts are per GC interval, not time-normalized rates.

The a11y helper methods are used by firmware UI components: menus, dialogs,
notifications, and layout use `ctx.a11y` to add/collect accessible text or
suppress it. This is active in-repository use of the text-collection API, not
evidence of an external consumer of an asynchronous finalizer. The only
in-repository replacement-handler caller found is BadgeBot in the simulator,
which suppresses/restores the default handler; its async-finalizer assertions
and fake async handler are tests introduced during our work, not upstream
interface requirements. The `A11yImplementation` stub methods are synchronous
and it does not define `finalise_frame`.

Given that evidence, the bounded F1 candidate is to make the built-in
`PrintA11y.finalise_frame` synchronous and call it synchronously from the
scheduler, then remove/update the BadgeBot tests that asserted async behavior.
This preserves the active add-alt/text collection behavior while removing one
generator per render. Confirm compatibility with any external/custom handlers
before changing the replacement-handler protocol. The `Event.wait()` generator
is intrinsic to the current event-wait API and must not be replaced with a
busy poll. F2 is more invasive because the async callback both signals
rendering, delays briefly, and waits for foreground focus; eliminating its
per-update generator needs a deliberate App/scheduler protocol change, not a
mechanical coroutine-to-function conversion.

| ID | Observation | Why deferred | Next evidence needed | Status |
|---|---|---|---|---|
| F1 | 842 generator allocations / 45,468 B per GC interval in `_render_task`, repeated in all three menu-idle intervals. | One render pass awaits `render_needed.wait()` and `ctx.a11y.finalise_frame()`; both calls construct a generator. The `sleep_ms(0)` await uses a reusable singleton and is not the source. | Consider making `PrintA11y.finalise_frame` synchronous and calling it synchronously from the scheduler: it has no internal awaits, and the only in-repo async-interface requirements found are our BadgeBot tests. Keep Event.wait synchronization unless a safe reusable waiter is designed. | Open; call sites traced; API reconsidered |
| F2 | 421 generator allocations / 23,576 B per GC interval in `app.run`, repeated in all three menu-idle intervals. | Each `await render_update()` calls the scheduler's async `mark_update_finished` closure, constructing one generator. `sleep_ms(50)` uses the reusable singleton. | Any reduction requires changing the App.run/render-update handshake, used by multiple app overrides; design and measure before changing the API. | Open; call site traced |
| F3 | `mp_ctx_from_ctx` allocates a context wrapper for `display.get_ctx()`. | Reusing the wrapper could affect identity, retained references, or the per-frame `a11y` field. | Check callers and identity/lifetime expectations, then measure wrapper allocations. | Open |
| F4 | The menu's initial focused-font-size calculation and its exception-only diagnostic frame exceed 44 B. | They are cold paths rather than recurring idle-frame churn. | Revisit only if startup/menu-open probe intervals show material churn. | Open |
| F5 | Search found 15 firmware-executable `asyncio.sleep()` calls with decimal-literal delays, plus one simulator-only occurrence. | Not all are idle paths; some run only in app-specific, retry, or threaded helper flows. | Six sites converted to `sleep_ms`; eight sites excluded per request; `patterninhibit.py` deferred for async-design review. | Selected replacements complete |
| F6 | `system.espnow.service._apply_power_management`: 3 / 300 B of frame allocation per GC interval in all three reports; Wi-Fi logs show configuration about every 10 seconds. | Low volume. The method intentionally retries `sta.config(pm=pm)` while the driver initializes and reasserts the setting in its reconciliation loop; `_radio_awake` only suppresses duplicate status messages. Skipping unchanged settings could lose recovery after a Wi-Fi state reset. | Keep reassertion unless reset/configuration lifecycle is established; frame reduction via a retry-helper split is possible but low priority. | Deferred; recurring, behavior-sensitive |
| F7 | `system.espnow.service._has_listeners`: 3 / 240 B of frame allocation, 6 dict views / 72 B, and 3 tuples / 48 B per GC interval, repeated in all three reports. | Avoidable: each call constructed one tuple of the two registries and two `dict.values()` views. | Iterate each registry directly through `_registry_has_listeners`; preserve lookup semantics. | Implemented; helper frames 12 B / 36 B; on-device probe pending |
| F8 | Three `GC_ALLOC_SITE` caller addresses are not yet symbolicated. | Caller totals may overlap frame/Python allocator instrumentation and must not be summed. | Resolve against the exact firmware ELF/map and correlate with allocation-kind records. | Open |
| F9 | Wi-Fi power mode is configured about every 10 seconds while idle. | The repeated `Set ps type` log and `_apply_power_management` count suggest redundant periodic work, but the exact cause is not proven. | Confirm `service.py` call path and test whether unchanged PM values can safely be skipped. | Open |
| F10 | `PatternInhibit._make_red` contains `await` in a synchronous method, and both call sites are synchronous. | CPython rejects the file; MicroPython compiles `_make_red` as a generator, but the callers discard the unstarted generator. Thus the 500 ms wait and LED writes do not execute. | No action planned on this playground; this is outside the requested scope and left for the upstream maintainers. | Intentionally out of scope per user (2026-10-05) |

### PatternInhibit findings (reference only; no playground work planned)

In [patterninhibit.py](../../modules/firmware_apps/patterninhibit.py),
`_make_red` is declared with `def`, not `async def`. Pylance reports
`"await" allowed only within async function`, and CPython `py_compile` raises
`SyntaxError: 'await' outside async function`. The branch-matched MicroPython
compiler does accept it, but `mpy-tool.py -d` shows `_make_red` has a
`YIELD_FROM` instruction and generator prelude. Both synchronous call sites
call it and discard its return value, so the generator is never advanced:
neither its 500 ms sleep nor its red LED writes run. This behavior is confirmed
by compiler output; actual on-device LED behavior has not been separately
probed.

Upstream checks on 2026-10-05:

- Upstream `main` contains the same `PatternInhibit` implementation.
- [Issue #234](https://github.com/emfcamp/badge-2024-software/issues/234)
  remains open and reports patterns not restarting after apps that disable
  them are minimized. This app emits `PatternDisable` but its cancel path calls
  `App.minimise()`, which only pops the foreground app; it does not emit
  `PatternEnable`.
- [PR #228](https://github.com/emfcamp/badge-2024-software/pull/228) is merged,
  but its fix is specific to the OTA app's own `minimise` override; it does not
  change the generic `App.minimise` behavior used here.
- [PR #392](https://github.com/emfcamp/badge-2024-software/pull/392) is merged
  and makes the back-LED manager obey `PatternEnable`/`PatternDisable`. It does
  not fix this app's unstarted generator or restore the pattern on minimize.
- [PR #308](https://github.com/emfcamp/badge-2024-software/pull/308) is an open
  draft proposing per-LED pattern overrides. It is a different approach to
  controlling LEDs while patterns run, not a fix for this app's lifecycle.

For context, do not add `time.sleep_ms(500)`. A blocking sleep in the
constructor or synchronous `update` would stall the single-threaded asyncio
loop, delaying event handling, input, rendering, and pattern updates for half a
second. Simply changing `_make_red` to `async def` is also insufficient:
`__init__` and `update` cannot await it, and merely calling an async function
does not run it. If retaining the delay, schedule it as a task with
`asyncio.create_task`, and cancel or guard it when the state changes so an old
delayed task cannot repaint LEDs after re-enabling the pattern. A tick-deadline
state machine in the existing synchronous update path is an even simpler
nonblocking alternative for this one-shot delay and avoids a task/generator
allocation. Whichever design the upstream maintainers choose, they should
handle minimize/termination so the pattern is re-enabled when this app gives up
LED control; that lifecycle issue is independent of the 500 ms delay.

### Float sleep replacement candidates

`modules/async_helpers.py:sleep_ms` uses MicroPython's native
`asyncio.sleep_ms(delay_ms)` when available. The MicroPython implementation
returns a `SingletonGenerator` specifically to avoid heap allocation. Its
CPython fallback converts milliseconds to seconds for simulator compatibility,
so device-side float-allocation benefits do not apply to that fallback.

The firmware-side search found 15 decimal-delay `asyncio.sleep()` call sites.
These are candidates, not claims of observed idle churn:

| File | Firmware call sites | Delay | Status / notes |
|---|---:|---:|---|
| [async_helpers.py](../../modules/async_helpers.py) | 1 | 100 ms | Excluded per request. Its `Message.wait` call at line 30 is CPython-only simulator code. |
| [dialog.py](../../modules/app_components/dialog.py) | 2 | 50 ms | Converted to `sleep_ms(50)`. |
| [twentyfour.py](../../modules/frontboards/twentyfour.py) | 1 | 100 ms | Converted to `sleep_ms(100)`; analogous to the converted TwentyTwentySix loop. |
| [hexpansionfw.py](../../modules/firmware_apps/hexpansionfw.py) | 6 | 100 ms | Excluded per request. |
| [patterninhibit.py](../../modules/firmware_apps/patterninhibit.py) | 1 | 500 ms | Deferred: `_make_red` is synchronous despite containing `await`, and both callers are synchronous. Review upstream issues/PRs before deciding how to correct its async behavior. |
| [tick_app.py](../../modules/firmware_apps/tick_app.py) | 1 | 100 ms | Excluded per request. |
| [app.py](../../modules/system/hexpansion/app.py) | 1 | 100 ms | Converted to `sleep_ms(100)`. |
| [app.py](../../modules/system/backleds/app.py) | 2 | 50 ms | Converted to `sleep_ms(50)`. |

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
- **Float-sleep replacements:** pre-commit passed on the four changed modules;
  branch-matched `mpy-cross -march=xtensawin -O2` compiled all four.
- **ESP-NOW listener lookup:** pre-commit passed; branch-matched bytecode
  measured `_has_listeners` at 12 B and `_registry_has_listeners` at 36 B.
  Five focused registry-behavior checks passed. Post-change device probe is
  pending.
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
