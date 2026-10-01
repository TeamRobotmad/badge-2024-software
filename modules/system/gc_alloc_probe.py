"""Sample net MicroPython heap growth across selected runtime callbacks."""

import gc

APP_UPDATE = 0
APP_BACKGROUND = 1
BADGEBOT_BACKGROUND = 2
APP_DRAW = 3
EVENT_SYNC = 4
EVENT_ASYNC = 5
BADGEBOT_STATE_BACKGROUND = 6
BADGEBOT_MAIN_UPDATE = 7

_NAMES = (
    "app.update",
    "app.background",
    "BadgeBot.background",
    "app.draw",
    "event.sync",
    "event.async_dispatch",
    "BadgeBot.state_background",
    "BadgeBot.main_update",
)
_SAMPLE_EVERY = (64, 32, 8, 16, 1, 1, 8, 16)
_mem_alloc = getattr(gc, "mem_alloc", None)
_collection_count = getattr(gc, "collection_count", None)
_enabled = False
_remaining = list(_SAMPLE_EVERY)
_collections_before = [0] * len(_NAMES)
_calls = [0] * len(_NAMES)
_samples = [0] * len(_NAMES)
_bytes = [0] * len(_NAMES)
_max_bytes = [0] * len(_NAMES)
_gc_overlap = [0] * len(_NAMES)


def enable() -> None:
    global _enabled
    if _mem_alloc is None:
        return
    _enabled = True
    index = 0
    while index < len(_NAMES):
        _remaining[index] = _SAMPLE_EVERY[index]
        _collections_before[index] = 0
        _calls[index] = 0
        _samples[index] = 0
        _bytes[index] = 0
        _max_bytes[index] = 0
        _gc_overlap[index] = 0
        index += 1


def begin(site: int):
    if not _enabled:
        return None
    _calls[site] += 1
    remaining = _remaining[site] - 1
    if remaining > 0:
        _remaining[site] = remaining
        return None
    _remaining[site] = _SAMPLE_EVERY[site]
    if _collection_count is not None:
        _collections_before[site] = _collection_count()
    return _mem_alloc()


def end(site: int, before) -> None:
    if before is None:
        return
    collections_after = _collection_count() if _collection_count is not None else 0
    record(site, before, _mem_alloc(), _collections_before[site], collections_after)


def record(
    site: int,
    before: int,
    after: int,
    collections_before: int = 0,
    collections_after: int = 0,
) -> None:
    if collections_after != collections_before:
        _gc_overlap[site] += 1
        return
    if after < before:
        _gc_overlap[site] += 1
        return
    allocated = after - before
    _samples[site] += 1
    _bytes[site] += allocated
    if allocated > _max_bytes[site]:
        _max_bytes[site] = allocated


def report_and_reset() -> None:
    if not _enabled:
        return
    index = 0
    while index < len(_NAMES):
        samples = _samples[index]
        if _calls[index] or _gc_overlap[index]:
            average = _bytes[index] // samples if samples else 0
            estimated = average * _calls[index]
            print(
                "B:GC churn %s calls=%d samples=%d avg=%dB est=%dB max=%dB gc_overlap=%d"
                % (
                    _NAMES[index],
                    _calls[index],
                    samples,
                    average,
                    estimated,
                    _max_bytes[index],
                    _gc_overlap[index],
                )
            )
        _calls[index] = 0
        _samples[index] = 0
        _bytes[index] = 0
        _max_bytes[index] = 0
        _gc_overlap[index] = 0
        index += 1
