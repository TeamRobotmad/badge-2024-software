"""Pylint plugin catching source patterns that mpy-cross cannot compile.

mpy-cross (MicroPython's bytecode compiler, used to build .mpy artifacts for
badge/HexDrive apps) rejects implicit concatenation of two or more adjacent
f-string literals, e.g.::

    print(f"a={x} " f"b={y}")
    s = (
        f"a={x} "
        f"b={y}"
    )

CPython's own parser accepts this fine (it is valid syntax), so the failure
only surfaces later as a mpy-cross SyntaxError, often pointing at a
misleading line number. This checker flags the pattern at lint time so it is
caught before a device/EEPROM build.
"""

from __future__ import annotations

import re
import tokenize

from pylint.checkers import BaseTokenChecker

_PREFIX_RE = re.compile(r"^[A-Za-z]*")

# Token types that may appear between two string literals without breaking
# "adjacency" for implicit concatenation purposes.
_SKIPPABLE_TOKENS = frozenset({tokenize.NL, tokenize.COMMENT, tokenize.INDENT, tokenize.DEDENT})


def _is_fstring_literal(tok_string: str) -> bool:
    prefix = _PREFIX_RE.match(tok_string).group(0)
    return "f" in prefix.lower()


class MpyCrossCompatChecker(BaseTokenChecker):
    name = "mpy-cross-compat"
    msgs = {
        "E9901": (
            "Adjacent f-string literals are not supported by mpy-cross; merge them into a single f-string",
            "mpy-adjacent-fstrings",
            "Implicit concatenation of two f-string literals compiles fine under CPython "
            "but mpy-cross rejects it with a misleading SyntaxError at an unrelated line. "
            "Merge the literals into one f-string (or use explicit + / str.join) before "
            "the change reaches a device/EEPROM build.",
        ),
    }

    def process_tokens(self, tokens) -> None:
        prev_was_fstring = False
        for tok_type, tok_string, start, _end, _line in tokens:
            if tok_type == tokenize.STRING:
                is_fstring = _is_fstring_literal(tok_string)
                if is_fstring and prev_was_fstring:
                    self.add_message("mpy-adjacent-fstrings", line=start[0])
                prev_was_fstring = is_fstring
            elif tok_type not in _SKIPPABLE_TOKENS:
                prev_was_fstring = False


def register(linter) -> None:
    linter.register_checker(MpyCrossCompatChecker(linter))
