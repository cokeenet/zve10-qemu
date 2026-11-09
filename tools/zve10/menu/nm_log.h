/* nm_log.h —— 新菜单的日志输出。
 *
 * 原来只写 /log/newmenu.log。但真机上"没有日志"有两种完全不同的含义：
 *   (a) 库根本没加载
 *   (b) 库加载了，但 open("/log/newmenu.log") 失败
 * 而 (b) 会被静默吞掉 —— 这正是真机排查卡住的地方。
 *
 * 现在改成：
 *   1. 依次尝试 /log、/tmp、/etc 三个位置（都在 vfat 上，通常至少一个可写）
 *   2. 全部失败时仍然写 stderr（QEMU serial 可见）
 *   3. 用 nm_log_path 记录实际写到哪里，方便事后判断
 *
 * 注意 budget 是【每进程】的静态变量。调用方必须先确认是 UI 进程再调用，
 * 否则相机每秒起很多进程，每个都从 600 重新计，/log 会被写爆。
 */
#ifndef NM_LOG_H
#define NM_LOG_H

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* 按可写性从高到低尝试。都是 vfat 上的持久分区，fastboot / agent 事后能读。 */
static const char *const nm_log_candidates[] = {
    "/log/newmenu.log",
    "/tmp/newmenu.log",
    "/etc/newmenu.log",
    0
};

/* 第一次成功后就固定下来，避免每行都试三个路径。 */
static const char *nm_log_path;

static int nm_log_open(void)
{
    if (nm_log_path) {
        return open(nm_log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    }
    for (int i = 0; nm_log_candidates[i]; i++) {
        int fd = open(nm_log_candidates[i], O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            nm_log_path = nm_log_candidates[i];
            return fd;
        }
    }
    return -1;
}

static int nm_log_budget = 600;

static void nm_log(const char *format, ...)
{
    if (nm_log_budget <= 0) return;
    nm_log_budget--;

    char text[512];
    va_list args;
    va_start(args, format);
    int size = vsnprintf(text, sizeof text, format, args);
    va_end(args);
    if (size <= 0) return;

    /* stderr 一直写：QEMU serial 直接可见，真机上若接了控制台也能看到。 */
    {
        char pre[16] = "NM ";
        write(2, pre, 3);
        write(2, text, size);
    }

    int fd = nm_log_open();
    if (fd < 0) return;
    ssize_t off = 0;
    while (off < size) {
        ssize_t n = write(fd, text + off, (size_t)(size - off));
        if (n <= 0) break;
        off += n;
    }
    fsync(fd);
    close(fd);
}

/* 供调用方报告"日志到底写到了哪" —— 没写成功时返回 "<none>"。 */
static const char *nm_log_where(void)
{
    return nm_log_path ? nm_log_path : "<none>";
}

#endif
