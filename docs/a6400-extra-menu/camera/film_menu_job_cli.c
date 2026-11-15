/* Read-only live-menu job selection. No INI, worker execution or camera writes.
 * The build header pins one reviewed UI artifact and its exact state layout.
 */
#include "film_settings_source.h"
#include "film_settings_gate.h"
#include "film_menu_job_build.h"
#include <stdio.h>

int main(void) {
    /* Camera _start returns through SYS_exit, not libc exit. A pipe is fully
     * buffered by default, so returning from main must not discard this
     * machine-readable authorization response. */
    if (setvbuf(stdout, NULL, _IONBF, 0)) {
        printf("FILM_UNAVAILABLE source=%d\n", -18);
        (void)fflush(stdout);
        return 11;
    }
    const struct film_settings_source source = {
        FILM_JOB_PROC, FILM_JOB_MODULE, FILM_JOB_MODULE_BYTES,
        FILM_JOB_MODULE_FNV, FILM_JOB_UI_RVA
    };
    struct film_settings settings = {0};
    int status = film_settings_source_get(&settings, &source);
    if (status) {
        printf("FILM_UNAVAILABLE source=%d\n", status);
        return 11;
    }
    struct film_job job;
    int gate = film_settings_job(&job, &settings);
    if (gate < 0) {
        printf("FILM_UNAVAILABLE source=%d\n", gate);
        return 11;
    }
    if (!gate) {
        puts("FILM_OFF");
        return 10;
    }
    printf("FILM_JOB preset=%s grain=%s halation=%s\n",
           job.preset, job.grain, job.halation);
    return 0;
}
