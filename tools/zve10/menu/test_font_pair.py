#!/usr/bin/env python3
"""Ensure the two font hooks are installed as a pair, never half.

The bug this pins down: observe_string_id forwards to native_entry_title, and
native_entry_title calls original_string_chars when it sees NE_TITLE. If the
string_id hook were installed while the string_chars hook failed, that pointer
would still be NULL and the camera's UI would jump to address 0 while drawing
the row we added.

Two invariants are checked:
  1. Both anchors are validated read-only before either hook is installed, and
     the install path returns early unless both match.
  2. native_entry_title refuses to run unless both trampolines are non-NULL.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC = ROOT / 'mini_main.c'
INC = ROOT / 'native_entry.inc'


def block(text, start_marker, end_marker):
    i = text.index(start_marker)
    j = text.index(end_marker, i)
    return text[i:j + len(end_marker)]


def main():
    src = SRC.read_text()
    inc = INC.read_text()

    hooks = block(src, '{', '\n    }\n')  # first block after the font comment
    # Locate the font block by its distinctive anchors instead of position.
    font = block(src, 'static const unsigned char pin_id[12] =',
                 'nm_log("NM hooked font titles\\n");\n        }\n    }')
    assert 'if (memcmp((void *)at_id, pin_id, 12) ||' in font, 'combined anchor check missing'
    assert 'memcmp((void *)at_chars, pin_chars, 12)' in font, 'chars anchor not checked'
    assert font.index('memcmp((void *)at_id') < font.index('ne_hook(at_chars'), \
        'anchors must be validated before any write'
    assert font.index('ne_hook(at_chars') < font.index('ne_hook(at_id'), \
        'string_chars must be installed before string_id'

    # The early-return must mention both, and must precede both hook calls.
    guard = font[font.index('if (memcmp'):font.index('} else if')]
    assert 'memcmp' in guard and 'nm_log("NM skip' in guard, 'no early return on anchor mismatch'

    title = block(inc, 'static uintptr_t native_entry_title(', '\n}')
    assert 'if (!original_string_id || !original_string_chars) return 0;' in title, \
        'title hook lacks the trampoline NULL guard'
    guard_at = title.index('if (!original_string_id || !original_string_chars)')
    assert guard_at < title.index('original_string_id(a, b, c, d)'), \
        'NULL guard must run before the first trampoline call'
    assert guard_at < title.index('original_string_chars(a, (uintptr_t)"New Menu"'), \
        'NULL guard must run before the title rewrite'

    print('PASS: font hooks are installed as a pair; both anchors validated '
          'read-only first, string_chars before string_id, and the title hook '
          'returns harmlessly when either trampoline is missing')


if __name__ == '__main__':
    main()
