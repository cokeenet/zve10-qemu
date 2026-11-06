/* payload_patch_caution.c — 原位改写 /usr/lib/CautionConfig.so 的一个字节
 *
 * 目标：ZV-E10 CautionConfig.so 偏移 0xD778E4：0x20 → 0x1B
 *       （切换 WiFi 设置页类型 TypeRK → TypeNB，解锁原生 2.4/5GHz 频带菜单）
 *
 * 安全约束（来自 ZV-E10-5GHz-WiFi-Unlock 的实机事故记录）：
 *   · 绝不整文件 cp —— 分区空间不足会把库截短，导致需要硬件 fastboot 救砖
 *   · 绝不用 busybox dd —— 没有 conv=notrunc，seek 写入会截断文件
 *   · 因此：O_RDWR 打开既有文件（不带 O_CREAT / O_TRUNC），定位后单字节写入
 *
 * 流程：
 *   1. mount -o remount,rw /usr
 *   2. open(O_RDWR) → 定位 0xD778E4 → 读回核对 == 0x20
 *   3. 写入 0x1B → fsync → 读回核对
 *   4. mount -o remount,ro /usr
 *   5. 结果写入 /log/.zve10_patch_result
 *
 * 构建见 build.sh：-nostdlib 纯 syscall，不依赖目标机 libc。
 */
#define SYS_exit   1
#define SYS_read   3
#define SYS_write  4
#define SYS_open   5
#define SYS_close  6
#define SYS_lseek 19
#define SYS_mount 21
#define SYS_fsync 118

#define OFF       0xD778E4u
#define OLD_BYTE  0x20
#define NEW_BYTE  0x1B

#define MS_RDONLY  1
#define MS_REMOUNT 32

static inline int s0(int n){register int r7 __asm__("r7")=n;register int r0 __asm__("r0");__asm__ volatile("svc #0":"=r"(r0):"r"(r7):"memory");return r0;}
static inline int s1(int n,int a){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;__asm__ volatile("svc #0":"+r"(r0):"r"(r7):"memory");return r0;}
static inline int s2(int n,int a,int b){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1):"memory");return r0;}
static inline int s3(int n,int a,int b,int c){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2):"memory");return r0;}
static inline int s5(int n,int a,int b,int c,int d,int e){register int r7 __asm__("r7")=n;register int r0 __asm__("r0")=a;register int r1 __asm__("r1")=b;register int r2 __asm__("r2")=c;register int r3 __asm__("r3")=d;register int r4 __asm__("r4")=e;__asm__ volatile("svc #0":"+r"(r0):"r"(r7),"r"(r1),"r"(r2),"r"(r3),"r"(r4):"memory");return r0;}

static void sapp(char *d, int *p, const char *s){ while(*s) d[(*p)++] = *s++; }
static void sappd(char *d, int *p, long v)
{
    char t[16]; int n = 0; unsigned long u;
    if (v < 0) { d[(*p)++] = '-'; u = (unsigned long)(-v); } else u = (unsigned long)v;
    if (!u) t[n++] = '0';
    while (u) { t[n++] = (char)('0' + (u % 10)); u /= 10; }
    while (n) d[(*p)++] = t[--n];
}
static void logk(char *b, int *p, const char *k, long v){ sapp(b, p, k); sappd(b, p, v); sapp(b, p, "\r\n"); }

__attribute__((constructor)) static void init(void)
{
    static char out[768];
    int p = 0;
    int rc;

    sapp(out, &p, "PATCH_CAUTION_START\r\n");

    /* 1) 不再自行 remount：由调用方用 shell 完成 mount -o remount,rw /usr */
    rc = 0;

    /* 2) 打开既有文件：flags=2 (O_RDWR)，不带 O_CREAT/O_TRUNC/O_APPEND */
    int fd = s3(SYS_open, (int)"/usr/lib/CautionConfig.so", 2, 0);
    logk(out, &p, "OPEN_FD=", fd);

    if (fd >= 0) {
        unsigned char cur = 0;
        if (s3(SYS_lseek, fd, (int)OFF, 0) == (int)OFF) {
            int n = s3(SYS_read, fd, (int)&cur, 1);
            logk(out, &p, "READ_N=", n);
            logk(out, &p, "OLD=", cur);

            if (n == 1 && cur == OLD_BYTE) {
                unsigned char nb = NEW_BYTE;
                if (s3(SYS_lseek, fd, (int)OFF, 0) == (int)OFF) {
                    int w = s3(SYS_write, fd, (int)&nb, 1);
                    logk(out, &p, "WRITE_N=", w);
                    logk(out, &p, "FSYNC_RC=", s1(SYS_fsync, fd));

                    /* 读回校验 */
                    cur = 0;
                    if (s3(SYS_lseek, fd, (int)OFF, 0) == (int)OFF &&
                        s3(SYS_read, fd, (int)&cur, 1) == 1) {
                        logk(out, &p, "VERIFY=", cur);
                        logk(out, &p, "RESULT_OK=", (cur == NEW_BYTE) ? 1 : 0);
                    } else {
                        logk(out, &p, "RESULT_OK=", 0);
                    }
                }
            } else {
                logk(out, &p, "SKIP_NOT_ORIGINAL=", 1);
            }
        } else {
            logk(out, &p, "SEEK_FAILED=", 1);
        }
        s1(SYS_close, fd);
    }

    /* 3) 不恢复只读：由调用方负责 */

    sapp(out, &p, "PATCH_CAUTION_END\r\n");

    /* 4) 落到 /log（与 rootfs 分开的分区，通常可写） */
    int lf = s3(SYS_open, (int)"/log/.zve10_patch_result", 0x241, 0644); /* O_WRONLY|O_CREAT|O_TRUNC */
    if (lf >= 0) { s3(SYS_write, lf, (int)out, p); s1(SYS_fsync, lf); s1(SYS_close, lf); }

    s1(SYS_exit, 0);
}
