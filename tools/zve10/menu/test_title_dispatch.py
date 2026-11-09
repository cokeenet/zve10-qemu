#!/usr/bin/env python3
"""Compile actual title callbacks into a host harness; do not load camera binaries."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent


def function(source, signature):
    start = source.index(signature)
    begin = source.index('{', start)
    depth = 0
    for index in range(begin, len(source)):
        if source[index] == '{':
            depth += 1
        elif source[index] == '}':
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError('function has no closing brace')


def main():
    native = (ROOT / 'native_entry.inc').read_text()
    mini = (ROOT / 'mini_main.c').read_text()
    header = (ROOT / 'native_entry.h').read_text()
    match = re.search(r'^#define\s+NE_TITLE\s+([^\n]+)', header, re.MULTILINE)
    assert match, 'NE_TITLE definition missing'
    harness = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define NE_TITLE TITLE_VALUE
static unsigned ne_title_sets, id_calls, chars_calls;
static uintptr_t expected_title;
static uintptr_t id_impl(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
    assert(a == 11 && b == 99 && c == 22 && d == 33);
    ++id_calls;
    return 123;
}
static uintptr_t chars_impl(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
    assert(a == 11 && c == 22 && d == 33);
    assert(strcmp((const char *)b, "New Menu") == 0);
    ++chars_calls;
    return 456;
}
static uintptr_t (*original_string_id)(uintptr_t, uintptr_t, uintptr_t, uintptr_t) = id_impl;
static uintptr_t (*original_string_chars)(uintptr_t, uintptr_t, uintptr_t, uintptr_t) = chars_impl;
ACTUAL_NATIVE_TITLE
ACTUAL_OBSERVER
int main(void) {
    (void)&native_entry_title;
    expected_title = NE_TITLE;
    assert(observe_string_id(11, 99, 22, 33) == 123);
    assert(id_calls == 1 && chars_calls == 0 && ne_title_sets == 0);
    assert(observe_string_id(11, expected_title, 22, 33) == 456);
    assert(id_calls == 1 && chars_calls == 1 && ne_title_sets == 1);
    return 0;
}
'''
    harness = harness.replace('TITLE_VALUE', match.group(1))
    harness = harness.replace('ACTUAL_NATIVE_TITLE', function(native, 'static uintptr_t native_entry_title('))
    harness = harness.replace('ACTUAL_OBSERVER', function(mini, 'static uintptr_t observe_string_id('))
    with tempfile.TemporaryDirectory(prefix='newmenu-title-test-') as directory:
        source = Path(directory) / 'title_test.c'
        binary = Path(directory) / 'title_test'
        source.write_text(harness)
        subprocess.run([os.environ.get('CC', 'cc'), '-Wall', '-Wextra', '-Werror',
                        str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print('PASS: ordinary IDs preserve dispatch; NE_TITLE uses literal New Menu and increments title count')


if __name__ == '__main__':
    main()
