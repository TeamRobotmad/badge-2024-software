import asyncio
import settings

from app import App
from events.emote import EmotePositiveEvent, EmoteNegativeEvent
from system.eventbus import eventbus
from system.hexpansion.events import HexpansionInsertionEvent, HexpansionRemovalEvent
from system.patterndisplay.events import PatternDisable, PatternEnable
from tildagonos import tildagonos, led_colours

# note that event.port is indexed from 1-6,
# hexpansion style, but this list is indexed from 0-5,
# Python style.
active_back_leds = [False] * 6
_BACK_LED_OFF = (0, 0, 0)


class BackLEDManager(App):
    def __init__(self):
        # routines that want to drive the back leds themselves for
        # a notification should take this lock.
        self.lock = asyncio.Lock()
        self.enabled = True
        self._leds = tildagonos.leds
        self._read_led_into = getattr(self._leds, "get_into", None)
        self._mirror_buffer = [0, 0, 0]
        self._last_back_led_rgb = [[-1, -1, -1] for _ in range(6)]
        self._back_leds_valid = False
        tildagonos.set_led_power(True)
        eventbus.on_async(HexpansionInsertionEvent, self.handle_insertion, self)
        eventbus.on_async(HexpansionRemovalEvent, self.handle_removal, self)

        eventbus.on_async(EmotePositiveEvent, self.handle_positive, self)
        eventbus.on_async(EmoteNegativeEvent, self.handle_negative, self)

        eventbus.on_async(PatternDisable, self.handle_disable, self)
        eventbus.on_async(PatternEnable, self.handle_enable, self)

    async def handle_enable(self, event):
        self.enabled = True
        self._back_leds_valid = False

    async def handle_disable(self, event):
        self.enabled = False
        self._back_leds_valid = False

    async def handle_positive(self, event):
        if not self.enabled:
            return

        if not settings.get("backleds_emotes", True):
            return

        await self.lock.acquire()
        try:
            for brightness in [1, 16, 64, 255, 64, 16, 1, 0]:
                for lednum in range(13, 19):
                    tildagonos.leds[lednum] = (0, brightness, 0)
                tildagonos.leds.write()
                await asyncio.sleep(0.05)
        finally:
            self._back_leds_valid = False
            self.lock.release()

    async def handle_negative(self, event):
        if not self.enabled:
            return

        if not settings.get("backleds_emotes", True):
            return

        await self.lock.acquire()
        try:
            for brightness in [1, 16, 64, 255, 64, 16, 1, 0]:
                for lednum in range(13, 19):
                    tildagonos.leds[lednum] = (brightness, 0, 0)
                tildagonos.leds.write()
                await asyncio.sleep(0.05)
        finally:
            self._back_leds_valid = False
            self.lock.release()

    async def handle_insertion(self, event):
        active_back_leds[event.port - 1] = True

    async def handle_removal(self, event):
        active_back_leds[event.port - 1] = False

    def background_update(self, delta):
        if not self.enabled:
            return

        if self.lock.locked():  # e.g. if emotes are being displayed
            return

        leds = self._leds
        read_led_into = self._read_led_into
        mirror_pattern = settings.get("pattern_mirror_hexpansions", False)
        changed = False
        i = 0
        while i < 6:
            if active_back_leds[i]:
                if mirror_pattern:
                    if read_led_into is None:
                        colour = leds[1 + (i * 2)]
                    else:
                        read_led_into(1 + (i * 2), self._mirror_buffer)
                        colour = self._mirror_buffer
                else:
                    colour = led_colours[i]
            else:
                colour = _BACK_LED_OFF

            red = colour[0]
            green = colour[1]
            blue = colour[2]
            previous = self._last_back_led_rgb[i]
            if (
                not self._back_leds_valid
                or red != previous[0]
                or green != previous[1]
                or blue != previous[2]
            ):
                leds[13 + i] = colour
                previous[0] = red
                previous[1] = green
                previous[2] = blue
                changed = True
            i += 1

        self._back_leds_valid = True
        if changed:
            leds.write()
