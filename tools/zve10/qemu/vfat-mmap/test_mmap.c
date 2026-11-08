/* 复刻 init 的 set_oreore_env（0x98B0）的读取逻辑，验证在 vfat 上是否可行
 *
 * 反汇编还原：
 *   fd = open("/setting/env.txt", O_RDONLY);      0x98BA
 *   if (fd < 0) return 0;                          0x98C4
 *   fstat(fd, &st);                                0x98C8
 *   size = st.st_size;                             0x98CC
 *   if (!size) return 0;                           0x98CE
 *   p = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);   0x98E0
 *   if (p == MAP_FAILED) { fprintf(...); return 0; }      0x98E4
 *   parse_and_set_env(p, size);                    0x9910
 *   munmap(p, size);
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/mman.h>

static size_t skip_comment_and_space(const char *p, size_t len)
{
    size_t i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)p[i];
        if (c == 0) { i++; continue; }
        if (isspace(c)) { i++; continue; }
        if (c == '#') { while (i < len && p[i] != '\n') i++; continue; }
        break;
    }
    return i;
}

static int parse_and_set_env(const char *buf, size_t len)
{
    size_t i = 0;
    int applied = 0;
    while (i < len) {
        i += skip_comment_and_space(buf + i, len - i);
        if (i >= len) break;
        size_t start = i;
        while (i < len && !isspace((unsigned char)buf[i]) && buf[i] != 0) i++;
        size_t toklen = i - start;
        if (toklen == 0) { i++; continue; }
        if (toklen > 1024) toklen = 1024;
        char *s = malloc(toklen + 1);
        if (!s) return applied;
        memcpy(s, buf + start, toklen);
        s[toklen] = 0;
        if (memchr(s, '=', toklen) == NULL) {
            printf("  unsetenv(%s)\n", s);
            unsetenv(s); free(s);
        } else {
            printf("  putenv(%s)\n", s);
            putenv(s); applied++;
        }
    }
    return applied;
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/setting/env.txt";
    printf("=== set_oreore_env 复刻: %s ===\n", path);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("✗ [0x98C4] open 失败: %s (errno=%d)\n", strerror(errno), errno);
        return 1;
    }
    printf("✓ open ok fd=%d\n", fd);

    struct stat st;
    if (fstat(fd, &st) != 0) {
        printf("✗ fstat 失败: %s\n", strerror(errno)); close(fd); return 1;
    }
    printf("✓ fstat ok: st_size=%lld\n", (long long)st.st_size);

    size_t size = (size_t)st.st_size;
    if (size == 0) {
        printf("✗ [0x98CE] st_size == 0，直接返回\n"); close(fd); return 1;
    }

    void *p = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        printf("✗ [0x98E4] mmap FAILED: %s (errno=%d)\n", strerror(errno), errno);
        printf("   这是真机上 LD_PRELOAD 丢失的最可能原因\n");
        close(fd); return 1;
    }
    printf("✓ mmap ok p=%p\n", p);

    printf("--- 文件内容 ---\n");
    fwrite(p, 1, size, stdout);
    printf("--- 内容结束 ---\n");

    int n = parse_and_set_env((const char *)p, size);
    munmap(p, size);
    close(fd);

    printf("=== 应用了 %d 个变量 ===\n", n);
    const char *v = getenv("LD_PRELOAD");
    printf("LD_PRELOAD = %s\n", v ? v : "<未设置>");
    return (v && n > 0) ? 0 : 1;
}
