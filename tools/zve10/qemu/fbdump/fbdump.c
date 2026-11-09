/* 从 /dev/mem 读帧缓冲，写成 PPM（便于转换 PNG）
 * 用法: fbdump <物理地址> <宽> <高> <bpp> <输出文件>
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr, "usage: %s <phys> <w> <h> <bpp> <out.ppm>\n", argv[0]);
        return 2;
    }
    unsigned long phys = strtoul(argv[1], NULL, 0);
    int w = atoi(argv[2]), h = atoi(argv[3]), bpp = atoi(argv[4]);
    const char *out = argv[5];
    size_t len = (size_t)w * h * (bpp / 8);

    printf("=== fbdump: phys=0x%lX %dx%d %dbpp -> %s (%zu B) ===\n",
           phys, w, h, bpp, out, len);

    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) { printf("open /dev/mem failed: %s\n", strerror(errno)); return 1; }

    unsigned long page = 4096;
    unsigned long base = phys & ~(page - 1);
    size_t delta = phys - base;
    size_t maplen = ((delta + len + page - 1) / page) * page;

    void *p = mmap(NULL, maplen, PROT_READ, MAP_SHARED, fd, base);
    if (p == MAP_FAILED) {
        printf("mmap failed: %s (base=0x%lX len=%zu)\n", strerror(errno), base, maplen);
        close(fd);
        return 1;
    }
    const uint8_t *src = (const uint8_t *)p + delta;

    /* 写 PPM (P6) */
    FILE *f = fopen(out, "wb");
    if (!f) { printf("fopen %s failed\n", out); munmap(p, maplen); close(fd); return 1; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t r, g, b;
            if (bpp == 16) {
                uint16_t v = src[(size_t)y * w * 2 + x * 2] |
                             (src[(size_t)y * w * 2 + x * 2 + 1] << 8);
                /* RGB565 */
                r = ((v >> 11) & 0x1F) << 3;
                g = ((v >> 5)  & 0x3F) << 2;
                b = ( v        & 0x1F) << 3;
            } else if (bpp == 32) {
                uint32_t v = *(const uint32_t *)&src[(size_t)y * w * 4 + x * 4];
                r = (v >> 16) & 0xFF; g = (v >> 8) & 0xFF; b = v & 0xFF;
            } else {
                r = g = b = src[(size_t)y * w + x];
            }
            fputc(r, f); fputc(g, f); fputc(b, f);
        }
    }
    fclose(f);
    munmap(p, maplen);
    close(fd);
    printf("=== wrote %s ===\n", out);
    return 0;
}
