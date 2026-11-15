#define _GNU_SOURCE
#include "film_settings_source.h"
#include "film_settings_maps.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif

int film_settings_now(uint32_t *out) {
    struct timespec t;
#ifdef __linux__
    if (syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &t)) return -1;
#else
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return -1;
#endif
    *out = (uint32_t)((uint64_t)t.tv_sec * 1000u + (unsigned)t.tv_nsec / 1000000u);
    return 0;
}

int film_file_matches(const char *path, uint32_t bytes, uint32_t expected) {
    unsigned char data[4096];
    uint32_t hash = 2166136261u, total = 0;
    ssize_t n;
    /* Avoid modern fstat symbols absent from camera glibc2.11. Exact bounded
     * read+EOF enforces the pinned length; nonblocking also rejects FIFO data. */
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return 0;
    while ((n = read(fd, data, sizeof(data))) > 0) {
        if (total > bytes || (unsigned)n > bytes - total) { close(fd); return 0; }
        for (ssize_t i = 0; i < n; i++) hash = (hash ^ data[i]) * 16777619u;
        total += (unsigned)n;
    }
    close(fd);
    return n == 0 && total == bytes && hash == expected;
}

static int proc_path(char *out, unsigned capacity, const char *root, const char *name) {
    int n = snprintf(out, capacity, "%s/%s", root, name);
    return n > 0 && (unsigned)n < capacity;
}

int film_settings_source_get(struct film_settings *out, const struct film_settings_source *s) {
    char path[512], line[512];
    if (!out || !s || !s->proc || !s->module ||
        !film_file_matches(s->module, s->bytes, s->fnv)) return -17;
    if (!proc_path(path, sizeof(path), s->proc, "comm")) return -10;
    FILE *f = fopen(path, "r");
    if (!f) return -10;
    int ok = fgets(line, sizeof(line), f) && !strcmp(line, "im.elf\n");
    fclose(f);
    if (!ok) return -11;
    if (!proc_path(path, sizeof(path), s->proc, "maps")) return -12;
    f = fopen(path, "r");
    if (!f) return -12;
    struct film_settings_map map = {0};
    while (fgets(line, sizeof(line), f)) film_settings_map_line(&map, line, s->module, s->rva);
    ok = !ferror(f); fclose(f);
    if (!ok) return -12;
    if (!map.image_count) return 10;
    if (map.image_count != 1 || map.state_count != 1) return -13;
    if (!proc_path(path, sizeof(path), s->proc, "mem")) return -14;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -14;
    unsigned char first[FILM_UI_SNAPSHOT_BYTES], second[FILM_UI_SNAPSHOT_BYTES];
    ok = pread(fd, first, sizeof(first), map.state) == sizeof(first) &&
         pread(fd, second, sizeof(second), map.state) == sizeof(second);
    close(fd);
    if (!ok) return -15;
    uint32_t now;
    if (film_settings_now(&now)) return -16;
    return film_settings_snapshot_get(out, first, second, sizeof(first), now);
}
