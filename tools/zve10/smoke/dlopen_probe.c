/* Prove whether /usr/lib/libnewmenu_usr.so can be dlopen'd on the camera
 * outside im.elf. Reports on stdout AND /log/dlopen_probe.log so the result
 * survives even if stdout goes to ttynull.
 *
 * Why this matters: /etc/ld.so.preload names both libraries and late processes
 * map both, yet im.elf maps neither and no library log exists. If dlopen works
 * here, the library is fine and the delivery order is the problem. If it fails,
 * the library itself is the problem and the preload path was never at fault.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

static int logfd = -1;

static void L(const char *fmt, ...)
{
    char b[512];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    write(1, b, (size_t)n);
    if (logfd >= 0) {
        write(logfd, b, (size_t)n);
        fsync(logfd);
    }
}

int main(void)
{
    logfd = open("/log/dlopen_probe.log", O_WRONLY | O_CREAT | O_APPEND, 0644);

    L("=== dlopen probe pid=%d uid=%d ===\n", getpid(), geteuid());

    /* 1. plain file access */
    L("access /usr/lib/libnewmenu_usr.so R_OK = %d\n",
      access("/usr/lib/libnewmenu_usr.so", R_OK));

    /* 2. dlopen RTLD_NOW (strict: resolves every symbol) */
    void *h = dlopen("/usr/lib/libnewmenu_usr.so", RTLD_NOW);
    L("dlopen(RTLD_NOW) = %p\n", h);
    if (!h) {
        const char *e = dlerror();
        L("  dlerror: %s\n", e ? e : "<null>");
    }

    /* 3. dlopen RTLD_LAZY as fallback */
    if (!h) {
        h = dlopen("/usr/lib/libnewmenu_usr.so", RTLD_LAZY);
        L("dlopen(RTLD_LAZY) = %p\n", h);
        if (!h) {
            const char *e = dlerror();
            L("  dlerror: %s\n", e ? e : "<null>");
        }
    }

    /* 4. exported symbols visible through the handle */
    if (h) {
        L("dlsym(menu_init) = %p\n", dlsym(h, "menu_init"));
        L("dlsym(GEN_Label_setText) = %p\n",
          dlsym(h, "_ZN9GEN_Label7setTextEj"));
    }

    /* 5. the agent library, for a same-boot control */
    void *a = dlopen("/etc/zve10_agent.so", RTLD_NOW);
    L("dlopen(agent RTLD_NOW) = %p\n", a);
    if (!a) {
        const char *e = dlerror();
        L("  dlerror: %s\n", e ? e : "<null>");
    }

    L("=== done ===\n");
    return 0;
}
