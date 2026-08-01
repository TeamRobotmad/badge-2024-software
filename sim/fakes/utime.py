import time


def sleep(i: int):
    time.sleep(i)


def ticks_ms() -> int:
    return int(time.monotonic() * 1_000)


def ticks_us() -> int:
    return int(time.monotonic() * 1_000_000)


def ticks_diff(a: int, b: int) -> int:
    return a - b


def ticks_add(ticks: int, delta: int) -> int:
    return ticks + delta
