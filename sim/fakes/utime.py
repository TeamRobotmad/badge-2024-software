import time


def sleep(i: int):
    time.sleep(i)

def ticks_ms():
    return int(time.monotonic() * 1000)
