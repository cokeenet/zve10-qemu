#!/usr/bin/env python3
"""Ensure insertion is restricted to the one verified native lifecycle call."""
from pathlib import Path
import importlib.util
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent


def main():
    spec = importlib.util.spec_from_file_location('extract', ROOT / 'test_title_dispatch.py')
    extract = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(extract)
    actual = extract.function((ROOT / 'native_entry.inc').read_text(),
                              'static int ne_caller_may_insert(')
    source = '''#include <assert.h>\n#include <stdint.h>\nstatic uintptr_t views_base;\n'''
    source += actual
    source += '''
int main(void) {
 assert(!ne_caller_may_insert(0x301921));
 views_base=0x15f88000;
 assert(ne_caller_may_insert(views_base+0x301921));
 assert(!ne_caller_may_insert(views_base+0x301920));
 assert(!ne_caller_may_insert(views_base+0x301923));
 assert(!ne_caller_may_insert(0));
 assert(!ne_caller_may_insert(UINTPTR_MAX));
 return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='newmenu-gate-') as directory:
        c = Path(directory) / 'test.c'
        exe = Path(directory) / 'test'
        c.write_text(source)
        subprocess.run([os.environ.get('CC','cc'),'-Wall','-Wextra','-Werror',
                        str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print('PASS: exact verified Thumb caller accepted; unset base, neighbors and arbitrary pointers rejected without dereference')


if __name__ == '__main__':
    main()
