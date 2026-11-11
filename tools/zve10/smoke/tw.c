/* Zero-risk twin of im_wrapper: same code path, but execs busybox instead of
 * im.elf. Proves on the real camera that (a) the ARM ELF built here actually
 * runs under the camera's /lib/ld-linux.so.3, (b) every symbol it imports
 * resolves against glibc 2.11.2, and (c) LD_PRELOAD set here does reach the
 * exec'd child's loader. It cannot disturb the UI because the child is busybox.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#define LIB_PATH  "/usr/lib/libnewmenu_usr.so"
#define TEST_EXEC "/bin/busybox"
#define LOG_PATH  "/log/tw.log"

static int logfd = -1;
static void L(const char *fmt, ...)
{
    char b[768]; va_list ap; int n;
    va_start(ap, fmt); n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (n <= 0) return;
    if (logfd >= 0) { write(logfd, b, (size_t)n); fsync(logfd); }
}

int main(int argc, char **argv)
{
    extern char **environ;
    logfd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    L("=== tw pid=%d uid=%d argc=%d ===\n", getpid(), geteuid(), argc);
    L("lib_readable=%d\n", access(LIB_PATH, R_OK) == 0);

    setenv("LD_PRELOAD", LIB_PATH, 1);
    L("set LD_PRELOAD, exec " TEST_EXEC " sh -c 'grep -c libnewmenu /proc/self/maps'\n");

    char *nargv[] = { TEST_EXEC, "sh", "-c",
        "echo CHILD_PID=$$; grep -c libnewmenu /proc/self/maps", NULL };
    execve(TEST_EXEC, nargv, environ);
    L("FATAL execve: %s\n", strerror(errno));
    return 127;
}
