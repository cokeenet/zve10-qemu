/* 独立测试：dlopen viewUnified2.so，看失败原因 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <errno.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "viewUnified2.so";
    printf("=== dlopen test: %s ===\n", path);

    /* 先加载 libObj（hook 需要） */
    void *obj = dlopen("libObj.so", RTLD_NOW | RTLD_GLOBAL);
    printf("libObj.so -> %p %s\n", obj, obj ? "" : dlerror());

    printf("dlopening %s ...\n", path);
    fflush(stdout);
    void *h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        const char *e = dlerror();
        printf("FAILED: %s\n", e ? e : "(no error)");
        return 1;
    }
    printf("OK handle=%p\n", h);

    void *ctor = dlsym(h, "_ZN18CmnViewSettingNodeC1EPPS_iPK24CmnViewSettingProperties");
    printf("caution ctor = %p\n", ctor);
    void *factory = dlsym(h, "ViewRootSelectorToInstance");
    printf("ViewRootSelectorToInstance = %p\n", factory);
    return 0;
}
