import os
import sys


class Pin:
    IN = 0
    OUT = 1

    def __init__(self, *args, **kwargs):
        self._value = 0

    def value(self, value=None):
        if value is None:
            return self._value
        self._value = int(bool(value))
        return self._value

    def on(self):
        self._value = 1
        return self._value

    def off(self):
        self._value = 0
        return self._value

    def toggle(self):
        return self.value(not self.value())

    def __call__(self, value=None):
        if value is None:
            return self.value()
        return self.value(value)


class Timer:

    def __init__(self, *args, **kwargs):
        pass

class PWM:

    def __init__(self, *args, **kwargs):
        pass

    def duty_u16(self, *args, **kwargs):
        pass

class ADC:
    ATTN_11DB = None

    def __init__(self, _1, atten):
        pass

    def read_uv(self):
        # A half full battery as seen by the ADC
        return 3.8e6 / 2


class I2C:
    def __init__(self, *args, **kwargs):
        pass

    def scan(self, *args, **kwargs):
        return []

    def writeto(self, *args, **kwargs):
        return 0

    def writeto_mem(self, *args, **kwargs):
        return 0

    def readfrom_mem(self, *args, **kwargs):
        return b''


class SPI:
    pass


class UART:
    def __init__(self, *args, **kwargs):
        pass

    def read(self, *args, **kwargs):
        return b''

    def write(self, *args, **kwargs):
        return 0

    def any(self):
        return 0

    def deinit(self):
        pass


def reset():
    print("beep boop i have reset")
    os.execv(sys.executable, ["python"] + sys.argv)


def disk_mode_flash():
    print("beep boop i'm now in flash disk mode")
    sys.exit(0)


def disk_mode_sd():
    print("beep boop i'm now in sd card disk mode")
    sys.exit(0)

#Added for use by BadgeBot
def disable_irq() -> int: ...

def enable_irq(_state: int) -> None: ...
