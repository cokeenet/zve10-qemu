/* 直接 dlopen 真 init（它是 PIE 可执行还是 ET_EXEC？）
 * 如果是 ET_EXEC 不能 dlopen，改用 fork+exec 加 LD_DEBUG
 *
 * 更简单的思路：写一个 test 程序，完全复刻 init main 的那一段：
 *   check_mode_file()  → 读 dmode，设标志
 *   if (flag) set_oreore_env()  → 读 env.txt
 * 然后打印结果
 *
 * 这正是 set_oreore_env 的调用条件，能在 QEMU 里验证真实行为
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

/* 复刻 check_mode_file 的标志位含义（从反汇编 0x93B0 推出） */
static int flag_after_check_mode_file = 0;

static void check_mode_file_at(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("  [check_mode_file] open %s FAILED: %s\n", path, strerror(errno));
        return;                       /* 0x93D4 → 0x947C，不设标志 */
    }
    char buf[1024];
    memset(buf, 0, sizeof buf);
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);

    /* 0x9404: 无条件设 flag = 1（在 open 成功之后） */
    flag_after_check_mode_file = 1;
    printf("  [check_mode_file] dmode=\"%s\" (read %zd B) → flag=1\n",
           n > 0 ? buf : "(empty)", n);
}

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
    size_t i = 0; int applied = 0;
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
        if (memchr(s, '=', toklen) == NULL) { unsetenv(s); free(s); }
        else { putenv(s); applied++; }
    }
    return applied;
}

/* 复刻 set_oreore_env（0x98B0），每个失败出口都打印 */
static int set_oreore_env_replica(const char *PATH)
{
    int fd = open(PATH, O_RDONLY);
    if (fd < 0) {
        printf("  [set_oreore_env] open(%s) FAILED: %s (0x98C4 出口)\n",
               PATH, strerror(errno));
        return 0;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        printf("  [set_oreore_env] fstat FAILED\n"); close(fd); return 0;
    }
    size_t size = (size_t)st.st_size;
    printf("  [set_oreore_env] st_size=%zu\n", size);
    if (size == 0) {
        printf("  [set_oreore_env] size==0 (0x98CE 出口)\n"); close(fd); return 0;
    }
    void *p = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        printf("  [set_oreore_env] mmap FAILED: %s (0x98E4 出口)\n", strerror(errno));
        close(fd); return 0;
    }
    int n = parse_and_set_env((const char *)p, size);
    munmap(p, size);
    close(fd);
    printf("  [set_oreore_env] applied %d vars\n", n);
    return n;
}

int main(int argc, char **argv)
{
    const char *base = (argc > 1) ? argv[1] : "/setting";
    printf("=== init 的 env.txt 处理链复刻 ===\n\n");

    printf("[1] check_mode_file()\n");
    { char p[512]; snprintf(p, sizeof p, "%s/mode/dmode", base); check_mode_file_at(p); }

    printf("\n[2] 门禁 0x9B80\n");
    if (!flag_after_check_mode_file) {
        printf("  flag==0 → SKIP set_oreore_env（这就是失败场景）\n");
        return 1;
    }
    printf("  flag==1 → 调用 set_oreore_env\n\n");

    printf("[3] set_oreore_env()\n");
    { char p[512]; snprintf(p, sizeof p, "%s/env.txt", base); set_oreore_env_replica(p); }

    printf("\n[4] 最终环境\n");
    const char *v = getenv("LD_PRELOAD");
    printf("  LD_PRELOAD = %s\n", v ? v : "<未设置>");
    return v ? 0 : 1;
}
