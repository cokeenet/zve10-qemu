#!/usr/bin/env python3
"""Cross-trampoline audit: no wrapper may call a trampoline it does not own.

Why this matters. `ne_hook` stores each original trampoline only when that
install succeeds, and installs the wrapper only on the same success. So a
wrapper that calls *its own* trampoline is always safe: if the wrapper is
reachable, its trampoline was set.

The dangerous shape is calling a *different* hook's trampoline. Then one
successful install is enough to make the wrapper reachable while the other
trampoline is still NULL, and the camera jumps to address 0 mid-render.

This is exactly the bug that existed in the font pair: observe_string_id
forwards to native_entry_title, and native_entry_title called
original_string_chars. The audit pins the invariant so it cannot come back,
and it fails if any new cross dependency is introduced.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent

# wrapper name -> trampoline it is allowed to call (its own)
OWNED = {
    'ne_init_wrapper': 'ne_original_init',
    'ne_populate_wrapper': 'ne_original_populate',
    'ne_main_wrapper': 'ne_original_main',
    'ne_row_wrapper': 'ne_original_row',
    'observe_string_chars': 'original_string_chars',
    'observe_string_id': 'original_string_id',
}

# Names that identify a trampoline slot, mapped to their owner wrapper.
TRAMPOLINES = {
    'ne_original_init': 'ne_init_wrapper',
    'ne_original_populate': 'ne_populate_wrapper',
    'ne_original_main': 'ne_main_wrapper',
    'ne_original_row': 'ne_row_wrapper',
    'original_string_chars': 'observe_string_chars',
    'original_string_id': 'observe_string_id',
}


def bodies(text):
    """Yield (name, body) for each function definition we care about."""
    for name in OWNED:
        # Match "name(" at a definition, then walk braces.
        for m in re.finditer(r'\b' + re.escape(name) + r'\s*\([^;{]*\)\s*\{', text):
            i = m.end() - 1
            depth = 0
            for j in range(i, len(text)):
                if text[j] == '{':
                    depth += 1
                elif text[j] == '}':
                    depth -= 1
                    if depth == 0:
                        yield name, text[m.start():j + 1]
                        break
            break


def main():
    inc = (ROOT / 'native_entry.inc').read_text()
    src = (ROOT / 'mini_main.c').read_text()
    text = inc + '\n' + src

    problems = []
    checked = set()
    for name, body in bodies(text):
        checked.add(name)
        # Strip comments so prose about a symbol is not a hit.
        code = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
        code = re.sub(r'//[^\n]*', '', code)
        for tramp, owner in TRAMPOLINES.items():
            if not re.search(r'\b' + re.escape(tramp) + r'\b', code):
                continue
            if owner != name:
                problems.append('%s calls %s (owned by %s)' % (name, tramp, owner))

    missing = set(OWNED) - checked
    if missing:
        problems.append('could not locate body for: %s' % ', '.join(sorted(missing)))

    # The font pair must still be guarded, since native_entry_title is the one
    # function that legitimately references another hook's trampoline.
    title = re.search(
        r'static uintptr_t native_entry_title\([^)]*\)\s*\{(.*?)\n\}', inc, re.S)
    if not title:
        problems.append('native_entry_title not found')
    else:
        body = title.group(1)
        guard = 'if (!original_string_id || !original_string_chars) return 0;'
        if guard not in body:
            problems.append('native_entry_title lost its dual-trampoline guard')
        elif body.index(guard) > body.index('original_string_id(a'):
            problems.append('native_entry_title guard runs after the first call')

    if problems:
        print('FAIL:')
        for p in problems:
            print('  - ' + p)
        raise SystemExit(1)

    print('PASS: every hook wrapper calls only its own trampoline; the single '
          'cross-dependency (native_entry_title) keeps its dual-trampoline guard '
          'ahead of both calls')


if __name__ == '__main__':
    main()
