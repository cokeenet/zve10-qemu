#!/bin/sh
# 在 QEMU guest 里测试 glibc 2.11 对坏 preload 的反应
# 这是在真机之前必须知道的安全边界
echo "=== PRELOAD SAFETY TEST ==="
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export LD_LIBRARY_PATH=/usr/lib:/lib

echo "[1] 正常 exec（基线）"
/bin/busybox echo "  baseline ok"

echo "[2] preload 指向不存在的文件"
printf '/does/not/exist.so\n' > /etc/ld.so.preload
/bin/busybox echo "  survived missing-so preload: $?"
/bin/busybox ls / > /dev/null 2>&1 && echo "  exec still works" || echo "  EXEC FAILED"

echo "[3] preload 指向非 ELF 文件"
printf 'not an elf file at all\n' > /tmp/notelf.so
printf '/tmp/notelf.so\n' > /etc/ld.so.preload
/bin/busybox echo "  survived bad-elf preload: $?"
/bin/busybox ls / > /dev/null 2>&1 && echo "  exec still works" || echo "  EXEC FAILED"

echo "[4] preload 指向真实 .so（我们自己的库）"
printf '/usr/lib/libnewmenu.so\n' > /etc/ld.so.preload
/bin/busybox echo "  survived real-so preload: $?"
/bin/busybox ls / > /dev/null 2>&1 && echo "  exec still works" || echo "  EXEC FAILED"

echo "[5] 清空 preload"
printf '' > /etc/ld.so.preload
/bin/busybox echo "  cleared"
echo "=== TEST END ==="
