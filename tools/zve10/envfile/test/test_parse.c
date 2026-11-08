/* 模拟 init 的 parse_and_set_env，验证 env.txt 格式 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

/* 对照反汇编：
 *   skip_comment_and_space: 跳过空白和 '#' 注释
 *   收集到空白/NUL 为止
 *   memchr 找 '='
 *   有 '=' → putenv，无 '=' → unsetenv
 */
static size_t skip_comment_and_space(const char *p, size_t len)
{
    size_t i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)p[i];
        if (c == 0) { i++; continue; }
        if (isspace(c)) { i++; continue; }
        if (c == '#') {
            /* 跳到行尾 */
            while (i < len && p[i] != '\n') i++;
            continue;
        }
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

        /* 收集一个 token，直到空白 */
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
            unsetenv(s);
            free(s);
        } else {
            printf("  putenv(%s)\n", s);
            putenv(s);      /* 故意不 free —— 和 init 一样 */
            applied++;
        }
    }
    return applied;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <file>\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz ? sz : 1);
    if (fread(buf, 1, sz, f) != (size_t)sz) { perror("read"); return 1; }
    fclose(f);

    printf("=== 解析 %s (%ld B) ===\n", argv[1], sz);
    int n = parse_and_set_env(buf, sz);
    printf("=== 应用了 %d 个变量 ===\n", n);

    const char *v = getenv("LD_PRELOAD");
    printf("\nLD_PRELOAD = %s\n", v ? v : "<未设置>");
    return 0;
}
