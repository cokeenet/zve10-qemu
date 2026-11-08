/* nm_log.h —— 新菜单的日志输出。
 *
 * 原来写 stderr，但 im.elf 的 stderr 在真机上不可见。
 * 改成写 /log/newmenu.log，可以事后用 fastboot 读出来。
 */
#ifndef NM_LOG_H
#define NM_LOG_H

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define NM_LOG_PATH "/log/newmenu.log"

static int nm_log_budget = 600;      /* 防止写爆 /log（启动期 dlopen 重试会刷很多行） */

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

    /* 同时写 stderr（QEMU serial 可见，真机上被丢） */
    {
        char pre[16] = "NM ";
        write(2, pre, 3);
        write(2, text, size);
    }

    int fd = open(NM_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        ssize_t off = 0;
        while (off < size) {
            ssize_t n = write(fd, text + off, (size_t)(size - off));
            if (n <= 0) break;
            off += n;
        }
        fsync(fd);
        close(fd);
    }
}

#endif
