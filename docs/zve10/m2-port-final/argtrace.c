#define _GNU_SOURCE
#include <stdio.h>

/* ★ Track messages we fabricated so osal_free_msg can release them correctly. */
#include <stdlib.h>
#define FAKE_MSG_SLOTS 64
static struct { void *payload; void *raw; int live; } g_fake_msgs[FAKE_MSG_SLOTS];
static void fake_msg_track(void *raw, void *payload);
static void *fake_msg_take(void *payload);
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <ucontext.h>
#include <stdint.h>
#include <dlfcn.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>

static unsigned long (*real_draw)(unsigned long,unsigned long,unsigned long,unsigned long);
static unsigned long (*real_update)(unsigned long,unsigned long,unsigned long,unsigned long);
static unsigned long (*real_start)(unsigned long,unsigned long,unsigned long,unsigned long);
static unsigned long (*real_uxinit)(unsigned long,unsigned long,unsigned long,unsigned long);
static unsigned long (*real_appfwinit)(unsigned long,unsigned long,unsigned long,unsigned long);
static unsigned long (*real_uxres)(unsigned long,unsigned long,unsigned long,unsigned long);
static unsigned long (*real_uxsus)(unsigned long,unsigned long,unsigned long,unsigned long);
static unsigned long g_life_n;
static volatile int cnt_mutex=0, cnt_cond=0, cnt_osalwai=0, cnt_draw=0, cnt_update=0;
static unsigned long (*real_app)(unsigned long,unsigned long,unsigned long,unsigned long);
/* Compile-time switches: environment variables do NOT survive bootin.elf's
 * execve of im.elf, so anything that must work inside im.elf has to be a
 * build-time constant rather than getenv(). */
#define ARG_LOG_DROP   1
/* env vars do not survive bootin.elf's execve of im.elf (ledger 120), so
 * anything that must work inside im.elf is a build-time constant. */
#define ARG_FORCE_MENU 0
#define ARG_FORCE_SITE3 0
#define ARG_AP_START_WATCH 0
static volatile int g_in_start;
static unsigned long g_sp_at_as_entry;
static unsigned long g_sp_min;
static unsigned long g_sp_samples;
static volatile int g_as_phase;
static volatile int g_menu_requested, g_menu_done, g_menu_ret;
static volatile int g_s3_after;
static volatile int g_as_pid, g_as_tid;
static void as_dump_sites(void);
static void ge_probe(void);
static void wl_dump(const char *tag);
static void grm_hook_install(void);
static void lz_probe_install(void);
static void dfb_hook_install(void);
static void re_hook_install(void);
static void fi_hook_install(void);
static unsigned frameiter_count(void);
static void stage_hooks_install(void);
static void stage_report(void);
static void psm_hook_install(void);
static void psm_report(void);
static void ws_hook_install(void);
static void base_print(void);
static void plt_resolve(const char *, unsigned, const char *);
static void *menu_force_thread(void *);
static void *site3_force_thread(void *);
static void note_caller(const char *, unsigned);
static const char *module_of(unsigned, unsigned *);
static void site3_report(void);
static void sp_note(const char *);
static void uinit_backtrace(const char *);
static void waitloop_note(unsigned);
static void mile_report(void);
static unsigned long as_site_probe(unsigned long,unsigned long,unsigned long,unsigned long);
static void *start_watchdog(void *);
static void *inject_thread(void *arg);
static void inject_touch(unsigned char x, unsigned char y, unsigned int t);
static void inject_input_event(unsigned qid, unsigned frame, unsigned code, unsigned sub, unsigned param);
static void inject_key_event(unsigned qid, unsigned key_code, unsigned sub, unsigned status, unsigned ex_info, unsigned from_type);
static unsigned long g_appfw_base;
static void *g_real_start_off;
static void *g_frame_ptr;


void report_got_slots(void);
static int (*g_real_process_event)(void *);
static unsigned long (*g_real_psm)(void *);
static void dump_thread_state(const char *);
static void probe_target_syscall(const char *);
static void probe_thread_pcs(const char *);
static void maps_probe(const char *);
static void *lib_text_base(const char *);
static void force_draw_once(void);
static void *force_draw_thread(void *);
static int g_in_rcv_after_start(void);
static void as_run_stages(unsigned long,unsigned long,unsigned long,unsigned long);
static int as_reported;

static void logline(const char *fmt, ...)
{
    char b[280]; va_list ap; va_start(ap,fmt);
    int n=vsnprintf(b,sizeof b,fmt,ap); va_end(ap);
    if(n>0) write(2,b,n);
}

/* ★★★★★ Route the frame draw through a counter.
 *
 * The relocation slot for the frame draw is known exactly (0x377afe0, verified
 * against the relocation table). Patching it means every attempt to draw calls
 * our wrapper first, which settles one question directly: is drawing ever
 * attempted at all? The wrapper records the call and forwards immediately, so
 * behaviour is unchanged if it does fire. DRAWFB_PATCH */
static unsigned long (*g_real_drawfb_got)(void);
static unsigned long g_drawfb_got_n;

static unsigned long drawfb_wrapper(void)
{
    /* ★ Pure forwarding, no logging.
     *
     * Every detail of the replacement is now correct yet the application
     * still stops before creating a single layer (ledger 350), so the question
     * is whether the replacement itself is tolerable or whether our own work
     * inside the wrapper is what breaks it. Logging needs stdio locks, and the
     * draw path may run before those are usable. Removing the logging for one
     * run separates those two possibilities with a single observation. */
    __atomic_fetch_add(&g_drawfb_got_n, 1, __ATOMIC_RELAXED);
    if (g_real_drawfb_got) return g_real_drawfb_got();
    return 0;
}

/* ★ Patch the two import slots the reference project patches.
 *
 * The reference makes the slot page writable and replaces both the notify and
 * the draw entries, because the application only reaches the draw through the
 * pair (ledger 346). Its two entries live on different pages here, so each page
 * is opened separately and restored afterwards. The replacement target is the
 * real symbol resolved by dlsym, which also gives the base directly instead of
 * deriving it from a page scan. FRAME_PROBES */
static void *(*g_real_notify_got)(void *);
static unsigned long g_notify_got_n;

static void *notify_wrapper(void *self)
{
    __atomic_fetch_add(&g_notify_got_n, 1, __ATOMIC_RELAXED);
    if (g_real_notify_got) return g_real_notify_got(self);
    return 0;
}

void patch_drawfb_slot(void)
{
    static int done;
    if (done) return;
    /* ★ Resolve the real functions from the interface library's base, not from
     * dlsym.
     *
     * This process hooks the draw symbol, so resolving it returns the hook
     * rather than the original, and forwarding into it recursed until the
     * application stopped short (ledger 337). The base is derived from a symbol
     * captured at load time, and both targets then come from fixed offsets, so
     * nothing here can resolve to our own code. */
    unsigned long libobj = 0;
    unsigned long psm = (unsigned long)__atomic_load_n(&g_real_psm, __ATOMIC_RELAXED);
    if (psm) libobj = (psm & ~1UL) - 0xe5610UL;
    void *entry = libobj ? (void *)(libobj + 0xe565aUL) : 0;
    void *nentry_direct = libobj ? (void *)(libobj + 0x106524UL) : 0;
    /* ★ Derive the base from the entry point that is already known.
     *
     * The path based lookup failed because the executable mapping of this
     * library is not listed the way the other libraries are (ledger 347). The
     * application's own entry was captured at the moment it was hooked, and it
     * sits at a fixed offset, so subtracting that offset gives the base without
     * consulting the maps at all. */
    void *base = 0;
    void *start = __atomic_load_n(&g_real_start_off, __ATOMIC_RELAXED);
    if (start) base = (void *)(((uintptr_t)start & ~(uintptr_t)1) - 0x54d580u);
    if (!entry || !base) {
        logline("DRAWFB_PATCH skip entry=%p base=%p start=%p libobj=%#lx psm=%#lx\n",
                entry, base, start, libobj, psm);
        return;
    }
    done = 1;
    unsigned long slot_addr = (unsigned long)(uintptr_t)base + 0x377afe0u;
    unsigned long *slot = (unsigned long *)slot_addr;
    unsigned long before = *slot;
    /* ★ Refuse to write unless the slot holds the trampoline, which is the
     * ordinary unbound state. Anything else means the loader or a previous
     * owner has a different view of this entry, and overwriting it would be
     * guessing rather than patching. */
    if ((before - (unsigned long)(uintptr_t)base) != 0x5289d0UL) {
        unsigned long delta = before - (unsigned long)(uintptr_t)base;
        logline("DRAWFB_PATCH skip slot=%#lx before=%#lx delta=%#lx (not the trampoline)\n",
                slot_addr, before, delta);
        return;
    }
    logline("DRAWFB_PATCH slot=%#lx before=%#lx expect=%#lx\n",
            slot_addr, before, (unsigned long)entry);
    /* ARM function: keep the address as-is (no thumb bit). */
    __atomic_store_n(&g_real_drawfb_got, (unsigned long (*)(void))entry, __ATOMIC_RELAXED);
    unsigned long page = slot_addr & ~0xffful;
    if (mprotect((void *)page, 0x1000, PROT_READ|PROT_WRITE) != 0) {
        logline("DRAWFB_PATCH mprotect errno=%d\n", errno);
        return;
    }
    *slot = (unsigned long)&drawfb_wrapper;
    mprotect((void *)page, 0x1000, PROT_READ);
    logline("DRAWFB_PATCH written=%#lx (was %#lx)\n", *slot, before);

    /* Second slot: notifyDraw lives on its own page. */
    void *nentry = nentry_direct;
    if (nentry) {
        unsigned long naddr = (unsigned long)(uintptr_t)base + 0x377b440u;
        unsigned long *nslot = (unsigned long *)naddr;
        unsigned long nbefore = *nslot;
        unsigned long npage = naddr & ~0xffful;
        __atomic_store_n(&g_real_notify_got, (void *(*)(void *))nentry, __ATOMIC_RELAXED);
        if (mprotect((void *)npage, 0x1000, PROT_READ|PROT_WRITE) == 0) {
            *nslot = (unsigned long)&notify_wrapper;
            mprotect((void *)npage, 0x1000, PROT_READ);
            logline("NOTIFY_PATCH slot=%#lx before=%#lx expect=%#lx written=%#lx\n",
                    naddr, nbefore, (unsigned long)nentry, *nslot);
        } else {
            logline("NOTIFY_PATCH mprotect errno=%d\n", errno);
        }
    } else {
        logline("NOTIFY_PATCH skip (symbol not found)\n");
    }
}

static void caller_owner(unsigned ra, char *buf, size_t n)
{
    unsigned long st = (unsigned long)__atomic_load_n(&g_real_start_off, __ATOMIC_RELAXED);
    unsigned long appfw = st ? ((st & ~1UL) - 0x54d580UL) : 0;
    unsigned long ps = (unsigned long)__atomic_load_n(&g_real_psm, __ATOMIC_RELAXED);
    unsigned long libobj = ps ? ((ps & ~1UL) - 0xe5610UL) : 0;
    if (appfw && ra >= appfw && ra < appfw + 0x3d4bc78UL)
        snprintf(buf, n, "appFw+%#lx", (unsigned long)ra - appfw);
    else if (libobj && ra >= libobj && ra < libobj + 0x3bfde8UL)
        snprintf(buf, n, "libObj+%#lx", (unsigned long)ra - libobj);
    else
        snprintf(buf, n, "other(%#x)", ra);
}

/* ★ Watch the jump slots over time instead of reading them once.
 *
 * A single read inside the send hook happens at whatever moment that hook first
 * fires, which may be before the routines in question are ever entered. A slot
 * that flips from the resolver stub to a real target later in the run proves the
 * call happened after that read, and a slot that never flips proves it never
 * happened at all. GOTWATCH */
static void *got_watcher(void *arg)
{
    (void)arg;
    unsigned long last_psm = 0, last_fb = 0;
    for (int i = 0; i < 90; ++i) {
        struct timespec ts = { 2, 0 };
        nanosleep(&ts, 0);
        unsigned long st = (unsigned long)__atomic_load_n(&g_real_start_off, __ATOMIC_RELAXED);
        if (!st) continue;
        unsigned long appfw = (st & ~1UL) - 0x54d580UL;
        unsigned long vp = *(volatile unsigned long *)(uintptr_t)(appfw + 0x377acecUL);
        unsigned long vf = *(volatile unsigned long *)(uintptr_t)(appfw + 0x377afe0UL);
        if (vp != last_psm) {
            last_psm = vp;
            logline("GOTWATCH psm %#lx rel=%#lx\n", vp, vp - appfw);
        }
        if (vf != last_fb) {
            last_fb = vf;
            logline("GOTWATCH drawfb %#lx rel=%#lx\n", vf, vf - appfw);
        }
    }
    return 0;
}

/* Naked stub removed: jumping to a target resolved via RTLD_NEXT inside the
 * stub lost the address the loader hook captured, and the run aborted before
 * any stage ran (ledger 394). The C hook is restored below. */

/* ★ Entry stub that does not move the stack, with a target captured by the
 * loader hook.
 *
 * The firmware's entry takes its own stack pointer as the frame pointer and the
 * second stage writes deep into that frame, so a hook with its own frame shifts
 * every one of those writes. The reference records the same hazard and answers
 * it by preserving registers and aligning the stack. The target cannot come
 * from RTLD_NEXT, because the application is loaded by name and the address only
 * exists in the value the loader hook already captured, so that value is stored
 * here and the stub jumps with bx, leaving the stack exactly as it found it.
 * START_NAKED2 */
static volatile void *g_start_target;

void argtrace_start_enter(unsigned long lr)
{
    void *t = (void *)__atomic_load_n(&g_start_target, __ATOMIC_RELAXED);
    logline("START_NAKED enter lr=%#lx target=%p\n", lr, t);
    { static int wonce; if (!__atomic_exchange_n(&wonce, 1, __ATOMIC_RELAXED)) {
        pthread_t th; if (pthread_create(&th, 0, got_watcher, 0) == 0) pthread_detach(th);
    } }
}

void argtrace_start_nosym(unsigned long lr)
{
    logline("START_NAKED no target, lr=%#lx; returning without entering\n", lr);
}

__attribute__((naked)) void Application_Start(void)
{
    __asm__ volatile(
        "ldr  ip, 1f\n"
        "ldr  ip, [ip]\n"
        "cmp  ip, #0\n"
        "beq  2f\n"
        "push {r0, r1, r2, r3, r12, lr}\n"
        "mov  r0, lr\n"
        "bl   argtrace_start_enter\n"
        "pop  {r0, r1, r2, r3, r12, lr}\n"
        "ldr  ip, 1f\n"
        "ldr  ip, [ip]\n"
        "bx   ip\n"
        "2:\n"
        "push {r0, r1, r2, r3, r12, lr}\n"
        "mov  r0, lr\n"
        "bl   argtrace_start_nosym\n"
        "pop  {r0, r1, r2, r3, r12, lr}\n"
        "bx   lr\n"
        ".align 2\n"
        "1: .word g_start_target\n"
    );
}

static unsigned long my_start(unsigned long a,unsigned long b,unsigned long c,unsigned long d)
{
    /* ★ Read the data segment slot that holds the top level callback.
     *
     * Six levels of static tracing from the widget constructors end at a routine
     * that is neither called directly nor in any table, whose address appears
     * exactly once with the thumb bit set, in a relocated data slot. Reading
     * that slot at run time separates "the callback was registered and simply
     * never invoked" from "the registration never happened" (ledger 385). */
    {
        unsigned long st = (unsigned long)__atomic_load_n(&g_real_start_off, __ATOMIC_RELAXED);
        if (st) {
            unsigned long appfw = (st & ~1UL) - 0x54d580UL;
            unsigned long slot = appfw + 0x3963a9cUL;
            /* offset must be inside the module; the data segment spans
             * 0x37e30a8 to 0x3d536e8. */
            if (0x3963a9cUL < 0x3d536e8UL) {
                unsigned long v = *(volatile unsigned long *)(uintptr_t)slot;
                unsigned long off = (v & ~1UL) ? ((v & ~1UL) - appfw) : 0;
                logline("TOPCB appfw=%#lx slot=%#lx value=%#lx off_in_appfw=%#lx thumb=%lu\n",
                        appfw, slot, v, off, v & 1UL);
                /* Invocation moved to drive_once: the entry thread id is set
                 * after this point, so the guard there is meaningful. */
            }
        }
    }
    /* ★ Read the gate's global event flag, which needs no frame.
     *
     * One of the gate's exits consumes a byte from the library's cleared data
     * segment and clears it after reading. Static analysis says the only code
     * that sets it is never called, so the flag should read zero and that exit
     * should never be taken; that claim has never been checked at run time and
     * this is the cheapest place to do it. Read only. GVFLAG */
    {
        /* The flag belongs to the *application* library, not the interface one:
         * the offset is a hundred and nine megabytes while the interface
         * library is under four, which is what the previous read got wrong
         * (ledger 368). The application's data segment spans 0x3d536e8 to
         * 0x85f2a78, and the offset sits inside it. */
        unsigned long st = (unsigned long)__atomic_load_n(&g_real_start_off, __ATOMIC_RELAXED);
        if (st) {
            unsigned long appfw = (st & ~1UL) - 0x54d580UL;
            unsigned long fa = appfw + 0x6ce6420UL;
            if (0x6ce6420UL < 0x85f2a78UL) {   /* offset < module size */
                unsigned int v1 = *(volatile unsigned char *)(uintptr_t)fa;
                logline("GVFLAG appfw=%#lx addr=%#lx value=%u\n", appfw, fa, v1);
            }
        }
    }
    /* ★ Trigger one render pass directly, the way the reference does.
     *
     * The reference's own comment says it best: this bypasses the model key
     * processing that never fires without capture events, while still
     * exercising the real interface to graphics path. Its two entry points are
     * plain exported methods, so no table or page is touched. UX_RENDER_TRIGGER */
    {
        static int done;
        if (!done) {
            done = 1;
            void *(*get_wgtsys)(void) = (void *(*)(void))dlsym(RTLD_DEFAULT,
                "_ZN2ux6wgtsys12WidgetSystem15sys_getInstanceEv");
            int (*nd)(void *) = (int (*)(void *))dlsym(RTLD_DEFAULT,
                "_ZN2ux6wgtsys12WidgetSystem10notifyDrawEv");
            if (get_wgtsys && nd) {
                void *wg = get_wgtsys();
                logline("UX_RENDER_TRIGGER wgtsys=%p\n", wg);
                unsigned long libobj3 = 0;
                { unsigned long p3 = (unsigned long)__atomic_load_n(&g_real_psm, __ATOMIC_RELAXED);
                  if (p3) libobj3 = (p3 & ~1UL) - 0xe5610UL; }
                if (wg) {
                    /* ★ Read the field the frame delay check consults.
                     *
                     * That check is three instructions: load the word at a fixed
                     * offset and report whether it is non zero, and the draw
                     * entry skips its whole body when it is zero (ledger 343).
                     * Reading the word here says whether the skip is the reason
                     * nothing is drawn, without calling anything else. */
                    /* List read removed: the slot offset used here pointed at
                     * a reference count rather than the singleton, and reading
                     * through it disturbed the run (ledger 360). */

                    /* ★ Inspect the list the draw gate consults.
                     *
                     * The gate takes the singleton, adds a fixed offset to reach
                     * a list head, and treats the list as empty when the head's
                     * next pointer equals the head's own address. The singleton
                     * here is the same one this code already holds, so the test
                     * needs no extra indirection: just the two words at the head
                     * and the head's own address. */
                    {
                        /* The gate is called with (singleton + 0x4b4); inside it
                         * reads the word at +12 and compares that against its own
                         * address plus 16, treating equality as empty. So the two
                         * values to look at are +0x4c0 and +0x4c4, not the head's
                         * first two words. */
                        unsigned long base2 = (unsigned long)wg + 0x4b4UL;
                        unsigned long v12 = *(volatile unsigned long *)(uintptr_t)(base2 + 12);
                        unsigned long v16 = base2 + 16;
                        logline("GATE_LINK base=%#lx v12=%#lx expect16=%#lx %s\n",
                                base2, v12, v16, (v12 == v16) ? "EMPTY" : "NON-EMPTY");
                    }
                    /* ★ Probe what notifyDraw would actually iterate.
                     *
                     * Symbol interposition cannot observe libObj's internal BL
                     * calls: 0x1061c0 (first widget) and 0xdec04 (next widget)
                     * are direct branches, never PLT, so an interposed
                     * onDrawNotice counting zero cannot distinguish "no widgets
                     * exist" from "the walk never reaches any". The widget list
                     * itself settles that, and its address is decodable from
                     * notifyDraw's own first instruction sequence:
                     *   ldr r3,[pc,#32] (=0x24eb98); add r3,pc
                     *     -> r3 = lib + 0x354d60
                     *   ldr r3,[r3,#0x1ba8] -> *(lib+0x356908) = singleton
                     *   ldr r0,[r3,#0]      -> the object, then +0x4b4 = list
                     * Everything below is a plain read of that structure.
                     *
                     * The base must NOT come from dlsym(notifyDraw): this very
                     * process hooks that symbol, so dlsym returns our hook and
                     * the derived base lands outside the library (ledger 337).
                     * Derive the base from a real entry captured at load time,
                     * never from dlsym on a symbol this process hooks. Two
                     * sources are tried because this point runs BEFORE the pump
                     * is first invoked, so g_real_psm is still zero here:
                     *   g_real_start_off -> appFw base -> its GOT slot that
                     *     holds processSystemMessage's resolved address, which
                     *     minus its offset gives the libObj base;
                     *   g_real_psm       -> direct, later.
                     * Whichever is available first wins. WLIST */
                    {
                        unsigned long lb = 0, viagot = 0;
                        unsigned long ps2 =
                            (unsigned long)__atomic_load_n(&g_real_psm, __ATOMIC_RELAXED);
                        if (ps2) { lb = (ps2 & ~1UL) - 0xe5610UL; viagot = 1; }
                        if (!lb) {
                            void *st = __atomic_load_n(&g_real_start_off, __ATOMIC_RELAXED);
                            if (st) {
                                unsigned long appfw = ((unsigned long)(uintptr_t)st & ~1UL) - 0x54d580UL;
                                unsigned long slot = appfw + 0x377acecUL;
                                unsigned long v = *(volatile unsigned long *)(uintptr_t)slot;
                                unsigned long delta = v ? (v - appfw) : 0;
                                if (delta == 0x5289d0UL) {
                                    logline("WLIST appfw=%#lx psm slot still PLT0\n", appfw);
                                } else if (v) {
                                    lb = (v & ~1UL) - 0xe5610UL;
                                    viagot = 2;
                                }
                            }
                        }
                        if (lb) {
                            unsigned long pslot = lb + 0x356908UL;
                            unsigned long single =
                                *(volatile unsigned long *)(uintptr_t)pslot;
                            unsigned long inner = single
                                ? *(volatile unsigned long *)(uintptr_t)single : 0;
                            logline("WLIST via=%lu lib=%#lx pslot=%#lx singleton=%#lx inner=%#lx\n",
                                    viagot, lb, pslot, single, inner);
                            if (inner) {
                                unsigned long head = inner + 0x4b4UL;
                                unsigned long fwd = *(volatile unsigned long *)(uintptr_t)(head + 4);
                                unsigned long back = *(volatile unsigned long *)(uintptr_t)(head + 8);
                                unsigned long cnt = 0;
                                unsigned long cur = head;
                                for (unsigned int i = 0; i < 512; ++i) {
                                    cur = *(volatile unsigned long *)(uintptr_t)(cur + 4);
                                    if (cur == head || !cur) break;
                                    ++cnt;
                                }
                                logline("WLIST head=%#lx fwd=%#lx back=%#lx count=%lu\n",
                                        head, fwd, back, cnt);
                            }
                        } else {
                            logline("WLIST no base yet\n");
                        }
                    }
                    unsigned int fd = *(volatile unsigned int *)((char *)wg + 0x544);
                    logline("FRAMEDELAY_READ wg=%p field544=%u (%#x)\n", wg, fd, fd);
                    /* Install the two GRM raster entry hooks BEFORE the first
                     * notifyDraw. The previous placement ran them after it,
                     * so the first pump call was never observed; the wrapped
                     * counters then only covered the second call. Installing
                     * here means every notifyDraw on this path is covered. */
                    grm_hook_install();
                    lz_probe_install();
                    dfb_hook_install();
                    re_hook_install();
                    fi_hook_install();
                    stage_hooks_install();
                    psm_hook_install();
                    ws_hook_install();
                    base_print();
                    /* ★ Observation-only: do NOT call notifyDraw from here.
                     * dlsym(RTLD_DEFAULT, ...) returns our own interposer, so a
                     * call made here shows up in DRAW_PROBE as if the firmware had
                     * made it. Two rounds of "notifyDraw ran 2 times" were in fact
                     * this probe calling itself. Skipping the call lets the next
                     * run show whether the firmware ever invokes it at all. */
                    logline("UX_RENDER_TRIGGER skipped (probe no longer drives notifyDraw)\n");
                    /* ★ Re-probe the widget list AFTER notifyDraw has run.
                     *
                     * The earlier probe runs before the pump is ever called, so
                     * neither g_real_psm nor a resolved GOT slot is available
                     * yet. By this point notifyDraw has been invoked, which
                     * means the symbol is bound and the base is derivable. Two
                     * samples of the same structure, one on each side of the
                     * call, separate "the list is genuinely empty" from "the
                     * list exists but the pump returns before walking it".
                     * WLIST2 */
                    {
                        unsigned long ps3 =
                            (unsigned long)__atomic_load_n(&g_real_psm, __ATOMIC_RELAXED);
                        unsigned long lb = ps3 ? ((ps3 & ~1UL) - 0xe5610UL) : 0;
                        if (lb) {
                            unsigned long pslot = lb + 0x356908UL;
                            unsigned long single =
                                *(volatile unsigned long *)(uintptr_t)pslot;
                            unsigned long inner = single
                                ? *(volatile unsigned long *)(uintptr_t)single : 0;
                            logline("WLIST2 lib=%#lx pslot=%#lx singleton=%#lx inner=%#lx\n",
                                    lb, pslot, single, inner);
                            if (inner) {
                                unsigned long head = inner + 0x4b4UL;
                                unsigned long fwd = *(volatile unsigned long *)(uintptr_t)(head + 4);
                                unsigned long back = *(volatile unsigned long *)(uintptr_t)(head + 8);
                                unsigned long cnt = 0;
                                unsigned long cur = head;
                                for (unsigned int i = 0; i < 512; ++i) {
                                    cur = *(volatile unsigned long *)(uintptr_t)(cur + 4);
                                    if (cur == head || !cur) break;
                                    ++cnt;
                                }
                                logline("WLIST2 head=%#lx fwd=%#lx back=%#lx count=%lu\n",
                                        head, fwd, back, cnt);
                            }
                        } else {
                            logline("WLIST2 no psm yet\n");
                        }
                    }
                    /* Correct-offset dump of the same list, using the layout
                     * libObj's own code proves (see wl_dump). This supersedes
                     * both WLIST and WLIST2, whose pointer arithmetic was
                     * wrong and whose "count=0" was therefore meaningless. */
                    wl_dump("pre");
                    /* Install the two GRM raster entry hooks now. By this point
                     * The hooks are already installed above, before the pump
                     * was first called; this comment previously described an
                     * install that ran too late to cover it. */
                    /* ★ Positive control for the pump counter.
                     *
                     * Nine separate measurements agree that the interface never
                     * runs, and all of them rest on this one counter reading
                     * zero. A zero that explains everything is exactly the one
                     * that most needs proving, so call the pump once here and
                     * confirm the counter moves. If it does not, that counter is
                     * an artefact and the nine readings collapse with it
                     * (ledger 371).
                     * PSM_POSCTRL */
                    {
                        void *(*gfxinst)(void) = (void *(*)(void))dlsym(RTLD_DEFAULT,
                            "_ZN2ux6gfxeng14GraphicsEngine15sys_getInstanceEv");
                        int (*psm)(void *) = (int (*)(void *))dlsym(RTLD_DEFAULT,
                            "_ZN2ux6gfxeng14GraphicsEngine20processSystemMessageEv");
                        logline("PSM_POSCTRL syms gfxinst=%p psm=%p\n",
                                (void *)gfxinst, (void *)psm);
                        if (gfxinst && psm) {
                            void *gi = gfxinst();
                            logline("PSM_POSCTRL gfxinst=%p\n", gi);
                            if (gi) {
                                int pr = psm(gi);
                                logline("PSM_POSCTRL psm returned %d\n", pr);
                            }
                        }
                    }
                    unsigned int fd2 = *(volatile unsigned int *)((char *)wg + 0x544);
                    logline("FRAMEDELAY_READ after field544=%u (%#x)\n", fd2, fd2);
                }
            } else {
                logline("UX_RENDER_TRIGGER symbols missing get=%p nd=%p\n",
                        (void *)get_wgtsys, (void *)nd);
            }
        }
    }
    /* ★ Read the callback slot from inside the guest.
     *
     * Everything needed for this lives in this very process: the interface
     * library's base follows from a symbol captured at load time, and the
     * object and callback are ordinary memory in the same address space. The
     * earlier attempts read guest memory from the host and failed for reasons
     * that do not apply here (ledger 343). Guarded so an implausible value
     * cannot fault, and read only — nothing is written. CHAIN_READ */
    {
        unsigned long psm = (unsigned long)__atomic_load_n(&g_real_psm, __ATOMIC_RELAXED);
        if (psm) {
            unsigned long libobj = (psm & ~1UL) - 0xe5610UL;
            void *obj = 0, *cb = 0;
            unsigned long libend = libobj + 0x3d74f0UL;
            logline("CHAIN_READ libobj=%#lx libend=%#lx\n", libobj, libend);
            void **slot = (void **)(uintptr_t)(libobj + 0x3c82d0UL);
            /* The slot lies inside the library's cleared segment, which is
             * mapped writable, so dereferencing it cannot fault here. */
            if ((unsigned long)(uintptr_t)slot >= libobj + 0x3c7828UL &&
                (unsigned long)(uintptr_t)slot < libend) {
                obj = __atomic_load_n(slot, __ATOMIC_RELAXED);
                logline("CHAIN_READ obj=%p\n", obj);
                if (obj) {
                    cb = __atomic_load_n((void **)((char *)obj + 0x34), __ATOMIC_RELAXED);
                    logline("CHAIN_READ callback=%p\n", cb);
                    /* ★ The callback is three instructions that return one byte
                     * from a global in this library's data segment, so reading
                     * that byte here answers what the pump would return without
                     * calling it at all. A non zero value means the pump is
                     * asked to keep going; zero means it is told to stop. */
                    {
                        unsigned char flag = 0;
                        unsigned long fa = libobj + 0x3581b6UL;
                        if (fa >= libobj + 0x357738UL && fa < libobj + 0x3c7828UL) {
                            flag = *(volatile unsigned char *)(uintptr_t)fa;
                            logline("FLAG_READ addr=%#lx value=%u\n", fa, flag);
                        }
                    }
                }
            } else {
                logline("CHAIN_READ slot outside library\n");
            }
        } else {
            logline("CHAIN_READ no psm yet\n");
        }
    }
    /* ★ Record our own thread id so the /proc task table can be matched to
     * this thread. Ledger 212 found two threads perpetually runnable and we
     * need to know whether one of them is this one. */
    {
        unsigned int tid = (unsigned int)syscall(SYS_gettid);
        logline("MY_TID Application_Start pid=%u tid=%u\n",
                (unsigned)getpid(), tid);
        /* ★ Read the index global directly at runtime.
     *
     * Ledger 239: static analysis proved both u16 and u32 readings land inside
     * .rodata, yet neither produces the zero the earlier probe measured, so the
     * variable must be rewritten during boot. Stop guessing which — read it. */
    {
        void *ab = lib_text_base("appFw.so");
        if (ab) {
            volatile unsigned *gv = (volatile unsigned *)((char *)ab + 0x37e39f0u);
            logline("GV_PROBE at_entry u32=%#x u16lo=%#x u16hi=%#x\n",
                    gv[0], gv[0] & 0xFFFFu, (gv[0] >> 16) & 0xFFFFu);
        } else {
            logline("GV_PROBE no appFw base\n");
        }
    }
    /* Capture the pointer the entry received in r0: both stages get it, and
     * the word at +1728 is exactly what decides whether the loop runs. */
    __atomic_store_n(&g_frame_ptr, (void *)(uintptr_t)a, __ATOMIC_RELAXED);
    { static int wonce; if (!__atomic_exchange_n(&wonce, 1, __ATOMIC_RELAXED)) {
        pthread_t th; if (pthread_create(&th, 0, got_watcher, 0) == 0) pthread_detach(th);
    } }
    /* ★ Read the gate's own inputs from the frame the entry received.
     *
     * The entry is called with this pointer and passes it unchanged to the
     * stage whose gate decides whether the frame routine runs, so the four
     * fields the gate tests are at fixed offsets from it. A single read here
     * says which of the gate's exits is taken, which no amount of sampling
     * could establish for code this short (ledger 355). Range-checked before
     * dereferencing, and read only. FRAME_FIELDS */
    {
        unsigned long fr = (unsigned long)a;
        if (fr > 0x1000 && fr < 0x40000000UL) {
            unsigned long f2160 = *(volatile unsigned long *)(uintptr_t)(fr + 2160);
            unsigned long f2192 = *(volatile unsigned long *)(uintptr_t)(fr + 2192);
            unsigned char b2192 = *(volatile unsigned char *)(uintptr_t)(fr + 2192);
            unsigned long f432  = *(volatile unsigned long *)(uintptr_t)(fr + 432);
            unsigned long f430  = *(volatile unsigned char *)(uintptr_t)(fr + 430);
            unsigned long f4    = *(volatile unsigned long *)(uintptr_t)(fr + 4);
            logline("FRAME_FIELDS frame=%#lx f4=%#lx f2160=%lu f2192=%lu(byte=%u) f432=%lu f430=%lu\n",
                    fr, f4, f2160, f2192, b2192, f432, f430);
        } else {
            logline("FRAME_FIELDS frame=%#lx (implausible, not read)\n", fr);
        }
    }
    __atomic_store_n(&g_as_pid, (int)getpid(), __ATOMIC_RELAXED);
        __atomic_store_n(&g_as_tid, (int)tid, __ATOMIC_RELAXED);
    }
    logline("ARGTRACE >>> Application_Start ENTER\n");
    ge_probe();
    /* Resolve the stubs site2 branches through, so the blocker is named. */
    if (getenv("PLT_RESOLVE") || 1) {
        plt_resolve("appFw.so", 0x923ce0u, "site2_0x923ce0");
        plt_resolve("appFw.so", 0x9246b8u, "frame_0x9246b8");
        plt_resolve("appFw.so", 0x925fb0u, "frame_0x925fb0");
        plt_resolve("appFw.so", 0x923900u, "frame_0x923900");
        plt_resolve("appFw.so", 0x9240d4u, "frame_0x9240d4");
    }
    __atomic_store_n(&g_in_start, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&g_as_phase, 1, __ATOMIC_RELAXED);
    if (ARG_AP_START_WATCH) {
        pthread_t t;
        if (pthread_create(&t, 0, start_watchdog, 0) == 0) pthread_detach(t);
    }
    if (ARG_FORCE_SITE3 && !__atomic_exchange_n(&g_menu_requested, 1, __ATOMIC_RELAXED)) {
        /* ★ site3 reserves 2048 bytes itself and then nests several frames
         * (0x54ef74, the vtable target, 0x723aac and its callees). The default
         * pthread stack is only 8 KB, so give this worker a generous one —
         * a stack overflow there would silently kill the thread mid-call and
         * look exactly like "the call never returns". */
        pthread_t st;
        pthread_attr_t at;
        if (pthread_attr_init(&at) == 0) {
            pthread_attr_setstacksize(&at, 256 * 1024);
            if (pthread_create(&st, &at, site3_force_thread, 0) == 0) pthread_detach(st);
            pthread_attr_destroy(&at);
        }
    }
    /* UDF call-site probing is DISABLED for good: SIGILL is never delivered to
     * user space in this guest (ledger layer 168), so as_site_probe() leaves the
     * process stopped inside Application_Start and every downstream observation
     * reads as "not reached". That single fact explains why so many rounds saw
     * createLayer=2 instead of 4 and [AS]=0 instead of 335. */
        /* BP_WINDOW and its busy wait are removed. The 90 second poll loop
         * pins the single core inside the entry thread, which starves the
         * firmware and drops the baseline to 1020 lines / AS=0 /
         * createLayer=2. A debugger window is not worth breaking the subject
         * being observed (ledger 259). */
    /* Touch injection removed: the entry is a static method that reads internal
     * engine state, and every attempt to drive it faults or stalls because the
     * widget system's own initialisation is not what feeds it (ledger 288).
     * Restoring the clean baseline keeps the port measurements trustworthy. */
    /* tail jump handled by the naked stub below */
    unsigned long r = real_start(a, b, c, d);
    __atomic_store_n(&g_as_phase, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&g_in_start, 0, __ATOMIC_RELAXED);
    logline("ARGTRACE <<< Application_Start RETURN 0x%lx\n", r);
    return r;
}


/* ★ Delayed injector: after the engine settles, send one event to each of the
 * two queues identified in ledger 277. One event per queue keeps the baseline
 * check meaningful. */
static void *inject_thread(void *arg)
{
    (void)arg;
    struct timespec t0, t1;
    syscall(SYS_clock_gettime, 1, &t0);
    do {
        syscall(SYS_sched_yield);
        syscall(SYS_clock_gettime, 1, &t1);
    } while ((t1.tv_sec - t0.tv_sec) < 60);
    logline("INJECT_INPUT begin\n");
    /* Touch first: this is the path the widget system actually consumes. */
    inject_touch(128, 120, 1000);      /* centre-ish, in u8 range */
    inject_touch(40, 40, 2000);        /* a corner */
    logline("INJECT_INPUT touches done\n");
    return 0;
}

/* ★ Lifecycle wrappers — record which of the three entries the firmware drives. */
static unsigned long my_appfwinit(unsigned long a,unsigned long b,unsigned long c,unsigned long d)
{
    unsigned long n=__atomic_fetch_add(&g_life_n,1,__ATOMIC_RELAXED);
    unsigned tid=(unsigned)syscall(SYS_gettid);
    logline("UXLIFE app_fw_init n=%lu tid=%u a=%#lx b=%#lx\n",n+1,tid,a,b);
    unsigned long r = real_appfwinit ? real_appfwinit(a,b,c,d) : 0;
    logline("UXLIFE app_fw_init RETURN %#lx\n", r);
    return r;
}
static unsigned long my_uxres(unsigned long a,unsigned long b,unsigned long c,unsigned long d)
{
    unsigned tid=(unsigned)syscall(SYS_gettid);
    logline("UXLIFE uxengine_res ENTER tid=%u a=%#lx\n",tid,a);
    unsigned long r = real_uxres ? real_uxres(a,b,c,d) : 0;
    logline("UXLIFE uxengine_res RETURN %#lx\n", r);
    return r;
}
static unsigned long my_uxsus(unsigned long a,unsigned long b,unsigned long c,unsigned long d)
{
    unsigned tid=(unsigned)syscall(SYS_gettid);
    logline("UXLIFE uxengine_sus ENTER tid=%u a=%#lx\n",tid,a);
    unsigned long r = real_uxsus ? real_uxsus(a,b,c,d) : 0;
    logline("UXLIFE uxengine_sus RETURN %#lx\n", r);
    return r;
}

/* Heartbeat: while inside Application_Start, prove the process is still alive and
 * report how long it has been in there, so "silent" is distinguishable from "dead". */
static void *start_watchdog(void *arg)
{
    (void)arg;
    unsigned n = 0;
    while (1) {
        sleep(5);   /* NOTE: does not elapse in this guest (ledger 194);
                     * the monotonic busy-wait replacement starved startup,
                     * so this probe is effectively inactive. Kept because
                     * other probes do not depend on it. */
        if (__atomic_load_n(&g_in_start, __ATOMIC_RELAXED)) {
            mile_report();
            logline("APPSTART still-inside t=%us frameiter_calls=%u\n",
                    (++n) * 5, frameiter_count());

            /* Reports are emitted unconditionally and before the state dump:
             * in the previous build they were reached but the process died
             * inside dump_thread_state, so nothing was printed and the counters
             * looked absent. Printing first keeps them observable regardless. */
            stage_report();
            psm_report();
            logline("COUNTERS-DONE t=%u\n", n * 5);
            /* ★ Drive drawFramebuffer from THIS thread, which is measured to run.
             *
             * R115/R116 established that a fresh pthread_create returns 0 but its
             * entry never executes here, while this watchdog thread demonstrably
             * does (its t=5s line appears AFTER the spawn point in fd7: spawn at
             * line 1527, this line at 1680). So the drive belongs on a thread that
             * is proven live, not on a newly created one.
             *
             * One shot only, after start-up has settled. FORCEDRAW */
            if (getenv("FORCEDRAW")) {
                static int fired;
                if (!__atomic_load_n(&fired, __ATOMIC_RELAXED)
                    && !__atomic_exchange_n(&fired, 1, __ATOMIC_RELAXED)) {
                    logline("FORCEDRAW trigger from watchdog t=%u\n", n * 5);
                    force_draw_once();
                    logline("FORCEDRAW watchdog returned\n");
                }
            }
            if (n >= 1) { dump_thread_state("stuck"); probe_thread_pcs("stuck"); }
            if (n == 2) probe_target_syscall("stuck");
        } else {
            n = 0;
        }
    }
    return 0;
}
__attribute__((constructor)) static void start_watchdog_init(void)
{
    pthread_t t;
    if (getenv("APPSTART_WATCH")) pthread_create(&t, 0, start_watchdog, 0);
}
static unsigned long my_uxinit(unsigned long a,unsigned long b,unsigned long c,unsigned long d)
{
    logline("ARGTRACE >>> uxengine_init2 ENTER\n");
    unsigned long r=real_uxinit(a,b,c,d);
    logline("ARGTRACE <<< uxengine_init2 RETURN 0x%lx\n",r);
    return r;
}
static unsigned long my_app(unsigned long a,unsigned long b,unsigned long c,unsigned long d)
{
    logline("ARGTRACE init enter arg=0x%lx str=\"",a);
    if(a){ char s[100]; int i; for(i=0;i<99;i++){ char ch=((const char*)a)[i]; s[i]=(ch>=32&&ch<127)?ch:'.'; if(!ch)break; } s[i]=0; write(2,s,strlen(s)); }
    logline("\"\n");
    unsigned long r=real_app(a,b,c,d);
    logline("ARGTRACE init return 0x%lx\n",r);
    return r;
}

static unsigned long my_draw(unsigned long a,unsigned long b,unsigned long c,unsigned long d)
{
    static int n=0;
    if(++n<=3) logline("ARGTRACE >>> drawFramebuffer #%d\n",n);
    cnt_draw++;
    extern unsigned long (*real_draw)(unsigned long,unsigned long,unsigned long,unsigned long);
    return real_draw(a,b,c,d);
}
static unsigned long my_update(unsigned long a,unsigned long b,unsigned long c,unsigned long d)
{
    static int n=0;
    if(++n<=3) logline("ARGTRACE >>> WidgetSystem::updateFrame #%d\n",n);
    cnt_update++;
    extern unsigned long (*real_update)(unsigned long,unsigned long,unsigned long,unsigned long);
    return real_update(a,b,c,d);
}
static int (*real_condwait)(void*,void*);
static int my_condwait(void *cv, void *m)
{
    if(++cnt_cond<=3) logline("ARGTRACE pthread_cond_wait #%d\n",cnt_cond);
    return real_condwait(cv,m);
}
void *dlsym(void *handle,const char *name)
{
    static void *(*native)(void*,const char*);
    if(!native) native=dlvsym(RTLD_NEXT,"dlsym","GLIBC_2.4");
    if(!native) return NULL;
    void *r=native(handle,name);
    if(!r && name && strcmp(name,"grm_init")==0){
        static void *(*nd)(const char*,int);
        if(!nd) nd=(void*(*)(const char*,int))dlvsym(RTLD_NEXT,"dlopen","GLIBC_2.4");
        logline("GRMFIX grm_init NULL -> dlopen libObj first\n");
        if(nd) nd("/usr/lib/libObj.so", 0x101);
        r = native(handle,name);
        logline("GRMFIX retry result=%p\n", r);
    }
    if(r && name && (strstr(name,"_init")||strstr(name,"Init"))){
        logline("INITSEQ resolve %s\n",name);
    }
    if(r && name && strcmp(name,"Application_System_Init")==0){
        real_app=(unsigned long(*)(unsigned long,unsigned long,unsigned long,unsigned long))r;
        logline("ARGTRACE hooked Application_System_Init = %p\n",r);
        return (void*)my_app;
    }
    if(r && name && strstr(name,"drawFramebuffer")){
        real_draw=(unsigned long(*)(unsigned long,unsigned long,unsigned long,unsigned long))r;
        logline("ARGTRACE hooked drawFramebuffer\n");
        return (void*)my_draw;
    }
    if(r && name && strstr(name,"WidgetSystem11updateFrame")){
        real_update=(unsigned long(*)(unsigned long,unsigned long,unsigned long,unsigned long))r;
        logline("ARGTRACE hooked updateFrame\n");
        return (void*)my_update;
    }
    if(r && name && strcmp(name,"pthread_cond_wait")==0){
        real_condwait=(int(*)(void*,void*))r;
        logline("ARGTRACE hooked pthread_cond_wait\n");
        return (void*)my_condwait;
    }
    if(r && name && strcmp(name,"Application_Start")==0){
        real_start=(unsigned long(*)(unsigned long,unsigned long,unsigned long,unsigned long))r;
        __atomic_store_n(&g_real_start_off, r, __ATOMIC_RELAXED);
        __atomic_store_n(&g_start_target, (void *)r, __ATOMIC_RELAXED);
        logline("ARGTRACE start target captured = %p\n", (void *)r);
        return (void*)my_start;
    }
    if(r && name && strcmp(name,"uxengine_init2")==0){
        real_uxinit=(unsigned long(*)(unsigned long,unsigned long,unsigned long,unsigned long))r;
        logline("ARGTRACE hooked uxengine_init2 = %p\n",r);
        return (void*)my_uxinit;
    }
    return r;
}

void *dlopen(const char *path,int flags)
{
    static void *(*native)(const char*,int);
    if(!native) native=dlvsym(RTLD_NEXT,"dlopen","GLIBC_2.4");
    if(!native){ errno=ENOSYS; return NULL; }
    void *h=native(path,flags);
    if(path && (strstr(path,"appFw")||strstr(path,"libObj")||strstr(path,"AdjChk")||strstr(path,"AppConfig"))){
        if(h) logline("ARGTRACE dlopen path=%s flags=%#x -> %p\n",path,flags,h);
        else {
            const char *e = dlerror();
            logline("ARGTRACE dlopen FAILED path=%s flags=%#x err=%s\n",path,flags,e?e:"(null)");
        }
    }
    /* ★ Now that the engine library's handle is in hand, resolve the two calls
     * the main loop actually depends on. dlsym(RTLD_NEXT) never finds them
     * because this library is opened at run time (ledger 294), so the handle is
     * the only reliable route. Record the addresses; that alone tells us
     * whether the loop's pump call is reachable at all. */
    if (h && path && strstr(path, "libObj")) {
        void *pe = dlsym(h, "_ZN2ux6wgtsys12WidgetSystem12processEventEv");
        void *psm = dlsym(h, "_ZN2ux6gfxeng14GraphicsEngine20processSystemMessageEv");
        logline("LIBOBJ_SYM handle=%p processEvent=%p processSystemMessage=%p\n",
                h, pe, psm);
        if (pe && !g_real_process_event)
            __atomic_store_n(&g_real_process_event, (int (*)(void *))pe, __ATOMIC_RELAXED);
        if (psm && !g_real_psm)
            __atomic_store_n(&g_real_psm, (unsigned long (*)(void *))psm, __ATOMIC_RELAXED);

    }
    return h;
}


/* ★ Patch the caller's relocation slot for the event pump.
 *
 * The caller reaches the pump through a GOT entry, so replacing the exported
 * symbol changes nothing (ledger 295). This must run *after* the application
 * library is mapped; doing it inside the engine's own dlopen happens too early
 * because the application's executable segment is not up yet (observed: the
 * base lookup returns zero there). */
void report_got_slots(void)
{
    logline("GOT_ENTER\n");
    static int done;
    if (done) return;
    done = 1;
    /* lib_text_base caches a miss, and in this call path the maps lookup has
     * already been poisoned by an earlier miss, so use the link map base that
     * the assert hook captured instead — same value, no file parsing. */
    /* Derive the base from the entry pointer we already hooked: its file
     * offset is exactly 0x54d580, so the subtraction is exact. The link map
     * base is only captured by the assert hook, which never runs, and the maps
     * parser caches misses, so neither is usable here. */
    void *base = NULL;
    /* ★ Clear the Thumb bit first. Function pointers in this ABI carry bit 0
     * set, so subtracting the plain file offset from the raw pointer leaves the
     * base one byte too high and every slot read lands mid-word (observed as
     * 0xd02de329 instead of a real address). */
    if (g_real_start_off)
        base = (void *)(((uintptr_t)g_real_start_off & ~(uintptr_t)1) - 0x54d580u);
    if (!base) base = (void *)(uintptr_t)g_appfw_base;
    logline("GOT_BASE base=%p (entry=%p)\n", base, g_real_start_off);
    if (!base) return;
    /* ★★★★★ The decisive slot.
     *
     * site1 loads a class pointer from this slot, adds 8 and stores the result
     * as the table that site3 later dispatches through. Unlike the entries
     * above it is a plain data relocation filled at load time, so lazy binding
     * does not apply and a zero here would mean the class was never registered
     * — which is precisely the kind of fact this whole chase has needed.
     * Offset verified against the literal pool and the relocation table. */
    static const struct { unsigned off; const char *name; } slots[] = {
        /* ★ Offset corrected: the earlier value was two bytes low because the
         * Thumb pc-relative add uses (addr + 4) without aligning down, so the
         * slot is 0x37d95b8. The relocation table confirms it: R_ARM_RELATIVE
         * at exactly that address, which the odd address could never be. */
        { 0x37d95b8u, "site1 class pointer (R_ARM_RELATIVE)" },
        { 0x377acf8u, "createLayer (JUMP_SLOT, control)" },
        { 0x377ae68u, "osal_rcv_msg_tmo (JUMP_SLOT, control)" },
        { 0x377b334u, "processEvent (JUMP_SLOT, read-only)" },
    };
    unsigned long plt0 = (unsigned long)(uintptr_t)base + 0x5289d0u;
    logline("GOT_PLT0 %#lx\n", plt0);
    for (unsigned i = 0; i < sizeof slots / sizeof slots[0]; i++) {
        unsigned long a = (unsigned long)(uintptr_t)base + slots[i].off;
        unsigned long v = *(volatile unsigned long *)a;
        logline("GOT_SLOT %-32s %#lx %s\n", slots[i].name, v,
                (v == plt0) ? "PLT0" : ((v == 0) ? "ZERO" : "RESOLVED"));
    }
    /* ★★★★★ Follow the chain to the dispatch target.
     *
     * site1 stores (classptr + 8) as the table, and site3 calls its first slot.
     * Both are plain memory at known offsets from an already validated pointer,
     * so reading them costs one indirection each and finally names the code the
     * entry stalls in. Each hop is range checked against the module's own
     * segments before being dereferenced. */
    {
        unsigned long lo = (unsigned long)(uintptr_t)base;
        unsigned long hi = lo + 0x8642a78UL;   /* end of .bss */
        unsigned long cls = *(volatile unsigned long *)((uintptr_t)base + 0x37d95b8u);
        logline("CHAIN cls=%#lx (in module? %s)\n", cls,
                (cls >= lo && cls < hi) ? "yes" : "no");
        if (cls >= lo && cls < hi) {
            unsigned long vt = cls + 8;
            unsigned long f0 = *(volatile unsigned long *)(uintptr_t)vt;
            logline("CHAIN vtable=%#lx first=%#lx off_from_base=%#lx\n",
                    vt, f0, f0 - lo);
            if (f0 >= lo && f0 < hi)
                logline("CHAIN target=appFw+%#lx (in module)\n", f0 - lo);
            else
                logline("CHAIN target=%#lx (outside appFw)\n", f0);
        }
        /* FRAME1728 moved to the delayed reader below. */
    }
}

/* ============ __assert_fail hook (merged from tools/guest/assert_probe.c) ============ */
#include <link.h>
#include <stdlib.h>
/* Stand-in services, OFF by default so we can A/B against the known-good run.
 * Set SVC_BACKUP=1 / SVC_SENSOR=1 in the environment to enable. */
static int g_svc_backup(void){ static int v=-1; if(v<0) v = getenv("SVC_BACKUP") ? 1 : 0; return v; }
static int g_svc_sensor(void){ static int v=-1; if(v<0) v = getenv("SVC_SENSOR") ? 1 : 0; return v; }
static int find_appfw_base(struct dl_phdr_info *info, size_t sz, void *data)
{
    (void)sz; (void)data;
    if (info->dlpi_name && strstr(info->dlpi_name, "appFw.so")) {
        g_appfw_base = (unsigned long)(uintptr_t)info->dlpi_addr;
        logline("ASSERTBASE base=0x%lx name=%s\n", g_appfw_base, info->dlpi_name);
    }
    return 0;
}
void __assert_fail(const char *assertion, const char *file,
                   unsigned int line, const char *function)
{
    logline("AS a=%s f=%s l=%u\n",
            assertion?assertion:"?", file?file:"?", line);
    logline("AS fn=%s\n", function?function:"?");
    logline("AS lr=0x%lx\n", (unsigned long)__builtin_return_address(0));
    if (!g_appfw_base) dl_iterate_phdr(find_appfw_base, 0);
    unsigned long spv;
    __asm__ volatile("mov %0, sp" : "=r"(spv));
    unsigned long *sp = (unsigned long *)spv;
    for (int i = 0; i < 64; i += 4)
        logline("STK %02x: %08x %08x %08x %08x\n", i*4,
                (unsigned)sp[i], (unsigned)sp[i+1],
                (unsigned)sp[i+2], (unsigned)sp[i+3]);
    if (g_appfw_base) {
        unsigned long lo = g_appfw_base + 0x52ab00UL;
        unsigned long hi = g_appfw_base + 0x2ce8ac8UL;
        for (int i = 0; i < 64; i++) {
            unsigned long v = sp[i] & ~1UL;
            if (v >= lo && v < hi)
                logline("CAND off=0x%08x raw=0x%08x\n",
                        (unsigned)(v - g_appfw_base), (unsigned)sp[i]);
        }
    }
    _exit(1);
}

/* ===== 阻塞原语钩子：定位 call#3 在等什么 ===== */
static int g_sem=0,g_rcv=0,g_ssd=0,g_usl=0;
static int g_ssm_n=0;   /* own counter for snd_sync_msg */
int osal_wai_sem_tmo(void *sem, int tmo)
{
    static int (*real)(void*,int);
    if(!real) real=(int(*)(void*,int))dlsym(RTLD_NEXT,"osal_wai_sem_tmo");
    if(g_sem<40){
        unsigned c=(unsigned)(uintptr_t)__builtin_return_address(0);
        unsigned tid=(unsigned)syscall(SYS_gettid);
        Dl_info di; const char *mod="?"; unsigned off=0;
        if(dladdr((void*)c,&di) && di.dli_fname){ mod=di.dli_fname; off=c-(unsigned)(uintptr_t)di.dli_fbase; }
        logline("%sSEM n=%d tid=%u tmo=%d mod=%s off=%#x\n",g_as_phase?"[AS]":"",++g_sem,tid,tmo,mod,off);
    }
    if(!real) return -1;
    return real(sem,tmo);
}
int osal_rcv_msg_tmo(void *q, void *m, int tmo)
{
    static int (*real)(void*,void*,int);
    if(!real) real=(int(*)(void*,void*,int))dlsym(RTLD_NEXT,"osal_rcv_msg_tmo");
    if(g_rcv<400){
        unsigned c=(unsigned)(uintptr_t)__builtin_return_address(0);
        Dl_info di; const char *mod="?"; unsigned off=0;
        if(dladdr((void*)c,&di) && di.dli_fname){ mod=di.dli_fname; off=c-(unsigned)(uintptr_t)di.dli_fbase; }
        logline("%sRCVC n=%d qid=%#x tmo=%d mod=%s off=%#x%s\n",g_as_phase?"[AS]":"",++g_rcv,(unsigned)q,tmo,mod,off, g_s3_after?" AFTER_S3":"");
    }
    if(!real) return -1;
    int result = real(q,m,tmo);
    if ((unsigned)q == 0x7a804cu || (unsigned)q == 0x7a0020u) {
        static unsigned int (*get_src)(void *);
        static unsigned int (*msg_size)(void *);
        if (!get_src) get_src = (unsigned int (*)(void *))dlsym(RTLD_DEFAULT, "osal_get_msg_src");
        if (!msg_size) msg_size = (unsigned int (*)(void *))dlsym(RTLD_DEFAULT, "osal_msg_size");
        unsigned int size = (result == 0 && m && *(void **)m && msg_size) ? msg_size(*(void **)m) : 0;
        unsigned int src = (result == 0 && m && *(void **)m && get_src) ? get_src(*(void **)m) : 0;
        unsigned char *p = (result == 0 && m) ? (unsigned char *)(*(void **)m) : 0;
        char hex[65]; unsigned int shown = size < 32 ? size : 32;
        unsigned int k;
        for (k = 0; k < shown; ++k) snprintf(hex + k * 2, sizeof(hex) - k * 2, "%02x", p[k]);
        hex[shown * 2] = 0;
        logline("RCVC_RET qid=%#x result=%d msg=%p size=%u source=%#x bytes=%s tid=%u\n",
                (unsigned)q, result, (m ? *(void **)m : 0), size, src, hex,
                (unsigned)syscall(SYS_gettid));
    }
    /* ★ Opt-in direct drive, placed at a point that runs AFTER start-up.
     *
     * Earlier attempts fired too early: processSystemMessage is called while the
     * container is still initialising, and calling the engine from there drove
     * createLayer down to 2 (baseline collapse, R102). This interposition instead
     * sits on the firmware's own receive path, which keeps being called long
     * after Application_Start has settled, and fires only once, only on the entry
     * thread. FORCEDRAW */
    if (getenv("FORCEDRAW")) {
        static int fired;
        /* Order matters: the flag must be set only after the condition holds.
         * The previous form exchanged the flag first, so the very first receive
         * (which happens before Application_Start) consumed the one shot and the
         * drive never fired. Conditional exchange, not exchange-then-test. */
        if (!__atomic_load_n(&fired, __ATOMIC_RELAXED) && g_in_rcv_after_start()
            && !__atomic_exchange_n(&fired, 1, __ATOMIC_RELAXED)) {
            /* ★ Run the drive on its OWN thread instead of inline here.
             *
             * Calling it inline put the draw inside the OSAL receive path, i.e.
             * on a thread that had just been inside osal_rcv_msg_tmo, holding
             * whatever that path holds and with the firmware's own stack. R104
             * showed the draw entering and never returning from exactly that
             * context. If drawFramebuffer needs to send/receive on the same
             * OSAL queue, or needs the engine's UI thread, inline from here is a
             * self-deadlock by construction.
             *
             * So hand the work to a fresh thread: the receive path returns
             * normally and stays usable, and the draw gets a clean stack with no
             * inherited locks. The thread logs its own tid, so the two cases stay
             * distinguishable. FORCEDRAW */
            /* Spawning is disabled: R116 measured pthread_create returning 0 with
             * the entry never running here, so the drive moved to the watchdog
             * thread, which is proven to execute. */
            logline("FORCEDRAW drive moved to watchdog thread\n");
        }
    }
    return result;
}

static int g_in_rcv_after_start(void)
{
    /* Application_Start has been entered and at least one probe has reported
     * progress, i.e. start-up is past its initial phase. */
    return __atomic_load_n(&g_in_start, __ATOMIC_RELAXED) != 0;
}

int osal_snd_sync_direct(unsigned q, void *m)
{
    static int (*real)(unsigned,void*);
    if(!real) real=(int(*)(unsigned,void*))dlsym(RTLD_NEXT,"osal_snd_sync_direct");
    if(g_ssd<20){
        unsigned c=(unsigned)(uintptr_t)__builtin_return_address(0);
        unsigned tid=(unsigned)syscall(SYS_gettid);
        unsigned w0=0,w1=0;
        if(m){ w0=((unsigned*)m)[0]; w1=((unsigned*)m)[1]; }
        Dl_info di; const char *mod="?"; unsigned off=0;
        if(dladdr((void*)c,&di) && di.dli_fname){
            mod=di.dli_fname;
            off=c-(unsigned)(uintptr_t)di.dli_fbase;
        }
        logline("BLK ssd n=%d tid=%u qid=%#x w0=%#x mod=%s off=%#x\n",
                ++g_ssd,tid,q,w0,mod,off);
    }
    if(!real) return -1;
    if(q==0x910042u){
        struct timespec ts; syscall(SYS_clock_gettime,1,&ts);
        unsigned sec=(unsigned)ts.tv_sec, usec=(unsigned)(ts.tv_nsec/1000);
        unsigned *w=(unsigned*)m;
        if(m) memset(m,0,32);
        if(w){ w[5]=0; w[6]=sec; w[7]=usec; }   /* +0x14 状态, +0x18 秒, +0x1c 微秒 */
        if(g_ssd<12) logline("FAKETS qid=%#x -> %u.%06u\n",q,sec,usec);
        return 0;
    }
    return real(q,m);
}
int usleep(useconds_t us)
{
    waitloop_note((unsigned)(uintptr_t)__builtin_return_address(0) & ~1u);
    static int (*real)(useconds_t);
    if(!real) real=(int(*)(useconds_t))dlsym(RTLD_NEXT,"usleep");
    if(g_usl<8) logline("BLK usleep n=%d us=%u\n",++g_usl,(unsigned)us);
    if(!real) return -1;
    return real(us);
}

static int g_reg=0;
int osal_reg_msg_queue_cb(unsigned qid, void *cb, void *arg)
{
    static int (*real)(unsigned,void*,void*);
    if(!real) real=(int(*)(unsigned,void*,void*))dlsym(RTLD_NEXT,"osal_reg_msg_queue_cb");
    if(g_reg<400) logline("REG n=%d qid=%#x cb=%p arg=%p\n",++g_reg,(unsigned)qid,cb,arg);
    if(!real) return -1;
    return real(qid,cb,arg);
}

int osal_snd_sync_msg(unsigned q, void *m)
{
    static int (*real)(unsigned,void*);
    if(!real) real=(int(*)(unsigned,void*))dlsym(RTLD_NEXT,"osal_snd_sync_msg");

    /* Unconditional probe: prove whether THIS function sees 0x8f0313 at all. */
    if(q == 0x8f0313u || q == 0x8f013au){
        static unsigned probe_n;
        unsigned n = __atomic_fetch_add(&probe_n, 1, __ATOMIC_RELAXED);
        if(n < 8)
            logline("SNDMSG_PROBE n=%u qid=%#x m=%p\n", n+1, q, m);
    }

    /* ★ 0x3e014d = CP-side backup queue. In QEMU there is no CP, so nothing ever
     * replies and libbackup retries forever (tens of thousands per run).
     * The sync send is serviced inline, so we can answer it here: write the reply
     * status word at +4 and return success.  (Mirrors chocotan/a7c2-re
     * menu-observe.c: reply status at +4, not +0x14.) */
    if(g_svc_backup() && q == 0x003e014du && m){
        static unsigned bs_n;
        unsigned n = __atomic_fetch_add(&bs_n, 1, __ATOMIC_RELAXED);
        /* PASS THROUGH: let the real sync send run (it will time out), exactly like
         * the run that reached 8394 lines. We only count, we do not fake the reply —
         * faking makes libbackup think the write landed and it stops advancing. */
        int r = real ? real(q, m) : -1;
        if(n < 5 || (n % 5000) == 0)
            logline("BACKUP_PASS qid=0x3e014d n=%u real_ret=%d\n", n+1, r);
        return r;
    }

    /* ★ 0x924127 = sensor GetInh/GetState sync query. Without a reply the
     * CInfraIndctr lazy-init leaves its sub-object dangling and the next
     * indicator log SEGVs. Reply all-zero: status 0, no inhibit flags.
     * (Mirrors chocotan/a7c2-re menu-observe.c, queue written there as 0x80924127.) */
    if(g_svc_sensor() && q == 0x0924127u && m){
        static unsigned sn_n;
        unsigned n = __atomic_fetch_add(&sn_n, 1, __ATOMIC_RELAXED);
        for(unsigned i = 4; i < 40 && i < 64; ++i) ((unsigned char *)m)[i] = 0;
        if(n < 5 || (n % 2000) == 0)
            logline("SENSOR_REPLY qid=0x924127 n=%u\n", n+1);
        return 0;
    }

    if(g_ssm_n<200){
        unsigned c=(unsigned)(uintptr_t)__builtin_return_address(0);
        unsigned tid=(unsigned)syscall(SYS_gettid);
        Dl_info di; const char *mod="?"; unsigned off=0;
        if(dladdr((void*)c,&di) && di.dli_fname){ mod=di.dli_fname; off=c-(unsigned)(uintptr_t)di.dli_fbase; }
        logline("SSM n=%d tid=%u qid=%#x mod=%s off=%#x\n",++g_ssm_n,tid,q,mod,off);
    }
    /* ★ Dump the first few messages on the queue that stalls Application_Start,
     * so we can see what the caller asked for and what it expects back. */
    if(q == 0x014c0424u && m){
        static unsigned q14_n;
        unsigned n = __atomic_fetch_add(&q14_n, 1, __ATOMIC_RELAXED);
        if(n < 6){
            unsigned *w = (unsigned *)m;
            logline("Q14C n=%u w=%08x %08x %08x %08x %08x %08x %08x %08x\n",
                    n+1, w[0],w[1],w[2],w[3],w[4],w[5],w[6],w[7]);
        }
    }
    if(q==0xcf016du){
        if(g_ssd<40) logline("FAKESSM qid=%#x\n",q);
        if(m) memset(m,0,0x20);
        return 0;
    }

    /* ★ The two logging queues have no consumer in this environment (ulogio
     * exits for lack of /dev/mem and /proc/klog). Left alone they fill up and
     * Application_Start stalls on its own log writes. The message content is
     * irrelevant to us, so acknowledge the send and drop it. This is honest:
     * we are discarding log output, not pretending something succeeded. */
    if((q == 0x8f0313u || q == 0x8f013au) && m && ARG_LOG_DROP){
        static unsigned ld_n;
        unsigned n = __atomic_fetch_add(&ld_n, 1, __ATOMIC_RELAXED);
        if(n < 5 || (n % 5000) == 0)
            logline("LOG_DROP qid=%#x n=%u\n", q, n+1);
        return 0;
    }
    if(!real) return -1;
    return real(q,m);
}

int osal_valloc_msg_wait(unsigned q, void **out, unsigned size, unsigned tmo)
{
    static int (*real)(unsigned,void**,unsigned,unsigned);
    if(!real) real=(int(*)(unsigned,void**,unsigned,unsigned))dlsym(RTLD_NEXT,"osal_valloc_msg_wait");

    /* ★ 0x3e014d: libbackup vallocs 112 bytes from the CP-side backup queue.
     * Without a CP this always fails, and the daemon retries forever.
     * The message never enters a kernel queue (the sync send is serviced inline),
     * so a malloc'd buffer carrying the 16-byte osal header (size at -8,
     * source at -0xc) satisfies osal_msg_size()/get_msg_src() exactly.
     * (Mirrors chocotan/a7c2-re menu-observe.c.) */
    if(g_svc_backup() && q == 0x003e014du && out){
        static unsigned ba_n;
        unsigned char *raw = (unsigned char *)calloc(1, 16 + (size ? size : 112));
        if(raw){
            *(unsigned int *)(raw + 8)  = size ? size : 112;  /* size at -8 */
            *(unsigned int *)(raw + 4)  = 0;                  /* source at -0xc */
            *out = raw + 16;
            fake_msg_track(raw, raw + 16);   /* so osal_free_msg frees it correctly */
            unsigned n = __atomic_fetch_add(&ba_n, 1, __ATOMIC_RELAXED);
            if(n < 5 || (n % 5000) == 0)
                logline("BACKUP_ALLOC qid=0x3e014d size=%u n=%u\n", size, n+1);
            return 0;
        }
        return -1024;
    }

    /* ★ 0x924127: libObj sensor family vallocs 40 bytes from the absent 0x8092-node
     * sensor queue. Same fabricated-header trick as the backup queue. */
    if(g_svc_sensor() && q == 0x0924127u && out){
        static unsigned sa_n;
        unsigned char *raw = (unsigned char *)calloc(1, 16 + (size ? size : 40));
        if(raw){
            *(unsigned int *)(raw + 8) = size ? size : 40;
            *(unsigned int *)(raw + 4) = 0;
            *out = raw + 16;
            fake_msg_track(raw, raw + 16);
            unsigned n = __atomic_fetch_add(&sa_n, 1, __ATOMIC_RELAXED);
            if(n < 5 || (n % 2000) == 0)
                logline("SENSOR_ALLOC qid=0x924127 size=%u n=%u\n", size, n+1);
            return 0;
        }
        return -1024;
    }

    if(!real) return -1;
    int r=real(q,out,size,tmo);

    /* ★ 0x8f013a is the ulog (logging) queue. The `ulogio` userspace daemon exits
     * in this environment, so nothing drains it; once the pool fills, every
     * allocation returns -1023 and appFw retries forever at +0x2c2b003, which is
     * exactly what stalls Application_Start. Nobody reads these log messages, so
     * hand out a throwaway buffer instead of failing. */
    if(r && (q == 0x8f013au || q == 0x8f0313u) && out && size && ARG_LOG_DROP){
        unsigned char *raw = (unsigned char *)calloc(1, 16 + size);
        if(raw){
            *(unsigned int *)(raw + 8) = size;
            *(unsigned int *)(raw + 4) = 0;
            *out = raw + 16;
            fake_msg_track(raw, raw + 16);
            static unsigned dn;
            unsigned n = __atomic_fetch_add(&dn, 1, __ATOMIC_RELAXED);
            if(n < 3 || (n % 5000) == 0)
                logline("ULOG_DRAIN n=%u size=%u (queue full; supplying throwaway buffer)\n", n+1, size);
            return 0;
        }
    }

    if(r && g_ssd<80) logline("VALLOCFAIL qid=%#x size=%u ret=%d\n",q,size,r);
    return r;
}

static void log_caller(const char *tag)
{
    unsigned c=(unsigned)(uintptr_t)__builtin_return_address(1);
    Dl_info di; const char *mod="?"; unsigned off=0;
    if(dladdr((void*)c,&di) && di.dli_fname){ mod=di.dli_fname; off=c-(unsigned)(uintptr_t)di.dli_fbase; }
    logline("%s caller=%#x mod=%s off=%#x\n",tag,c,mod,off);
}
void abort(void)
{
    log_caller("ABORT");
    for(;;) syscall(SYS_exit_group, 134);
}
void _exit(int code)
{
    unsigned sp; __asm__ volatile("mov %0, sp":"=r"(sp));
    unsigned *p=(unsigned*)sp;
    int n=0;
    for(int i=0;i<96 && n<8;i++){
        unsigned a=p[i];
        if(a<0x1000) continue;
        Dl_info di;
        if(dladdr((void*)a,&di) && di.dli_fname && di.dli_fbase){
            unsigned off=a-(unsigned)(uintptr_t)di.dli_fbase;
            if(off>0x10000 && off<0x4000000){
                logline("STK a=%#x mod=%s off=%#x\n",a,di.dli_fname,off);
                n++;
            }
        }
    }
    syscall(SYS_exit_group, code);
    for(;;);
}

/* 用 OSAL 自己的注册 API 真正创建缺失队列 */
static int dummy_cb(void *a, void *b, void *c, void *d) { (void)a;(void)b;(void)c;(void)d; return 0; }
__attribute__((constructor)) static void init_queues(void)
{
    int (*reg)(unsigned,void*,void*) = (int(*)(unsigned,void*,void*))dlsym(RTLD_NEXT,"osal_reg_msg_queue_cb");
    if(!reg) return;
    unsigned qids[3]={0x8f0313u,0x8f013au,0xcf016du};
    for(int i=0;i<3;i++){
        int r=reg(qids[i],(void*)dummy_cb,0);
        logline("QCREATE qid=%#x ret=%d\n",qids[i],r);
    }
}



int Backup_reg_cb(void *a, void *b, void *c)
{
    static int (*real)(void*,void*,void*);
    if(!real) real=(int(*)(void*,void*,void*))dlsym(RTLD_NEXT,"Backup_reg_cb");
    unsigned ra=(unsigned)(uintptr_t)__builtin_return_address(0);
    Dl_info di; const char *mod="?"; unsigned off=0;
    if(dladdr((void*)ra,&di) && di.dli_fname){ mod=di.dli_fname; off=ra-(unsigned)(uintptr_t)di.dli_fbase; }
    int r = real ? real(a,b,c) : -99;
    if(g_ssd<80) logline("BREGCB mod=%s off=%#x ret=%d\n",mod,off,r);
    return r;
}

/* ★ Track messages we fabricated so osal_free_msg can release them correctly.
 * The allocator we hand out (calloc + 16-byte osal header) must not be passed
 * to the real OSAL free, which expects a kernel-pool pointer. (chocotan/a7c2-re
 * keeps the same kind of table; without it the heap corrupts later.) */
static void fake_msg_track(void *raw, void *payload)
{
    for (int i = 0; i < FAKE_MSG_SLOTS; ++i) {
        if (!__atomic_load_n(&g_fake_msgs[i].live, __ATOMIC_RELAXED)) {
            g_fake_msgs[i].raw = raw;
            g_fake_msgs[i].payload = payload;
            __atomic_store_n(&g_fake_msgs[i].live, 1, __ATOMIC_RELAXED);
            return;
        }
    }
    /* table full: leak rather than free with the wrong allocator */
}

static void *fake_msg_take(void *payload)
{
    for (int i = 0; i < FAKE_MSG_SLOTS; ++i) {
        if (__atomic_load_n(&g_fake_msgs[i].live, __ATOMIC_RELAXED) &&
            g_fake_msgs[i].payload == payload) {
            void *raw = g_fake_msgs[i].raw;
            __atomic_store_n(&g_fake_msgs[i].live, 0, __ATOMIC_RELAXED);
            return raw;
        }
    }
    return 0;
}

int osal_free_msg(unsigned q, void *m)
{
    static int (*real)(unsigned,void*);
    if(!real) real=(int(*)(unsigned,void*))dlsym(RTLD_NEXT,"osal_free_msg");

    /* Fast path: only the two queues we fabricate for can ever own one of our
     * buffers. Everything else goes straight to the real allocator, untouched. */
    if (m && (q == 0x08f013au || (g_svc_backup() && q == 0x003e014du) || (g_svc_sensor() && q == 0x0924127u))) {
        void *raw = fake_msg_take(m);
        if (raw) { free(raw); return 0; }
    }
    if (real) return real(q, m);
    return -1;
}

/* ★ Probe the two GRM drawing entry points directly. If these are never called,
 * the engine never asked for a single pixel (the a7c2 report's "only a clear
 * screen arrives" stage). Count and log the first few, then pass through. */
static int (*g_real_fillrect)(unsigned, unsigned, void *, void *);
static int (*g_real_rectblit)(unsigned, unsigned, void *, void *);
static unsigned g_fill_n, g_blit_n;

int GRM_gpermFillrect(unsigned a, unsigned b, void *c, void *d)
{
    if(!g_real_fillrect) g_real_fillrect = (int(*)(unsigned,unsigned,void*,void*))dlsym(RTLD_NEXT,"GRM_gpermFillrect");
    unsigned n = __atomic_fetch_add(&g_fill_n, 1, __ATOMIC_RELAXED);
    if(n < 20) logline("GRMFILL n=%u a=%u b=%u c=%p\n", n+1, a, b, c);
    return g_real_fillrect ? g_real_fillrect(a,b,c,d) : -1;
}

int GRM_gpermRectblit(unsigned a, unsigned b, void *c, void *d)
{
    if(!g_real_rectblit) g_real_rectblit = (int(*)(unsigned,unsigned,void*,void*))dlsym(RTLD_NEXT,"GRM_gpermRectblit");
    unsigned n = __atomic_fetch_add(&g_blit_n, 1, __ATOMIC_RELAXED);
    if(n < 20) logline("GRMBLIT n=%u a=%u b=%u c=%p\n", n+1, a, b, c);
    return g_real_rectblit ? g_real_rectblit(a,b,c,d) : -1;
}

/* ★ Locate the first call inside Application_Start that never returns.
 * Application_Start (appFw 0x54d580) makes six sequential calls:
 *   0x92559c, 0x54d154, 0x54d2b0, 0x54f664 (the frame loop), 0x923c84, 0x54d538
 * Rather than hooking each (their signatures are unknown), we instrument the
 * *call sites*: replace each 4-byte `bl/blx` with a `bl` into a shim that logs,
 * then performs the original call and logs the return. A missing RETURN is the
 * answer. Each site is patched at runtime, then restored. */
static void *g_appfw;
static void appfw_locate(void)
{
    Dl_info di;
    if (!g_appfw && dladdr((void *)&real_start, &di) && di.dli_fbase)
        g_appfw = di.dli_fbase;
}

/* Sites inside Application_Start, as offsets from its entry. */

/* Read the Thumb-2 BL/BLX target at a site so we can name the real callee. */

static int as_reported;

/* ★ Patched call-site tracing for Application_Start.
 *
 * The six sequential calls are each a 32-bit Thumb BL. Rather than decode the
 * encoding (error-prone), we patch each call site in place: save the original
 * halfwords, overwrite with a BL to a per-site shim. The shim logs "step N
 * entered", executes the *original* instruction semantics by calling the target
 * the caller intended, logs "step N returned", then the patched BL's own return
 * address continues the caller. Any step whose RETURN never prints is the one
 * that blocks.
 *
 * Because injecting a shim requires knowing the ABI, we instead take the much
 * cheaper route: patch each call site to a shim that ONLY logs and then jumps
 * back to re-execute the original instruction from a restored copy. That keeps
 * the ABI untouched — the shim re-enters the original call normally.
 */
static void *as_maps_base(const char *soname)
{
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512];
    void *base = 0;
    while (fgets(line, sizeof line, f)) {
        if (strstr(line, soname)) {
            unsigned lo = 0;
            if (sscanf(line, "%x-", &lo) == 1) { base = (void *)(unsigned long)lo; break; }
        }
    }
    fclose(f);
    return base;
}

/* ★ Dump the raw instruction words at each Application_Start call site, together
 * with the resolved appFw base from /proc/self/maps. Decoding happens offline on
 * the host, where we can try both ARM and Thumb and cross-check against objdump. */
static void as_dump_sites(void)
{
    void *base = as_maps_base("appFw.so");
    logline("ASDUMP appfw_base=%p\n", base);
    if (!base) return;
    unsigned entry = (unsigned)(unsigned long)base + 0x54d580u;
    logline("ASDUMP entry=%#x (expect Application_Start)\n", entry);
    /* entry offsets of the six call instructions */
    static const unsigned offs[] = { 0x10, 0x1a, 0x26, 0x2c, 0x30, 0x36 };
    for (unsigned i = 0; i < sizeof(offs)/sizeof(offs[0]); ++i) {
        unsigned at = entry + offs[i];
        unsigned w0 = *(volatile unsigned *)at;
        unsigned w1 = *(volatile unsigned *)(at + 4);
        logline("ASDUMP site=%u off=%#x at=%#x w0=%08x w1=%08x\n",
                i, offs[i], at, w0, w1);
    }
}

/* ★ Call the six stages of Application_Start ourselves, one at a time, logging
 * before and after each. Application_Start normally runs all six and returns;
 * by doing it here we learn which stage never comes back. Offsets are from
 * appFw.so, verified against both objdump and the runtime instruction words.
 *
 * Stage 0 and 4 are cross-library BLX (targets outside appFw) and cannot be
 * reached by simple base+offset, so we skip them and note that. */
static unsigned long g_as_stage_mask = 0xFFFFFFFFu;

static void as_run_stages(unsigned long a, unsigned long b, unsigned long c, unsigned long d)
{
    void *base = as_maps_base("appFw.so");
    if (!base) { logline("ASSTEP no appfw base\n"); return; }
    logline("ASSTEP base=%p\n", base);

    struct { unsigned off; const char *name; } stages[] = {
        { 0x54d154, "stage1" },
        { 0x54d2b0, "stage2" },
        { 0x54f664, "stage3-frame-loop" },
        { 0x54d538, "stage5" },
    };
    for (unsigned i = 0; i < sizeof(stages)/sizeof(stages[0]); ++i) {
        if (!(g_as_stage_mask & (1u << i))) continue;
        void (*fn)(unsigned long,unsigned long,unsigned long,unsigned long) =
            (void (*)(unsigned long,unsigned long,unsigned long,unsigned long))
            ((char *)base + stages[i].off);
        logline("ASSTEP %s ENTER off=%#x fn=%p\n", stages[i].name, stages[i].off, fn);
        fn(a, b, c, d);
        logline("ASSTEP %s RETURNED\n", stages[i].name);
    }
    logline("ASSTEP all-done\n");
}

/* ★ Call-site probing by instruction patch, done safely.
 *
 * Application_Start (appFw +0x54d580) is six straight-line calls. We cannot
 * simply call the callees ourselves — their arguments come from the caller's
 * stack frame (see the failed AS_STAGES attempt). Instead we patch each call
 * site in place: replace the 4-byte BL with a BL to a small ARM-mode stub that
 * logs, then executes the SAME original call and returns to the original
 * next-PC. The caller's registers and stack are untouched, so the callee sees
 * exactly what it would have seen.
 *
 * A stub is 3 ARM instructions:
 *     push {lr}
 *     ldr  lr, =orig_target   ; via literal pool
 *     blx  lr
 *     ... log via a helper ...
 *     pop  {pc}
 * That is too much to inline. Simpler and equally informative: patch the site
 * to BL into a C function with __attribute__((naked))-like behaviour is not
 * available portably either.
 *
 * Pragmatic alternative used here: leave the code alone and instead probe from
 * the *callee* side, but only for callees we can safely hook by symbol. The two
 * appFw-internal stages (0x54d154, 0x54d2b0, 0x54f664, 0x54d538) are reachable
 * as plain pointers; we wrap them so they log entry/exit when the firmware
 * itself calls them... which is exactly what a breakpoint does.
 *
 * => Use the debugger. This function exists only to print the resolved
 *    addresses for the debugger session. */
static void as_probe_print(void)
{
    void *base = as_maps_base("appFw.so");
    if (!base) { logline("ASCP no base\n"); return; }
    logline("ASCP base=%p\n", base);
    static const unsigned offs[] = { 0x54d590, 0x54d59a, 0x54d5a6, 0x54d5ac, 0x54d5b0, 0x54d5b6 };
    static const char *names[] = { "site0","site1","site2","site3","site4","site5" };
    for (unsigned i = 0; i < 6; ++i)
        logline("ASCP %s addr=%p\n", names[i], (char *)base + offs[i]);
}

/* ★ Log-queue drain: give the two logging queues a real consumer.
 *
 * `ulogio` (the userspace logging daemon) exits in this environment because it
 * needs /dev/mem and /proc/klog. With nobody draining 0x8f013a and 0x8f0313 the
 * pool fills up, every allocation fails, and Application_Start blocks on its very
 * first log write — which is why the drawing entry points are never reached.
 *
 * We do not care about the log contents, so we register a callback that simply
 * returns 0 and let the OSAL free the message the normal way. The callback
 * signature matches the firmware's: (msg, arg).
 */
static unsigned g_log_sink_a, g_log_sink_b;
static int log_sink_cb(void *msg, void *arg)
{
    (void)msg; (void)arg;
    /* Returning 0 tells the OSAL the message was consumed. */
    return 0;
}

static void log_sinks_install(void)
{
    int (*reg)(unsigned, void *, void *) =
        (int (*)(unsigned, void *, void *))dlsym(RTLD_NEXT, "osal_reg_msg_queue_cb");
    if (!reg) { logline("LOG_SINK no reg fn\n"); return; }
    int r1 = reg(0x8f013au, (void *)log_sink_cb, 0);
    int r2 = reg(0x8f0313u, (void *)log_sink_cb, 0);
    logline("LOG_SINK install qid=0x8f013a ret=%d  qid=0x8f0313 ret=%d\n", r1, r2);
}

__attribute__((constructor)) static void log_sinks_maybe(void)
{
    if (getenv("LOG_SINK")) {
        /* Register once the OSAL library is actually up. */
        for (int i = 0; i < 100; ++i) {
            void *h = dlopen("libosal_uipc.so", RTLD_NOLOAD | RTLD_LAZY);
            if (h) { dlclose(h); break; }
            usleep(100000);
        }
        log_sinks_install();
    }
}

/* ★ The u_osal_* layer is the real userspace entry point.
 *
 * `osal_snd_sync_msg` (0x3ec9) is a 16-byte trampoline that branches to
 * `u_osal_snd_sync_msg` (0x2605) whenever bit 15 of the queue id is clear —
 * which is true for every queue we have seen. The application image also
 * dispatches some call sites through tables, so hooking only the exported
 * wrapper misses most calls; hooking this layer catches both direct and
 * indirect callers.
 */
static int (*g_real_u_snd_sync)(unsigned, void *);
static int (*g_real_u_valloc)(unsigned, void **, unsigned, unsigned);
static int (*g_real_u_free)(unsigned, void *);
static unsigned g_u_sync_n, g_u_valloc_n;

int u_osal_snd_sync_msg(unsigned q, void *m)
{
    if (!g_real_u_snd_sync)
        g_real_u_snd_sync = (int (*)(unsigned, void *))dlsym(RTLD_NEXT, "u_osal_snd_sync_msg");
    unsigned n = __atomic_fetch_add(&g_u_sync_n, 1, __ATOMIC_RELAXED);
    if (n < 20)
        logline("U_SYNC n=%u qid=%#x\n", n + 1, q);
    if (!g_real_u_snd_sync) return -1;
    return g_real_u_snd_sync(q, m);
}

int u_osal_valloc_msg_wait(unsigned q, void **out, unsigned size, unsigned tmo)
{
    if (!g_real_u_valloc)
        g_real_u_valloc = (int (*)(unsigned, void **, unsigned, unsigned))
                          dlsym(RTLD_NEXT, "u_osal_valloc_msg_wait");
    unsigned n = __atomic_fetch_add(&g_u_valloc_n, 1, __ATOMIC_RELAXED);
    if (n < 20)
        logline("U_VALLOC n=%u qid=%#x size=%u\n", n + 1, q, size);
    if (!g_real_u_valloc) return -1;
    return g_real_u_valloc(q, out, size, tmo);
}

int u_osal_free_msg(unsigned q, void *m)
{
    if (!g_real_u_free)
        g_real_u_free = (int (*)(unsigned, void *))dlsym(RTLD_NEXT, "u_osal_free_msg");
    if (!g_real_u_free) return -1;
    return g_real_u_free(q, m);
}

/* ★ The real drawing entry points. appFw imports these from libObj:
 *   ux::gfxeng::GraphicsEngine::drawFramebuffer()   (250 bytes)
 *   ux::wgtsys::WidgetSystem::notifyDraw()          (78 bytes)
 *   ux::gfxeng::TextSprite::drawText()              (76 bytes)
 *   ux::wgtsys::WidgetBase::onDrawNotice()
 * Earlier I hooked GRM_gpermFillrect, which nothing in this tree calls —
 * wrong target. These are the ones the application actually uses.
 *
 * C++ member functions take `this` in r0; we do not need the other args,
 * so a plain (void*) signature is safe for logging and forwarding. */
static void *(*g_real_drawfb)(void *);
static void *(*g_real_notifydraw)(void *);
static unsigned g_drawfb_n, g_notifydraw_n;

void *__dis__ZN2ux6gfxeng14GraphicsEngine15drawFramebufferEv(void *self)
{
    static const char *sym = "_ZN2ux6gfxeng14GraphicsEngine15drawFramebufferEv";
    if (!g_real_drawfb) g_real_drawfb = (void *(*)(void *))dlsym(RTLD_NEXT, sym);
    unsigned n = __atomic_fetch_add(&g_drawfb_n, 1, __ATOMIC_RELAXED);
    if (n < 20) logline("DRAW_PROBE drawFramebuffer n=%u self=%p\n", n + 1, self);
    return g_real_drawfb ? g_real_drawfb(self) : 0;
}

void *_ZN2ux6wgtsys12WidgetSystem10notifyDrawEv(void *self)
{
    static const char *sym = "_ZN2ux6wgtsys12WidgetSystem10notifyDrawEv";
    if (!g_real_notifydraw) g_real_notifydraw = (void *(*)(void *))dlsym(RTLD_NEXT, sym);
    unsigned n = __atomic_fetch_add(&g_notifydraw_n, 1, __ATOMIC_RELAXED);
    if (n < 20) logline("DRAW_PROBE notifyDraw n=%u self=%p\n", n + 1, self);
    return g_real_notifydraw ? g_real_notifydraw(self) : 0;
}

/* ★ Read the GraphicsEngine singleton pointer directly from libObj's GOT.
 *
 * libObj.so's .got spans 0x354d60..0x357738. sys_getInstance() at 0x101e70
 * resolves to: r3 = pc + 0x252ee8 (= 0x354d60, the .got base), then
 * r3 += 0x2730 → 0x357490, then r0 = *(r3). So the singleton pointer lives at
 * libObj_base + 0x357490.
 *
 * If that word is non-zero once the engine is up, the GraphicsEngine object
 * exists and only the *trigger* for drawFramebuffer is missing. If it is zero,
 * the engine never finished constructing and that is the real blocker.
 */
static void ge_probe(void)
{
    void *base = lib_text_base("libObj.so");
    if (!base) { logline("GE_PROBE no libobj base\n"); return; }
    volatile unsigned *slot = (volatile unsigned *)((char *)base + 0x357490u);
    logline("GE_PROBE base=%p slot=%p value=%#010x\n", base, (void *)slot, *slot);
}

/* ★ Dump the widget list using the structure's OWN traversal rules.
 *
 * The previous attempt used the wrong pointers and therefore reported an empty
 * list that was not empty. libObj's own code settles the layout:
 *
 *   0x1061ac (the gate notifyDraw calls first):
 *       ldr  r3,[r0,#12]      ; node = *(head + 12)
 *       adds r0,#16
 *       cmp  r3,r0
 *       movne r0,r3 / moveq r0,#0   ; EMPTY  <=>  *(head+12) == head+16
 *
 *   0xdeb30 (the "next node" accessor):
 *       ldr  r3,[r0,#0]       ; next = *(node + 0)   (not node + 4)
 *       cbnz r3, return
 *
 * So: only the +12 / +16 comparison decides emptiness, and the successor lives
 * at node+0. Every value below is a plain read; nothing is called. WLDUMP */
static void wl_dump(const char *tag)
{
    unsigned long ps = (unsigned long)__atomic_load_n(&g_real_psm, __ATOMIC_RELAXED);
    unsigned long lb = ps ? ((ps & ~1UL) - 0xe5610UL) : 0;
    if (!lb) { logline("WLDUMP %s no libobj base\n", tag); return; }

    unsigned long slot = lb + 0x356908UL;
    unsigned long single = *(volatile unsigned long *)(uintptr_t)slot;
    unsigned long inner = single ? *(volatile unsigned long *)(uintptr_t)single : 0;
    if (!inner) { logline("WLDUMP %s no singleton\n", tag); return; }

    unsigned long head = inner + 0x4b4UL;
    unsigned long w8 = *(volatile unsigned long *)(uintptr_t)(head + 8);
    unsigned long w12 = *(volatile unsigned long *)(uintptr_t)(head + 12);
    int empty = (w12 == head + 16);
    logline("WLDUMP %s inner=%#lx head=%#lx [8]=%#lx [12]=%#lx expect16=%#lx %s\n",
            tag, inner, head, w8, w12, head + 16, empty ? "EMPTY" : "NON-EMPTY");
    if (empty) return;

    /* Inspect exactly ONE candidate node, then stop.
     *
     * The previous version chased *(node+0) as a successor and very quickly
     * walked into text and other mappings -- node[2] and node[3] were plainly
     * instruction bytes -- which faulted and knocked startup down to
     * createLayer=2. Reading a single node is enough to learn whether it holds
     * a vtable that resolves into libObj, and it cannot run away. Only the
     * first candidate's first four words are read, all inside the object that
     * the gate itself dereferenced. WLDUMP */
    {
        unsigned long node = w12;
        unsigned long q0 = *(volatile unsigned long *)(uintptr_t)node;
        unsigned long q1 = *(volatile unsigned long *)(uintptr_t)(node + 4);
        unsigned long q2 = *(volatile unsigned long *)(uintptr_t)(node + 8);
        unsigned long q3 = *(volatile unsigned long *)(uintptr_t)(node + 12);
        const char *verdict = "unclassified";
        if (q0 >= lb && q0 < lb + 0x3d74f0UL) verdict = "vptr-in-libObj";
        else if (q0 == head + 0 || q1 == head || q2 == head) verdict = "sentinel-shaped";
        logline("WLDUMP %s node=%#lx q0=%#lx q1=%#lx q2=%#lx q3=%#lx %s\n",
                tag, node, q0, q1, q2, q3, verdict);
        logline("WLDUMP %s head-relative node=%#lx head=%#lx\n",
                tag, node - head, head);
    }
}


/* ★ Inline-hook the two GRM raster entry points, the way a7c2-re does.
 *
 * Symbol interposition cannot see libObj's own internal BL calls, so the
 * interposed GRM_gpermFillrect counter has always read zero for a reason that
 * has nothing to do with whether rasterisation ran. a7c2-re solves the same
 * class of problem by patching the loaded text page in place: copy the entry
 * bytes into an anonymous trampoline, redirect the entry through it, and put
 * the page back to RX afterwards. libnativeentry's ne_hook is the ARM32
 * version of exactly that, and this code is its transplant.
 *
 * Preconditions, all checked before anything is written:
 *   - the 12 bytes at the entry must match the pinned prologue exactly
 *     (checked against libObj f079...; a mismatch means a different build and
 *      the hook is abandoned rather than guessed);
 *   - the copied prologue must be PC-independent. GRM_gpermFillrect starts
 *     stmdb/sub/mov/add/movs/mov and GRM_gpermRectblit likewise, so the 12
 *     bytes move whole. onDrawNotice is deliberately NOT hooked here: its
 *     first instructions include `b.w 108b14`, which is PC-relative and would
 *     target the wrong place once copied.
 *
 * Nothing is called; the wrappers only count and forward. GRMHOOK */
static unsigned grm_jump(unsigned char *where, uintptr_t target)
{
    uintptr_t literal = ((uintptr_t)where + 7u) & ~(uintptr_t)3u;
    uintptr_t pc = ((uintptr_t)where + 4u) & ~(uintptr_t)3u;
    uint16_t instruction[2] = {0xf8df, (uint16_t)(0xf000u | (literal - pc))};
    uint16_t nop = 0xbf00;
    uint32_t address = (uint32_t)target;
    memcpy(where, instruction, 4);
    if (literal != (uintptr_t)where + 4u) memcpy(where + 4, &nop, 2);
    memcpy((void *)literal, &address, 4);
    return (unsigned)(literal + 4u - (uintptr_t)where);
}

static int grm_hook(uintptr_t address, const unsigned char expected[12],
                    void *replacement, void **original)
{
    if (memcmp((void *)address, expected, 12)) return -1;
    unsigned char *code = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) return -2;
    memcpy(code, (void *)address, 12);
    grm_jump(code + 12, (address + 12) | 1u);
    __builtin___clear_cache((char *)code, (char *)code + 20);
    uintptr_t callable = (uintptr_t)code | 1u;
    memcpy(original, &callable, sizeof callable);
    uintptr_t page = address & ~(uintptr_t)4095;
    unsigned bytes = (address & 3u) ? 10u : 8u;
    size_t span = ((address + bytes + 4095u) & ~(uintptr_t)4095u) - page;
    if (mprotect((void *)page, span, PROT_READ | PROT_WRITE | PROT_EXEC)) return -3;
    unsigned written = grm_jump((void *)address, (uintptr_t)replacement);
    __builtin___clear_cache((char *)address, (char *)address + written);
    if (mprotect((void *)page, span, PROT_READ | PROT_EXEC)) return -4;
    return 0;
}

static void *(*g_grm_fill_orig)(void *);
static void *(*g_grm_blit_orig)(void *);
static unsigned long g_grm_fill_n, g_grm_blit_n;

static void *grm_fill_wrapper(void *a0)
{
    unsigned long n = __atomic_fetch_add(&g_grm_fill_n, 1, __ATOMIC_RELAXED);
    if (n < 20)
        logline("GRMHOOK fillrect n=%lu tid=%u caller=%p\n",
                n + 1, (unsigned)syscall(SYS_gettid),
                __builtin_return_address(0));
    return g_grm_fill_orig ? g_grm_fill_orig(a0) : 0;
}

static void *grm_blit_wrapper(void *a0)
{
    unsigned long n = __atomic_fetch_add(&g_grm_blit_n, 1, __ATOMIC_RELAXED);
    if (n < 20)
        logline("GRMHOOK rectblit n=%lu tid=%u caller=%p\n",
                n + 1, (unsigned)syscall(SYS_gettid),
                __builtin_return_address(0));
    return g_grm_blit_orig ? g_grm_blit_orig(a0) : 0;
}

static void grm_hook_install(void)
{
    static int state;
    if (__atomic_exchange_n(&state, 1, __ATOMIC_RELAXED)) return;

    unsigned long ps = (unsigned long)__atomic_load_n(&g_real_psm, __ATOMIC_RELAXED);
    unsigned long lb = ps ? ((ps & ~1UL) - 0xe5610UL) : 0;
    if (!lb) {
        logline("GRMHOOK no libobj base; not installed\n");
        return;
    }

    static const unsigned char pin_fill[12] = {
        0x2d,0xe9,0xf0,0x4f,0x99,0xb0,0x14,0x46,0x04,0xaf,0x30,0x22 };
    static const unsigned char pin_blit[12] = {
        0x2d,0xe9,0xf0,0x4f,0xbb,0xb0,0x14,0x46,0x04,0xaf,0x9c,0x22 };

    int r1 = grm_hook(lb + 0x117b4cUL, pin_fill, (void *)grm_fill_wrapper,
                      (void **)&g_grm_fill_orig);
    logline("GRMHOOK fillrect install=%d orig=%p\n", r1, (void *)g_grm_fill_orig);

    int r2 = grm_hook(lb + 0x117c86UL, pin_blit, (void *)grm_blit_wrapper,
                      (void **)&g_grm_blit_orig);
    logline("GRMHOOK rectblit install=%d orig=%p\n", r2, (void *)g_grm_blit_orig);
}




/* ★ Observe Lz77Decoder::decodeImpl without changing what it does.
 *
 * The entry at libObj+0x10bff8 begins with ten PC-independent bytes and then a
 * PC-relative `bl 1095b4`. So a ten-byte trampoline is the largest safe copy:
 * those ten bytes are executed from the trampoline and control returns to
 * entry+10, leaving the original `bl` in place, untouched, to run at its true
 * address. Copying twelve bytes (the GRM hook's size) would have moved that `bl`
 * and redirected it to a wrong target.
 *
 * Offsets are NOT the ones a7c2-re records for its build. Reading this function's
 * own body shows the fields it consumes:
 *   ldr.w fp,[r5,#12]   object+0x0c   format-like, compared against 768..772
 *   ldrh  r0,[r5,#16]   object+0x10
 *   ldrh.w r9,[r5,#18]  object+0x12
 *   ldr   r2,[r5,#20]   object+0x14
 * a7c2-re's software decoder reads +0x18/+0x1c/+0x1e/+0x20 instead, i.e. 0x0c
 * further along. Rather than assume either layout, this records a flat snapshot
 * of the object and the values below are read from measurement.
 *
 * The wrapper only logs and forwards. LZPROBE */
static unsigned char *g_lz_tramp;
static unsigned long (*g_lz_orig)(void *);
static unsigned g_lz_calls;

static void *lz_wrapper(void *object)
{
    unsigned n = __atomic_fetch_add(&g_lz_calls, 1, __ATOMIC_RELAXED);
    if (n < 6 && object) {
        const unsigned char *p = object;
        logline("LZPROBE call=%u obj=%p "
                "w04=%#x w08=%#x w0c=%#x h10=%#x h12=%#x w14=%#x w18=%#x w1c=%#x w20=%#x\n",
                n + 1, object,
                *(const unsigned *)(p + 4), *(const unsigned *)(p + 8),
                *(const unsigned *)(p + 0x0c),
                *(const unsigned short *)(p + 0x10), *(const unsigned short *)(p + 0x12),
                *(const unsigned *)(p + 0x14),
                *(const unsigned *)(p + 0x18), *(const unsigned *)(p + 0x1c),
                *(const unsigned *)(p + 0x20));
    } else if (n == 6) {
        logline("LZPROBE limit=6 (later calls still forwarded)\n");
    }
    return g_lz_orig ? (void *)g_lz_orig(object) : 0;
}

static void lz_probe_install(void)
{
    static int state;
    if (__atomic_exchange_n(&state, 1, __ATOMIC_RELAXED)) return;

    unsigned long ps = (unsigned long)__atomic_load_n(&g_real_psm, __ATOMIC_RELAXED);
    unsigned long lb = ps ? ((ps & ~1UL) - 0xe5610UL) : 0;
    if (!lb) { logline("LZPROBE no libobj base\n"); return; }

    static const unsigned char pin[12] = {
        0x2d,0xe9,0xf0,0x4f,0x8d,0xb0,0x00,0xaf,0x05,0x46,0xfd,0xf7 };
    const unsigned pro_len = 10;
    uintptr_t addr = lb + 0x10bff8UL;

    if (memcmp((void *)addr, pin, pro_len)) {
        logline("LZPROBE prologue mismatch; not installed\n");
        return;
    }
    unsigned char *code = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) { logline("LZPROBE mmap failed\n"); return; }
    memcpy(code, (void *)addr, pro_len);
    grm_jump(code + pro_len, (addr + pro_len) | 1u);
    __builtin___clear_cache((char *)code, (char *)code + 32);
    g_lz_tramp = code;
    unsigned long callable = (unsigned long)code | 1u;
    memcpy(&g_lz_orig, &callable, sizeof callable);

    uintptr_t page = addr & ~(uintptr_t)4095;
    size_t span = ((addr + 8u + 4095u) & ~(uintptr_t)4095u) - page;
    if (mprotect((void *)page, span, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        logline("LZPROBE mprotect rw failed\n");
        return;
    }
    unsigned written = grm_jump((void *)addr, (uintptr_t)lz_wrapper);
    __builtin___clear_cache((char *)addr, (char *)addr + written);
    if (mprotect((void *)page, span, PROT_READ | PROT_EXEC))
        logline("LZPROBE mprotect rx failed\n");
    logline("LZPROBE installed addr=%#lx tramp=%p orig=%#lx jump_bytes=%u\n",
            (unsigned long)addr, (void *)code, callable, written);
}



/* ★ Observe the real draw entry by hooking its code, not its symbol.
 *
 * The existing probe tried to reach drawFramebuffer through dlsym and never
 * produced its "ARGTRACE hooked drawFramebuffer" line, so the interposer was
 * never installed. The resulting zero count was then read as "the firmware never
 * draws", which that measurement could not support: an uninstalled hook proves
 * nothing.
 *
 * ZV-1M2's libObj does export it:
 *   _ZN2ux6gfxeng14GraphicsEngine15drawFramebufferEv  at libObj+0xe565b, 250 B
 * and its first twelve bytes are PC-independent:
 *   e92d41f0  stmdb sp!,{r4,r5,r6,r7,r8,lr}
 *   4604      mov   r4,r0
 *   b082      sub   sp,#8
 *   f1040574  add.w r5,r4,#0x74
 *   af00      add   r7,sp,#0
 * so grm_hook's twelve-byte trampoline applies unchanged, with control returning
 * to entry+12. DFBHOOK */
static unsigned char *g_dfb_tramp;
static unsigned long (*g_dfb_orig)(void *);
static unsigned g_dfb_calls;

static void *dfb_wrapper(void *self)
{
    unsigned n = __atomic_fetch_add(&g_dfb_calls, 1, __ATOMIC_RELAXED);
    if (n < 6)
        logline("DFBHOOK call=%u self=%p tid=%u f23a=%#x\n",
                n + 1, self, (unsigned)syscall(SYS_gettid),
                self ? *(unsigned *)((char *)self + 0x23a) : 0u);
    else if (n == 6)
        logline("DFBHOOK limit=6 (later calls still forwarded)\n");
    return (void *)(g_dfb_orig ? (long)g_dfb_orig(self) : 0);
}

static void dfb_hook_install(void)
{
    static int state;
    if (__atomic_exchange_n(&state, 1, __ATOMIC_RELAXED)) return;
    unsigned long ps = (unsigned long)__atomic_load_n(&g_real_psm, __ATOMIC_RELAXED);
    unsigned long lb = ps ? ((ps & ~1UL) - 0xe5610UL) : 0;
    if (!lb) { logline("DFBHOOK no libobj base\n"); return; }

    static const unsigned char pin[12] = {
        0x2d,0xe9,0xf0,0x41,0x04,0x46,0x82,0xb0,0x04,0xf1,0x74,0x05 };
    const unsigned pro_len = 12;
    uintptr_t addr = lb + 0xe565aUL;

    if (memcmp((void *)addr, pin, pro_len)) {
        logline("DFBHOOK prologue mismatch at %#lx; not installed\n", (unsigned long)addr);
        return;
    }
    unsigned char *code = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) { logline("DFBHOOK mmap failed\n"); return; }
    memcpy(code, (void *)addr, pro_len);
    grm_jump(code + pro_len, (addr + pro_len) | 1u);
    __builtin___clear_cache((char *)code, (char *)code + 32);
    g_dfb_tramp = code;
    unsigned long callable = (unsigned long)code | 1u;
    memcpy(&g_dfb_orig, &callable, sizeof callable);

    uintptr_t page = addr & ~(uintptr_t)4095;
    size_t span = ((addr + 8u + 4095u) & ~(uintptr_t)4095u) - page;
    if (mprotect((void *)page, span, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        logline("DFBHOOK mprotect rw failed\n"); return;
    }
    unsigned written = grm_jump((void *)addr, (uintptr_t)dfb_wrapper);
    __builtin___clear_cache((char *)addr, (char *)addr + written);
    if (mprotect((void *)page, span, PROT_READ | PROT_EXEC))
        logline("DFBHOOK mprotect rx failed\n");
    logline("DFBHOOK installed addr=%#lx tramp=%p jump_bytes=%u\n",
            (unsigned long)addr, (void *)code, written);
}



/* ★ Print every module's load base once, from /proc/self/maps.
 *
 * Address arithmetic over fifty rounds kept going wrong because the bases had to
 * be inferred from incidental log lines: a GOT slot minus a .plt offset, or a hook
 * address minus a known entry offset. Each inference is a chance for a wrong
 * constant, and one was indeed wrong (the appFw base derived that way does not
 * match the addresses actually executed).
 *
 * The maps file states the answer directly. This reads it once, prints the base
 * of every mapped file, and touches nothing else: no hook, no patch, no guest
 * state. Pure observation, so it cannot perturb the boot. BASEPRINT */
static void base_print(void)
{
    static int done;
    if (__atomic_exchange_n(&done, 1, __ATOMIC_RELAXED)) return;
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) { logline("BASEPRINT no maps\n"); return; }
    char line[512];
    char seen[64][40];
    int nseen = 0;
    while (fgets(line, sizeof line, f)) {
        char *slash = strchr(line, '/');
        if (!slash) continue;
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        /* Base = start of the first permission row for this path; the loader
         * always maps the object's lowest segment first, so the first row seen
         * for a path is its base. */
        int dup = 0;
        for (int i = 0; i < nseen; ++i)
            if (strcmp(seen[i], slash) == 0) { dup = 1; break; }
        if (dup) continue;
        if (nseen < 64) snprintf(seen[nseen++], sizeof seen[0], "%s", slash);
        logline("BASEPRINT %s base=%#lx\n", slash, (unsigned long)strtoul(line, 0, 16));
    }
    fclose(f);
    logline("BASEPRINT done modules=%d\n", nseen);
}



/* ★ Observe the render container call from inside Application_Start.
 *
 * Application_Start is 72 bytes: 54d590 A, 54d59a B, 54d5a6 C, 54d5ac render,
 * 54d5b0 D, 54d5b6 E. Call C (0x54d2b0) was measured entering and returning
 * normally, so control reaches the byte just before the render call. The only
 * remaining question is whether 0x54f664 itself runs.
 *
 * It is called once per Application_Start (not per frame), so hooking it does not
 * carry the per-frame timing cost that sank the earlier hot-path attempt.
 *
 * Its first ten bytes are PC-independent:
 *   e92d4ff0 stmdb.w sp!,{r4-r11,lr}
 *   f5ad6d00 sub.w  sp,sp,#0x...
 *   b087     sub    sp,#28
 * and the next instruction starts at +10, so the trampoline copies ten bytes and
 * returns to entry+10. RENDERENTRY */
static unsigned char *g_re_tramp;
static unsigned long (*g_re_orig)(void *);
static unsigned g_re_enter, g_re_leave;

static void *re_wrapper(void *a0)
{
    unsigned n = __atomic_fetch_add(&g_re_enter, 1, __ATOMIC_RELAXED);
    if (n < 3) logline("RENDERENTRY enter=%u a0=%p tid=%u\n",
                       n + 1, a0, (unsigned)syscall(SYS_gettid));
    void *r = g_re_orig ? (void *)(long)g_re_orig(a0) : 0;
    unsigned m = __atomic_fetch_add(&g_re_leave, 1, __ATOMIC_RELAXED);
    if (m < 3) logline("RENDERENTRY leave=%u ret=%p\n", m + 1, r);
    return r;
}

static void re_hook_install(void)
{
    static int state;
    if (__atomic_exchange_n(&state, 1, __ATOMIC_RELAXED)) return;
    unsigned long st = (unsigned long)__atomic_load_n(&g_real_start_off, __ATOMIC_RELAXED);
    unsigned long ab = st ? ((st & ~1UL) - 0x54d580UL) : 0;
    if (!ab) { logline("RENDERENTRY no appfw base\n"); return; }

    static const unsigned char pin[12] = {
        0x2d,0xe9,0xf0,0x4f,0xad,0xf5,0x00,0x6d,0x87,0xb0,0xe2,0x4d };
    const unsigned pro_len = 10;
    uintptr_t addr = ab + 0x54f664UL;

    if (memcmp((void *)addr, pin, pro_len)) {
        logline("RENDERENTRY prologue mismatch at %#lx\n", (unsigned long)addr);
        return;
    }
    unsigned char *code = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) { logline("RENDERENTRY mmap failed\n"); return; }
    memcpy(code, (void *)addr, pro_len);
    grm_jump(code + pro_len, (addr + pro_len) | 1u);
    __builtin___clear_cache((char *)code, (char *)code + 32);
    g_re_tramp = code;
    unsigned long callable = (unsigned long)code | 1u;
    memcpy(&g_re_orig, &callable, sizeof callable);

    uintptr_t page = addr & ~(uintptr_t)4095;
    size_t span = ((addr + 8u + 4095u) & ~(uintptr_t)4095u) - page;
    if (mprotect((void *)page, span, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        logline("RENDERENTRY mprotect rw failed\n"); return;
    }
    unsigned written = grm_jump((void *)addr, (uintptr_t)re_wrapper);
    __builtin___clear_cache((char *)addr, (char *)addr + written);
    if (mprotect((void *)page, span, PROT_READ | PROT_EXEC))
        logline("RENDERENTRY mprotect rx failed\n");
    logline("RENDERENTRY installed addr=%#lx tramp=%p jump_bytes=%u\n",
            (unsigned long)addr, (void *)code, written);
}


/* ★ Count calls into the function that owns the frame loop.
 *
 * 0x54f548 is the enclosing function of the render loop: it holds the global
 * state test (cmp r3,#7 at 54f566), calls 54f468, and later reaches the 54fa60
 * loop whose iterated tail calls notifyDraw and drawFramebuffer. The container
 * 54f664 calls 54f548 once per iteration (bl at 54f78a).
 *
 * Everything downstream being zero therefore reduces to one question that has
 * never been measured: is 54f548 entered at all? Its first twelve bytes are
 *   e92d4ff0 stmdb.w sp!,{r4-r11,lr}
 *   b083     sub sp,#12
 *   af02     add r7,sp,#8
 *   4681     mov r9,r0
 *   4688     mov r8,r1
 * all PC-independent, so a twelve-byte trampoline applies.
 *
 * Delivery is counter-only: the first three calls are logged, and nothing is
 * printed per call afterwards, because this sits in the per-iteration path and
 * per-call logging there is exactly what perturbed the earlier hot-path attempt.
 * FRAMEITER */
static unsigned char *g_fi_tramp;
static unsigned long (*g_fi_orig)(void *, void *, void *);
static unsigned g_fi_calls;

static void *fi_wrapper(void *a0, void *a1, void *a2)
{
    unsigned n = __atomic_fetch_add(&g_fi_calls, 1, __ATOMIC_RELAXED);
    if (n < 3) logline("FRAMEITER call=%u a0=%p tid=%u\n",
                       n + 1, a0, (unsigned)syscall(SYS_gettid));
    return (void *)(long)(g_fi_orig ? g_fi_orig(a0, a1, a2) : 0);
}

static unsigned frameiter_count(void) { return __atomic_load_n(&g_fi_calls, __ATOMIC_RELAXED); }

static void fi_hook_install(void)
{
    static int state;
    if (__atomic_exchange_n(&state, 1, __ATOMIC_RELAXED)) return;
    unsigned long st = (unsigned long)__atomic_load_n(&g_real_start_off, __ATOMIC_RELAXED);
    unsigned long ab = st ? ((st & ~1UL) - 0x54d580UL) : 0;
    if (!ab) { logline("FRAMEITER no appfw base\n"); return; }

    static const unsigned char pin[12] = {
        0x2d,0xe9,0xf0,0x4f,0x83,0xb0,0x02,0xaf,0x81,0x46,0x88,0x46 };
    const unsigned pro_len = 12;
    uintptr_t addr = ab + 0x54f548UL;

    if (memcmp((void *)addr, pin, pro_len)) {
        logline("FRAMEITER prologue mismatch at %#lx\n", (unsigned long)addr);
        return;
    }
    unsigned char *code = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) { logline("FRAMEITER mmap failed\n"); return; }
    memcpy(code, (void *)addr, pro_len);
    grm_jump(code + pro_len, (addr + pro_len) | 1u);
    __builtin___clear_cache((char *)code, (char *)code + 32);
    g_fi_tramp = code;
    unsigned long callable = (unsigned long)code | 1u;
    memcpy(&g_fi_orig, &callable, sizeof callable);

    uintptr_t page = addr & ~(uintptr_t)4095;
    size_t span = ((addr + 8u + 4095u) & ~(uintptr_t)4095u) - page;
    if (mprotect((void *)page, span, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        logline("FRAMEITER mprotect rw failed\n"); return;
    }
    unsigned written = grm_jump((void *)addr, (uintptr_t)fi_wrapper);
    __builtin___clear_cache((char *)addr, (char *)addr + written);
    if (mprotect((void *)page, span, PROT_READ | PROT_EXEC))
        logline("FRAMEITER mprotect rx failed\n");
    logline("FRAMEITER installed addr=%#lx tramp=%p jump_bytes=%u\n",
            (unsigned long)addr, (void *)code, written);
}


/* ★ Dedicated thread entry for the one-shot direct drive. FORCEDRAW */
static void *force_draw_thread(void *arg)
{
    (void)arg;
    logline("FORCEDRAW thread start tid=%u\n", (unsigned)syscall(SYS_gettid));
    force_draw_once();
    logline("FORCEDRAW thread done\n");
    return 0;
}

/* ★ Count every stage of the render container in one run.
 *
 * Chasing the call chain one level per round produced forty rounds of static
 * analysis and no frame, because each round could only confirm that the level
 * below it was zero. The container 0x54f664 is entered (RENDERENTRY enter=1) and
 * its frame-iteration callee 0x54f548 is never called (frameiter_calls=0), but
 * which of the container's own calls ran has never been measured.
 *
 * So all of them are counted at once, in a single run, each with its own verified
 * prologue and gate:
 *
 *   0x54f664  12B  容器入口            (already known: enter=1)
 *   0x54ef74   6B  push/sub/mov        container call #1 @ 0x54f674
 *   0x712100   6B  push/sub/movs       container call #2 @ 0x54f716
 *   0x60e2f4   4B  push/add            container call #3 @ 0x54f71a
 *   0x54f468  10B  stmdb/sub/add/mov    next level inside 0x54f548
 *   0x54f548  12B  渲染迭代            (already known: 0)
 *
 * Every wrapper is counter-only after three log lines, because these sit on the
 * per-iteration path and per-call logging is what sank the earlier hot-path try.
 * Pages are mprotected once per distinct page and restored together, so six
 * hooks in adjacent functions cannot undo each other's RX restoration.
 * STAGEMAP */
typedef struct { uintptr_t addr; unsigned pro_len; const unsigned char *pin; void *wrap; } StageSpec;
static unsigned g_stage_n[6];
static unsigned long g_stage_orig[6];
static int g_stage_ready[6];

#define STAGE_COUNT 6

static void *stage_common(unsigned idx, void *a0, void *a1)
{
    unsigned n = __atomic_fetch_add(&g_stage_n[idx], 1, __ATOMIC_RELAXED);
    if (n < 3) logline("STAGEMAP stage=%u call=%u tid=%u\n",
                       idx, n + 1, (unsigned)syscall(SYS_gettid));
    unsigned long f = __atomic_load_n(&g_stage_orig[idx], __ATOMIC_RELAXED);
    if (!f) return 0;
    void *(*fp)(void *, void *) = (void *(*)(void *, void *))f;
    return fp(a0, a1);
}

static void *sw0(void *a, void *b) { return stage_common(0, a, b); }
static void *sw1(void *a, void *b) { return stage_common(1, a, b); }
static void *sw2(void *a, void *b) { return stage_common(2, a, b); }
static void *sw3(void *a, void *b) { return stage_common(3, a, b); }
static void *sw4(void *a, void *b) { return stage_common(4, a, b); }
static void *sw5(void *a, void *b) { return stage_common(5, a, b); }

static void *g_stage_wrap[STAGE_COUNT] = { (void*)sw0,(void*)sw1,(void*)sw2,(void*)sw3,(void*)sw4,(void*)sw5 };

static void stage_report(void)
{
    logline("STAGEMAP report 0=%u 1=%u 2=%u 3=%u 4=%u 5=%u\n",
            g_stage_n[0], g_stage_n[1], g_stage_n[2],
            g_stage_n[3], g_stage_n[4], g_stage_n[5]);
}

static void stage_hooks_install(void)
{
    static int state;
    if (__atomic_exchange_n(&state, 1, __ATOMIC_RELAXED)) return;
    unsigned long st = (unsigned long)__atomic_load_n(&g_real_start_off, __ATOMIC_RELAXED);
    unsigned long ab = st ? ((st & ~1UL) - 0x54d580UL) : 0;
    if (!ab) { logline("STAGEMAP no appfw base\n"); return; }

    static const unsigned char p0[12] = {0x2d,0xe9,0xf0,0x4f,0xad,0xf5,0x00,0x6d,0x87,0xb0,0xe2,0x4d};
    static const unsigned char p1[8]  = {0xf0,0xb5,0x83,0xb0,0x04,0x46,0,0};
    static const unsigned char p2[8]  = {0xf0,0xb5,0xf7,0xb0,0x00,0x20,0,0};
    static const unsigned char p3[8]  = {0x80,0xb5,0x00,0xaf,0,0,0,0};
    static const unsigned char p4[12] = {0x2d,0xe9,0xf0,0x47,0x82,0xb0,0x00,0xaf,0x8a,0x46,0,0};
    static const unsigned char p5[12] = {0x2d,0xe9,0xf0,0x4f,0x83,0xb0,0x02,0xaf,0x81,0x46,0x88,0x46};

    StageSpec spec[STAGE_COUNT] = {
        { ab + 0x54f664UL, 12, p0, g_stage_wrap[0] },
        { ab + 0x54ef74UL,  6, p1, g_stage_wrap[1] },
        { ab + 0x712100UL,  6, p2, g_stage_wrap[2] },
        { ab + 0x60e2f4UL,  4, p3, g_stage_wrap[3] },
        { ab + 0x54f468UL, 10, p4, g_stage_wrap[4] },
        { ab + 0x54f548UL, 12, p5, g_stage_wrap[5] },
    };

    unsigned blocked = 0;
    for (int i = 0; i < STAGE_COUNT; ++i) {
        uintptr_t a = spec[i].addr;
        if (memcmp((void *)a, spec[i].pin, spec[i].pro_len)) {
            logline("STAGEMAP stage=%d prologue mismatch at %#lx\n", i, (unsigned long)a);
            ++blocked; continue;
        }
        unsigned char *code = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (code == MAP_FAILED) { logline("STAGEMAP stage=%d mmap failed\n", i); ++blocked; continue; }
        memcpy(code, (void *)a, spec[i].pro_len);
        grm_jump(code + spec[i].pro_len, (a + spec[i].pro_len) | 1u);
        __builtin___clear_cache((char *)code, (char *)code + spec[i].pro_len + 20);
        unsigned long callable = (unsigned long)code | 1u;
        __atomic_store_n(&g_stage_orig[i], callable, __ATOMIC_RELAXED);
        __atomic_store_n(&g_stage_ready[i], 1, __ATOMIC_RELAXED);
    }

    /* One protect window per distinct page, so hooks in adjacent functions do not
     * race each other's RX restore. */
    uintptr_t pages[STAGE_COUNT]; unsigned npages = 0;
    for (int i = 0; i < STAGE_COUNT; ++i) {
        if (!g_stage_ready[i]) continue;
        uintptr_t pg = spec[i].addr & ~(uintptr_t)4095;
        int dup = 0;
        for (unsigned k = 0; k < npages; ++k) if (pages[k] == pg) dup = 1;
        if (!dup) pages[npages++] = pg;
    }
    for (unsigned k = 0; k < npages; ++k)
        mprotect((void *)pages[k], 4096, PROT_READ | PROT_WRITE | PROT_EXEC);

    for (int i = 0; i < STAGE_COUNT; ++i) {
        if (!g_stage_ready[i]) continue;
        uintptr_t a = spec[i].addr;
        unsigned written = grm_jump((void *)a, (uintptr_t)spec[i].wrap);
        __builtin___clear_cache((char *)a, (char *)a + written);
    }
    for (unsigned k = 0; k < npages; ++k)
        mprotect((void *)pages[k], 4096, PROT_READ | PROT_EXEC);

    logline("STAGEMAP installed=%u blocked=%u pages=%u\n",
            (unsigned)(STAGE_COUNT - blocked), blocked, npages);
}


/* ★ Measure GraphicsEngine::processSystemMessage, the render loop's only switch.
 *
 * The container's own code says it plainly (appFw+0x54fb3e):
 *   blx gfxeng@plt
 *   blx GraphicsEngine::processSystemMessage@plt
 *   cmp r0,#0
 *   bne.w 0x54f700          back into the render path
 *   else: return            leave the container
 * and 0x54f6fe is `b.n 0x54fb3e`, so every iteration goes through that switch.
 *
 * So "no frame" reduces to "processSystemMessage returned zero". It was recorded
 * as zero calls long ago, but that came from an interposer that never installed
 * (the same dlsym blind spot drawFramebuffer had), so the number meant nothing.
 * This hooks the function body instead and records the actual return values.
 *
 * Its entry is
 *   80b5     push {r7,lr}
 *   00af     add r7,sp,#0
 *   bde88040 ldmia.w sp!,{r7,lr}
 *   2ef098bc b.w <relative>          <-- PC-relative from byte 8 on
 * so eight bytes move whole and control returns to entry+8 with the original b.w
 * intact. PSMPROBE */
static unsigned char *g_psm_tramp;
static unsigned long (*g_psm_orig)(void *);
static unsigned g_psm_calls, g_psm_nonzero;

static void *psm_wrapper(void *self)
{
    unsigned n = __atomic_fetch_add(&g_psm_calls, 1, __ATOMIC_RELAXED);
    void *r = g_psm_orig ? (void *)(long)g_psm_orig(self) : 0;
    unsigned nz = 0;
    if ((long)r != 0) { nz = 1; __atomic_fetch_add(&g_psm_nonzero, 1, __ATOMIC_RELAXED); }
    if (n < 6)
        logline("PSMPROBE call=%u self=%p ret=%p nonzero=%u tid=%u\n",
                n + 1, self, r, nz, (unsigned)syscall(SYS_gettid));
    else if (n == 6)
        logline("PSMPROBE limit=6 (counting continues)\n");
    /* ★ Opt-in direct drive from a point that is measured to run and to return.
     *
     * Every previous attempt to schedule the drive depended on timing that does
     * not work here (sleep never elapses in this guest, ledger 194) or on a
     * watchdog block that has been observed to not execute. processSystemMessage
     * is different: it is measured to be entered exactly once and to return a
     * value, so a call placed immediately after it is guaranteed to happen.
     *
     * This does not make the firmware render by itself; it asks whether the draw
     * function produces pixels at all in this model. Off unless FORCEDRAW=1. */
    return r;
}

static void psm_report(void)
{
    logline("PSMPROBE report calls=%u nonzero=%u\n", g_psm_calls, g_psm_nonzero);
}

static void psm_hook_install(void)
{
    static int state;
    if (__atomic_exchange_n(&state, 1, __ATOMIC_RELAXED)) return;
    unsigned long ps = (unsigned long)__atomic_load_n(&g_real_psm, __ATOMIC_RELAXED);
    unsigned long lb = ps ? ((ps & ~1UL) - 0xe5610UL) : 0;
    if (!lb) { logline("PSMPROBE no libobj base\n"); return; }

    static const unsigned char pin[8] = { 0x80,0xb5,0x00,0xaf,0xbd,0xe8,0x80,0x40 };
    const unsigned pro_len = 8;
    uintptr_t addr = lb + 0xe5610UL;

    if (memcmp((void *)addr, pin, pro_len)) {
        logline("PSMPROBE prologue mismatch at %#lx\n", (unsigned long)addr);
        return;
    }
    unsigned char *code = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) { logline("PSMPROBE mmap failed\n"); return; }
    memcpy(code, (void *)addr, pro_len);
    grm_jump(code + pro_len, (addr + pro_len) | 1u);
    __builtin___clear_cache((char *)code, (char *)code + 32);
    g_psm_tramp = code;
    unsigned long callable = (unsigned long)code | 1u;
    memcpy(&g_psm_orig, &callable, sizeof callable);

    uintptr_t page = addr & ~(uintptr_t)4095;
    size_t span = ((addr + 8u + 4095u) & ~(uintptr_t)4095u) - page;
    if (mprotect((void *)page, span, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        logline("PSMPROBE mprotect rw failed\n"); return;
    }
    unsigned written = grm_jump((void *)addr, (uintptr_t)psm_wrapper);
    __builtin___clear_cache((char *)addr, (char *)addr + written);
    if (mprotect((void *)page, span, PROT_READ | PROT_EXEC))
        logline("PSMPROBE mprotect rx failed\n");
    logline("PSMPROBE installed addr=%#lx tramp=%p jump_bytes=%u\n",
            (unsigned long)addr, (void *)code, written);
}



/* ★ Count the last call made before the container reaches its message pump.
 *
 * The container 0x54f664 runs its first call (0x54ef74) once and then never
 * reaches 0x54f6fe, where it would call processSystemMessage to decide whether
 * to render. Between those two points it makes six PLT calls and finally
 *   appFw+0x54f6e2  bl 0x54e8f0
 * and 0x54e8f0 contains the only two calls in that stretch that can wait:
 *   blx osal_snd_sync_direct@plt
 *   blx osal_valloc_msg_wait@plt
 * so it is the prime suspect for where the container stops.
 *
 * Entry bytes b0b5 88b0 0446 00af are push/sub/mov/add -- eight PC-independent
 * bytes -- so an eight-byte trampoline applies and control returns to entry+8.
 * Enter and leave are both counted, which is the whole question here: entered
 * and never returned means this is the stall. WAITSUSPECT */
static unsigned char *g_ws_tramp;
static unsigned long (*g_ws_orig)(void *);
static unsigned g_ws_enter, g_ws_leave;

static void *ws_wrapper(void *a0)
{
    unsigned n = __atomic_fetch_add(&g_ws_enter, 1, __ATOMIC_RELAXED);
    if (n < 4) logline("WAITSUSPECT enter=%u a0=%p tid=%u\n",
                       n + 1, a0, (unsigned)syscall(SYS_gettid));
    void *r = g_ws_orig ? (void *)(long)g_ws_orig(a0) : 0;
    unsigned m = __atomic_fetch_add(&g_ws_leave, 1, __ATOMIC_RELAXED);
    if (m < 4) logline("WAITSUSPECT leave=%u ret=%p\n", m + 1, r);
    return r;
}

static void ws_hook_install(void)
{
    static int state;
    if (__atomic_exchange_n(&state, 1, __ATOMIC_RELAXED)) return;
    unsigned long st = (unsigned long)__atomic_load_n(&g_real_start_off, __ATOMIC_RELAXED);
    unsigned long ab = st ? ((st & ~1UL) - 0x54d580UL) : 0;
    if (!ab) { logline("WAITSUSPECT no appfw base\n"); return; }

    static const unsigned char pin[8] = { 0xb0,0xb5,0x88,0xb0,0x04,0x46,0x00,0xaf };
    const unsigned pro_len = 8;
    uintptr_t addr = ab + 0x54e8f0UL;

    if (memcmp((void *)addr, pin, pro_len)) {
        logline("WAITSUSPECT prologue mismatch at %#lx\n", (unsigned long)addr);
        return;
    }
    unsigned char *code = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) { logline("WAITSUSPECT mmap failed\n"); return; }
    memcpy(code, (void *)addr, pro_len);
    grm_jump(code + pro_len, (addr + pro_len) | 1u);
    __builtin___clear_cache((char *)code, (char *)code + 32);
    g_ws_tramp = code;
    unsigned long callable = (unsigned long)code | 1u;
    memcpy(&g_ws_orig, &callable, sizeof callable);

    uintptr_t page = addr & ~(uintptr_t)4095;
    size_t span = ((addr + 8u + 4095u) & ~(uintptr_t)4095u) - page;
    if (mprotect((void *)page, span, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        logline("WAITSUSPECT mprotect rw failed\n"); return;
    }
    unsigned written = grm_jump((void *)addr, (uintptr_t)ws_wrapper);
    __builtin___clear_cache((char *)addr, (char *)addr + written);
    if (mprotect((void *)page, span, PROT_READ | PROT_EXEC))
        logline("WAITSUSPECT mprotect rx failed\n");
    logline("WAITSUSPECT installed addr=%#lx tramp=%p jump_bytes=%u\n",
            (unsigned long)addr, (void *)code, written);
}

/* ★ Force one drawFramebuffer() call using the singleton we already located.
 *
 * We know GraphicsEngine's instance pointer lives at libObj_base + 0x357490 and
 * that drawFramebuffer (libObj + 0xe565a, ARM) is a complete implementation with
 * no precondition. Calling it directly answers the central question: does the
 * rendering path work at all, or is it broken beyond the missing trigger?
 *
 * This runs once, after Application_Start has been reached, and is guarded so a
 * fault cannot take down the whole boot silently. */
static void force_draw_once(void)
{
    static int done;
    if (__atomic_exchange_n(&done, 1, __ATOMIC_RELAXED)) return;

    /* Derive the base from the captured real processSystemMessage, the same way
     * DFBHOOK / LZPROBE do. lib_text_base() was measured to return a different
     * value for this library (its PT_LOAD p_vaddr is not zero), so the singleton
     * slot computed from it would be wrong. */
    unsigned long ps0 = (unsigned long)__atomic_load_n(&g_real_psm, __ATOMIC_RELAXED);
    unsigned long base_l = ps0 ? ((ps0 & ~1UL) - 0xe5610UL) : 0;
    if (!base_l) { logline("FORCE_DRAW no libobj base\n"); return; }
    void *base = (void *)base_l;
    volatile unsigned *slot = (volatile unsigned *)((char *)base + 0x357490u);
    void *self = (void *)*slot;
    logline("FORCE_DRAW base=%p self_slot=%p self=%p\n", base, (void *)slot, self);
    if (!self) return;

    unsigned fn = (unsigned)((char *)base + 0xe565bu);
    register unsigned r0 __asm__("r0") = (unsigned)self;
    register unsigned r12 __asm__("r12") = fn;
    __asm__ volatile("mov lr, pc\n bx r12\n" : "+r"(r0) : "r"(r12) : "lr", "memory");
    logline("FORCE_DRAW returned\n");

    volatile unsigned *o = (volatile unsigned *)self;

    /* ★ Analyse the 640x480x2 frame buffer candidate properly.
     * A real frame is not just "non-zero": it has few unique colours, rows that
     * differ from each other, and a repeating border. Report enough statistics to
     * tell a rendered UI apart from an arbitrary heap region. */
    {
        unsigned cand = o[8];
        logline("FRAME try cand=%#x (o8)\n", cand);
        if (cand >= 0x04000000u && cand <= 0x30000000u) {
            unsigned char *p = (unsigned char *)cand;
            unsigned nz_bytes = 0, nz_pixels = 0, rowdiff = 0;
            unsigned uniq[256]; for (int i = 0; i < 256; ++i) uniq[i] = 0;
            unsigned nz_low = 0;
            for (unsigned i = 0; i < 307200u; ++i) {
                unsigned px = p[2*i] | (p[2*i+1] << 8);
                if (px) { ++nz_pixels; ++nz_low; }
                uniq[(px >> 8) & 0xFF] = 1;
                if (p[2*i]) ++nz_bytes;
                if (p[2*i+1]) ++nz_bytes;
            }
            unsigned ucount = 0; for (int i = 0; i < 256; ++i) if (uniq[i]) ++ucount;
            /* row diversity: compare row 100 and row 300 */
            for (unsigned c = 0; c < 640; ++c) {
                unsigned a = p[2*(100*640+c)] | (p[2*(100*640+c)+1] << 8);
                unsigned b = p[2*(300*640+c)] | (p[2*(300*640+c)+1] << 8);
                if (a != b) ++rowdiff;
            }
            logline("FRAME cand=%#x nz_pixels=%u nz_bytes=%u highbyte_uniq=%u rowdiff=%u\n",
                    cand, nz_pixels, nz_bytes, ucount, rowdiff);
            logline("FRAME px0..7: %04x %04x %04x %04x %04x %04x %04x %04x\n",
                    p[0]|(p[1]<<8), p[2]|(p[3]<<8), p[4]|(p[5]<<8), p[6]|(p[7]<<8),
                    p[8]|(p[9]<<8), p[10]|(p[11]<<8), p[12]|(p[13]<<8), p[14]|(p[15]<<8));
            /* simple FNV-1a over the whole 614400 bytes so the frame has an id */
            unsigned h = 2166136261u;
            for (unsigned i = 0; i < 614400u; ++i) { h ^= p[i]; h *= 16777619u; }
            logline("FRAME fnv1a=%08x\n", h);
        }
        logline("FD2 o6=%08x o8=%08x o10=%08x\n", o[6], o[8], o[10]);
    }
}

/* ★ Dump the actual mapping lines for libObj so we can compute the real
 * executable base instead of trusting dladdr's dli_fbase. */
static void maps_probe(const char *soname)
{
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) { logline("MAPS_PROBE cannot open maps\n"); return; }
    char line[512];
    int n = 0;
    logline("MAPS_PROBE begin for %s\n", soname);
    while (fgets(line, sizeof line, f) && n < 12) {
        if (strstr(line, soname)) {
            /* strip newline for tidy output */
            char *nl = strchr(line, '\n');
            if (nl) *nl = 0;
            logline("MAPS_PROBE %s\n", line);
            ++n;
        }
    }
    fclose(f);
    logline("MAPS_PROBE end\n");
}

/* ★ dladdr's dli_fbase is WRONG on this glibc 2.11 for multi-LOAD shared
 * objects: measured 0x09f2a000 vs the real r-xp start 0x0a0cd000, an offset of
 * 0x1a3000. Every address computed from dli_fbase was therefore bogus (that is
 * why forcing drawFramebuffer hit an unhandled prefetch abort).
 *
 * This helper parses /proc/self/maps and returns the start of the executable
 * mapping for a given library, which is the correct base for vaddr arithmetic. */
static void *lib_text_base(const char *soname)
{
    /* ★ Cache: this used to fopen + parse /proc/self/maps on every call, which
     * cost several rounds of confusion when it landed on hot paths (ledger 159,
     * 172). The mapping never changes after load, so compute it once. */
    /* Ledger 243: one slot could not serve two libraries. The UI alternates
     * libObj.so and appFw.so lookups, so each call evicted the other and some
     * callers received a base from the wrong module — createLayer n=1/n=2
     * reported nonsense (0xdca7c96e) while n=3..6 reported 0x7239c0. Use a
     * small table keyed by name instead. */
    static struct { char name[32]; void *base; } cache[8];
    static int cache_n;
    for (int ci = 0; ci < cache_n; ci++)
        if (strncmp(cache[ci].name, soname, sizeof cache[ci].name - 1) == 0)
            return cache[ci].base;

    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512];
    void *base = 0;
    while (fgets(line, sizeof line, f)) {
        /* ★ Exact path tail match. "libObj.so" is a prefix of other
         * libraries here, so strstr matched the wrong one and every libObj
         * offset in this session used a foreign base (0xa70f000 instead of
         * the real 0x04000000). Compare the basename exactly. */
        /* ★ Diagnose the real maps text before choosing a rule.
         *
         * Exact basename matching found nothing, so the mapped path is not
         * literally "libObj.so". Print every line that contains the stem once,
         * then fall back to a suffix test that tolerates a trailing marker. */
        if (strstr(line, soname)) {
            static int dumped;
            if (!dumped) {
                dumped = 1;
                logline("MAPS_MATCH stem=%s line=%s", soname, line);
            }
        }
        {
            const char *slash2 = strrchr(line, '/');
            if (!slash2) continue;
            const char *bn = slash2 + 1;
            size_t bl = strcspn(bn, " \n");
            size_t wl = strlen(soname);
            if (bl < wl) continue;
            if (strncmp(bn + (bl - wl), soname, wl) != 0) continue;
        }
        if (!strstr(line, "r-xp")) continue;   /* executable segment only */
        unsigned lo = 0;
        if (sscanf(line, "%x-", &lo) == 1) { base = (void *)(unsigned long)lo; break; }
    }
    fclose(f);
    if (base && cache_n < (int)(sizeof cache / sizeof cache[0])) {
        snprintf(cache[cache_n].name, sizeof cache[cache_n].name, "%s", soname);
        cache[cache_n].base = base;
        cache_n++;
    }
    return base;
}


/* Per-site stubs are generated by hand because we need exact register control:
 *   push {r0-r3, r12, lr}
 *   mov  r0, #idx
 *   bl   site_log
 *   pop  {r0-r3, r12, lr}
 *   <original instruction>
 *   bx   <return address>   -- not needed: we restore the instruction and let
 *                              the caller continue, so the stub must jump back
 * Instead of hand-writing 6 stubs in asm, the simpler and equally valid
 * approach is used below: we only RECORD that the call site was reached by
 * replacing the `bl` with a `bl` to a stub that logs then branches to the
 * original target, using the fact that a `bl` from the stub can return to a
 * fixed continuation we then `bx` to.
 *
 * Given the complexity and the risk of corrupting a live process, this probe is
 * deliberately limited to a *read-only* check of whether Application_Start ever
 * reaches its later instructions, by sampling PC from a watchdog thread. */
static void *as_pc_watch(void *arg)
{
    (void)arg;
    void *base = lib_text_base("appFw.so");
    if (!base) return 0;
    unsigned start = (unsigned)((char *)base + 0x54d580u) & ~1u;
    unsigned end   = start + 0x42u;             /* the 72-byte function */
    logline("ASPC watch range %#x..%#x\n", start, end);

    for (int i = 0; i < 400; ++i) {              /* ~200 s at 0.5 s */
        usleep(500000);
        for (int t = 0; t < 64; ++t) {
            char path[64];
            snprintf(path, sizeof path, "/proc/self/task/%d/stat", 0);
            (void)path;
            break;
        }
        /* We cannot portably read another thread's PC from userspace, so this
         * watchdog only proves the thread is alive. Recorded honestly. */
        if (i == 0) logline("ASPC watchdog alive (cannot sample peer PC)\n");
    }
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * ★★★★★ Call-site presence probe via UDF traps.
 *
 * Goal: learn which of Application_Start's six calls actually execute. The frame
 * loop (site3, appFw+0x54f664) is the one we most need to know about.
 *
 * Method (safest possible instrumentation of live firmware code):
 *   1. mprotect the code page(s) holding the six call sites writable.
 *   2. Overwrite each 4-byte Thumb BL/BLX with two 16-bit `udf` instructions
 *      (0xde01 0xde01). This needs no Thumb-2 branch encoder at all — a wrong
 *      branch encoding is what has burned us before; `udf` is a fixed literal.
 *   3. Install a SIGILL handler. On a trap whose PC matches a site it:
 *         - records which site fired,
 *         - writes back the saved original 4 bytes,
 *         - returns, so the CPU re-executes the real instruction.
 *   4. After Application_Start returns, restore every site.
 *
 * Net effect: the firmware runs *exactly* its own code; we only observe.
 * ══════════════════════════════════════════════════════════════════════════ */
#define AS_SITE_MAX 2
static struct {
    unsigned off;
    unsigned char orig[4];
    unsigned addr;          /* absolute, resolved at arm time */
    const char *name;
    volatile int hit;
} as_site_tab[AS_SITE_MAX] = {
    /* Only site2 and site3 are instrumented: site2 proves the mechanism works
     * (we know it executes), site3 is the one whose absence we are proving. */
    { 0x26, {0}, 0, "site2_bl_54d2b0_SHOULD_HIT", 0 },
    { 0x2c, {0}, 0, "site3_bl_54f664_QUESTION",   0 },
};
static volatile int g_as_armed;

static void as_sigill(int sig, siginfo_t *si, void *uc)
{
    (void)sig;
    ucontext_t *u = (ucontext_t *)uc;
    unsigned pc = (unsigned)u->uc_mcontext.arm_pc;
    /* The trap address may be the udf or one past it depending on the core;
     * accept a small window around each site. */
    for (int i = 0; i < AS_SITE_MAX; ++i) {
        if (!as_site_tab[i].addr) continue;
        if (pc >= as_site_tab[i].addr && pc < as_site_tab[i].addr + 4) {
            if (!as_site_tab[i].hit) {
                as_site_tab[i].hit = 1;
                /* ★ NOT logline(): stdio is not async-signal-safe and deadlocks
                 * inside a signal handler. Build the line by hand and use the
                 * bare write() syscall. */
                char buf[160];
                int n = 0;
                const char *tag = "ASSITE HIT idx=";
                while (*tag) buf[n++] = *tag++;
                buf[n++] = (char)('0' + i);
                buf[n++] = ' ';
                for (const char *q = as_site_tab[i].name; *q && n < 130; ++q) buf[n++] = *q;
                const char *tail = " executed\n";
                while (*tail) buf[n++] = *tail++;
                ssize_t w = write(2, buf, (size_t)n);
                (void)w;
            }
            memcpy((void *)as_site_tab[i].addr, as_site_tab[i].orig, 4);
            __builtin___clear_cache((char *)as_site_tab[i].addr,
                                    (char *)as_site_tab[i].addr + 4);
            /* ★★★ The previous attempt set only arm_pc and the process stopped
             * responding. The missing piece is the Thumb bit: these sites hold
             * Thumb-2 instructions, so the CPSR T bit (0x20) MUST be set when
             * we hand control back, otherwise the core decodes the restored
             * bytes as ARM and diverges. Set both explicitly. */
            u->uc_mcontext.arm_pc   = as_site_tab[i].addr;   /* even: the site */
            u->uc_mcontext.arm_cpsr |= 0x20u;                /* CPSR.T = Thumb  */
            return;
        }
    }
    /* Not ours — restore default disposition and let it die loudly. */
    signal(SIGILL, SIG_DFL);
}

static unsigned long as_site_probe(unsigned long a, unsigned long b,
                                   unsigned long c, unsigned long d)
{
    void *base = lib_text_base("appFw.so");
    if (!base) { logline("ASSITE no appfw base\n"); return 0; }

    unsigned entry = (unsigned)((char *)base + 0x54d580u);
    logline("ASSITE arming at entry=%#x\n", entry);

    /* 1) make the pages writable (cover the whole 0x10..0x3a span) */
    unsigned lo = (entry + 0x10u) & ~0xFFFu;
    unsigned hi = (entry + 0x3Au + 0xFFFu) & ~0xFFFu;
    int mpr = mprotect((void *)lo, hi - lo, PROT_READ | PROT_WRITE | PROT_EXEC);
    logline("ASSITE mprotect ret=%d errno=%d range=%#x..%#x\n", mpr, errno, lo, hi);
    if (mpr != 0) {
        logline("ASSITE mprotect FAILED - cannot instrument\n");
        return 0;
    }

    /* 2) install handler */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = as_sigill;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGILL, &sa, 0);

    /* 3) patch */
    static const unsigned char udf4[4] = { 0x01, 0xde, 0x01, 0xde };
    for (int i = 0; i < AS_SITE_MAX; ++i) {
        unsigned addr = entry + as_site_tab[i].off;
        as_site_tab[i].addr = addr;
        memcpy(as_site_tab[i].orig, (void *)addr, 4);
        memcpy((void *)addr, udf4, 4);
        logline("ASSITE patched idx=%d addr=%#x orig=%02x%02x%02x%02x now=%02x%02x%02x%02x\n",
                i, addr, as_site_tab[i].orig[0], as_site_tab[i].orig[1],
                as_site_tab[i].orig[2], as_site_tab[i].orig[3],
                ((unsigned char*)addr)[0], ((unsigned char*)addr)[1],
                ((unsigned char*)addr)[2], ((unsigned char*)addr)[3]);
    }
    __builtin___clear_cache((char *)lo, (char *)hi);
    g_as_armed = 1;

    /* 4) run the real thing */
    {
        const char *m = "ASSITE about to call real_start\n";
        ssize_t w = write(2, m, strlen(m)); (void)w;
    }
    unsigned long r = real_start(a, b, c, d);
    {
        const char *m = "ASSITE real_start returned\n";
        ssize_t w = write(2, m, strlen(m)); (void)w;
    }

    /* 5) report + restore */
    g_as_armed = 0;
    for (int i = 0; i < AS_SITE_MAX; ++i) {
        memcpy((void *)as_site_tab[i].addr, as_site_tab[i].orig, 4);
    }
    __builtin___clear_cache((char *)lo, (char *)hi);
    mprotect((void *)lo, hi - lo, PROT_READ | PROT_EXEC);

    logline("ASSITE ---- summary (returned %#lx) ----\n", r);
    for (int i = 0; i < AS_SITE_MAX; ++i)
        logline("ASSITE %d %-28s %s\n", i, as_site_tab[i].name,
                as_site_tab[i].hit ? "EXECUTED" : "not reached");
    return r;
}

/* ★★★★★ THE frame loop, found at appFw+0x54f894:
 *
 *     54f894: blx UXEngine::wgtsys()                  ; get the widget system
 *     54f898: blx WidgetSystem::processEvent()        ; pump one event
 *     54f89c: cmp r0, #0
 *     54f89e: bne 54f894                              ; loop while non-zero
 *
 * So the engine's main loop is `while (wgtsys()->processEvent()) {}`. These two
 * are ordinary exported C++ symbols, so a plain LD_PRELOAD hook works — no code
 * patching, no ABI guessing. If this fires, the engine reaches its event pump.
 * If processEvent always returns non-zero, the loop never exits.
 */
static void *(*g_real_wgtsys)(void);
static int (*g_real_process_event)(void *);
static unsigned long g_uxloop_wgtsys, g_uxloop_events;
static unsigned long g_uxloop_nonzero;

/* wgtsys hook DISABLED: exporting this symbol intercepts libObj's own internal
 * calls, and when dlsym(RTLD_NEXT) fails we return NULL, which kills UI init
 * before the four appFw createLayer calls can happen (observed: 4 -> 2).
 * Lesson: never hook a hot accessor whose NULL return breaks the caller. */

int _ZN2ux6wgtsys12WidgetSystem12processEventEv(void *self)
{
    static const char *sym = "_ZN2ux6wgtsys12WidgetSystem12processEventEv";
    if (!g_real_process_event) g_real_process_event = (int (*)(void *))dlsym(RTLD_NEXT, sym);
    int r = g_real_process_event ? g_real_process_event(self) : 0;
    unsigned long n = __atomic_fetch_add(&g_uxloop_events, 1, __ATOMIC_RELAXED);
    if (r) __atomic_fetch_add(&g_uxloop_nonzero, 1, __ATOMIC_RELAXED);
    if (n < 10 || (n % 500) == 0)
        logline("%sUXLOOP processEvent n=%lu ret=%d self=%p\n", g_as_phase ? "[AS]" : "", n + 1, r, self);
    return r;
}

/* ★★★★★ appFw+0x54fb38 is the OTHER branch of the engine's main loop:
 *
 *   r0 != 0  →  while (wgtsys()->processEvent()) {}     (0x54f894)
 *   r0 == 0  →  gfxeng()->processSystemMessage()        (0x54fb3e/0x54fb42)
 *               and then the function RETURNS (0x54fb5c)
 *
 * Hooking gfxeng / processSystemMessage tells us which branch actually ran.
 * If processSystemMessage fires and processEvent never does, the engine is
 * taking the "no widgets yet" path and exiting the frame loop immediately —
 * which is exactly what a missing menu would produce.
 */
static void *(*g_real_gfxeng)(void);
static int (*g_real_psysmsg)(void *);
static unsigned long g_gfxeng_n, g_psysmsg_n, g_psysmsg_nz, g_gfxeng_as;

void *__dis__ZN8UXEngine6gfxengEv(void)
{
    static const char *sym = "_ZN8UXEngine6gfxengEv";
    if (!g_real_gfxeng) g_real_gfxeng = (void *(*)(void))dlsym(RTLD_NEXT, sym);
    void *r = g_real_gfxeng ? g_real_gfxeng() : 0;
    unsigned long n = __atomic_fetch_add(&g_gfxeng_n, 1, __ATOMIC_RELAXED);
    if (g_as_phase)
        __atomic_fetch_add(&g_gfxeng_as, 1, __ATOMIC_RELAXED);
    if (n < 30) logline("%sGFXLOOP gfxeng n=%lu -> %p\n", g_as_phase ? "[AS]" : "", n + 1, r);
    return r;
}

int __dis__ZN2ux6gfxeng14GraphicsEngine20processSystemMessageEv(void *self)
{
    static const char *sym = "_ZN2ux6gfxeng14GraphicsEngine20processSystemMessageEv";
    if (!g_real_psysmsg) g_real_psysmsg = (int (*)(void *))dlsym(RTLD_NEXT, sym);
    int r = g_real_psysmsg ? g_real_psysmsg(self) : 0;
    unsigned long n = __atomic_fetch_add(&g_psysmsg_n, 1, __ATOMIC_RELAXED);
    if (r) __atomic_fetch_add(&g_psysmsg_nz, 1, __ATOMIC_RELAXED);
    if (n < 30 || (n % 200) == 0)
        logline("%sGFXLOOP processSystemMessage n=%lu ret=%d\n", g_as_phase ? "[AS]" : "", n + 1, r);
    return r;
}

/* ★ Pinpoint which of the two "wgtsys → something" sites ran.
 *
 *   site A: 0x54ed1e  wgtsys()  →  0x54ed22 updateCurrentTime()
 *                          →  0x54ed32 wgtsys()  →  0x54ed36 processEvent()
 *   site B: 0x54f894  wgtsys()  →  0x54f898 processEvent()
 *
 * If updateCurrentTime fires but processEvent does not, we know the engine
 * reaches site A's first half and dies/stops between 0x54ed22 and 0x54ed36.
 * If updateCurrentTime also stays zero, the three wgtsys calls came from one of
 * the other six sites and neither loop was entered at all.
 */
static int (*g_real_updtime)(void *);
static unsigned long g_updtime_n;

/* Enabled: the disabled prefix meant this counter never resolved, so its zero
 * reading said nothing. It is the frame routine's very first call, which makes
 * it a single point test of whether that routine runs at all (ledger 365). */
int _ZN2ux6wgtsys12WidgetSystem17updateCurrentTimeEv(void *self)
{
    static const char *sym = "_ZN2ux6wgtsys12WidgetSystem17updateCurrentTimeEv";
    if (!g_real_updtime) g_real_updtime = (int (*)(void *))dlsym(RTLD_NEXT, sym);
    int r = g_real_updtime ? g_real_updtime(self) : 0;
    unsigned long n = __atomic_fetch_add(&g_updtime_n, 1, __ATOMIC_RELAXED);
    if (n < 10 || (n % 500) == 0)
        logline("%sUPDTIME updateCurrentTime n=%lu self=%p\n",
                g_as_phase ? "[AS]" : "", n + 1, self);
    return r;
}

/* ★ TextSprite::drawText() is imported by appFw and is the concrete text
 * drawing primitive. If the engine ever gets as far as drawing a label, this
 * fires. Combined with the loop hooks it tells us whether the engine is in
 * "drawing" mode or "no widgets" mode.
 *
 * Also hook UXEngine::resmgr() which the engine calls right after the frame
 * loop (0x54ed40) — that gives another positional marker. */
static int (*g_real_drawtext)(void *);
static void *(*g_real_resmgr)(void);
static unsigned long g_drawtext_n, g_resmgr_n;

int __dis__ZN2ux6gfxeng10TextSprite8drawTextEv(void *self)
{
    static const char *sym = "_ZN2ux6gfxeng10TextSprite8drawTextEv";
    if (!g_real_drawtext) g_real_drawtext = (int (*)(void *))dlsym(RTLD_NEXT, sym);
    int r = g_real_drawtext ? g_real_drawtext(self) : 0;
    unsigned long n = __atomic_fetch_add(&g_drawtext_n, 1, __ATOMIC_RELAXED);
    if (n < 10 || (n % 500) == 0)
        logline("%sDRAWTEXT drawText n=%lu self=%p ret=%d\n",
                g_as_phase ? "[AS]" : "", n + 1, self, r);
    return r;
}

void *__dis__ZN8UXEngine6resmgrEv(void)
{
    static const char *sym = "_ZN8UXEngine6resmgrEv";
    if (!g_real_resmgr) g_real_resmgr = (void *(*)(void))dlsym(RTLD_NEXT, sym);
    void *r = g_real_resmgr ? g_real_resmgr() : 0;
    unsigned long n = __atomic_fetch_add(&g_resmgr_n, 1, __ATOMIC_RELAXED);
    if (n < 8) logline("%sRESMGR resmgr n=%lu -> %p\n",
                       g_as_phase ? "[AS]" : "", n + 1, r);
    return r;
}

/* memset / __aeabi_uldivmod hooks were REMOVED: hooking memset intercepts
 * every libc-internal call and broke the boot (ENTER dropped to 0, log at 299
 * lines). Recorded in ledger layer 157 rather than retried. */
static void mile_report(void) { }

/* ★★★★★ WidgetSystem::createLayer(const ux::core::ID&) — this is where the UI is
 * actually built. appFw+0x7239bc and three more sites around 0x7239ea/0x723a1a/
 * 0x723a48 call it, each preceded by ux::core::ID::genID(const char*) so the
 * layer is named.
 *
 * If createLayer fires, the engine is creating its screen/layer stack and the
 * menu infrastructure is at least partially alive. If it never fires, the engine
 * stops before building any UI — a much stronger statement than "no drawing".
 *
 * The ID argument is a small object; we dump its first words to see whether it
 * carries a pointer to the name string. */
static int (*g_real_createlayer)(void *, void *);
static unsigned long g_cl_n;

int _ZN2ux6wgtsys12WidgetSystem11createLayerERKNS_4core2IDE(void *self, void *id)
{
    { extern void report_got_slots(void); static int once; if (!once) { once = 1; report_got_slots(); } }
    static const char *sym = "_ZN2ux6wgtsys12WidgetSystem11createLayerERKNS_4core2IDE";
    if (!g_real_createlayer) g_real_createlayer = (int (*)(void *, void *))dlsym(RTLD_NEXT, sym);
    unsigned ra = (unsigned)(uintptr_t)__builtin_return_address(0) & ~1u;
    void *ab = lib_text_base("appFw.so");
    unsigned off = ab ? (ra - (unsigned)(uintptr_t)ab) : 0;
    unsigned long n = __atomic_fetch_add(&g_cl_n, 1, __ATOMIC_RELAXED);
    unsigned w0 = 0, w1 = 0, w2 = 0, w3 = 0;
    if (id) { unsigned *p = (unsigned *)id; w0 = p[0]; w1 = p[1]; w2 = p[2]; w3 = p[3]; }
    if (n < 12)
        { char who[40]; caller_owner(ra, who, sizeof who);
        logline("%sCREATELAYER n=%lu self=%p id=%p w=%08x %08x %08x %08x caller=%s\n",
                g_as_phase ? "[AS]" : "", n + 1, self, id, w0, w1, w2, w3, who); }
    if (n == 2) uinit_backtrace("UINIT ");   /* only once, on the first AS layer */
    int r = g_real_createlayer ? g_real_createlayer(self, id) : 0;
    if (n < 12)
        logline("CREATELAYER n=%lu ret=%d (NULL=%d) caller_appfw_off=%#x\n",
                n + 1, r, r == 0,
                lib_text_base("appFw.so")
                    ? ra - (unsigned)(uintptr_t)lib_text_base("appFw.so") : 0);
    return r;
}

/* ★ appFw+0x723c92 compares a size against 0xa00000 (10 MB) and, when at least
 * that big, runs a diagnostic sequence ending in printf + usleep — a resource
 * wait / retry path. The probe lives in the EXISTING usleep hook (see above);
 * here we only keep the counter and the caller filter helper. */
static unsigned long g_waitloop_n;
static void waitloop_note(unsigned ra)
{
    void *ab = lib_text_base("appFw.so");
    if (!ab) return;
    unsigned off = ra - (unsigned)(uintptr_t)ab;
    if (off >= 0x723000u && off < 0x725000u) {
        unsigned long n = __atomic_fetch_add(&g_waitloop_n, 1, __ATOMIC_RELAXED);
        if (n < 15)
            logline("%sWAITLOOP usleep n=%lu caller=appfw+%#x\n",
                    g_as_phase ? "[AS]" : "", n + 1, off);
    }
}

/* ★★★ WidgetSystem's per-frame API. These are the functions a real frame loop
 * must call. Hooking them with caller addresses tells us exactly how far the
 * engine gets each frame:
 *
 *   updateFrame()      (132B) — advance one frame
 *   isFrameDelay()     ( 36B) — is the frame being delayed/throttled
 *   notifyDraw()       ( 78B) — tell widgets to draw
 *   getLayer(ID)       ( 30B) — fetch a layer by id
 *   getWidget(ID)      ( 30B) — fetch a widget by id
 *
 * All are exported C++ members: plain preload hooks, no code patching. */
static void *(*g_real_getlayer)(void *, void *);
static int (*g_real_updframe)(void *);
static int (*g_real_isframedelay)(void *);
static void *(*g_real_getwidget)(void *, void *);
static unsigned long g_updframe_n, g_isfd_n, g_getlayer_n, g_getwidget_n;

static void fapi_log(const char *tag, unsigned ra, unsigned long n)
{
    void *ab = lib_text_base("appFw.so");
    unsigned off = ab ? (ra - (unsigned)(uintptr_t)ab) : 0xFFFFFFFFu;
    logline("%s%s n=%lu caller=appfw+%#x\n", g_as_phase ? "[AS]" : "", tag, n, off);
}

void *_ZNK2ux6wgtsys12WidgetSystem8getLayerERKNS_4core2IDE(void *self, void *id)
{
    static const char *sym = "_ZNK2ux6wgtsys12WidgetSystem8getLayerERKNS_4core2IDE";
    if (!g_real_getlayer) g_real_getlayer = (void *(*)(void *, void *))dlsym(RTLD_NEXT, sym);
    void *r = g_real_getlayer ? g_real_getlayer(self, id) : 0;
    unsigned long n = __atomic_fetch_add(&g_getlayer_n, 1, __ATOMIC_RELAXED);
    if (n < 12) fapi_log("FRAMEAPI getLayer -> ", (unsigned)(uintptr_t)__builtin_return_address(0) & ~1u, n + 1);
    return r;
}

void *_ZNK2ux6wgtsys12WidgetSystem9getWidgetERKNS_4core2IDE(void *self, void *id)
{
    static const char *sym = "_ZNK2ux6wgtsys12WidgetSystem9getWidgetERKNS_4core2IDE";
    if (!g_real_getwidget) g_real_getwidget = (void *(*)(void *, void *))dlsym(RTLD_NEXT, sym);
    void *r = g_real_getwidget ? g_real_getwidget(self, id) : 0;
    unsigned long n = __atomic_fetch_add(&g_getwidget_n, 1, __ATOMIC_RELAXED);
    if (n < 12) fapi_log("FRAMEAPI getWidget -> ", (unsigned)(uintptr_t)__builtin_return_address(0) & ~1u, n + 1);
    return r;
}

int _ZN2ux6wgtsys12WidgetSystem11updateFrameEv(void *self)
{
    static const char *sym = "_ZN2ux6wgtsys12WidgetSystem11updateFrameEv";
    if (!g_real_updframe) g_real_updframe = (int (*)(void *))dlsym(RTLD_NEXT, sym);
    int r = g_real_updframe ? g_real_updframe(self) : 0;
    unsigned long n = __atomic_fetch_add(&g_updframe_n, 1, __ATOMIC_RELAXED);
    if (n < 15 || (n % 100) == 0)
        fapi_log("FRAMEAPI updateFrame -> ", (unsigned)(uintptr_t)__builtin_return_address(0) & ~1u, n + 1);
    return r;
}

int _ZNK2ux6wgtsys12WidgetSystem12isFrameDelayEv(void *self)
{
    static const char *sym = "_ZNK2ux6wgtsys12WidgetSystem12isFrameDelayEv";
    if (!g_real_isframedelay) g_real_isframedelay = (int (*)(void *))dlsym(RTLD_NEXT, sym);
    int r = g_real_isframedelay ? g_real_isframedelay(self) : 0;
    unsigned long n = __atomic_fetch_add(&g_isfd_n, 1, __ATOMIC_RELAXED);
    if (n < 8) {
        void *ab = lib_text_base("appFw.so");
        unsigned off = ab ? ((unsigned)(uintptr_t)__builtin_return_address(0) & ~1u) - (unsigned)(uintptr_t)ab : 0;
        logline("%sFRAMEAPI isFrameDelay ret=%d n=%lu caller=appfw+%#x\n",
                g_as_phase ? "[AS]" : "", r, n + 1, off);
    }
    return r;
}

/* ★ Who calls the UI initialisation region (appFw+0x723xxx)?
 *
 * That region calls ResourceManager::switchBankEth() early and often. Hooking
 * it with a caller filter over the 0x723000..0x726000 range would only repeat
 * the same addresses. Instead we hook UXEngine::resmgr(), which the UI init does
 * NOT call, but which the *caller* of the UI init very likely does.
 *
 * Cleaner: hook the WidgetSystem accessor again but capture a small call stack,
 * so we can see one level UP from 0x723xxx. That answers "which function enters
 * the UI initialisation", i.e. where in Application_Start's control flow we are.
 */
static unsigned long g_uinit_n;

static void uinit_backtrace(const char *tag)
{
    void *ab = lib_text_base("appFw.so");
    void *b  = lib_text_base("libObj.so");
    void *rs = as_maps_base("libRsrcHotshoe.so");
    (void)rs;
    /* Walk three frames of __builtin_frame_address. On this ARM EABI the frame
     * pointer chain is usable inside our own C code. */
    void *fp = __builtin_frame_address(0);
    for (int i = 0; i < 6 && fp; ++i) {
        void **frame = (void **)fp;
        void *ret = frame[1];        /* saved lr */
        unsigned ra = (unsigned)(uintptr_t)ret & ~1u;
        unsigned off = 0; const char *mod = "?";
        if (ab && ra >= (unsigned)(uintptr_t)ab && ra < (unsigned)(uintptr_t)ab + 0x3d00000u) {
            mod = "appFw"; off = ra - (unsigned)(uintptr_t)ab;
        } else if (b && ra >= (unsigned)(uintptr_t)b && ra < (unsigned)(uintptr_t)b + 0x400000u) {
            mod = "libObj"; off = ra - (unsigned)(uintptr_t)b;
        }
        logline("%sBT[%d] %s+%#x\n", tag, i, mod, off);
        void *next = frame[0];
        if (next <= fp) break;
        fp = next;
    }
}

/* ★★★★★ Stack-pointer sampling to prove whether the frame-loop function
 * (appFw+0x54f664, which allocates 2048+28 bytes of frame) actually runs.
 *
 * All its callees are internal to appFw, so no symbol hook can see inside it.
 * But entering it pushes SP down by ~2100 bytes. A watchdog thread cannot read
 * another thread's SP from userspace on this kernel, so instead we sample the
 * MAIN thread's stack usage indirectly: we record the lowest SP ever observed
 * by a function we CAN hook (WidgetSystem accessors), and compare it with the
 * SP observed at Application_Start entry.
 *
 * A drop of >= 2048 bytes between those two points is evidence the frame loop
 * ran (nothing else in this path allocates that much).
 */

static unsigned long sp_now(void)
{
    unsigned long sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    return sp;
}

static void sp_note(const char *tag)
{
    unsigned long sp = sp_now();
    unsigned long cur = __atomic_load_n(&g_sp_min, __ATOMIC_RELAXED);
    if (!cur || sp < cur) __atomic_store_n(&g_sp_min, sp, __ATOMIC_RELAXED);
    __atomic_fetch_add(&g_sp_samples, 1, __ATOMIC_RELAXED);
    logline("STKPROBE %s sp=%#lx (entry_sp=%#lx depth=%ld)\n",
            tag, sp, g_sp_at_as_entry,
            g_sp_at_as_entry ? (long)(g_sp_at_as_entry - sp) : 0L);
}

/* ★★★★★ Force site3: call appFw+0x54f664 ourselves.
 *
 * Evidence says Application_Start reaches site2 (UI init, createLayer ×4) but
 * never reaches site3 (0x54f664, the function containing the event pump).
 * Rather than patch firmware code, we call it directly from our hook AFTER the
 * real Application_Start has done its own work — so the engine state is exactly
 * what the firmware built, and we merely add the missing step.
 *
 * 0x54f664 is Thumb-2 (symbol address is odd) and takes one argument in r0
 * (Application_Start passes `r7`, its own frame base). We pass the same kind of
 * pointer: the GraphicsEngine/WidgetSystem singletons are already valid, and
 * Application_Start's frame layout is what the callee expects, so we pass the
 * frame base we can compute from our own stack is NOT equivalent.
 *
 * Therefore: we cannot safely synthesise r0. Instead we verify reachability by
 * a cheaper route — write a marker to a scratch global that our own watchdog
 * reports, and ONLY if a later experiment shows r0 can be reconstructed do we
 * attempt the call. This keeps the firmware unmodified and the system bootable.
 */
static volatile int g_site3_requested;
static void site3_report(void)
{
    logline("SITE3 requested=%d\n", g_site3_requested);
}

/* ★★★★★ Layer::setActive / setClear — the two virtual calls right after the
 * fourth createLayer in the UI initialisation:
 *
 *   723a48: blx createLayer(ID)      -> r0 = Layer*
 *   723a4c: movs r1, #1
 *   723a56: blx r3                   ; vtable+4   -> setActive(true)
 *   723a5c: movs r1, #0
 *   723a62: blx r3                   ; vtable+12  -> setClear(false)
 *
 * Both are exported symbols, so plain preload hooks observe them. If setActive
 * fires, the layer was activated and the UI should be live. If it does not, the
 * engine stops between creating the layer and activating it — that would be the
 * exact boundary we have been hunting. */
static int (*g_real_setactive)(void *, int);
static int (*g_real_setclear)(void *, int);
static unsigned long g_setactive_n, g_setclear_n;

int _ZN2ux6wgtsys5Layer9setActiveEb(void *self, int on)
{
    static const char *sym = "_ZN2ux6wgtsys5Layer9setActiveEb";
    if (!g_real_setactive) g_real_setactive = (int (*)(void *, int))dlsym(RTLD_NEXT, sym);
    unsigned long n = __atomic_fetch_add(&g_setactive_n, 1, __ATOMIC_RELAXED);
    logline("%sLAYERVT setActive n=%lu self=%p on=%d\n",
            g_as_phase ? "[AS]" : "", n + 1, self, on);
    int r = g_real_setactive ? g_real_setactive(self, on) : 0;
    logline("LAYERVT setActive n=%lu ret=%d\n", n + 1, r);
    return r;
}

int _ZN2ux6wgtsys5Layer8setClearEb(void *self, int on)
{
    static const char *sym = "_ZN2ux6wgtsys5Layer8setClearEb";
    if (!g_real_setclear) g_real_setclear = (int (*)(void *, int))dlsym(RTLD_NEXT, sym);
    unsigned long n = __atomic_fetch_add(&g_setclear_n, 1, __ATOMIC_RELAXED);
    unsigned ra = (unsigned)(uintptr_t)__builtin_return_address(0) & ~1u;
    void *ab = lib_text_base("appFw.so");
    unsigned off = ab ? ra - (unsigned)(uintptr_t)ab : 0;
    logline("%sLAYERVT setClear n=%lu self=%p on=%d caller=appfw+%#x\n",
            g_as_phase ? "[AS]" : "", n + 1, self, on, off);
    int r = g_real_setclear ? g_real_setclear(self, on) : 0;
    logline("LAYERVT setClear n=%lu ret=%d\n", n + 1, r);
    return r;
}

/* ★ Correct module-aware caller attribution.
 *
 * The previous helper subtracted the appFw base unconditionally and produced
 * nonsense such as appfw+0xdca50986 for calls that actually came from libObj.
 * This resolves the module by scanning /proc/self/maps for the range containing
 * the address, which is the only reliable way with several loaded libraries. */
static const char *module_of(unsigned ra, unsigned *off_out)
{
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) { if (off_out) *off_out = ra; return "?"; }
    char line[512];
    static char name[64];
    const char *res = 0;
    unsigned off = ra;
    while (fgets(line, sizeof line, f)) {
        unsigned lo = 0, hi = 0;
        if (sscanf(line, "%x-%x", &lo, &hi) != 2) continue;
        if (ra < lo || ra >= hi) continue;
        const char *slash = strrchr(line, '/');
        if (slash) {
            const char *base = strrchr(line, '/');
            snprintf(name, sizeof name, "%s", base + 1);
            char *nl = strchr(name, '\n'); if (nl) *nl = 0;
            res = name;
        } else {
            res = "[anon]";
        }
        /* segment start is not the module base; use the first mapping of the
         * same file name by scanning again — approximated by 'lo - offset'',
         * which is what we report as the module-relative address. */
        const char *p = strstr(line, "00000000");
        if (p) off = ra - lo;
        else {
            unsigned moff = 0;
            if (sscanf(strstr(line, " r") ? strstr(line, " r") : line, "%*s %x", &moff) == 1)
                off = ra - lo + moff;
            else off = ra - lo;
        }
        break;
    }
    fclose(f);
    if (off_out) *off_out = off;
    return res ? res : "?";
}

static void note_caller(const char *tag, unsigned ra)
{
    unsigned off = 0;
    const char *mod = module_of(ra, &off);
    logline("%sCALLER %s ra=%#x mod=%s off=%#x\n", g_as_phase ? "[AS]" : "", tag, ra, mod, off);
}

/* ★ UXEngine::wgtsys() — the widget-system accessor. Re-enabled after the round
 * that removed it during debugging. Kept strictly O(1): a counter and one log
 * line for the first few calls, nothing else. Any dlsym failure is logged rather
 * than silently returning NULL (that mistake cost several rounds). */
static void *(*g_real_wgtsys2)(void);
static unsigned long g_wgtsys_calls;

void *_ZN8UXEngine6wgtsysEv(void)
{
    static const char *sym = "_ZN8UXEngine6wgtsysEv";
    if (!g_real_wgtsys2) {
        g_real_wgtsys2 = (void *(*)(void))dlsym(RTLD_NEXT, sym);
        if (!g_real_wgtsys2) logline("WGTSYS HOOK dlsym FAILED for %s\n", sym);
    }
    void *r = g_real_wgtsys2 ? g_real_wgtsys2() : 0;
    unsigned long n = __atomic_fetch_add(&g_wgtsys_calls, 1, __ATOMIC_RELAXED);
    if (n < 12)
        logline("%sWGTSYS n=%lu ret=%p\n", g_as_phase ? "[AS]" : "", n + 1, r);
    return r;
}

/* ★ Widget lifecycle probes — the pieces that would run once a widget exists.
 *
 * libObj exports no widget CONSTRUCTOR (those live inside appFw's own derived
 * classes), but it does export the base-class methods that every widget must go
 * through to appear on screen:
 *
 *   Widget::initNode(Node&)       0xe1934 — attach to the render node tree
 *   Widget::initSprite(Sprite&,t) 0xe1948 — give it a drawable sprite
 *   Widget::reset(bool)           0xe18e0 — (re)initialise state
 *   Widget::getUserWidgetType()   0xe0e28
 *
 * If none of these ever fires, no widget was ever instantiated — which matches
 * getWidget()==0 and explains the absence of every draw call. These hooks are
 * strictly O(1). */
static int  (*g_real_initnode)(void *, void *);
static int  (*g_real_initsprite)(void *, void *, unsigned);
static int  (*g_real_wreset)(void *, int);
static unsigned long g_initnode_n, g_initsprite_n, g_wreset_n;

int _ZN2ux6wgtsys6Widget8initNodeERNS_6gfxeng4NodeE(void *self, void *node)
{
    static const char *sym = "_ZN2ux6wgtsys6Widget8initNodeERNS_6gfxeng4NodeE";
    if (!g_real_initnode) g_real_initnode = (int (*)(void *, void *))dlsym(RTLD_NEXT, sym);
    unsigned long n = __atomic_fetch_add(&g_initnode_n, 1, __ATOMIC_RELAXED);
    if (n < 10) logline("%sWIDGET initNode n=%lu self=%p node=%p\n",
                        g_as_phase ? "[AS]" : "", n + 1, self, node);
    return g_real_initnode ? g_real_initnode(self, node) : 0;
}

int _ZN2ux6wgtsys6Widget10initSpriteERNS_6gfxeng6SpriteEt(void *self, void *sp, unsigned t)
{
    static const char *sym = "_ZN2ux6wgtsys6Widget10initSpriteERNS_6gfxeng6SpriteEt";
    if (!g_real_initsprite) g_real_initsprite = (int (*)(void *, void *, unsigned))dlsym(RTLD_NEXT, sym);
    unsigned long n = __atomic_fetch_add(&g_initsprite_n, 1, __ATOMIC_RELAXED);
    if (n < 10) logline("%sWIDGET initSprite n=%lu self=%p sprite=%p type=%u\n",
                        g_as_phase ? "[AS]" : "", n + 1, self, sp, t);
    return g_real_initsprite ? g_real_initsprite(self, sp, t) : 0;
}

int _ZN2ux6wgtsys6Widget5resetEb(void *self, int deep)
{
    static const char *sym = "_ZN2ux6wgtsys6Widget5resetEb";
    if (!g_real_wreset) g_real_wreset = (int (*)(void *, int))dlsym(RTLD_NEXT, sym);
    unsigned long n = __atomic_fetch_add(&g_wreset_n, 1, __ATOMIC_RELAXED);
    if (n < 10) logline("%sWIDGET reset n=%lu self=%p deep=%d\n",
                        g_as_phase ? "[AS]" : "", n + 1, self, deep);
    return g_real_wreset ? g_real_wreset(self, deep) : 0;
}

/* ★★★ Decisive probe: does the frame-loop helper actually run?
 *
 * Full chain (established statically, ledger 178):
 *   Application_Start 0x54d580
 *     └─ 0x54d5ac  bl 0x54f664        (site3, the frame loop)
 *          └─ 0x54f674 bl 0x54ef74    (its FIRST call)
 *               └─ 0x54efa6 bl 0x723aac  (menu builder -> 94 widget ctors)
 *
 * 0x54ef74 makes exactly one exported call: GRM_fbrmRegisterTriggerFrameworkCb
 * at 0x54efe8. Hooking it tells us whether 0x54ef74 ever executes — and since
 * it is site3's first call, that answers whether site3 executes at all.
 *
 * We use the caller offset (relative to appFw's executable base) to distinguish
 * this site from any other caller of the same export. */
static int (*g_real_regtrig)(void *, void *);
static unsigned long g_regtrig_n;

int GRM_fbrmRegisterTriggerFrameworkCb(void *cb, void *arg)
{
    static const char *sym = "GRM_fbrmRegisterTriggerFrameworkCb";
    if (!g_real_regtrig) g_real_regtrig = (int (*)(void *, void *))dlsym(RTLD_NEXT, sym);
    unsigned long n = __atomic_fetch_add(&g_regtrig_n, 1, __ATOMIC_RELAXED);
    if (n < 8) {
        unsigned ra = (unsigned)(uintptr_t)__builtin_return_address(0) & ~1u;
        void *ab = lib_text_base("appFw.so");
        unsigned off = ab ? ra - (unsigned)(uintptr_t)ab : 0;
        logline("%sSITE3PROBE RegisterTriggerCb n=%lu cb=%p arg=%p caller_appfw=%#x %s\n",
                g_as_phase ? "[AS]" : "", n + 1, cb, arg, off,
                (off == 0x54efe8u) ? "*** FROM 0x54ef74 (site3 helper) ***" : "");
    }
    return g_real_regtrig ? g_real_regtrig(cb, arg) : 0;
}

/* ★★★★★ FORCE MENU — the reference project's actual strategy: instead of asking
 * why the firmware stops before the menu builder, just CALL the menu builder.
 *
 * appFw+0x723aac is the menu builder (ledger 178):
 *   - takes NO arguments (verified: its prologue ignores r0..r3 and it reads
 *     all state through two global-table lookups 0x54e140 / 0x54e108)
 *   - is a Thumb-2 function
 *   - dispatches on a state value to create widgets via 94 constructors
 *
 * By this point in the boot everything it needs already exists:
 *   - ResourceManager initialised (it calls sys_getInstance right after the check)
 *   - WidgetSystem singleton valid
 *   - 4 UI layers created, activated, clear-configured
 *   - 640x480 render target established
 *
 * We call it on a separate thread, a few seconds after Application_Start has
 * entered, so we do not disturb the firmware's own execution. */

static void *menu_force_thread(void *arg)
{
    (void)arg;
    sleep(20);
    void *base = lib_text_base("appFw.so");
    if (!base) { logline("FORCEMENU no appfw base\n"); return 0; }

    unsigned fn_even = (unsigned)((char *)base + 0x723aacu);
    unsigned fn = fn_even | 1u;                  /* Thumb bit */

    /* Verify the bytes are readable AND executable-reachable before branching.
     * The FORCE_DRAW episode (ledger 149) showed a readable address can still
     * fault on execute if the thumb bit is wrong, so read back and print. */
    volatile unsigned *w = (volatile unsigned *)(fn_even);
    logline("FORCEMENU entry=%#x thumb=%d w0=%08x w1=%08x\n",
            fn, fn & 1, w[0], w[1]);
    if ((w[0] & 0xFFFF) != 0xB5B0)
        logline("FORCEMENU WARNING: expected push{r4,r5,r7,lr}=b5b0 at entry\n");

    /* NOTE: an earlier version called 0x54e140 / 0x54e108 directly to read the
     * builder's state inputs. That CRASHED for the same reason as the FORCE_DRAW
     * episode (ledger 149): those are Thumb-2 functions, so a plain C function
     * pointer built from an EVEN address enters ARM state and faults instantly.
     * Removed rather than fixed — the two values are not needed, and every extra
     * call is another chance to introduce a mode error. */

    logline("FORCEMENU branching to builder\n");
    register unsigned r12 __asm__("r12") = fn;
    register unsigned r0  __asm__("r0")  = 0;
    __asm__ volatile("mov lr, pc\n bx r12\n" : "+r"(r0) : "r"(r12) : "lr", "memory");

    g_menu_ret = (int)r0;
    g_menu_done = 1;
    logline("FORCEMENU builder returned %d\n", g_menu_ret);
    return 0;
}

/* ★★★★★ Locate where the menu builder stalls.
 *
 * Calling 0x723aac directly deadlocks the whole process (ledger 180). The builder
 * is dominated by ResourceManager calls:
 *     sys_getInstance()  then  switchBankEth(bank)  repeated many times,
 *     plus prefetchImages / prefetchTextImage / processCommands.
 * Resource switching waits for a completion that a real camera's resource
 * service would raise. We hook the whole family so the LAST one printed before
 * the freeze names the exact stall point.
 *
 * All hooks are O(1): one counter plus one bounded log line. */
static void *(*g_rm_getinst)(void);
static int  (*g_rm_switchbank)(void *, unsigned);
static int  (*g_rm_prefetch)(void *, unsigned *, unsigned char, unsigned char);
static int  (*g_rm_syncprefetch)(void *, unsigned *, unsigned char, unsigned char);
static int  (*g_rm_proccmd)(void *, unsigned);
static unsigned long g_rm_n[8];
static const char *g_rm_tag[8] = { "getInst","switchBank","prefetch","syncPrefetch","procCmd" };

void *_ZN2ux6resmgr15ResourceManager15sys_getInstanceEv(void)
{
    static const char *sym = "_ZN2ux6resmgr15ResourceManager15sys_getInstanceEv";
    if (!g_rm_getinst) g_rm_getinst = (void *(*)(void))dlsym(RTLD_NEXT, sym);
    void *r = g_rm_getinst ? g_rm_getinst() : 0;
    unsigned long n = __atomic_fetch_add(&g_rm_n[0], 1, __ATOMIC_RELAXED);
    if (n < 40 || (n % 200) == 0)
        logline("%sRM getInst n=%lu -> %p\n", g_as_phase ? "[AS]" : "", n + 1, r);
    return r;
}

int _ZN2ux6resmgr15ResourceManager10switchBankEth(void *self, unsigned bank)
{
    static const char *sym = "_ZN2ux6resmgr15ResourceManager10switchBankEth";
    if (!g_rm_switchbank) g_rm_switchbank = (int (*)(void *, unsigned))dlsym(RTLD_NEXT, sym);
    unsigned long n = __atomic_fetch_add(&g_rm_n[1], 1, __ATOMIC_RELAXED);
    if (n < 40 || (n % 200) == 0)
        logline("%sRM switchBank ENTER n=%lu self=%p bank=%u\n",
                g_as_phase ? "[AS]" : "", n + 1, self, bank);
    int r = g_rm_switchbank ? g_rm_switchbank(self, bank) : 0;
    if (n < 40) logline("RM switchBank LEAVE n=%lu ret=%d\n", n + 1, r);
    return r;
}

int _ZN2ux6resmgr15ResourceManager14prefetchImagesEPjhh(void *self, unsigned *ids, unsigned char a, unsigned char b)
{
    static const char *sym = "_ZN2ux6resmgr15ResourceManager14prefetchImagesEPjhh";
    if (!g_rm_prefetch) g_rm_prefetch = (int (*)(void *, unsigned *, unsigned char, unsigned char))dlsym(RTLD_NEXT, sym);
    unsigned long n = __atomic_fetch_add(&g_rm_n[2], 1, __ATOMIC_RELAXED);
    unsigned id0 = ids ? ids[0] : 0;
    if (n < 30) logline("%sRM prefetchImages ENTER n=%lu ids=%p id0=%#x a=%u b=%u\n",
                        g_as_phase ? "[AS]" : "", n + 1, ids, id0, a, b);
    int r = g_rm_prefetch ? g_rm_prefetch(self, ids, a, b) : 0;
    if (n < 30) logline("RM prefetchImages LEAVE n=%lu ret=%d\n", n + 1, r);
    return r;
}

int _ZN2ux6resmgr15ResourceManager18syncPrefetchImagesEPjhh(void *self, unsigned *ids, unsigned char a, unsigned char b)
{
    static const char *sym = "_ZN2ux6resmgr15ResourceManager18syncPrefetchImagesEPjhh";
    if (!g_rm_syncprefetch) g_rm_syncprefetch = (int (*)(void *, unsigned *, unsigned char, unsigned char))dlsym(RTLD_NEXT, sym);
    unsigned long n = __atomic_fetch_add(&g_rm_n[3], 1, __ATOMIC_RELAXED);
    if (n < 30) logline("%sRM syncPrefetch ENTER n=%lu\n", g_as_phase ? "[AS]" : "", n + 1);
    int r = g_rm_syncprefetch ? g_rm_syncprefetch(self, ids, a, b) : 0;
    if (n < 30) logline("RM syncPrefetch LEAVE n=%lu ret=%d\n", n + 1, r);
    return r;
}

int _ZN2ux6resmgr15ResourceManager15processCommandsEj(void *self, unsigned n2)
{
    static const char *sym = "_ZN2ux6resmgr15ResourceManager15processCommandsEj";
    if (!g_rm_proccmd) g_rm_proccmd = (int (*)(void *, unsigned))dlsym(RTLD_NEXT, sym);
    unsigned long n = __atomic_fetch_add(&g_rm_n[4], 1, __ATOMIC_RELAXED);
    if (n < 30) logline("%sRM processCommands ENTER n=%lu arg=%u\n", g_as_phase ? "[AS]" : "", n + 1, n2);
    int r = g_rm_proccmd ? g_rm_proccmd(self, n2) : 0;
    if (n < 30) logline("RM processCommands LEAVE n=%lu ret=%d\n", n + 1, r);
    return r;
}

/* ★★★★★ Resolve the ONE remaining suspect: site2's `blx 0x923ce0`.
 *
 * site2 (appFw+0x54d2b0) has 10 calls. Nine are accounted for:
 *   - 0x54d318 (create 4 layers)  : measured to succeed
 *   - 0x52abc0 (struct copy)      : memset+memcpy, cannot block
 *   - 0x79df64 (vtable write)     : constructor, cannot block
 *   - the rest are tiny local helpers
 * Only 0x54d346's `blx 0x923ce0` remains, and 0x923ce0 is one of the image's
 * own ARM stubs (12-byte stub + 4-byte literal, 16-byte groups), NOT the ELF
 * .plt. Its literal could not be decoded statically, so resolve it AT RUNTIME:
 * read the literal, follow the stub's own arithmetic, and dladdr the result.
 *
 * ARM stub form at S:
 *   e59fc004  ldr ip,[pc,#4]     ; ip = *(S+12)      (PC=S+8)
 *   e08fc00c  add ip,pc,#12      ; ip = ip + (S+20)  (PC=S+12)
 *   e12fff1c  bx  ip
 * so target = literal + S + 20.
 */
static void plt_resolve(const char *soname, unsigned stub_off, const char *label)
{
    void *base = lib_text_base(soname);
    if (!base) { logline("PLT no base for %s\n", soname); return; }
    unsigned S = (unsigned)((char *)base + stub_off);
    unsigned *p = (unsigned *)S;
    unsigned lit = p[3];                       /* *(S+12) */
    unsigned tgt = lit + S + 20;               /* add ip,pc,#12 with PC=S+12, +8 pipeline */
    Dl_info di;
    const char *mod = "?"; unsigned off = 0; void *fn = (void *)(uintptr_t)tgt;
    if (dladdr(fn, &di) && di.dli_fbase) {
        mod = di.dli_fname; off = tgt - (unsigned)(uintptr_t)di.dli_fbase;
    }
    logline("PLT %s stub_off=%#x S=%p w=%08x %08x %08x lit=%#x → target=%#x mod=%s off=%#x\n",
            label, stub_off, (void *)S, p[0], p[1], p[2], lit, tgt, mod, off);
}

/* ★ Dump every thread's kernel wait channel while Application_Start is stuck.
 *
 * The guest process stays alive (the watchdog keeps beating) but the entry
 * function never returns, so the thread is blocked somewhere. /proc/self/task
 * enumerates threads and each one's wchan names the kernel-side wait, which
 * distinguishes "blocked on a futex/semaphore" from "spinning in user code".
 * Pure reads, no side effects on the firmware. */
static void dump_thread_state(const char *why)
{
    /* Ledger 213: /proc/self/task only covers the calling process. The harness
     * is preloaded into both the boot shim and the application loader, and the
     * watchdog that triggers this dump runs in the former while the entry point
     * runs in the latter, so "self" was the wrong process for thirteen rounds.
     * Prefer the pid recorded at entry. */
    int want = __atomic_load_n(&g_as_pid, __ATOMIC_RELAXED);
    char base[48];
    if (want > 0) snprintf(base, sizeof base, "/proc/%d/task", want);
    else          snprintf(base, sizeof base, "/proc/self/task");
    logline("TSTATE %s scanning %s (our pid=%u)\n", why, base, (unsigned)getpid());
    DIR *d = opendir(base);
    if (!d) { logline("TSTATE %s opendir failed\n", why); return; }
    struct dirent *de;
    int shown = 0;
    logline("TSTATE %s ---\n", why);
    /* Ledger 215: this cap was 12 while the process runs 64 threads, so the
             * entry thread (enumerated after the cut) was silently dropped and
             * looked absent for thirteen rounds. Raise the cap and always print
             * the total so truncation is never mistaken for absence again. */
    unsigned total = 0, shown2 = 0;
    while (1) {
        struct dirent *dd = readdir(d);
        if (!dd) break;
        if (dd->d_name[0] != '.') total++;
    }
    rewinddir(d);
    logline("TSTATE %s total_threads=%u\n", why, total);
    while ((de = readdir(d)) && shown < 80) {
        char path[96], buf[128];
        int fd, n;
        if (de->d_name[0] == '.') continue;
        snprintf(path, sizeof path, "%s/%s/stat", base, de->d_name);
        fd = open(path, O_RDONLY);
        if (fd >= 0) {
            n = read(fd, buf, sizeof buf - 1);
            close(fd);
            if (n > 0) {
                buf[n] = 0;
                /* stat field 3 is the state letter, field 2 is the name */
                char *rp = strrchr(buf, ')');
                char st = (rp && rp[1] == ' ') ? rp[2] : '?';
                logline("TSTATE tid=%s state=%c", de->d_name, st);
                snprintf(path, sizeof path, "%s/%s/wchan", base, de->d_name);
                fd = open(path, O_RDONLY);
                if (fd >= 0) {
                    int n2 = read(fd, buf, sizeof buf - 1);
                    close(fd);
                    if (n2 > 0) { buf[n2] = 0; logline(" wchan=%s", buf); }
                }
                /* ★ For threads waiting in a futex, name the address they
                 * are waiting on.
                 *
                 * The entry thread waits on a lock in every run, and the
                 * obvious next question is which word it is sleeping on; the
                 * kernel exposes it directly as the first argument of the
                 * blocking syscall, so no instrumentation of the guest is
                 * needed to learn it (ledger 346). Read only. */
                {
                    char sp[128];
                    int sfd, sn;
                    snprintf(sp, sizeof sp, "%s/%s/syscall", base, de->d_name);
                    sfd = open(sp, O_RDONLY);
                    if (sfd >= 0) {
                        sn = read(sfd, sp, sizeof sp - 1);
                        close(sfd);
                        if (sn > 0) {
                            sp[sn] = 0;
                            logline("SYSCALL tid=%s %s", de->d_name, sp);
                        }
                    }
                }
                logline("\n");
                shown++;
                continue;
            }
        }
        snprintf(path, sizeof path, "%s/%s/wchan", base, de->d_name);
        fd = open(path, O_RDONLY);
        if (fd >= 0) {
            n = read(fd, buf, sizeof buf - 1);
            close(fd);
            if (n > 0) { buf[n] = 0; logline("TSTATE tid=%s wchan=%s\n", de->d_name, buf); }
        }
        shown++;
    }
    closedir(d);
}

/* ★★★★★ Read the user-space PC of every thread from /proc.
 *
 * Ledger 190 established there is no uninterruptible sleep, so nothing waits on
 * hardware; a runnable thread is executing a user-space loop. Linux exposes
 * /proc/<pid>/task/<tid>/syscall, whose last two fields are the user stack
 * pointer and the user PC — exactly the address we need, and reading it does not
 * touch the process. For a thread in a syscall it also gives the syscall number.
 *
 * Format: "sysno arg1..arg6 sp pc"  or  "running" for a thread on CPU.
 */
static void probe_thread_pcs(const char *why)
{
    DIR *d = opendir("/proc/self/task");
    if (!d) return;
    struct dirent *de;
    int shown = 0;
    logline("SYSCALL_PROBE %s ---\n", why);
    while ((de = readdir(d)) && shown < 14) {
        char path[120], buf[256];
        int fd, n;
        if (de->d_name[0] == '.') continue;
        snprintf(path, sizeof path, "/proc/self/task/%s/syscall", de->d_name);
        fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        n = read(fd, buf, sizeof buf - 1);
        close(fd);
        if (n <= 0) continue;
        buf[n] = 0;
        char *nl = strchr(buf, '\n'); if (nl) *nl = 0;
        logline("SYSCALL_PROBE tid=%s %s\n", de->d_name, buf);
        shown++;
    }
    closedir(d);
}

/* ★★★★★ FORCE_SITE3 — the reference project's technique, adapted.
 *
 * Ledger 192 established the shape of this firmware's lifecycle entry:
 *
 *   Application_Start 0x54d580 allocates a 2208-byte frame at r7, then calls
 *     site2(0x54d2b0) with r0=r7   -- fills offsets 0,4,8,12,16,0x870
 *     site3(0x54f664) with r0=r7   -- reads offsets 24 and 0x6A8 only
 *     site5(0x54d538) with r0=r7
 *
 * The frame offsets site3 reads (24, 1704) do NOT overlap what site2 writes
 * (0,4,8,12,16,2160), so site3 can be invoked on a frame site2 never touched.
 * That makes this a controlled experiment: allocate a zeroed frame ourselves and
 * call site3 alone, on its own thread, well after the firmware has settled.
 *
 * site3 saves nine registers and reserves two kilobytes, so we give it a frame
 * of the same size Application_Start uses and keep our own allocation separate. */
static void *site3_force_thread(void *arg)
{
    (void)arg;
    logline("FORCESITE3 thread started, waiting for the layer creations\n");
    /* sleep() proved unreliable here: the log for this thread stops at the
     * sleep while other threads keep running, so the wait never elapses.
     * Busy-wait on the flag instead, with yields so we do not starve them. */
    {
        int spins = 0;
        while (!__atomic_load_n(&g_in_start, __ATOMIC_RELAXED) && spins < 200000) {
            sched_yield();
            spins++;
        }
        logline("FORCESITE3 flag seen after %d yields (in_start=%d)\n",
                spins, (int)__atomic_load_n(&g_in_start, __ATOMIC_RELAXED));
    }
    /* ★ Wait for the firmware itself to finish the layer creations before we do
     * anything. Ledger 192 showed site2 builds four UI layers; if we call site3
     * before that, we are running against a half-initialised engine (measured:
     * createLayer dropped to 2 and only 22 phase lines appeared). Polling the
     * counter costs nothing and keeps out of the firmware's way, unlike the
     * 60-million-iteration spin that starved the single core last round. */
    {
        unsigned long long spins = 0;
        while (__atomic_load_n(&g_cl_n, __ATOMIC_RELAXED) < 6ULL &&
               spins < 400000000ULL) {
            sched_yield();
            spins++;
        }
        logline("FORCESITE3 createLayer=%llu after %llu yields\n",
                (unsigned long long)__atomic_load_n(&g_cl_n, __ATOMIC_RELAXED),
                spins);
    }
    logline("FORCESITE3 woke\n");
    void *base = lib_text_base("appFw.so");
    if (!base) { logline("FORCESITE3 no appFw base\n"); return 0; }

    unsigned fn = (unsigned)((char *)base + 0x54f664u) | 1u;   /* Thumb-2 */
    volatile unsigned *w = (volatile unsigned *)(fn & ~1u);
    logline("FORCESITE3 entry=%#x w0=%08x w1=%08x\n", fn, w[0], w[1]);
    if ((w[0] & 0xFFFF) != 0xE92D)
        logline("FORCESITE3 WARNING expected stmdb e92d\n");

    /* Same size frame the real entry allocates: 2208 bytes, 8-byte aligned. */
    static unsigned frame[2208 / 4] __attribute__((aligned(8)));
    memset((void *)frame, 0, sizeof frame);
    __atomic_store_n(&g_s3_after, 1, __ATOMIC_RELAXED);
    /* ★ Ledger 197: site3's first call (0x54ef74) does
     *   ldr r0,[frame+16] ; ldr r3,[r0] ; blx r3
     * and Application_Start puts frame+2204 into frame+16. That object is
     * built by site1 (0x54d154(frame+2204)), which writes its vtable at
     * *(frame+2204) = vtable+8. Without site1 running first the frame+16
     * pointer leads to a zero vtable slot and the call faults.
     *
     * So reproduce the firmware's own order: site1 first, then site3. */
    {
        typedef void (*ctor_t)(void *);
        unsigned o = (unsigned)(uintptr_t)frame + 2204;
        ctor_t site1 = (ctor_t)((char *)base + 0x54d154u);
        /* 0x54d154 is Thumb-2: set the bit through a register, not the pointer
         * type, so the compiler cannot strip it. */
        unsigned s1 = (unsigned)(uintptr_t)site1 | 1u;
        logline("FORCESITE3 calling site1(%#x) to build the object at frame+2204\n", o);
        register unsigned r12s __asm__("r12") = s1;
        register unsigned r0s  __asm__("r0")  = o;
        __asm__ volatile("mov lr, pc\n bx r12\n" : "+r"(r0s) : "r"(r12s)
                         : "lr", "memory", "r1", "r2", "r3", "r4", "r5");
        logline("FORCESITE3 site1 returned, *(frame+16)=%#x *(frame+2204)=%#x\n",
                frame[16 / 4], *(volatile unsigned *)o);
    }
    /* The firmware's own call order puts frame+2204 into frame+16. */
    frame[16 / 4] = (unsigned)(uintptr_t)frame + 2204;
    logline("FORCESITE3 frame+16 := %#x\n", frame[16 / 4]);

    /* Predict which branch 0x723aac will take: it dispatches on the values
     * returned by two plain table lookups. Both are cheap and non-blocking
     * (ledger 181 verified the table base 0x2cebbc4 lives in .rodata and the
     * index at 0x37e39ea in .data). Compose the address with the thumb bit set
     * through a register — a plain C function pointer keeps the low bit and
     * enters ARM state, which crashed this exact call in ledger 182. */
    /* NOTE (ledger 240): calling the firmware helpers from here is unsafe.
     *   · "mov lr, pc; bx r12" saves the instruction itself as the return
     *     address, so control never returns to the right place and r0 is
     *     unreliable — which is why this probe always reported 0.
     *   · replacing it with "blx" does return, but perturbed the run
     *     (createLayer 6 -> 5), so that is not acceptable either.
     * The index global is therefore read directly by GV_PROBE at entry and
     * at every lock acquisition, which requires no calling at all. */

    logline("S3_AFTER_BEGIN frame=%p\n", (void *)frame);
    logline("FORCESITE3 frame=%p calling site3\n", (void *)frame);

    register unsigned r12 __asm__("r12") = fn;
    register unsigned r0  __asm__("r0")  = (unsigned)(uintptr_t)frame;
    __asm__ volatile("mov lr, pc\n bx r12\n"
                     : "+r"(r0) : "r"(r12) : "lr", "memory", "r1", "r2", "r3",
                       "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11");
    logline("FORCESITE3 site3 RETURNED r0=%#x\n", r0);
    logline("S3_AFTER_END\n");
    return 0;
}

/* ★★★★★ Mutex trace, modelled on the reference project's mutex-trace.c
 *
 * The a7c2 record ships exactly this diagnostic and it is worth copying the
 * shape rather than inventing one:
 *   · resolve the real symbol once in a CONSTRUCTOR, so the hook body never
 *     calls dlsym (that recursion is what destroyed startup in ledger 217)
 *   · resolve with dlvsym against libc's own version, not RTLD_NEXT
 *   · stay silent for every valid mutex and do a plain tail call — the hook
 *     must cost nothing on the hot path
 *   · report only the invalid case, which is where the real bug shows up
 *
 * We widen "invalid" slightly: report the first N locks whose address is below
 * a sanity floor OR that live outside every mapping, because a wild pointer
 * from the firmware is exactly the kind of thing we are hunting. */
extern void *dlvsym(void *, const char *, const char *);

static int (*g_native_lock)(void *);
static int (*g_native_unlock)(void *);

static int mutex_trace_resolve(void)
{
    int (*fn)(void *) = (int (*)(void *))dlvsym((void *)-1, "pthread_mutex_lock", "GLIBC_2.4");
    if (!fn) fn = (int (*)(void *))dlvsym((void *)-1, "pthread_mutex_lock", "GLIBC_2.17");
    if (!fn) {
        write(2, "MUTEX_TRACE_RESOLVE_FAILED\n", 28);
        return 0;
    }
    __atomic_store_n(&g_native_lock, fn, __ATOMIC_RELEASE);
    return 1;
}

__attribute__((constructor)) static void mutex_trace_init(void)
{
    mutex_trace_resolve();
    g_native_unlock = (int (*)(void *))dlvsym((void *)-1, "pthread_mutex_unlock", "GLIBC_2.4");
    if (!g_native_unlock)
        g_native_unlock = (int (*)(void *))dlvsym((void *)-1, "pthread_mutex_unlock", "GLIBC_2.17");
}

int pthread_mutex_lock(void *mutex)
{
    int (*fn)(void *) = __atomic_load_n(&g_native_lock, __ATOMIC_ACQUIRE);
    if (!fn) { if (!mutex_trace_resolve()) return 0; fn = g_native_lock; }

    /* ★ Report the ENTRY THREAD's lock calls specifically.
     *
     * Ledger 225 got the interposition working without breaking startup by
     * following the reference project's shape. The invalid-pointer test is
     * their condition; ours is different — the entry thread is blocked on a
     * perfectly valid futex, so we need to know WHICH mutex it takes and
     * releases, in order, to see which one it never comes back from. */
    {
        unsigned tid = (unsigned)syscall(SYS_gettid);
        if (tid == (unsigned)__atomic_load_n(&g_as_tid, __ATOMIC_RELAXED)) {
            static unsigned long ent_n;
            unsigned long n = __atomic_fetch_add(&ent_n, 1, __ATOMIC_RELAXED);
            if (n < 40) {
                Dl_info di; const char *mod = "?"; unsigned off = 0;
                unsigned ra = (unsigned)(uintptr_t)__builtin_return_address(0) & ~1u;
                if (dladdr((void *)ra, &di) && di.dli_fname && di.dli_fbase) {
                    mod = di.dli_fname; off = ra - (unsigned)(uintptr_t)di.dli_fbase;
                }
                /* ★ Also report the caller of the caller. The immediate return
                 * address is the inline std::mutex::lock thunk, which every
                 * caller shares, so it cannot distinguish business sites. The
                 * next frame up is the real one. Ledger 227 showed the stall is
                 * not inside lock() itself, so we need the business context. */
                /* ★ Walk the frame chain by hand instead of trusting
                 * __builtin_return_address(1), which returned "?+0" because
                 * the intermediate frame is inlined (ledger 228).
                 *
                 * The caller thunk at 0x60e348 is a real frame, so following
                 * the saved frame pointer twice should reach the business
                 * caller that actually took the lock. Report three levels so a
                 * single run identifies which of the ten candidates fired. */
                logline("ENTRY_LOCK n=%lu mutex=%p caller=%s+%#x\n",
                        n + 1, mutex, mod, off);
                /* VT_PROBE removed: dereferencing vtable[0] before the object
                 * is constructed faults and kills startup (1027 lines, AS=0,
                 * createLayer=2). Read the chain only when the object is known
                 * valid, which we cannot guarantee from this hook. */
                {
                    void *ab2 = lib_text_base("appFw.so");
                    if (ab2) {
                        volatile unsigned *gv2 = (volatile unsigned *)((char *)ab2 + 0x37e39f0u);
                        logline("GV_PROBE at_lock n=%lu u32=%#x\n", n + 1, gv2[0]);
                    }
                }
                {
                    /* AArch32 frame pointer: the frame itself is at fp, the
                     * saved caller fp is at fp[0], the return address at fp[-1]
                     * for this toolchain (stmfd sp!,{...fp,lr}). Probe the two
                     * common layouts and report every plausible code pointer. */
                    unsigned *guess = __builtin_frame_address(0);
                    for (int lvl = 1; lvl <= 3 && guess; lvl++) {
                        unsigned nfp = guess[0], ret_a = guess[-1], ret_b = guess[1];
                        logline("ENTRY_FRAME lvl=%d fp=%p next=%#x ra[-1]=%#x ra[+1]=%#x\n",
                                lvl, (void *)guess, nfp, ret_a, ret_b);
                        if (nfp <= (unsigned)(uintptr_t)guess || !(nfp & 3)) break;
                        guess = (unsigned *)(uintptr_t)nfp;
                    }
                }
            }
        }
    }

    /* Report only suspicious addresses, and only the first handful. */
    if ((unsigned long)mutex < 4096) {
        static unsigned long bad_n;
        unsigned long n = __atomic_fetch_add(&bad_n, 1, __ATOMIC_RELAXED);
        if (n < 10) {
            unsigned tid = (unsigned)syscall(SYS_gettid);
            logline("MUTEX_TRACE_INVALID n=%lu tid=%u mutex=%p is_entry=%d caller=%p\n",
                    n + 1, tid, mutex,
                    (int)(tid == (unsigned)__atomic_load_n(&g_as_tid, __ATOMIC_RELAXED)),
                    __builtin_return_address(0));
        }
    }
    return fn(mutex);
}

int pthread_mutex_unlock(void *mutex)
{
    int (*fn)(void *) = __atomic_load_n(&g_native_unlock, __ATOMIC_ACQUIRE);
    if (!fn) return 0;
    {
        unsigned tid = (unsigned)syscall(SYS_gettid);
        if (tid == (unsigned)__atomic_load_n(&g_as_tid, __ATOMIC_RELAXED)) {
            static unsigned long ent_u;
            unsigned long n = __atomic_fetch_add(&ent_u, 1, __ATOMIC_RELAXED);
            if (n < 40) logline("ENTRY_UNLOCK n=%lu mutex=%p\n", n + 1, mutex);
        }
    }
    return fn(mutex);
}

/* ★★★★★ Is the entry thread waiting on a condition variable?
 *
 * Ledger 227 established that the third mutex acquisition SUCCEEDED (the mutex
 * word read back as free), so the stall is after that lock is taken. The most
 * common thing to block on next in this kind of UI engine is a condition
 * variable, whose wait channel is reported identically as futex_wait_queue_me.
 *
 * Same shape as the mutex trace that works (ledger 225): resolve once in a
 * constructor with dlvsym, keep the hook body a plain tail call, and only
 * report for the thread recorded at entry. COND_HOOK */
static int (*g_native_cond_wait)(void *, void *);
static int (*g_native_cond_timedwait)(void *, void *, const void *);
static unsigned long g_cond_n;

__attribute__((constructor)) static void cond_hook_init(void)
{
    g_native_cond_wait = (int (*)(void *, void *))
        dlvsym((void *)-1, "pthread_cond_wait", "GLIBC_2.4");
    g_native_cond_timedwait = (int (*)(void *, void *, const void *))
        dlvsym((void *)-1, "pthread_cond_timedwait", "GLIBC_2.4");
}

int pthread_cond_wait(void *cond, void *mutex)
{
    int (*fn)(void *, void *) = __atomic_load_n(&g_native_cond_wait, __ATOMIC_ACQUIRE);
    if (!fn) return 0;
    {
        unsigned tid = (unsigned)syscall(SYS_gettid);
        if (tid == (unsigned)__atomic_load_n(&g_as_tid, __ATOMIC_RELAXED)) {
            unsigned long n = __atomic_fetch_add(&g_cond_n, 1, __ATOMIC_RELAXED);
            if (n < 20) {
                Dl_info di; const char *mod = "?"; unsigned off = 0;
                unsigned ra = (unsigned)(uintptr_t)__builtin_return_address(0) & ~1u;
                if (dladdr((void *)ra, &di) && di.dli_fname && di.dli_fbase) {
                    mod = di.dli_fname; off = ra - (unsigned)(uintptr_t)di.dli_fbase;
                }
                logline("ENTRY_COND n=%lu cond=%p mutex=%p caller=%s+%#x\n",
                        n + 1, cond, mutex, mod, off);
            }
        }
    }
    return fn(cond, mutex);
}

int pthread_cond_timedwait(void *cond, void *mutex, const void *abstime)
{
    int (*fn)(void *, void *, const void *) =
        __atomic_load_n(&g_native_cond_timedwait, __ATOMIC_ACQUIRE);
    if (!fn) return 0;
    {
        unsigned tid = (unsigned)syscall(SYS_gettid);
        if (tid == (unsigned)__atomic_load_n(&g_as_tid, __ATOMIC_RELAXED)) {
            unsigned long n = __atomic_fetch_add(&g_cond_n, 1, __ATOMIC_RELAXED);
            if (n < 20) {
                Dl_info di; const char *mod = "?"; unsigned off = 0;
                unsigned ra = (unsigned)(uintptr_t)__builtin_return_address(0) & ~1u;
                if (dladdr((void *)ra, &di) && di.dli_fname && di.dli_fbase) {
                    mod = di.dli_fname; off = ra - (unsigned)(uintptr_t)di.dli_fbase;
                }
                logline("ENTRY_CONDTW n=%lu cond=%p mutex=%p caller=%s+%#x\n",
                        n + 1, cond, mutex, mod, off);
            }
        }
    }
    return fn(cond, mutex, abstime);
}

/* ★★★★★ Inject one input event into the interface engine's queue.
 *
 * Ledger 277 identified the two queues the engine waits on indefinitely
 * (0x7a804c and 0x7a0020, both waiting inside InfraInput). The machine has no
 * input device at all (ledger 276), so nothing ever arrives there and the main
 * loop never redraws.
 *
 * Shape follows the reference project's inject-cp-event: allocate a message
 * through the original osal calls, fill the documented fields, and send it to
 * the queue. Only one message per queue, so the baseline can be checked while
 * the effect is still visible. INJECT_INPUT */
static int (*g_valloc_msg)(unsigned, void **, unsigned, int);
static int (*g_set_msg_src)(void *, unsigned);
static int (*g_snd_msg)(unsigned, void *);
static unsigned long g_inject_n;

static void inject_input_event(unsigned qid, unsigned frame, unsigned code,
                               unsigned sub, unsigned param)
{
    if (!g_valloc_msg) {
        /* Use RTLD_DEFAULT, not a versioned dlvsym: these osal symbols are
         * exported unversioned by libosal_uipc.so, so asking for GLIBC_2.4
         * never matches (ledger 278). */
        g_valloc_msg = (int (*)(unsigned, void **, unsigned, int))
            dlsym(RTLD_DEFAULT, "osal_valloc_msg_wait");
        if (!g_valloc_msg) logline("INJECT no valloc_msg\n");
    }
    if (!g_set_msg_src)
        g_set_msg_src = (int (*)(void *, unsigned))
            dlsym(RTLD_DEFAULT, "osal_set_msg_src");
    if (!g_snd_msg)
        g_snd_msg = (int (*)(unsigned, void *))
            dlsym(RTLD_DEFAULT, "osal_snd_msg");
    if (!g_valloc_msg || !g_snd_msg) { logline("INJECT resolve failed\n"); return; }

    void *msg = 0;
    int r = g_valloc_msg(qid, &msg, 48, 1);
    if (r || !msg) {
        logline("INJECT valloc qid=%#x r=%d\n", qid, r);
        return;
    }
    unsigned char *b = (unsigned char *)msg;
    for (int i = 0; i < 48; i++) b[i] = 0;
    b[0] = frame & 0xff;  b[1] = (frame >> 8) & 0xff;
    b[2] = code & 0xff;   b[3] = (code >> 8) & 0xff;
    b[0xa] = sub & 0xff;  b[0xb] = (sub >> 8) & 0xff;
    b[0x1c] = param & 0xff; b[0x1d] = (param >> 8) & 0xff;
    b[0x1e] = (param >> 16) & 0xff; b[0x1f] = (param >> 24) & 0xff;
    b[8] = param & 0xff; b[9] = (param >> 8) & 0xff;
    if (g_set_msg_src) {
        unsigned source = 0x2003u;
        const char *env_source = getenv("INJECT_INPUT_SOURCE");
        if (env_source && *env_source) {
            char *end = 0; unsigned long parsed = strtoul(env_source, &end, 0);
            if (end && !*end && parsed <= 0xffffffffUL) source = (unsigned)parsed;
        }
        g_set_msg_src(msg, source);
    }
    int sr = g_snd_msg(qid, msg);
    unsigned long n = __atomic_fetch_add(&g_inject_n, 1, __ATOMIC_RELAXED);
    logline("INJECT_INPUT n=%lu qid=%#x frame=%#x code=%#x sub=%u param=%#x send=%d\n",
            n + 1, qid, frame, code, sub, param, sr);
}


/* ★ Deliver a real InfraInput key message, the way a7c2-re's inject-key does.
 *
 * The two queues libObj blocks on are InfraInput receive loops:
 *   libObj+0x209c91  waits on qid 0x7a804c (literal pool [0x209f34] = 0x7a804c)
 *   libObj+0x20a5c1  waits on qid 0x7a0020 (literal pool [0x20a844] = 0x7a0020)
 * both with r2 = 0xffffffff, i.e. osal_rcv_msg_tmo(..., -1) -- an infinite wait.
 *
 * The observed traffic for 0x7a0020 is 20 bytes, five little-endian u32 words:
 *   {key_code, sub_key_code, status, ex_info, from_type}
 * a7c2-re's inject-key.c allocates exactly 20 bytes and fills those five words
 * through the original OSAL calls. Our earlier injector sent a 48-byte CP event
 * (frame/code/sub/param), which is the wrong shape for this queue -- the receive
 * loop parses five words, so a 48-byte CP body is not a key message.
 *
 * The values below are not invented: the 20-byte bodies already observed on this
 * queue were 02000007 0000... , 01000007 0000... , 03000007 0000... ,
 * 04000007 0000... , i.e. key_code 2,1,3,4 with the other four words zero.
 * This sends the first of those. INJECT_KEY */
static unsigned long g_key_n;

static void inject_key_event(unsigned qid, unsigned key_code, unsigned sub,
                             unsigned status, unsigned ex_info, unsigned from_type)
{
    if (!g_valloc_msg) {
        g_valloc_msg = (int (*)(unsigned, void **, unsigned, int))
            dlsym(RTLD_DEFAULT, "osal_valloc_msg_wait");
    }
    if (!g_set_msg_src)
        g_set_msg_src = (int (*)(void *, unsigned))dlsym(RTLD_DEFAULT, "osal_set_msg_src");
    if (!g_snd_msg)
        g_snd_msg = (int (*)(unsigned, void *))dlsym(RTLD_DEFAULT, "osal_snd_msg");
    if (!g_valloc_msg || !g_snd_msg) { logline("INJECT_KEY resolve failed\n"); return; }

    const unsigned size = 20;            /* five u32, as a7c2-re allocates */
    void *msg = 0;
    int r = g_valloc_msg(qid, &msg, size, 1);
    if (r || !msg) {
        logline("INJECT_KEY valloc qid=%#x r=%d\n", qid, r);
        return;
    }
    unsigned *w = (unsigned *)msg;
    w[0] = key_code; w[1] = sub; w[2] = status; w[3] = ex_info; w[4] = from_type;
    if (g_set_msg_src) g_set_msg_src(msg, 0x2003u);
    int sr = g_snd_msg(qid, msg);
    unsigned long n = __atomic_fetch_add(&g_key_n, 1, __ATOMIC_RELAXED);
    logline("INJECT_KEY n=%lu qid=%#x key=%#x sub=%#x status=%#x ex=%#x from=%#x send=%d\n",
            n + 1, qid, key_code, sub, status, ex_info, from_type, sr);
}

/* ★★★★★ Inject a touch through the engine's own public entry points.
 *
 * libObj exports exactly three mouse/touch posters:
 *   WidgetSystem::postMousePress  (u8 x, u8 y, MsecTime)
 *   WidgetSystem::postMouseRelease(u8 x, u8 y, MsecTime)
 *   WidgetSystem::postMouseMove   (u8 x, s16 dx, s16 dy, MsecTime)
 * These are the documented way an input layer hands a touch to the widget
 * system, so calling them is exactly what a real panel would do. No hardware
 * needed, and no guest memory gets patched.
 *
 * Coordinates are limited to u8 for press/release, so stay inside 0..255.
 * TOUCH_INJECT */
static unsigned long (*g_postPress)(void *, unsigned char, unsigned char, unsigned int);
static unsigned long (*g_postRelease)(void *, unsigned char, unsigned char, unsigned int);

static void inject_touch(unsigned char x, unsigned char y, unsigned int t)
{
    /* Resolve through the engine's own instance getter rather than a global:
     * the singleton address moves every run. */
    void *(*getInst)(void) = (void *(*)(void))dlsym(RTLD_DEFAULT,
        "_ZN2ux6wgtsys12WidgetSystem15sys_getInstanceEv");
    if (!g_postPress)
        g_postPress = (unsigned long (*)(void *, unsigned char, unsigned char, unsigned int))
            dlsym(RTLD_DEFAULT, "_ZN2ux6wgtsys12WidgetSystem14postMousePressEhhNS_4core8MsecTimeE");
    if (!g_postRelease)
        g_postRelease = (unsigned long (*)(void *, unsigned char, unsigned char, unsigned int))
            dlsym(RTLD_DEFAULT, "_ZN2ux6wgtsys12WidgetSystem16postMouseReleaseEhhNS_4core8MsecTimeE");
    if (!getInst || !g_postPress || !g_postRelease) {
        logline("TOUCH_INJECT resolve getInst=%p press=%p release=%p\n",
                (void *)getInst, (void *)g_postPress, (void *)g_postRelease);
        return;
    }
    /* ★ Read the engine's own singleton slot first, without calling anything.
     *
     * postMousePress is a static entry: its first instruction overwrites r0 by
     * loading a pointer from libObj + 0x3c80b2 (.bss). If that slot is still
     * NULL the function will write to a low address and fault, which looks
     * exactly like a hang from outside. Reading it costs one dereference and
     * settles the question before we risk the call. */
    {
        void *lb = lib_text_base("libObj.so");
        if (lb) {
            volatile unsigned *slot = (volatile unsigned *)((char *)lb + 0x3c80b2u);
            logline("TOUCH_SLOT libObj=%p slot=%p value=%#x\n",
                    lb, (void *)slot, *slot);
        } else {
            logline("TOUCH_SLOT no libObj base\n");
        }
    }
    void *ws = getInst();
    logline("TOUCH_INJECT resolved getInst=%p press=%p release=%p\n",
            (void *)getInst, (void *)g_postPress, (void *)g_postRelease);
    if (!ws) { logline("TOUCH_INJECT no widget system instance\n"); return; }
    logline("TOUCH_INJECT ws=%p, calling press(%u,%u,%u)\n", ws, x, y, t);
    unsigned long a = g_postPress(ws, x, y, t);
    logline("TOUCH_INJECT press returned %#lx\n", a);
    unsigned long b = g_postRelease(ws, x, y, t + 50);
    logline("TOUCH_INJECT release returned %#lx\n", b);
}

/* ★★★★★ Intercept the input callback registration.
 *
 * The application imports InfraInput::SetCallback through the PLT, so an
 * interposer sees it even though the engine library's internal callers do not
 * exist. If it fires we learn the callback pointer and the event id, which is
 * everything needed to deliver a synthetic touch by calling that pointer
 * directly — no engine internals involved. CALLBACK_PROBE */
static unsigned long g_cbk_n;
static int (*g_cbk_fn)(int, void *);
static int *g_cbk_id;

int _ZN10InfraInput11SetCallbackEPiPFviP16_InfraInputEventE(int *id, int (*fn)(int, void *))
{
    static unsigned long (*real)(int *, int (*)(int, void *));
    if (!real)
        real = (unsigned long (*)(int *, int (*)(int, void *)))
            dlsym(RTLD_NEXT, "_ZN10InfraInput11SetCallbackEPiPFviP16_InfraInputEventE");
    unsigned long n = __atomic_fetch_add(&g_cbk_n, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&g_cbk_fn, fn, __ATOMIC_RELAXED);
    __atomic_store_n(&g_cbk_id, id, __ATOMIC_RELAXED);
    unsigned tid = (unsigned)syscall(SYS_gettid);
    logline("INPUTCB n=%lu tid=%u id_ptr=%p fn=%p\n", n + 1, tid, (void *)id, (void *)fn);
    if (!real) return 0;
    return real(id, fn);
}

/* Also watch the key-list registration: it tells us which key ids the
 * application cares about. */
int _ZN10InfraInput15SetEventKeyListEiP18_InfraInputKeyListi(int a, void *list, int b)
{
    static unsigned long (*real)(int, void *, int);
    if (!real)
        real = (unsigned long (*)(int, void *, int))
            dlsym(RTLD_NEXT, "_ZN10InfraInput15SetEventKeyListEiP18_InfraInputKeyListi");
    static unsigned long n;
    unsigned long c = __atomic_fetch_add(&n, 1, __ATOMIC_RELAXED);
    if (c < 20) {
        unsigned w0 = 0, w1 = 0, w2 = 0;
        if (list) { unsigned *p = (unsigned *)list; w0 = p[0]; w1 = p[1]; w2 = p[2]; }
        logline("INPUTKEYLIST n=%lu a=%d list=%p w=%#x %#x %#x b=%d\n",
                c + 1, a, list, w0, w1, w2, b);
    }
    if (!real) return 0;
    return real(a, list, b);
}

/* ★ Record every Backup key the firmware touches.
 *
 * The reference project seeds one boot record and its interface then advances
 * past the wait that blocks ours (ledger 322). Which key carries that state is
 * unknown for this firmware, so log the keys actually read and written instead
 * of guessing: the set is small and each hit names its caller. Read only from
 * our side; the calls themselves are forwarded unchanged. BACKUP_PROBE */
int Backup_nread(unsigned int key, void *buf, int len)
{
    static int (*real)(unsigned, void *, int);
    if (!real) real = (int (*)(unsigned, void *, int))dlsym(RTLD_NEXT, "Backup_nread");
    int r = real ? real(key, buf, len) : -1;
    static unsigned long n;
    unsigned long c = __atomic_fetch_add(&n, 1, __ATOMIC_RELAXED);
    if (c < 40)
        logline("BACKUP_nread n=%lu key=%#x len=%d ret=%d tid=%u\n",
                c + 1, key, len, r, (unsigned)syscall(SYS_gettid));
    /* ★ The two single byte keys the entry thread consults are the only state
     * it reads during interface initialisation (ledger 323). Print their
     * contents: a zero here would mean the interface is told it is not ready,
     * which is directly actionable. Guarded so a short read cannot overrun. */
    /* ★ Check whether the big configuration blocks are actually populated.
     *
     * The reference seeds an indicator config block so a lazy singleton finishes
     * initialising; the analogous risk here is that a block reads back as a
     * valid length but holds only zeroes, which would look like success while
     * leaving the singleton unfinished. Printing the magic word answers that
     * directly, and a magic of zero is unambiguous. Read only. BACKUP_DUMP */
    if (r > 0 && buf && len >= 4 && (key == 0xbd0000u || key == 0xbd0002u ||
                                     key == 0xbd0001u || key == 0xbd0003u)) {
        const unsigned char *b = (const unsigned char *)buf;
        static unsigned long nd;
        if (__atomic_fetch_add(&nd, 1, __ATOMIC_RELAXED) < 12)
            logline("BACKUP_DUMP key=%#x len=%d head=%02x%02x%02x%02x\n",
                    key, len, b[0], b[1], b[2], b[3]);
    }
    if (r > 0 && buf && len > 0 && (key == 0x30143u || key == 0x30131u)) {
        const unsigned char *b = (const unsigned char *)buf;
        logline("BACKUP_VALUE key=%#x ret=%d bytes=%02x %02x %02x %02x tid=%u\n",
                key, r, b[0], len > 1 ? b[1] : 0, len > 2 ? b[2] : 0, len > 3 ? b[3] : 0,
                (unsigned)syscall(SYS_gettid));
    }
    return r;
}

int Backup_write(unsigned int a, unsigned int key, const void *buf)
{
    static int (*real)(unsigned, unsigned, const void *);
    if (!real) real = (int (*)(unsigned, unsigned, const void *))dlsym(RTLD_NEXT, "Backup_write");
    int r = real ? real(a, key, buf) : -1;
    static unsigned long n;
    unsigned long c = __atomic_fetch_add(&n, 1, __ATOMIC_RELAXED);
    if (c < 40)
        logline("BACKUP_write n=%lu a=%#x key=%#x ret=%d tid=%u\n",
                c + 1, a, key, r, (unsigned)syscall(SYS_gettid));
    return r;
}

/* ★ Watch the queue statistics call that drives the redraw policy.
 *
 * The grading in 0x54f548 compares the value this returns against 200, 30 and
 * 4 to decide how much of the interface to update, and only the branch below
 * five permits the full update that draws (ledger 317). Its queue has never
 * appeared in any receive log, so either nothing consumes it and the backlog
 * grows without bound, or the call fails and the caller falls back to zero.
 * Printing both the queue id and the result distinguishes those two cases with
 * a single observation. Read only; the call is forwarded unchanged. STATQ_PROBE */
int osal_stat_msgq(unsigned int qid, void *out)
{
    static int (*real)(unsigned, void *);
    if (!real) real = (int (*)(unsigned, void *))dlsym(RTLD_NEXT, "osal_stat_msgq");
    int r = real ? real(qid, out) : -1;
    static unsigned long n;
    unsigned long c = __atomic_fetch_add(&n, 1, __ATOMIC_RELAXED);
    if (c < 30) {
        unsigned used = 0;
        if (out && r == 0) used = *(unsigned *)out;
        logline("STATQ n=%lu qid=%#x ret=%d used=%u tid=%u\n",
                c + 1, qid, r, used, (unsigned)syscall(SYS_gettid));
    }
    return r;
}

/* Scheduling override removed: forcing ordinary scheduling collapsed the boot
 * (485 lines, no entry, no layers, framebuffer untouched), so this firmware
 * depends on the priorities it requests. The reference's motivation does not
 * transfer here. */

/* ★ Intercept the loop pump by defining its exported name.
 *
 * Everything else about the pump is inferred: the loop calls it, it reads a
 * global object and a callback at a fixed offset, and that callback returns a
 * byte from the library's data segment. What has never been measured is whether
 * the pump is called at all and what it returns at that moment. Defining the
 * exported name routes every external caller here, which answers both with one
 * run, and forwarding keeps behaviour identical. PSM_HOOK */
static unsigned long g_psm_n, g_psm_true;
int _ZN2ux6gfxeng14GraphicsEngine20processSystemMessageEv(void *self)
{
    static int (*real)(void *);
    if (!real) {
        real = (int (*)(void *))dlsym(RTLD_NEXT,
            "_ZN2ux6gfxeng14GraphicsEngine20processSystemMessageEv");
        logline("PSM_HOOK resolve real=%p\n", (void *)real);
    }
    unsigned long n = __atomic_fetch_add(&g_psm_n, 1, __ATOMIC_RELAXED);
    int r = real ? real(self) : 0;
    if (r) __atomic_fetch_add(&g_psm_true, 1, __ATOMIC_RELAXED);
    if (n < 6) {
        unsigned long psm = (unsigned long)__atomic_load_n(&g_real_psm, __ATOMIC_RELAXED);
        unsigned long libobj = psm ? ((psm & ~1UL) - 0xe5610UL) : 0;
        unsigned int flag = 0;
        if (libobj) flag = *(volatile unsigned char *)(uintptr_t)(libobj + 0x3581b6UL);
        logline("PSM_HOOK call=%lu ret=%d flag=%u self=%p\n", n + 1, r, flag, self);
    }
    return r;
}

/* ★ Count the widget level draw notice, by defining its exported name.
 *
 * The draw entry reached from the interface's notify routine is unexported, but
 * the widget base class exports its draw notice, and that is the layer a real
 * redraw must pass through. Counting calls here measures whether the drawing
 * chain is entered at all, which sampling cannot answer for such short code
 * (ledger 355). Forwarded unchanged; counting only. DRAWNOTICE_HOOK */
static unsigned long g_notice_n;
int _ZN2ux6wgtsys10WidgetBase12onDrawNoticeEv(void *self)
{
    static int (*real)(void *);
    if (!real) {
        real = (int (*)(void *))dlsym(RTLD_NEXT,
            "_ZN2ux6wgtsys10WidgetBase12onDrawNoticeEv");
        logline("DRAWNOTICE_HOOK resolve real=%p\n", (void *)real);
    }
    unsigned long n = __atomic_fetch_add(&g_notice_n, 1, __ATOMIC_RELAXED);
    /* ★ Record the caller as well as the count.
     *
     * This routine is neither virtual nor referenced by any direct branch in the
     * library, so whatever reaches it must arrive through a derived class's own
     * code. The return address names that site, which is the only way to learn
     * who is supposed to trigger a draw. */
    void *caller = __builtin_return_address(0);
    int r = real ? real(self) : 0;
    if (n < 8) logline("DRAWNOTICE call=%lu ret=%d self=%p caller=%p\n",
                       n + 1, r, self, caller);
    return r;
}

/* ★ Count the graphics engine's resource update, which is the step that turns
 * the widget tree into framebuffer content.
 *
 * The interface's notify routine was entered once but the widget draw notice
 * never ran, and the routines between them turned out to be list walking and
 * reference counting rather than drawing (ledger 360). The engine exports both
 * the framebuffer present and a resource update, so counting those separates
 * "the tree was never traversed" from "the tree was traversed but nothing was
 * presented". Counters only, forwarded unchanged. UPDATERES_HOOK */
static unsigned long g_updres_n, g_drawfb_n2;
int _ZN2ux6gfxeng14GraphicsEngine14updateResourceEv(void *self)
{
    static int (*real)(void *);
    if (!real) real = (int (*)(void *))dlsym(RTLD_NEXT,
        "_ZN2ux6gfxeng14GraphicsEngine14updateResourceEv");
    unsigned long n = __atomic_fetch_add(&g_updres_n, 1, __ATOMIC_RELAXED);
    int r = real ? real(self) : 0;
    if (n < 6) logline("UPDATERES call=%lu ret=%d self=%p caller=%p\n",
                       n + 1, r, self, __builtin_return_address(0));
    return r;
}

/* WGT_PROBE removed: every entry it wanted to count is already instrumented
 * earlier in this file (UXLOOP for processEvent, FRAMEAPI for updateFrame,
 * and equivalents for the queries), so a second definition conflicts. */

/* ★ Drive the interface from the send hook, the way the reference does.
 *
 * The reference does not wait for its frame routine to run: it hooks the message
 * send entry, waits until screen construction has finished, checks it is on the
 * interface thread, and only then invokes its menu initialisation. Its own
 * comment records that calling from inside construction re-entered the manager
 * and aborted, which is exactly the mistake this session made by calling from
 * the entry point too early (ledger 377).
 *
 * The equivalent trigger here is four layers created, which is what the
 * construction stage produces before the message loop would start. Attempted
 * once only, and only on the entry thread. DRIVE_AT_SEND */
static int g_drive_done;

static void drive_once(const char *why)
{
    if (__atomic_load_n(&g_drive_done, __ATOMIC_RELAXED)) return;
    /* Wait for construction: the stage creates four layers before the loop. */
    if (__atomic_load_n(&g_cl_n, __ATOMIC_RELAXED) < 4) return;
    /* Only on the entry thread, as the reference insists. */
    int it = __atomic_load_n(&g_as_tid, __ATOMIC_RELAXED);
    if (it > 0 && (int)syscall(SYS_gettid) != it) return;
    if (__atomic_exchange_n(&g_drive_done, 1, __ATOMIC_RELAXED)) return;

    void *(*gfxinst)(void) = (void *(*)(void))dlsym(RTLD_DEFAULT,
        "_ZN2ux6gfxeng14GraphicsEngine15sys_getInstanceEv");
    int (*updres)(void *) = (int (*)(void *))dlsym(RTLD_DEFAULT,
        "_ZN2ux6gfxeng14GraphicsEngine14updateResourceEv");
    void *(*wgtsys)(void) = (void *(*)(void))dlsym(RTLD_DEFAULT,
        "_ZN2ux6wgtsys12WidgetSystem15sys_getInstanceEv");
    int (*nd)(void *) = (int (*)(void *))dlsym(RTLD_DEFAULT,
        "_ZN2ux6wgtsys12WidgetSystem10notifyDrawEv");

    logline("DRIVE_AT_SEND why=%s gfxinst=%p updres=%p wgtsys=%p nd=%p cl_n=%lu\n",
            why, (void *)gfxinst, (void *)updres, (void *)wgtsys, (void *)nd,
            __atomic_load_n(&g_cl_n, __ATOMIC_RELAXED));
    /* ★ Do not drive the engine from inside the firmware's own send path.
     *
     * This block used to call gfxeng() and updateResource() the moment the
     * firmware sent any message. The ordering evidence shows what that did:
     *   RENDERENTRY enter=1 / STAGEMAP stage=1
     *   ENTRY_LOCK n=2 ... ENTRY_UNLOCK n=2
     *   DRIVE_AT_SEND why=snd          <-- re-entered here
     *   UPDATERES call=1 ret=...
     * and the container made no further progress afterwards: never reached
     * 0x54e8f0, never reached the pump at 0x54f6fe, never rendered.
     *
     * So the probe was mutating engine state in the middle of the firmware's
     * own critical path. It is disabled here; only the pre-call field snapshot
     * and the log line remain, neither of which calls into the engine. */
    if (0 && gfxinst && updres) {
        void *gi = gfxinst();
        logline("DRIVE_AT_SEND gfxeng=%p\n", gi);
        if (gi) {
            /* ★ Read the engine's own fields before the update.
             *
             * The update routine first tests a word at offset 0x24 and, when it
             * is zero, takes an error path; the widget walk happens afterwards
             * and depends on the tree being populated. Reading these offsets
             * before and after says whether the tree is empty, which is the
             * difference between "the walk found nothing" and "the walk never
             * got that far". UPD_STRUCT */
            /* The routine receives engine+0x3c and then tests a word at +0x24
             * from that pointer, so the field that gates it is at engine+0x60.
             * The widget walk starts from whatever the iterator constructor
             * returns, so the tree's first node is read the same way. */
            unsigned int f60 = *(volatile unsigned int *)((char *)gi + 0x60);
            unsigned int f3c = *(volatile unsigned int *)((char *)gi + 0x3c);
            unsigned int f40 = *(volatile unsigned int *)((char *)gi + 0x40);
            unsigned int f44 = *(volatile unsigned int *)((char *)gi + 0x44);
            logline("UPD_STRUCT before f60=%#x f3c=%#x f40=%#x f44=%#x gi=%p\n",
                    f60, f3c, f40, f44, gi);
            int r = updres(gi);
            unsigned int a60 = *(volatile unsigned int *)((char *)gi + 0x60);
            logline("UPD_STRUCT after  f60=%#x r=%d\n", a60, r);
        }
    }
    if (wgtsys && nd) {
        void *wg = wgtsys();
        logline("DRIVE_AT_SEND wgtsys=%p\n", wg);
        if (wg) {
            /* Same reason as UX_RENDER_TRIGGER: a call placed here is a probe
             * self-call, not firmware behaviour. */
            logline("DRIVE_AT_SEND notifyDraw skipped (observation only)\n");
        }
    }

    /* ★ Positive control for the accessor counter.
     *
     * The conclusion that the frame routine never runs rests on that counter
     * staying at zero for the routine's call site, but the accessor lives in
     * another module whose own calls bypass the interposition point. Call it
     * once: if the counter moves the hook is reachable and the zero measured a
     * real thing; if it does not, that zero was an artefact.
     * GFXENG_POSCTRL */
    {
        void *(*ge)(void) = (void *(*)(void))dlsym(RTLD_DEFAULT, "_ZN8UXEngine6gfxengEv");
        logline("GFXENG_POSCTRL sym=%p\n", (void *)ge);
        /* Where does the application's own call land? */
        {
            unsigned long st2 = (unsigned long)__atomic_load_n(&g_real_start_off, __ATOMIC_RELAXED);
            if (st2) {
                unsigned long appfw = (st2 & ~1UL) - 0x54d580UL;
                unsigned long slot = appfw + 0x377ac9cUL;
                unsigned long v = *(volatile unsigned long *)(uintptr_t)slot;
                logline("GFXENG_GOT slot=%#lx value=%#lx ourhook=%p\n",
                        slot, v, (void *)ge);
                /* Same question for the pump: is its slot pointing at this
                 * library or straight at the implementation? */
                {
                    unsigned long s2 = appfw + 0x377acecUL;
                    unsigned long v2 = *(volatile unsigned long *)(uintptr_t)s2;
                    void *ourpsm = dlsym(RTLD_DEFAULT,
                        "_ZN2ux6gfxeng14GraphicsEngine20processSystemMessageEv");
                    logline("PSM_GOT slot=%#lx value=%#lx ourhook=%p\n",
                            s2, v2, ourpsm);
                }
            }
        }
        if (ge) {
            void *r = ge();
            logline("GFXENG_POSCTRL ret=%p\n", r);
        }
    }

    /* ★ Scan the interface singleton for a screen identifier field.
     *
     * The reference's manager keeps the current screen at a fixed offset and its
     * apply call is verified by that field changing, so this firmware's
     * equivalent object should expose the same thing somewhere. Scanning a
     * bounded window for values in the identifier range locates it without
     * guessing an offset. WGSCAN */
    {
        void *(*wg_get)(void) = (void *(*)(void))dlsym(RTLD_DEFAULT,
            "_ZN2ux6wgtsys12WidgetSystem15sys_getInstanceEv");
        if (wg_get) {
            void *wg = wg_get();
            logline("WGSCAN obj=%p\n", wg);
            if (wg) {
                for (unsigned off = 0; off < 0x600; off += 4) {
                    unsigned int v = *(volatile unsigned int *)((char *)wg + off);
                    if (v == 0x308u || v == 0x4b7u || v == 0x346u ||
                        v == 0x2cau || v == 0x309u)
                        logline("WGSCAN HIT off=%#x value=%#x\n", off, v);
                }
            }
        }
    }

    /* ★ Compare the running bytes with the file's bytes at the stage entry.
     *
     * Forward and reverse evidence disagree about whether that stage runs, so
     * the next thing to rule out is that the code at that address is not the code
     * on disk. Reading the first words at run time and comparing them to the
     * known opcodes settles whether the address identifies the same routine.
     * RUNTIMEBYTES */
    {
        unsigned long st = (unsigned long)__atomic_load_n(&g_real_start_off, __ATOMIC_RELAXED);
        if (st) {
            unsigned long appfw = (st & ~1UL) - 0x54d580UL;
            unsigned long a = appfw + 0x54f664UL;
            unsigned long b = appfw + 0x54d5acUL;
            const unsigned char *p1 = (const unsigned char *)(uintptr_t)a;
            const unsigned char *p2 = (const unsigned char *)(uintptr_t)b;
            logline("RTBYTES stage3=%02x %02x %02x %02x (expect e9 2d 4f f0)\n",
                    p1[0], p1[1], p1[2], p1[3]);
            logline("RTBYTES callsite=%02x %02x %02x %02x (expect f0 02 f8 5a)\n",
                    p2[0], p2[1], p2[2], p2[3]);
        }
    }

    /* ★ Compare the captured entry address with the module's own copy of it.
     *
     * Everything else measured so far assumes the loader handed this library the
     * address the module itself believes its entry sits at. If those two differ
     * the library has been intercepting something other than the real entry, and
     * every conclusion drawn from stages behind that hook describes a path the
     * firmware never takes. STARTCMP */
    {
        unsigned long st = (unsigned long)__atomic_load_n(&g_real_start_off, __ATOMIC_RELAXED);
        if (st) {
            unsigned long captured = st & ~1UL;
            unsigned long appfw = captured - 0x54d580UL;
            unsigned long expect = appfw + 0x54d580UL;
            logline("STARTCMP captured=%#lx appfw=%#lx expect=%#lx match=%d\n",
                    captured, appfw, expect, captured == expect);
        }
    }

    /* ★ Read the words where the interface initialisation stores its layers.
     *
     * That routine keeps the four created layers in cleared data slots, so a
     * non-zero word there is direct evidence that it completed, independent of
     * any counter or hook. Scanning a small window avoids depending on a hand
     * computed offset being exactly right. LAYERGLB */
    {
        unsigned long st = (unsigned long)__atomic_load_n(&g_real_start_off, __ATOMIC_RELAXED);
        if (st) {
            unsigned long appfw = (st & ~1UL) - 0x54d580UL;
            int nz = 0;
            unsigned long first = 0, firstoff = 0;
            for (unsigned long off = 0x3d55000UL; off < 0x3d57000UL; off += 4) {
                unsigned long v = *(volatile unsigned long *)(uintptr_t)(appfw + off);
                if (v > 0x1000UL && v < 0x80000000UL) {
                    if (!nz) { first = v; firstoff = off; }
                    ++nz;
                }
            }
            logline("LAYERGLB nonzero=%d firstoff=%#lx first=%#lx\n",
                    nz, firstoff, first);
        }
    }

    /* ★ Inspect what the third stage dereferences first.
     *
     * Its opening call loads a pointer from the frame at a fixed offset and then
     * reads a table pointer through it before dispatching. If that pointer is
     * absent or does not point at a module, the dispatch cannot land anywhere
     * meaningful, which would explain a stage that appears never to run while
     * every earlier stage completes. The frame itself is the entry's stack
     * pointer minus a fixed prologue, so it can be recovered here.
     * FRAME16 */
    {
        unsigned long st = (unsigned long)__atomic_load_n(&g_real_start_off, __ATOMIC_RELAXED);
        if (st) {
            unsigned long appfw = (st & ~1UL) - 0x54d580UL;
            unsigned long ps = (unsigned long)__atomic_load_n(&g_real_psm, __ATOMIC_RELAXED);
            unsigned long libobj = ps ? ((ps & ~1UL) - 0xe5610UL) : 0;
            /* site2 stores its second argument at frame+16 and that argument is
             * frame+2204, so the value is a stack address inside the same frame. */
            unsigned long s3reg = *(volatile unsigned long *)(uintptr_t)(appfw + 0x3d53a64UL);
            logline("FRAME16 appfw=%#lx s3reg=%#lx libobj=%#lx\n",
                    appfw, s3reg, libobj);
        }
    }

    /* ★ Read the global that the third stage's first real callee stores.
     *
     * That routine writes its own address into a fixed slot in the cleared data
     * segment, so the slot is non-zero if and only if the stage reached that
     * call. Combined with the queue traffic that stage would produce, this
     * localises where the stage stops. S3REG */
    {
        unsigned long st = (unsigned long)__atomic_load_n(&g_real_start_off, __ATOMIC_RELAXED);
        if (st) {
            unsigned long appfw = (st & ~1UL) - 0x54d580UL;
            unsigned long g = appfw + 0x3d53a64UL;
            unsigned long v = *(volatile unsigned long *)(uintptr_t)g;
            logline("S3REG slot=%#lx value=%#lx %s\n", g, v,
                    v ? "STAGE3_REACHED_0x54f68a" : "stage3_not_reached");
        }
    }

    /* ★ Sweep every slot this harness claims to measure.
     *
     * A slot still holding the resolver stub means that function was never
     * called; a slot holding the implementation means the counter for it is
     * blind. Either way the number printed for it says nothing until this is
     * known, and several conclusions in this session rest on such numbers.
     * GOT_SWEEP */
    {
        unsigned long st = (unsigned long)__atomic_load_n(&g_real_start_off, __ATOMIC_RELAXED);
        if (st) {
            unsigned long appfw = (st & ~1UL) - 0x54d580UL;
            struct { unsigned long off; const char *name; } sw[] = {
                {0x377ac9cUL, "gfxeng"},
                {0x377acecUL, "processSystemMessage"},
                {0x377acf8UL, "createLayer"},
                {0x377afe0UL, "drawFramebuffer"},
                {0x377b440UL, "notifyDraw"},
                {0x377b480UL, "updateFrame"},
                {0x377adc8UL, "initSprite"},
                {0x377b0b8UL, "setParent"},
                {0x377b3e8UL, "GRMRegisterTrigger"},
            };
            for (unsigned i = 0; i < sizeof sw / sizeof sw[0]; ++i) {
                unsigned long v = *(volatile unsigned long *)(uintptr_t)(appfw + sw[i].off);
                unsigned long rel = v - appfw;
                const char *kind = (rel == 0x5289d0UL) ? "PLT0(never-called)"
                                 : (rel < 0x3d4bc78UL) ? "inside-appFw"
                                 : "resolved";
                logline("GOTS %-22s slot=%#lx value=%#lx rel=%#lx %s\n",
                        sw[i].name, appfw + sw[i].off, v, rel, kind);
            }
        }
    }

    /* ★ Read both slots again AFTER at least one call has gone through, so the
     * lazy binder has resolved them. If the pump's slot still points at the
     * implementation rather than at this library, every counter for it has been
     * blind to the firmware's own calls and the "never called" reading is an
     * artefact rather than a measurement. PSM_GOT_AFTER */
    {
        unsigned long st3 = (unsigned long)__atomic_load_n(&g_real_start_off, __ATOMIC_RELAXED);
        if (st3) {
            unsigned long appfw = (st3 & ~1UL) - 0x54d580UL;
            unsigned long sp2 = appfw + 0x377acecUL;
            unsigned long sg = appfw + 0x377ac9cUL;
            unsigned long vp = *(volatile unsigned long *)(uintptr_t)sp2;
            unsigned long vg = *(volatile unsigned long *)(uintptr_t)sg;
            void *ourpsm = dlsym(RTLD_DEFAULT,
                "_ZN2ux6gfxeng14GraphicsEngine20processSystemMessageEv");
            void *realpsm = (void *)__atomic_load_n(&g_real_psm, __ATOMIC_RELAXED);
            logline("PSM_GOT_AFTER value=%#lx ourhook=%p real=%p\n",
                    vp, ourpsm, realpsm);
            logline("GFXENG_GOT_AFTER value=%#lx ourhook=%p\n",
                    vg, dlsym(RTLD_DEFAULT, "_ZN8UXEngine6gfxengEv"));
        }
    }

    /* ★ a7c2-style input delivery: the current firmware waits on the two
     * InfraInput queues below.  This is deliberately opt-in and one-shot;
     * the reference uses the same original OSAL allocation/source/send API,
     * never a direct handler call. */
    if (getenv("INJECT_INPUT_QUEUES") &&
        __atomic_load_n(&g_cl_n, __ATOMIC_RELAXED) >= 4) {
        static int once_input;
        if (!__atomic_exchange_n(&once_input, 1, __ATOMIC_RELAXED)) {
            logline("INJECT_INPUT_QUEUES begin tid=%u cl_n=%lu\n",
                    (unsigned)syscall(SYS_gettid),
                    __atomic_load_n(&g_cl_n, __ATOMIC_RELAXED));
            /* Layout copied from a7c2 inject-cp-event.c: 48-byte body,
             * little-endian frame/code/sub/param, then OSAL source. */
            inject_input_event(0x7a804cu, 0x2003u, 0x8201u, 3u, 1u);
            inject_input_event(0x7a0020u, 0x2003u, 0x8201u, 3u, 1u);
            /* ★ Then a real 20-byte InfraInput key, shape per a7c2-re inject-key. */
            inject_key_event(0x7a0020u, 2u, 0u, 0u, 0u, 0u);
            logline("INJECT_INPUT_QUEUES done\n");
        }
    }

    /* ★ Inject the co-processor notification, the way the reference does.
     *
     * The reference delivers a dataflow notification through the same message
     * calls the co-processor would use, rather than calling any handler. This
     * firmware has that send path inside the third stage's exit, and it targets
     * queue 0x5503e0 with a twenty byte message whose first word is 0x20f0000
     * - the very queue this stage also polls, and one that never appears in any
     * receive log. Delivering there reproduces what the co-processor would do.
     * INJECT_CP */
    if (__atomic_load_n(&g_cl_n, __ATOMIC_RELAXED) >= 4) {
        static int once_cp;
        if (!__atomic_exchange_n(&once_cp, 1, __ATOMIC_RELAXED)) {
            int (*va)(unsigned, void **, unsigned, int) =
                (int (*)(unsigned, void **, unsigned, int))dlsym(RTLD_DEFAULT, "osal_valloc_msg_wait");
            int (*sn)(unsigned, void *) =
                (int (*)(unsigned, void *))dlsym(RTLD_DEFAULT, "osal_snd_msg");
            logline("INJECT_CP syms valloc=%p snd=%p\n", (void *)va, (void *)sn);
            if (va && sn) {
                void *m = 0;
                int r = va(0x5503e0u, &m, 20, 1);
                logline("INJECT_CP alloc=%d msg=%p\n", r, m);
                if (!r && m) {
                    unsigned char *b = (unsigned char *)m;
                    for (int i = 0; i < 20; ++i) b[i] = 0;
                    b[0] = 0x00; b[1] = 0x00;          /* frame */
                    b[2] = 0x0f; b[3] = 0x02;          /* 0x020f */
                    /* ★ Poll the queue the way this firmware itself does.
                     *
                     * The stage's only statistics call is against this same
                     * queue, so using it here reports in the firmware's own
                     * terms whether the queue exists and whether the message
                     * is pending or already consumed. */
                    int (*st)(unsigned, void *) =
                        (int (*)(unsigned, void *))dlsym(RTLD_DEFAULT, "osal_stat_msgq");
                    unsigned int info[24];
                    for (int i = 0; i < 24; ++i) info[i] = 0;
                    int pre = st ? st(0x5503e0u, info) : -999;
                    logline("INJECT_CP statq_pre ret=%d words=%u %u %u %u %u %u\n",
                            pre, info[0], info[1], info[2], info[3], info[4], info[5]);
                    int sr = sn(0x5503e0u, m);
                    logline("INJECT_CP send=%d\n", sr);
                    for (int i = 0; i < 24; ++i) info[i] = 0;
                    int post = st ? st(0x5503e0u, info) : -999;
                    logline("INJECT_CP statq_post ret=%d words=%u %u %u %u %u %u\n",
                            post, info[0], info[1], info[2], info[3], info[4], info[5]);
                }
            }
        }
    }

    /* Direct invocation of the registered handler was attempted and aborted
     * the run: construction counts fell from forty to two and the handler never
     * returned, so its selector is a real object rather than a flag. Delivery
     * has to go through whatever dispatches that event instead of calling the
     * handler directly (ledger 387). */

}

int osal_snd_msg(unsigned int id, void *message)
{
    static int (*real)(unsigned, void *);
    if (!real) real = (int (*)(unsigned, void *))dlsym(RTLD_NEXT, "osal_snd_msg");
    static unsigned long n;
    unsigned long c = __atomic_fetch_add(&n, 1, __ATOMIC_RELAXED);
    if (c < 10) logline("SND_DRIVE n=%lu id=%#x tid=%u\n", c + 1, id,
                        (unsigned)syscall(SYS_gettid));
    drive_once("snd");
    return real ? real(id, message) : -1;
}

/* ★ Count widget node construction.
 *
 * The engine's resource update takes its normal path — its gating word holds a
 * heap pointer — yet nothing is drawn, and the walk it performs dispatches a
 * table slot other than the notice function instrumented earlier (ledger 380).
 * So the question is whether any widget nodes exist to walk. The node
 * constructor is exported, which makes that countable without touching
 * internals. Counting only, forwarded unchanged. WIDGETNODE_HOOK */
static unsigned long g_wnode_n;
void _ZN2ux6wgtsys10WidgetNodeC1Ev(void *self)
{
    static void (*real)(void *);
    if (!real) real = (void (*)(void *))dlsym(RTLD_NEXT,
        "_ZN2ux6wgtsys10WidgetNodeC1Ev");
    unsigned long n = __atomic_fetch_add(&g_wnode_n, 1, __ATOMIC_RELAXED);
    if (n < 8) logline("WNODE ctor n=%lu self=%p caller=%p\n",
                       n + 1, self, __builtin_return_address(0));
    if (real) real(self);
}

/* ★ Count construction of the widget class itself.
 *
 * The node constructor is exported but measuring it gave zero, and the node
 * type is a wrapper rather than the widget hierarchy's base, so that zero could
 * not settle whether any widgets exist. The widget class exports both of its
 * constructor variants at one address, which makes it the honest place to count
 * instances. Counting only, forwarded unchanged. WIDGET_CTOR_HOOK */
static unsigned long g_widget_n;
void _ZN2ux6wgtsys6WidgetC1Ev(void *self)
{
    static void (*real)(void *);
    if (!real) real = (void (*)(void *))dlsym(RTLD_NEXT, "_ZN2ux6wgtsys6WidgetC1Ev");
    unsigned long n = __atomic_fetch_add(&g_widget_n, 1, __ATOMIC_RELAXED);
    if (n < 10) logline("WIDGET_CTOR n=%lu self=%p caller=%p\n",
                        n + 1, self, __builtin_return_address(0));
    if (real) real(self);
}

/* The address is shared by the C2 variant, so one definition covers both. */
void _ZN2ux6wgtsys6WidgetC2Ev(void *self)
    __attribute__((alias("_ZN2ux6wgtsys6WidgetC1Ev")));

/* ★ Count and time the engine accessor that precedes the pump.
 *
 * The frame routine calls this accessor one instruction before it calls the
 * pump, and the pump has never been seen. Counting this accessor separates two
 * very different situations that look identical from the pump's zero alone:
 * either the routine is never entered, or it is entered and this accessor is
 * what fails to return. GFXENG_HOOK */
static unsigned long g_gfxeng_n;
void *_ZN8UXEngine6gfxengEv(void)
{
    static void *(*real)(void);
    if (!real) real = (void *(*)(void))dlsym(RTLD_NEXT, "_ZN8UXEngine6gfxengEv");
    unsigned long n = __atomic_fetch_add(&g_gfxeng_n, 1, __ATOMIC_RELAXED);
    logline("GFXENG enter n=%lu caller=%p\n", n + 1, __builtin_return_address(0));
    void *r = real ? real() : 0;
    logline("GFXENG leave n=%lu ret=%p\n", n + 1, r);
    return r;
}
