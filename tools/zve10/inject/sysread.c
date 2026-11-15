/* 读目标进程若干地址的字节 + 写回。用于检查/恢复 hook 落点。 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "用法: %s <pid> <addr> <count> [hexbytes-to-write]\n", argv[0]);
        return 2;
    }
    int pid = atoi(argv[1]);
    unsigned long addr = strtoul(argv[2], 0, 0);
    int n = atoi(argv[3]);
    if (n < 1 || n > 64) n = 64;

    char mp[64];
    snprintf(mp, sizeof mp, "/proc/%d/mem", pid);
    int fd = open(mp, O_RDWR);
    if (fd < 0) { fprintf(stderr, "open mem: errno=%d\n", errno); return 1; }

    unsigned char buf[64];
    if (pread(fd, buf, n, (off_t)addr) != n) { fprintf(stderr, "read failed\n"); close(fd); return 1; }

    printf("addr=0x%08lX cur=", addr);
    for (int i = 0; i < n; i++) printf("%02x", buf[i]);
    printf("\n");

    if (argc >= 5) {
        const char *hex = argv[4];
        size_t hl = strlen(hex);
        if (hl != (size_t)n * 2) { fprintf(stderr, "hex length %zu != %d\n", hl, n * 2); close(fd); return 1; }
        unsigned char out[64];
        for (int i = 0; i < n; i++) {
            unsigned v;
            if (sscanf(hex + i * 2, "%2x", &v) != 1) { fprintf(stderr, "bad hex\n"); close(fd); return 1; }
            out[i] = (unsigned char)v;
        }
        if (pwrite(fd, out, n, (off_t)addr) != n) { fprintf(stderr, "write failed errno=%d\n", errno); close(fd); return 1; }
        unsigned char rb[64];
        if (pread(fd, rb, n, (off_t)addr) != n) { fprintf(stderr, "readback failed\n"); close(fd); return 1; }
        printf("wrote, readback=");
        for (int i = 0; i < n; i++) printf("%02x", rb[i]);
        printf(" %s\n", memcmp(rb, out, n) == 0 ? "MATCH" : "MISMATCH");
    }
    close(fd);
    return 0;
}
