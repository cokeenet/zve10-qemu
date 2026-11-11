/* Replace /usr/bin/im.elf with this wrapper, via a bind mount, so the menu
 * library reaches im.elf on a snapshot-resume boot.
 *
 * Why a wrapper instead of relying on /etc/ld.so.preload: measured on the live
 * camera, every process created after the resume point maps both preload
 * libraries and every process created before it maps neither, with a clean
 * boundary at starttime ~2148. im.elf (starttime 104) is a restored image, not
 * a fresh exec, so its loader never runs and never reads the preload file.
 *
 * A bind-mounted wrapper forces a real exec. bootin execs the hard-coded path
 * /usr/bin/im.elf, so with the wrapper bound over it, bootin's exec runs this
 * process, whose loader DOES read /etc/ld.so.preload. We additionally set
 * LD_PRELOAD explicitly before exec'ing the real binary, so the library loads
 * even if the preload file becomes absent or unreadable.
 *
 * The wrapper deliberately does NOT dlopen the library. Doing so would run its
 * constructor inside the wrapper, which is not the UI process and lacks the
 * libraries the constructor probes; the library's own log must record im.elf,
 * not the wrapper.
 *
 * The wrapper never changes what the real binary does. It only builds the
 * environment and execs. Setting LD_PRELOAD alone is harmless: the library's
 * constructor exits immediately for any process whose cmdline lacks "im.elf",
 * and /z_safe makes it observation-only even there.
 *
 * Diagnostics go to /log/nm_wrapper.log because im.elf's stderr is
 * /dev/console, which is invisible over the network.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#define LIB_PATH   "/usr/lib/libnewmenu_usr.so"
#define REAL_PATH  "/usr/bin/im.elf.real"
#define LOG_PATH   "/log/nm_wrapper.log"

static int logfd = -1;

static void L(const char *fmt, ...)
{
    char b[768];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (logfd >= 0) {
        write(logfd, b, (size_t)n);
        fsync(logfd);
    }
}

int main(int argc, char **argv)
{
    extern char **environ;
    char self[256] = "?";

    logfd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);

    ssize_t rl = readlink("/proc/self/exe", self, sizeof self - 1);
    if (rl > 0) self[rl] = 0;

    L("=== wrapper pid=%d uid=%d exe=%s argc=%d argv0=%s ===\n",
      getpid(), geteuid(), self, argc, argc > 0 ? argv[0] : "<null>");
    L("lib=%d real=%d force=%d safe=%d\n",
      access(LIB_PATH, R_OK) == 0,
      access(REAL_PATH, F_OK) == 0,
      access("/z_nm_force", F_OK) == 0,
      access("/z_safe", F_OK) == 0);

    /* If the real binary is missing, exec'ing it would leave the camera with no
       UI at all. Failing loudly here keeps bootin's child status honest. */
    if (access(REAL_PATH, X_OK) != 0) {
        L("FATAL: %s not executable (%s); leaving UI down\n",
          REAL_PATH, strerror(errno));
        return 127;
    }

    /* The library's constructor only acts when /proc/self/cmdline contains
       "im.elf". The real binary's argv[0] is /usr/bin/im.elf.real, which
       contains it, so the gate passes in the process that matters. */
    setenv("LD_PRELOAD", LIB_PATH, 1);
    L("set LD_PRELOAD=" LIB_PATH ", exec %s\n", REAL_PATH);

    argv[0] = REAL_PATH;
    execve(REAL_PATH, argv, environ);

    L("FATAL: execve(%s) failed: %s\n", REAL_PATH, strerror(errno));
    return 127;
}
