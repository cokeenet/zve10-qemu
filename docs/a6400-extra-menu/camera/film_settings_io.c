#define _POSIX_C_SOURCE 200809L
#include "film_settings_io.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int film_settings_publish(const char *path,const struct film_settings *settings) {
    char text[128],temporary[256];
    int count=film_settings_format(settings,text,sizeof(text));
    if(!path || !path[0] || strlen(path)>240 || count<1)return -1;
    int n=snprintf(temporary,sizeof(temporary),"%s.new",path);
    if(n<1 || (unsigned)n>=sizeof(temporary))return -1;
    /* O_EXCL with O_CREAT also rejects an existing symlink. Avoid non-POSIX
     * O_NOFOLLOW, which is hidden by feature macros on some host systems. */
    int fd=open(temporary,O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd<0)return -1;
    int ok=1,written=0;
    while(written<count) {
        ssize_t amount=write(fd,text+written,(size_t)(count-written));
        if(amount<0 && errno==EINTR)continue;
        if(amount<=0){ok=0;break;}
        written+=(int)amount;
    }
    if(close(fd))ok=0;
    if(ok && !rename(temporary,path))return 0;
    unlink(temporary);return -1;
}
