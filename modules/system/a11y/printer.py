class PrintA11y:
    def __init__(self):
        self.alts = []
        self._cached_alt = None
        self.collected = []
        self.last_strings = []
        self._last_strings_are_alts = False
        self.inhibit = False
        self.process = None

    def add_alt(self, app, text, always=False, transient=False):
        if not self.inhibit:
            cached_alt = self._cached_alt
            if (
                cached_alt is None
                or cached_alt[0] != text
                or cached_alt[1] != always
                or cached_alt[2] != transient
            ):
                cached_alt = (text, always, transient)
                self._cached_alt = cached_alt
            self.alts.append(cached_alt)

    def collect_text(self, text):
        if not self.inhibit:
            self.collected.append(text)

    def reset(self):
        self.collected.clear()
        self.alts.clear()

    def get_all_strings(self):
        if self.alts:
            return [s for (s, a, t) in self.alts]
        return self.collected

    def get_deduped_strings(self):
        use_alts = bool(self.alts)
        strings = self.alts if use_alts else self.collected
        if self._last_strings_are_alts == use_alts and self.last_strings == strings:
            return

        has_transients = False
        if use_alts:
            index = 0
            while index < len(strings):
                if strings[index][2]:
                    has_transients = True
                    break
                index += 1
        output_strings = []
        index = 0
        while index < len(strings):
            if use_alts:
                string, always, transient = strings[index]
            else:
                string = strings[index]
                always = False
                transient = False
            if transient:
                output_strings.append(string)
            elif always:
                output_strings.append(string)
            elif not has_transients and len(self.last_strings) > index:
                if self._last_strings_are_alts:
                    previous_string = self.last_strings[index][0]
                else:
                    previous_string = self.last_strings[index]
                if previous_string != string:
                    output_strings.append(string)
            index += 1

        self.last_strings = strings[:]
        self._last_strings_are_alts = use_alts
        return output_strings

    async def finalise_frame(self):
        text = self.get_deduped_strings()
        if text:
            print("[Screen reader] " + " ".join(text))


_printa11y = PrintA11y()
