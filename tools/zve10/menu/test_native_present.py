#!/usr/bin/env python3
"""Test actual native adapter validation, GPU wait ordering and thread ownership."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
HARNESS = r'''
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "full/ui-present.h"
typedef uintptr_t (*entry_fn)(uintptr_t,uintptr_t,uintptr_t,uintptr_t);
static uintptr_t object_base = 0x10000000;
static uint32_t ne_packed;
#include "native_present_adapter.h"
static unsigned overlays, waits, submits;
static uintptr_t wait_status;
static uint32_t request[14] = {
    0x20000000,0x20100000,0,4,0,0x4401,640,480,640,480,0,0,640,480
};
static uintptr_t original_submit(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d) {
    assert(a == (uintptr_t)request && b == 2 && c == 3 && d == 4);
    ++submits; return 77;
}
static uintptr_t original_wait(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d) {
    (void)a; assert(b == 2 && c == 3 && d == 4);
    ++waits; return wait_status;
}
static void nm_overlay_completed(const struct nm_pending_present *pending) {
    assert(waits > 0); /* callback only after original wait returns */
    assert(pending->physical == request[1]);
    assert(pending->span == UI_PRESENT_BYTES);
    assert(pending->caller == object_base + 0x777935);
    ++overlays;
}
static void capture(void) { nm_capture_present((uintptr_t)request, object_base + 0x777935); }
static void *other_thread(void *unused) {
    (void)unused;
    assert(nm_pending_present.span == 0);
    assert(nm_observe_engine_wait(2,2,3,4) == wait_status);
    return NULL;
}
int main(void) {
    nm_original_present = original_submit;
    nm_original_engine_wait = original_wait;
    assert(nm_observe_present((uintptr_t)request,2,3,4) == 77);
    assert(submits == 1 && overlays == 0); /* native submit always preserved */
    ne_packed = 0x100;
    wait_status = 0;
    capture();
    assert(nm_observe_engine_wait(2,2,3,4) == 0);
    assert(overlays == 0 && nm_pending_present.span != 0);
    wait_status = 0x40000000;
    assert(nm_observe_engine_wait(1,2,3,4) == wait_status);
    assert(overlays == 0 && nm_pending_present.span != 0);
    pthread_t thread;
    assert(pthread_create(&thread,NULL,other_thread,NULL) == 0);
    assert(pthread_join(thread,NULL) == 0);
    assert(overlays == 0 && nm_pending_present.span != 0);
    assert(nm_observe_engine_wait(2,2,3,4) == wait_status);
    assert(overlays == 1 && nm_pending_present.span == 0);
    nm_observe_engine_wait(2,2,3,4);
    assert(overlays == 1); /* completion consumed once */
    capture(); ne_packed = 0;
    nm_observe_engine_wait(2,2,3,4);
    assert(overlays == 1 && nm_pending_present.span == 0);
    ne_packed = 0x100;
    nm_capture_present((uintptr_t)request,object_base + 0x777937);
    nm_observe_engine_wait(2,2,3,4); assert(overlays == 1);
    request[7] = 479; capture();
    nm_observe_engine_wait(2,2,3,4); assert(overlays == 1);
    request[7] = 480; request[1] = request[0]; capture();
    nm_observe_engine_wait(2,2,3,4); assert(overlays == 1);
    nm_capture_present(0,object_base+0x777935);
    assert(nm_pending_present.span == 0);
    return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix='newmenu-present-test-') as directory:
        source = Path(directory) / 'test.c'
        binary = Path(directory) / 'test'
        source.write_text(HARNESS)
        subprocess.run([os.environ.get('CC','cc'), '-std=gnu11', '-Wall', '-Wextra',
                        '-Werror', '-pthread', '-I', str(ROOT), str(source),
                        '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print('PASS: native submit/wait preserved; overlay only after engine-2 completion, on owner thread, once per valid frame')


if __name__ == '__main__':
    main()
