/* 最小探针库：只记录哪个进程加载了自己 */
#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

static void w(const char *m, int n) {
    int fd = open("/log/tiny.log", O_WRONLY|O_CREAT|O_APPEND, 0644);
    if (fd >= 0) { write(fd, m, n); close(fd); }
}

__attribute__((constructor)) static void boot(void)
{
    char b[256]; int n = 0;
    const char *p = "TINY pid=";
    while (*p) b[n++] = *p++;
    int v = (int)getpid();
    char t[12]; int k = 0;
    if (!v) t[k++] = '0';
    while (v) { t[k++] = '0' + (v % 10); v /= 10; }
    while (k) b[n++] = t[--k];
    b[n++] = ' '; b[n++] = 'c'; b[n++] = '=';

    int fd = open("/proc/self/cmdline", O_RDONLY);
    if (fd >= 0) {
        char c[96];
        ssize_t r = read(fd, c, sizeof c - 1);
        close(fd);
        if (r > 0) {
            for (ssize_t i = 0; i < r && n < 240; i++)
                b[n++] = (c[i] == 0) ? '|' : (char)c[i];
        }
    }
    b[n++] = '\n';
    w(b, n);
}
