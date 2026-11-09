/* Included after ui-headless.h and the native tree/event/overlay implementation. */
#include "transport.h"
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <poll.h>

static int ne_post_function(uint32_t seq, uint32_t function);
static uint64_t ne_overlay_hash(uint32_t physical);

static pthread_mutex_t ne_transport_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ne_transport_cond = PTHREAD_COND_INITIALIZER;
static struct headless_snapshot ne_latest;
static struct ne_observation ne_observed;
static uint64_t ne_cache_frame, ne_retired_frame, ne_observed_frame;
static unsigned ne_latest_valid, ne_overlay_captured, ne_glyph_images;
static int ne_started, ne_stopped, ne_error_number;
static const char *ne_error_phase;

static void ne_transport_fail_locked(const char *phase, int error)
{
    if (!ne_error_phase) {
        ne_error_phase = phase;
        ne_error_number = error ? error : EIO;
    }
    pthread_cond_broadcast(&ne_transport_cond);
}

static void ne_transport_cache(const struct headless_snapshot *copies,
                               unsigned count, uint64_t frame)
{
    int selected = -1;
    unsigned overlay = 0;
    uint64_t fingerprint = 0;
    if (!copies || count < 1 || count > 2 || !frame) {
        pthread_mutex_lock(&ne_transport_mutex);
        ne_transport_fail_locked("cache_argument", EINVAL);
        pthread_mutex_unlock(&ne_transport_mutex);
        return;
    }
    for (unsigned i = 0; i < count; ++i) {
        if (!copies[i].pixels || copies[i].frame != frame || copies[i].index != i) {
            pthread_mutex_lock(&ne_transport_mutex);
            ne_transport_fail_locked("cache_identity", EPROTO);
            pthread_mutex_unlock(&ne_transport_mutex);
            return;
        }
        /* Inspect this transaction's copies, never the old retained diagnostic. */
        size_t offset;
        for (offset = 2; offset < HEADLESS_BYTES; offset += 2)
            if (memcmp(copies[i].pixels, copies[i].pixels + offset, 2)) break;
        if (offset == HEADLESS_BYTES) continue;
        uint64_t expected = ne_overlay_hash(copies[i].physical);
        uint64_t hash = expected ? headless_fingerprint(copies[i].pixels, HEADLESS_BYTES)
                                 : copies[i].fingerprint;
        if (selected < 0 || (expected && hash == expected)) {
            selected = (int)i;
            fingerprint = hash;
            overlay = expected && hash == expected;
        }
    }
    pthread_mutex_lock(&ne_transport_mutex);
    if (ne_stopped || ne_error_phase) goto out;
    if (frame <= ne_cache_frame) {
        ne_transport_fail_locked("cache_order", EPROTO);
        goto out;
    }
    ne_cache_frame = frame;
    ne_latest_valid = 0;
    if (selected >= 0) {
        unsigned char *pixels = ne_latest.pixels;
        if (!pixels) pixels = malloc(HEADLESS_BYTES);
        if (!pixels) {
            ne_transport_fail_locked("cache_allocate", ENOMEM);
            goto out;
        }
        memcpy(pixels, copies[selected].pixels, HEADLESS_BYTES);
        ne_latest = copies[selected];
        ne_latest.pixels = pixels;
        ne_latest.fingerprint = fingerprint;
        ne_latest_valid = 1;
        ne_overlay_captured = overlay;
    }
    pthread_cond_broadcast(&ne_transport_cond);
out:
    pthread_mutex_unlock(&ne_transport_mutex);
}

static void ne_transport_retired(uint64_t frame)
{
    pthread_mutex_lock(&ne_transport_mutex);
    if (!ne_stopped && !ne_error_phase) {
        if (!frame || frame > ne_cache_frame || frame < ne_retired_frame)
            ne_transport_fail_locked("retirement_order", EPROTO);
        else {
            ne_retired_frame = frame;
            pthread_cond_broadcast(&ne_transport_cond);
        }
    }
    pthread_mutex_unlock(&ne_transport_mutex);
}

static void ne_transport_ui(const struct ne_observation *observation, uint64_t frame)
{
    pthread_mutex_lock(&ne_transport_mutex);
    if (!ne_stopped && !ne_error_phase) {
        if (!observation || frame < ne_observed_frame)
            ne_transport_fail_locked("ui_argument", EINVAL);
        else {
            ne_observed = *observation;
            ne_observed_frame = frame;
            ne_glyph_images = native_glyph_images;
            pthread_cond_broadcast(&ne_transport_cond);
        }
    }
    pthread_mutex_unlock(&ne_transport_mutex);
}

static int ne_transport_line(const char *format, ...)
{
    char line[512];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (length <= 0 || (size_t)length >= sizeof line) {
        errno = EMSGSIZE;
        return -1;
    }
    int output = __sync_fetch_and_add(&ne_transport_fd, 0);
    if (output < 0) output = 2;
    for (size_t offset = 0; offset < (size_t)length;) {
        ssize_t written = write(output, line + offset, (size_t)length - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) {
            if (!written) errno = EIO;
            return -1;
        }
        offset += (size_t)written;
    }
    return 0;
}

static int ne_transport_coherent_locked(int initial)
{
    return ne_latest_valid && ne_latest.frame &&
        ne_retired_frame >= ne_latest.frame && ne_observed_frame == ne_latest.frame &&
        (!initial || (ne_observed.view && ne_observed.view_state == 1 &&
                      ne_observed.node_installed == 1 && ne_glyph_images >= 7));
}

static int ne_transport_wait_locked(int initial)
{
    while (!ne_error_phase && !ne_stopped && !ne_transport_coherent_locked(initial)) {
        int status = pthread_cond_wait(&ne_transport_cond, &ne_transport_mutex);
        if (status) ne_transport_fail_locked("condition_wait", status);
    }
    return ne_error_phase || ne_stopped ? -1 : 0;
}

/* headless_pages() calls _exit on errors; keep its checks with thread-local failure. */
static int ne_transport_pages(struct headless_snapshot *snapshot)
{
    if (!snapshot->pixels || sysconf(_SC_PAGESIZE) != 4096) {
        errno = EINVAL;
        return -1;
    }
    int fd = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    int error = 0;
    snapshot->segments = 0;
    for (size_t offset = 0; offset < HEADLESS_BYTES;) {
        uintptr_t address = (uintptr_t)snapshot->pixels + offset;
        uint64_t entry = 0;
        size_t bytes = 0;
        off_t position = (off_t)((address / 4096) * 8);
        while (bytes < sizeof entry) {
            ssize_t n = pread(fd, (unsigned char *)&entry + bytes,
                              sizeof entry - bytes, position + (off_t)bytes);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { error = n ? errno : EIO; goto out; }
            bytes += (size_t)n;
        }
        uint64_t pfn = entry & ((UINT64_C(1) << 55) - 1);
        uint64_t physical = pfn * 4096 + (address & 4095);
        size_t length = 4096 - (address & 4095);
        if (length > HEADLESS_BYTES - offset) length = HEADLESS_BYTES - offset;
        if (!(entry & (UINT64_C(1) << 63)) || (entry & (UINT64_C(1) << 62)) ||
            !pfn || pfn >= 0x40000u || physical > 0x40000000u - length ||
            snapshot->segments >= HEADLESS_MAX_PAGES) {
            error = EFAULT;
            goto out;
        }
        snapshot->page[snapshot->segments++] =
            (struct headless_segment){physical, offset, length};
        offset += length;
    }
out:
    if (close(fd) && !error) error = errno;
    if (error) { errno = error; return -1; }
    return 0;
}

static int ne_transport_export(const struct headless_snapshot *snapshot,
                               const struct ne_observation *state,
                               uint32_t capture_id, unsigned overlay)
{
    if (ne_transport_line("NE_STATE {\"capture_id\":%" PRIu32
        ",\"pid\":%" PRIu32 ",\"ui_tid\":%" PRIu32 ",\"view\":%" PRIu32
        ",\"view_state\":%" PRIu32 ",\"submode\":%" PRIu32 ",\"parent\":%" PRIu32
        ",\"count\":%" PRIu32 ",\"selected_index\":%" PRIu32 ",\"selected_own\":%" PRIu32
        ",\"node_installed\":%" PRIu32 ",\"title_sets\":%" PRIu32 ",\"overlay\":%" PRIu32
        ",\"category\":%" PRIu32 ",\"entries\":%" PRIu32 ",\"exits\":%" PRIu32
        ",\"last_input\":%" PRIu32 "}\n",
        capture_id, state->pid, state->ui_tid, state->view, state->view_state,
        state->submode, state->parent, state->count, state->selected_index,
        state->selected_own, state->node_installed, state->title_sets,
        state->overlay, state->category, state->entries, state->exits, state->last_input) ||
        ne_transport_line("UI_CAPTURE_META {\"frame\":%" PRIu64 ",\"record_index\":%u,\"bitmap\":%" PRIu32
        ",\"source_physical\":%" PRIu32 ",\"width\":640,\"height\":480,\"format\":1,\"bpp\":2,\"stride\":1280,"
        "\"byte_length\":%u,\"segments\":%u,\"fingerprint_fnv1a64\":\"%016" PRIx64
        "\",\"committed_menu_frame\":false,\"capture_id\":%" PRIu32 ",\"overlay_captured\":%s}\n",
        snapshot->frame, snapshot->index, snapshot->bitmap, snapshot->physical,
        (unsigned)HEADLESS_BYTES, snapshot->segments, snapshot->fingerprint,
        capture_id, overlay ? "true" : "false")) return -1;
    for (unsigned i = 0; i < snapshot->segments; ++i)
        if (ne_transport_line("UI_CAPTURE_PAGE {\"frame\":%" PRIu64 ",\"index\":%u,\"physical\":%" PRIu64
            ",\"capture_offset\":%zu,\"length\":%zu}\n", snapshot->frame, i,
            snapshot->page[i].physical, snapshot->page[i].offset, snapshot->page[i].length)) return -1;
    return ne_transport_line("UI_CAPTURE_READY {\"frame\":%" PRIu64 ",\"segments\":%u,\"capture_id\":%" PRIu32 "}\n",
                             snapshot->frame, snapshot->segments, capture_id);
}

struct ne_kernel_termios {
    uint32_t iflag, oflag, cflag, lflag;
    unsigned char line, cc[19];
};
_Static_assert(sizeof(struct ne_kernel_termios) == 36, "kernel termios ABI");

static int ne_transport_ioctl(int fd, unsigned long request, void *value)
{
    int status;
    do { status = ioctl(fd, request, value); } while (status < 0 && errno == EINTR);
    return status;
}

static int ne_transport_readline(int fd, char *line, size_t capacity)
{
    size_t length = 0;
    for (;;) {
        pthread_mutex_lock(&ne_transport_mutex);
        int error = ne_error_phase ? ne_error_number : (ne_stopped ? ECANCELED : 0);
        pthread_mutex_unlock(&ne_transport_mutex);
        if (error) { errno = error; return -1; }
        struct pollfd input = {fd, POLLIN, 0};
        int ready = poll(&input, 1, 250);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) return -1;
        if (!ready) continue;
        if (input.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            errno = EIO;
            return -1;
        }
        if (!(input.revents & POLLIN)) continue;
        unsigned char byte;
        ssize_t n = read(fd, &byte, 1);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { if (!n) errno = EPIPE; return -1; }
        if (byte == '\n') { line[length] = 0; return 0; }
        if (byte < 32 || byte > 126) { errno = EPROTO; return -1; }
        if (length + 1 >= capacity) { errno = EMSGSIZE; return -1; }
        line[length++] = (char)byte;
    }
}

static int ne_transport_ticket(const char **text, uint32_t *ticket)
{
    const char *p = *text;
    uint32_t value = 0;
    if (*p < '1' || *p > '9') return -1;
    do {
        unsigned digit = (unsigned)(*p - '0');
        if (value > (UINT32_MAX - digit) / 10) return -1;
        value = value * 10 + digit;
        ++p;
    } while (*p >= '0' && *p <= '9');
    *text = p;
    *ticket = value;
    return 0;
}

static void *ne_transport_control(void *unused)
{
    (void)unused;
    int fd = -1, error = 0, mode_saved = 0, termios_saved = 0, pinned = 0, stopped = 0;
    const char *phase = "uart_open";
    uint32_t old_mode = 0, mode = 1, capture_id = 0, key_seq = 0;
    struct ne_kernel_termios original, raw, actual;
    struct headless_snapshot retained = {0};
    struct ne_observation state;
    unsigned overlay;
    fd = open("/dev/ttyAM0", O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) goto failed;
    phase = "uart_getmode";
    if (ne_transport_ioctl(fd, _IOR('u', 1, uint32_t), &old_mode)) goto failed;
    mode_saved = 1;
    phase = "uart_setmode";
    if (ne_transport_ioctl(fd, _IOW('u', 2, uint32_t), &mode)) goto failed;
    phase = "uart_readback";
    mode = 0;
    if (ne_transport_ioctl(fd, _IOR('u', 1, uint32_t), &mode)) goto failed;
    if (mode != 1) { errno = EPROTO; goto failed; }
    phase = "termios_get";
    if (ne_transport_ioctl(fd, 0x5401, &original)) goto failed;
    termios_saved = 1;
    raw = original;
    raw.iflag = raw.oflag = raw.lflag = 0;
    raw.cflag = (raw.cflag & ~UINT32_C(0x130)) | UINT32_C(0x8b0);
    raw.cc[6] = 1;
    raw.cc[5] = 0;
    phase = "termios_set";
    if (ne_transport_ioctl(fd, 0x5402, &raw)) goto failed;
    phase = "termios_readback";
    if (ne_transport_ioctl(fd, 0x5401, &actual)) goto failed;
    if (memcmp(&raw, &actual, sizeof raw)) { errno = EPROTO; goto failed; }
    __sync_lock_test_and_set(&ne_transport_fd, fd);
    pthread_mutex_lock(&ne_transport_mutex);
    if (ne_transport_wait_locked(1)) {
        phase = ne_error_phase ? ne_error_phase : "stopped";
        error = ne_error_number ? ne_error_number : ECANCELED;
        pthread_mutex_unlock(&ne_transport_mutex);
        goto cleanup;
    }
    state = ne_observed;
    uint64_t ready_frame = ne_latest.frame;
    unsigned glyphs = ne_glyph_images;
    pthread_mutex_unlock(&ne_transport_mutex);
    phase = "ready_write";
    if (ne_transport_line("NE_READY {\"frame\":%" PRIu64 ",\"pid\":%" PRIu32
        ",\"ui_tid\":%" PRIu32 ",\"view\":%" PRIu32 ",\"title_sets\":%" PRIu32
        ",\"native_glyph_images\":%u}\n", ready_frame, state.pid,
        state.ui_tid, state.view, state.title_sets, glyphs)) goto failed;
    for (;;) {
        char line[96];
        phase = "command_read";
        if (ne_transport_readline(fd, line, sizeof line)) goto failed;
        pthread_mutex_lock(&ne_transport_mutex);
        if (ne_error_phase) {
            phase = ne_error_phase;
            error = ne_error_number;
        }
        pthread_mutex_unlock(&ne_transport_mutex);
        if (error) goto cleanup;
        const char *argument;
        uint32_t ticket;
        phase = "command";
        if (!strncmp(line, "CAPTURE ", 8)) {
            argument = line + 8;
            if (ne_transport_ticket(&argument, &ticket) || *argument || ticket <= capture_id) {
                errno = EPROTO; goto failed;
            }
            if (retained.pixels) { errno = EBUSY; goto failed; }
            phase = "capture_allocate";
            retained.pixels = malloc(HEADLESS_BYTES);
            if (!retained.pixels) { errno = ENOMEM; goto failed; }
            pthread_mutex_lock(&ne_transport_mutex);
            if (ne_transport_wait_locked(0)) {
                phase = ne_error_phase ? ne_error_phase : "stopped";
                error = ne_error_number ? ne_error_number : ECANCELED;
                pthread_mutex_unlock(&ne_transport_mutex);
                goto cleanup;
            }
            unsigned char *pixels = retained.pixels;
            retained = ne_latest;
            retained.pixels = pixels;
            memcpy(retained.pixels, ne_latest.pixels, HEADLESS_BYTES);
            state = ne_observed;
            overlay = ne_overlay_captured;
            pthread_mutex_unlock(&ne_transport_mutex);
            phase = "capture_pin";
            if (mlock(retained.pixels, HEADLESS_BYTES)) goto failed;
            pinned = 1;
            phase = "capture_pages";
            if (ne_transport_pages(&retained)) goto failed;
            retained.fingerprint = headless_fingerprint(retained.pixels, HEADLESS_BYTES);
            if (state.overlay) {
                uint64_t rendered = ne_overlay_hash(retained.physical);
                overlay = overlay && rendered && rendered == retained.fingerprint;
            }
            capture_id = ticket;
            phase = "capture_write";
            if (ne_transport_export(&retained, &state, capture_id, overlay)) goto failed;
        } else if (!strncmp(line, "ACK ", 4)) {
            argument = line + 4;
            if (ne_transport_ticket(&argument, &ticket) || *argument ||
                !retained.pixels || ticket != capture_id) { errno = EPROTO; goto failed; }
            phase = "capture_unpin";
            if (munlock(retained.pixels, HEADLESS_BYTES)) goto failed;
            pinned = 0;
            free(retained.pixels);
            retained.pixels = NULL;
            phase = "ack_write";
            if (ne_transport_line("NE_ACK %" PRIu32 "\n", ticket)) goto failed;
        } else if (!strncmp(line, "KEY ", 4)) {
            argument = line + 4;
            if (ne_transport_ticket(&argument, &ticket) || *argument != ' ' || ticket <= key_seq) {
                errno = EPROTO; goto failed;
            }
            if (retained.pixels) { errno = EBUSY; goto failed; }
            ++argument;
            static const char *const names[] = {"DOWN", "UP", "RIGHT", "LEFT", "ENTER", "MENU"};
            uint32_t function;
            for (function = 1; function <= 6; ++function)
                if (!strcmp(argument, names[function - 1])) break;
            if (function > 6) { errno = EPROTO; goto failed; }
            phase = "key_enqueue";
            if (ne_post_function(ticket, function)) { errno = EIO; goto failed; }
            key_seq = ticket;
            phase = "key_write";
            if (ne_transport_line("NE_KEY_SENT {\"seq\":%" PRIu32 ",\"function\":%" PRIu32 "}\n",
                                  ticket, function)) goto failed;
        } else if (!strcmp(line, "STOP")) {
            if (retained.pixels) { errno = EBUSY; goto failed; }
            stopped = 1;
            goto cleanup;
        } else { errno = EPROTO; goto failed; }
    }
failed:
    pthread_mutex_lock(&ne_transport_mutex);
    phase = ne_error_phase ? ne_error_phase : phase;
    error = ne_error_phase ? ne_error_number : (errno ? errno : EIO);
    pthread_mutex_unlock(&ne_transport_mutex);
cleanup:
    pthread_mutex_lock(&ne_transport_mutex);
    ne_stopped = 1;
    ne_latest_valid = 0;
    free(ne_latest.pixels);
    memset(&ne_latest, 0, sizeof ne_latest);
    pthread_cond_broadcast(&ne_transport_cond);
    pthread_mutex_unlock(&ne_transport_mutex);
    if (pinned && munlock(retained.pixels, HEADLESS_BYTES)) {
        if (!error) { phase = "cleanup_unpin"; error = errno; }
        else record("native.transport.cleanup_unpin", (uintptr_t)errno);
    }
    free(retained.pixels);
    if (termios_saved && ne_transport_ioctl(fd, 0x5402, &original)) {
        if (!error) { phase = "termios_restore"; error = errno; }
        else record("native.transport.termios_restore", (uintptr_t)errno);
    }
    if (mode_saved && ne_transport_ioctl(fd, _IOW('u', 2, uint32_t), &old_mode)) {
        if (!error) { phase = "uart_restore"; error = errno; }
        else record("native.transport.uart_restore", (uintptr_t)errno);
    }
    __sync_lock_test_and_set(&ne_transport_fd, -1);
    if (fd >= 0 && close(fd)) {
        if (!error) { phase = "uart_close"; error = errno; }
        else record("native.transport.uart_close", (uintptr_t)errno);
    }
    if (error) {
        if (ne_transport_line("NE_ERROR {\"phase\":\"%s\",\"errno\":%d}\n", phase, error))
            record("native.transport.error_write", (uintptr_t)errno);
    } else if (stopped && ne_transport_line("NE_STOPPED\n"))
        record("native.transport.stopped_write", (uintptr_t)errno);
    return NULL;
}

static void ne_transport_start(void)
{
    pthread_mutex_lock(&ne_transport_mutex);
    if (ne_started || ne_stopped) {
        pthread_mutex_unlock(&ne_transport_mutex);
        return;
    }
    ne_started = 1;
    pthread_attr_t attributes;
    int status = pthread_attr_init(&attributes);
    int initialized = !status;
    if (!status) status = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    if (!status) status = pthread_create(&thread, &attributes, ne_transport_control, NULL);
    if (initialized) pthread_attr_destroy(&attributes);
    if (status) {
        ne_stopped = 1;
        ne_latest_valid = 0;
        free(ne_latest.pixels);
        memset(&ne_latest, 0, sizeof ne_latest);
    }
    pthread_mutex_unlock(&ne_transport_mutex);
    if (status && ne_transport_line("NE_ERROR {\"phase\":\"thread_start\",\"errno\":%d}\n", status))
        record("native.transport.error_write", (uintptr_t)errno);
}
