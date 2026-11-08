/* 假的 im.elf：打印启动瞬间的 /etc、/setting 可见性 + 完整环境
 * 用于验证 bootin execve(im.elf) 那一刻，各分区是否已挂载
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

extern char **environ;

static void check(const char *path, const char *label)
{
    struct stat st;
    int r = stat(path, &st);
    printf("  %-32s %s", label, r == 0 ? "OK" : "MISSING");
    if (r == 0) printf("  (size=%lld, mode=%o)", (long long)st.st_size, st.st_mode & 0777);
    else printf("  (%s)", strerror(errno));
    printf("\n");
}

static void show_file(const char *path, const char *label)
{
    int fd = open(path, O_RDONLY);
    printf("  %-32s ", label);
    if (fd < 0) { printf("cannot open (%s)\n", strerror(errno)); return; }
    char buf[256];
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) { printf("empty\n"); return; }
    buf[n] = 0;
    for (ssize_t i = 0; i < n; i++) if (buf[i] == '\n') buf[i] = '|';
    printf("[%s]\n", buf);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("\n========== FAKE im.elf (mount timing probe) ==========\n");

    printf("[mounts]\n");
    FILE *f = fopen("/proc/mounts", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof line, f)) {
            if (strstr(line, " / ") || strstr(line, " /etc ") ||
                strstr(line, " /setting ") || strstr(line, " /usr ") ||
                strstr(line, " /system ")) {
                printf("  %s", line);
            }
        }
        fclose(f);
    }

    printf("\n[paths]\n");
    check("/etc", "/etc dir");
    check("/etc/ld.so.preload", "/etc/ld.so.preload");
    check("/etc/toprc", "/etc/toprc");
    check("/setting", "/setting dir");
    check("/setting/env.txt", "/setting/env.txt");
    check("/usr/bin/im.elf", "/usr/bin/im.elf");

    printf("\n[files]\n");
    show_file("/etc/ld.so.preload", "/etc/ld.so.preload");
    show_file("/setting/env.txt", "/setting/env.txt");
    show_file("/setting/mode/dmode", "/setting/mode/dmode");

    printf("\n[environ]\n");
    for (char **e = environ; e && *e; e++)
        printf("  %s\n", *e);

    printf("========== END ==========\n\n");
    return 0;
}
