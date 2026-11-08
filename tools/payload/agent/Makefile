# ZV-E10 Agent 构建
#
# 相机是 ARMv7 Cortex-A9，glibc 2.11.2，softfp ABI（Flags 0x5000002）。
# 但实测 hard-float 工具链编译出的库也能正常加载。
#
# 用法:
#   make                     用默认工具链
#   make TC=/path/to/tc      指定工具链前缀（不含 gcc）

TC ?= arm-buildroot-linux-gnueabihf-

CC      := $(TC)gcc
READELF := $(TC)readelf
STRIP   := $(TC)strip

TARGET  := zve10_agent.so
SRC     := agent.c
CFLAGS  := -shared -fPIC -O2 -Wall -Wextra
LDFLAGS :=

.PHONY: all clean check

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
	@echo
	@echo "=== 生成 $@ ==="
	@ls -la $@
	@echo
	@echo "=== 依赖检查（应该只有 libc.so.6）==="
	@$(READELF) -dW $@ | grep NEEDED || true
	@echo
	@echo "=== ABI ==="
	@$(READELF) -hW $@ | grep Flags || true

# 检查能否安全部署
check: $(TARGET)
	@echo "=== 危险 API 检查 ==="
	@if $(READELF) -sW --dyn-syms $(TARGET) | grep -qE 'pthread_create'; then \
		echo "  ⚠ 发现 pthread_create —— ld.so 构造函数阶段会死锁！"; \
	else echo "  ✓ 无 pthread_create"; fi
	@if $(READELF) -sW --dyn-syms $(TARGET) | grep -qE 'dl_iterate_phdr'; then \
		echo "  ⚠ 发现 dl_iterate_phdr —— 回调传 NULL 会段错误，导致无限重启！"; \
	else echo "  ✓ 无 dl_iterate_phdr"; fi
	@echo
	@echo "=== 导出符号 ==="
	@$(READELF) -sW --dyn-syms $(TARGET) | grep -E 'GLOBAL.*DEFAULT.*[0-9]$$' | head -10 || true

clean:
	rm -f $(TARGET)

install: $(TARGET)
	@echo "手动部署："
	@echo "  1. 把 $(TARGET) 推到相机 /etc/"
	@echo "  2. echo '/etc/$(TARGET)' > /etc/ld.so.preload"
	@echo "  3. sync && reboot"
