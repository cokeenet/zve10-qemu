/* 在 QEMU 里跑，检查 bootin 启动 im.elf 时 envp 的实际来源
 *
 * 做法：不 hook bootin，而是做一个【假的 im.elf】
 *   替换 /usr/bin/im.elf 为一个打印自己 environ 的程序
 *   这样能看到 bootin 到底传了什么
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

extern char **environ;

int main(int argc, char **argv)
{
    printf("=== FAKE im.elf ===\n");
    printf("argc=%d\n", argc);
    for (int i = 0; i < argc; i++)
        printf("  argv[%d] = %s\n", i, argv[i] ? argv[i] : "(null)");

    printf("\n--- environ (%d entries) ---\n", (int)! !environ);
    if (environ) {
        for (char **e = environ; *e; e++)
            printf("  %s\n", *e);
    }

    printf("\n--- /proc/self/environ (kernel copy) ---\n");
    FILE *f = fopen("/proc/self/environ", "rb");
    if (f) {
        char buf[4096];
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[n] = 0;
        for (size_t i = 0; i < n; i++) if (buf[i] == 0) buf[i] = '\n';
        printf("%s\n", buf);
    }
    printf("=== END FAKE ===\n");
    return 0;
}
