from _sim import _sim
import leds


def _get_into(string, index, result):
    read_into = getattr(type(string), "get_into", None)
    if read_into is not None:
        return read_into(string, index, result)
    colour = string[index]
    channel = 0
    while channel < len(result):
        result[channel] = colour[channel]
        channel += 1
    return result


class NeoPixel:

    def __init__(self, pin, n, bpp=3, timing=1):
        self.pin = pin
        self.n = n
        self.bpp = bpp
        self.timing = timing

    def write(self):
        _sim.leds_update()

    def fill(self, color):
        leds.set_all_rgb(*color)

    def __setitem__(self, item, value):
        leds.set_rgb(item, *value)

    def set_many(self, start, values, count, values_start=0):
        pixel = 0
        while pixel < count:
            self[start + pixel] = values[values_start + pixel]
            pixel += 1

    def __getitem__(self, item):
        return leds.get_rgb(item)

    def get_into(self, item, result):
        colour = leds.get_rgb(item)
        channel = 0
        while channel < len(result):
            result[channel] = colour[channel]
            channel += 1
        return result

class MergedNeoPixel:
    def __init__(self, string, indices):
        self.string = string
        self.indices = indices
        self.n = len(indices)

    def __setitem__(self, i, v):
        leds = self.indices[i]
        for led in leds:
            self.string[led] = v

    def set_many(self, start, values, count, values_start=0):
        pixel = 0
        while pixel < count:
            self[start + pixel] = values[values_start + pixel]
            pixel += 1

    def __getitem__(self, i):
        leds = self.indices[i]
        for led in leds:
            return self.string[led]
        raise KeyError("No such LED in this string")

    def get_into(self, i, result):
        leds = self.indices[i]
        if len(leds) == 0:
            raise KeyError("No such LED in this string")
        return _get_into(self.string, leds[0], result)

    def fill(self, v):
        self.string.fill(v)

    def write(self):
        self.string.write()


class ComposedNeoPixel:
    def __init__(self, string, offset=0):
        self.strings = []
        self.offsets = []
        self.lengths = []
        self.add_string(string, offset)

    def add_string(self, string, offset):
        self.strings.append(string)
        self.offsets.append(offset)
        self.lengths.append(string.n)

    def __setitem__(self, i, v):
        bad_indexes = []
        for string_idx, (string, offset, length) in enumerate(
            zip(self.strings, self.offsets, self.lengths)
        ):
            index = i - offset
            if index >= 0 and index < length:
                try:
                    string[index] = v
                except:
                    bad_indexes.append(string_idx)
        for bad_idx in sorted(bad_indexes, reverse=True):
            del self.strings[bad_idx]
            del self.offsets[bad_idx]
            del self.lengths[bad_idx]

    def set_many(self, start, values, count, values_start=0):
        pixel = 0
        while pixel < count:
            self[start + pixel] = values[values_start + pixel]
            pixel += 1

    def __getitem__(self, i):
        for string, offset, length in zip(self.strings, self.offsets, self.lengths):
            index = i - offset
            if index >= 0 and index < length:
                return string[index]
        raise KeyError("No such LED in this string")

    def get_into(self, i, result):
        for string, offset, length in zip(self.strings, self.offsets, self.lengths):
            index = i - offset
            if index >= 0 and index < length:
                return _get_into(string, index, result)
        raise KeyError("No such LED in this string")

    def fill(self, v):
        for string in self.strings:
            string.fill(v)

    def write(self):
        for string in self.strings:
            string.write()

    @property
    def n(self):
        return max(offset + length for (offset, length) in zip(self.offsets, self.lengths))


class CorrectedNeoPixel:
    def __init__(self, string, alterations):
        self.string = string
        self.alterations = alterations
        self.n = string.n

    def __setitem__(self, i, v):
        alteration = self.alterations[i]
        if alteration is not None:
            v = alteration(v)
        self.string[i] = v

    def set_many(self, start, values, count, values_start=0):
        pixel = 0
        while pixel < count:
            self[start + pixel] = values[values_start + pixel]
            pixel += 1

    def __getitem__(self, i):
        return self.string[i]

    def get_into(self, i, result):
        return _get_into(self.string, i, result)

    def fill(self, v):
        for i in range(self.n):
            self[i] = v

    def write(self):
        self.string.write()


class DimCorrection:
    def __init__(self, amount):
        self._amount = None
        self._amount_percent = None
        self.amount = amount

    @property
    def amount(self):
        return self._amount

    @amount.setter
    def amount(self, amount):
        if amount == self._amount:
            return
        self._amount = amount
        amount_percent = int(amount * 100 + 0.5)
        if amount_percent < 0:
            amount_percent = 0
        elif amount_percent > 100:
            amount_percent = 100
        self._amount_percent = amount_percent

    def __call__(self, v):
        new_val = []
        amount_percent = self._amount_percent
        for channel in v:
            channel = channel * amount_percent // 100
            if channel > 255:
                channel = 255
            if channel < 0:
                channel = 0
            new_val.append(channel)
        return new_val

    def apply_into(self, v, result):
        amount_percent = self._amount_percent
        index = 0
        while index < len(v):
            channel = v[index] * amount_percent // 100
            if channel > 255:
                channel = 255
            if channel < 0:
                channel = 0
            result[index] = channel
            index += 1


class ColourCorrection:
    def __init__(self, amounts):
        self.amounts = amounts

    def __call__(self, v):
        new_val = []
        for channel, amount in zip(v, self.amounts):
            channel *= amount
            channel = int(channel)
            if channel > 255:
                channel = 255
            if channel < 0:
                channel = 0
            new_val.append(channel)
        return new_val

    def apply_into(self, v, result):
        index = 0
        while index < len(v) and index < len(self.amounts):
            channel = int(v[index] * self.amounts[index])
            if channel > 255:
                channel = 255
            if channel < 0:
                channel = 0
            result[index] = channel
            index += 1


class CallbackCorrection:
    def __init__(self, callback, **kwargs):
        self.callback = callback
        self.kwargs = kwargs

    def __call__(self, v):
        return self.callback(v, **self.kwargs)
