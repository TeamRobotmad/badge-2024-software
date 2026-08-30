"""Test suite bootstrap.

These tests run under CPython, but the modules under test are written for
MicroPython. Importing this package wires up the things needed for that:

  * puts ``modules/`` and ``sim/fakes/`` on ``sys.path`` so the flat imports
    used by the firmware (``from async_queue import Queue`` etc.) resolve, and
    native MicroPython C modules (e.g. ``ota``) are replaced by their CPython
    fakes.
  * shims the MicroPython-only ``time.ticks_us`` / ``time.ticks_diff`` used by
    ``perf_timer`` and ``sys.print_exception`` used by the eventbus error
    handling so they import and run under CPython.
  * stubs the native ``display`` and ``tildagonos`` modules (the latter pulls
    in the full graphical simulator which is not available in CI)
  * imports ``system.scheduler`` before anything imports.
    On the badge boot happens to import scheduler first, here we do it explicitly
    to prevent import issues.

Run from the project root with:  python -m unittest discover -t . -s tests
"""

import os
import sys
import time
import types

_MODULES = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "modules"
)
_SIM_FAKES = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "sim", "fakes"
)

if _MODULES not in sys.path:
    sys.path.insert(0, _MODULES)

if _SIM_FAKES not in sys.path:
    sys.path.insert(0, _SIM_FAKES)

if not hasattr(time, "ticks_ms"):
    time.ticks_ms = lambda: int(time.monotonic() * 1_000)
if not hasattr(time, "ticks_us"):
    time.ticks_us = lambda: int(time.monotonic() * 1_000_000)
if not hasattr(time, "ticks_diff"):
    time.ticks_diff = lambda a, b: a - b
if not hasattr(time, "ticks_add"):
    time.ticks_add = lambda ticks, delta: ticks + delta
if not hasattr(time, "sleep_ms"):
    time.sleep_ms = lambda ms: time.sleep(ms / 1_000)
if not hasattr(time, "sleep_us"):
    time.sleep_us = lambda us: time.sleep(us / 1_000_000)

if not hasattr(sys, "print_exception"):
    import traceback

    def _print_exception(exc, file=sys.stderr):
        traceback.print_exception(type(exc), exc, exc.__traceback__, file=file)

    sys.print_exception = _print_exception

sys.modules.setdefault("display", types.ModuleType("display"))

# tildagonos is a hardware module that pulls in the full graphical simulator
# (neopixel -> _sim -> ctx -> wasmtime / pygame) which is not available in CI.
# Pre-register a lightweight stub so the import chain succeeds without those
# heavy dependencies.
_tildagonos_stub = types.ModuleType("tildagonos")
_tildagonos_stub.tildagonos = None
_tildagonos_stub.led_colours = []
sys.modules.setdefault("tildagonos", _tildagonos_stub)

# _sim is the simulator core; it imports ctx/pygame/wasmtime and calls
# pygame.init() at module level - none of which are present in CI.
# Stub it so sim/fakes that import "_sim" succeed without those dependencies.
_sim_inner = types.ModuleType("_sim")
_sim_inner._sim = None
sys.modules.setdefault("_sim", _sim_inner)

import system.scheduler  # noqa: E402,F401
