#!/usr/bin/env python3
"""Exercise the actual constructor discovery callback without loading Sony code."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent


def main():
    spec = importlib.util.spec_from_file_location('extract', ROOT / 'test_title_dispatch.py')
    extract = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(extract)
    callback = extract.function((ROOT / 'mini_main.c').read_text(), 'static int nm_phdr(')
    harness = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <link.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
static int nm_installed;
static uintptr_t object_base, views_base;
static void *ne_view_handle;
static unsigned opens;
static void *fake_open(const char *path,int flags) {
    assert(strcmp(path,"/usr/lib/viewUnified2.so") == 0);
    assert(flags == (RTLD_NOLOAD|RTLD_LAZY));
    ++opens; return (void *)0x1234;
}
static void *fake_sym(void *handle,const char *name) {
    assert(handle == (void *)0x1234);
    assert(strcmp(name,"ViewRootSelectorToInstance") == 0);
    return (void *)0x5001;
}
static int fake_addr(const void *symbol,Dl_info *info) {
    assert(symbol == (void *)0x5001); info->dli_fbase=(void *)0x4000;return 1;
}
#define dlopen fake_open
#define dlsym fake_sym
#define dladdr fake_addr
ACTUAL_CALLBACK
int main(void) {
    struct dl_phdr_info info={0};
    info.dlpi_name="/usr/lib/libObj.so";info.dlpi_addr=0x2000;
    assert(nm_phdr(&info,sizeof info,NULL) == 0);
    assert(object_base == 0x2000 && opens == 0);
    info.dlpi_name="/usr/lib/viewUnified2.so";
    assert(nm_phdr(&info,sizeof info,NULL) == 0);
    assert(views_base == 0x4000 && ne_view_handle == (void *)0x1234 && opens == 1);
    nm_installed=1;
    assert(nm_phdr(&info,sizeof info,NULL) == 0 && opens == 1);
    return 0;
}
'''.replace('ACTUAL_CALLBACK', callback)
    with tempfile.TemporaryDirectory(prefix='newmenu-phdr-test-') as directory:
        source = Path(directory) / 'test.c'
        executable = Path(directory) / 'test'
        source.write_text(harness)
        subprocess.run([os.environ.get('CC','cc'),'-Wall','-Wextra','-Werror',
                        str(source),'-o',str(executable)],check=True)
        subprocess.run([str(executable)],check=True)
    print('PASS: constructor discovery retains both view handle and base without interposer side effects')


if __name__ == '__main__':
    main()
