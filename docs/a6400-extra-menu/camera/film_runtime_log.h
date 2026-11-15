#ifndef FILM_RUNTIME_LOG_H
#define FILM_RUNTIME_LOG_H
#include <stddef.h>
#define FILM_LOG_CAP 65536u
#define FILM_LOG_RECORD 2048u

/* One thread owns one atomically created file in a trusted parent directory;
 * hostile same-UID concurrent mutation is outside this contract. No stdout
 * adoption, child pipe, deadline, kill, file-size limit, native or JPEG call. */
struct film_log {
    int fd,rotate,io_error,trusted,bad_record;
    unsigned used,pending,dropping,rotations;
    char line[FILM_LOG_RECORD];
};
int film_log_open(struct film_log *,const char *new_path,int rotate);
int film_log_printf(struct film_log *,const char *,...);
int film_log_puts(struct film_log *,const char *);
int film_log_putchar(struct film_log *,int);
int film_log_close(struct film_log *);
#endif
