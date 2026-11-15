/* Exact-module owned-word shutdown request. Never calls Sony or unloads UI.
 * Outer coordinator must stop dispatch/capture and wait for every job first.
 */
#define _GNU_SOURCE
#include "film_settings_source.h"
#include "film_settings_maps.h"
#include "film_ui_stop_build.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

_Static_assert(sizeof(unsigned)==4,"owned control word must be 32-bit");
_Static_assert((FILM_UI_STOP_CONTROL_RVA&3u)==0,"owned control word alignment");
_Static_assert(FILM_UI_STOP_CONTROL_RVA<FILM_UI_STOP_UI_RVA ||
    FILM_UI_STOP_CONTROL_RVA>=FILM_UI_STOP_UI_RVA+FILM_UI_SNAPSHOT_BYTES,
    "owned stop word must not overlap the UI snapshot ABI");

struct observed {
    uint32_t base,state,control,request;
    unsigned char first[FILM_UI_SNAPSHOT_BYTES],second[FILM_UI_SNAPSHOT_BYTES];
};
static uint32_t word(const void *p,unsigned at) {
    uint32_t value;memcpy(&value,(const unsigned char *)p+at,4);return value;
}
static int fail(const char *reason) {
    printf("FILM_UI_STOP_UNAVAILABLE reason=%s\n",reason);return 11;
}
static int path(char *out,unsigned capacity,const char *name) {
    int n=snprintf(out,capacity,"%s/%s",FILM_UI_STOP_PROC,name);
    return n>0 && (unsigned)n<capacity;
}
static int identity(char start[24]) {
    char file[512],line[1024];
    if(!path(file,sizeof(file),"comm"))return 0;
    FILE *f=fopen(file,"r");
    if(!f)return 0;
    int ok=fgets(line,sizeof(line),f) && !strcmp(line,"im.elf\n");
    fclose(f);if(!ok || !path(file,sizeof(file),"stat"))return 0;
    f=fopen(file,"r");if(!f)return 0;
    ok=fgets(line,sizeof(line),f)!=0;fclose(f);
    if(!ok || strncmp(line,"149 (im.elf) ",13))return 0;
    char *p=line+13;
    for(unsigned field=3;field<22;field++) {
        if(!*p || *p=='\n')return 0;
        while(*p && *p!=' ' && *p!='\n')++p;
        if(*p!=' ')return 0;
        while(*p==' ')++p;
    }
    unsigned length=0;
    while(p[length]>='0' && p[length]<='9' && length<23)++length;
    if(!length || length>20 || (p[length]!=' ' && p[length]!='\n'))return 0;
    memcpy(start,p,length);start[length]=0;return 1;
}
static int absent(const char *file) {
    int fd=open(file,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd>=0){close(fd);return 0;}
    return errno==ENOENT;
}
static const char *outer_stopped(void) {
    int fd=open(FILM_UI_STOP_MARKER,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return "outer_not_stopped";
    char byte;
    int ok=lseek(fd,0,SEEK_END)>=0 && read(fd,&byte,1)==0;
    close(fd);if(!ok)return "outer_not_stopped";
    if(!absent(FILM_UI_STOP_CAPTURE_LOCK) || !absent(FILM_UI_STOP_INFLIGHT) ||
       !absent(FILM_UI_STOP_JOB_LOCK))return "outer_busy";
    return 0;
}
/* The final zero-fill page of an ELF LOAD has no pathname in /proc/maps.
 * Derive only this reviewed module's exact continuation from its pinned ELF;
 * an arbitrary writable anonymous neighbor is never an owned control range.
 * This camera's reviewed ARM32 mappings use 4 KiB pages. */
struct bss_geometry {
    uint32_t image_page,segment_page,file_page,memory_page,offset_bias;
};
static uint32_t little32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 |
           (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static unsigned little16(const unsigned char *p) {
    return (unsigned)p[0] | (unsigned)p[1]<<8;
}
static int bss_geometry(struct bss_geometry *out) {
    unsigned char header[52],program[32];
    int fd=open(FILM_UI_STOP_MODULE,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return 0;
    int ok=pread(fd,header,sizeof(header),0)==sizeof(header);
    if(!ok || memcmp(header,"\177ELF\1\1\1",7) ||
       little16(header+16)!=3u || little16(header+18)!=40u ||
       little32(header+20)!=1u || little16(header+40)!=52u ||
       little16(header+42)!=32u){close(fd);return 0;}
    uint32_t at=little32(header+28);unsigned count=little16(header+44);
    if(!count || count>32u || at<sizeof(header) || at>FILM_UI_STOP_MODULE_BYTES ||
       count>(FILM_UI_STOP_MODULE_BYTES-at)/sizeof(program)){close(fd);return 0;}
    unsigned anchors=0,covers=0;struct bss_geometry found={0};
    for(unsigned i=0;ok && i<count;++i) {
        if(pread(fd,program,sizeof(program),at+i*sizeof(program))!=sizeof(program)){ok=0;break;}
        if(little32(program)!=1u)continue; /* PT_LOAD */
        uint32_t offset=little32(program+4),rva=little32(program+8);
        uint32_t files=little32(program+16),memory=little32(program+20),flags=little32(program+24);
        if(files>memory || offset>FILM_UI_STOP_MODULE_BYTES ||
           files>FILM_UI_STOP_MODULE_BYTES-offset || memory>UINT32_MAX-rva){ok=0;break;}
        if(!offset && !rva && flags==5u && files>=at+count*sizeof(program)) {
            if(memory>UINT32_MAX-4095u){ok=0;break;}
            ++anchors;found.image_page=(memory+4095u)&~4095u;
        }
        if(FILM_UI_STOP_CONTROL_RVA<rva || FILM_UI_STOP_CONTROL_RVA>UINT32_MAX-4u ||
           FILM_UI_STOP_CONTROL_RVA+4u>rva+memory)continue;
        ++covers;
        uint32_t file_end=rva+files,memory_end=rva+memory;
        if(flags!=6u || !files || files>=memory || FILM_UI_STOP_CONTROL_RVA<file_end ||
           rva<offset || ((rva-offset)&4095u) ||
           file_end>UINT32_MAX-4095u || memory_end>UINT32_MAX-4095u){ok=0;break;}
        found.segment_page=rva&~4095u;
        found.file_page=(file_end+4095u)&~4095u;
        found.memory_page=(memory_end+4095u)&~4095u;
        found.offset_bias=rva-offset;
    }
    close(fd);
    if(!ok || anchors!=1u || covers!=1u || found.file_page>=found.memory_page)return 0;
    *out=found;return 1;
}
struct stop_mapping {
    uint32_t low,high,offset;
    char permissions[5],device[16],inode[24];
    int module,anonymous;
};
static int map_record(struct stop_mapping *out,const char *line) {
    unsigned low,high,offset;int at=0;
    if(sscanf(line,"%x-%x %4s %x %15s %23s %n",&low,&high,out->permissions,&offset,
              out->device,out->inode,&at)!=6 || at<=0 || low>=high ||
       strlen(out->permissions)!=4u)return 0;
    out->low=low;out->high=high;out->offset=offset;
    size_t length=strcspn(line+at,"\r\n");
    out->module=strlen(FILM_UI_STOP_MODULE)==length && !memcmp(line+at,FILM_UI_STOP_MODULE,length);
    out->anonymous=!length && !offset && !strcmp(out->device,"00:00") && !strcmp(out->inode,"0");
    return 1;
}
static int mapping(struct observed *out) {
    char file[512],line[1024];
    if(!path(file,sizeof(file),"maps"))return 0;
    FILE *f=fopen(file,"r");if(!f)return 0;
    struct film_settings_map ui={0};unsigned control_count=0,control_ranges=0;uint32_t control=0;
    struct bss_geometry bss={0};int have_bss=bss_geometry(&bss);
    struct stop_mapping previous={0},image={0};int have_previous=0,have_image=0,ok=1;
    while(fgets(line,sizeof(line),f)) {
        size_t length=strlen(line);
        if(length==sizeof(line)-1 && line[length-1]!='\n'){ok=0;break;}
        struct stop_mapping current={0};
        if(!map_record(&current,line) || (have_previous && current.low<previous.high)){ok=0;break;}
        film_settings_map_line(&ui,line,FILM_UI_STOP_MODULE,FILM_UI_STOP_UI_RVA);
        if(current.module && !current.offset && !strcmp(current.permissions,"r-xp")) {
            image=current;++have_image;
        }
        if(ui.image_count==1 && FILM_UI_STOP_CONTROL_RVA<=UINT32_MAX-ui.base) {
            control=ui.base+FILM_UI_STOP_CONTROL_RVA;
            if(control>=current.low && control<=UINT32_MAX-4u && control+4u<=current.high) {
                ++control_ranges;
                if(!strcmp(current.permissions,"rw-p")) {
                    if(current.module)++control_count;
                    else if(have_bss && have_previous && have_image==1 && current.anonymous &&
                            bss.memory_page<=UINT32_MAX-ui.base &&
                            bss.image_page<=UINT32_MAX-ui.base && !(ui.base&4095u) &&
                            image.low==ui.base && image.high==ui.base+bss.image_page &&
                            current.low==ui.base+bss.file_page && current.high==ui.base+bss.memory_page &&
                            previous.module && !strcmp(previous.permissions,"rw-p") &&
                            !(previous.low&4095u) && !(previous.high&4095u) && !(previous.offset&4095u) &&
                            previous.high==current.low && previous.low>=ui.base &&
                            previous.low-ui.base>=bss.segment_page && previous.low-ui.base<bss.file_page &&
                            previous.low-ui.base>=bss.offset_bias &&
                            previous.offset==previous.low-ui.base-bss.offset_bias &&
                            !strcmp(previous.device,image.device) && !strcmp(previous.inode,image.inode) &&
                            ui.state>=previous.low && ui.state<=UINT32_MAX-FILM_UI_SNAPSHOT_BYTES &&
                            ui.state+FILM_UI_SNAPSHOT_BYTES<=previous.high)++control_count;
                }
            }
        }
        previous=current;have_previous=1;
    }
    ok=ok && !ferror(f);fclose(f);
    if(!ok || ui.image_count!=1 || ui.state_count!=1 || control_ranges!=1 || control_count!=1)return 0;
    out->base=ui.base;out->state=ui.state;out->control=control;return 1;
}
static const char *read_state(int fd,struct observed *out) {
    uint32_t first,second;
    if(pread(fd,&first,4,out->control)!=4 ||
       pread(fd,out->first,sizeof(out->first),out->state)!=sizeof(out->first) ||
       pread(fd,out->second,sizeof(out->second),out->state)!=sizeof(out->second) ||
       pread(fd,&second,4,out->control)!=4)
        return "read";
    if(first>2u || second>2u)return "control";
    if(first!=second)return "transition";
    out->request=second;
    if(second==2u) {
        /* A stopped ticker no longer supplies a heartbeat. Acknowledge only
         * the same attached UI identity and two stable inactive snapshots. */
        static const unsigned fixed[]={0x68,0xc0,0xc4,0x18c,0x190};
        if(memcmp(out->first,out->second,16))return "state";
        for(unsigned i=0;i<sizeof(fixed)/sizeof(*fixed);i++)
            if(word(out->first,fixed[i])!=word(out->second,fixed[i]))return "state";
        if(word(out->second,0x68)!=0x956f0u || word(out->second,0xc0)!=380u ||
           !word(out->second,0xc4) || word(out->second,0x18c)!=1u ||
           word(out->second,0x190)!=0u)return "state";
        return 0;
    }
    uint32_t now;struct film_settings settings;
    if(film_settings_now(&now))return "state";
    int snapshot=film_settings_snapshot_get(&settings,out->first,
        out->second,sizeof(out->first),now);
    /* The natural callback clears running before publishing ACK=2. While our
     * request is pending, retry this inactive/torn window within the existing
     * deadline; it is never an acknowledgement or permission for a write. */
    if(snapshot)return second==1u && (snapshot==1 || snapshot==2)?"transition":"state";
    return 0;
}
static const char *still_same(const struct observed *original,const char *start) {
    char current[24];struct observed map={0};
    if(!identity(current) || strcmp(current,start))return "identity";
    if(!film_file_matches(FILM_UI_STOP_MODULE,FILM_UI_STOP_MODULE_BYTES,
                          FILM_UI_STOP_MODULE_FNV))return "artifact";
    if(!mapping(&map) || map.base!=original->base || map.state!=original->state ||
       map.control!=original->control)return "maps";
    return 0;
}
int main(int argc,char **argv) {
    /* The camera's raw SYS_exit startup does not flush fully buffered pipes. */
    if(setvbuf(stdout,NULL,_IONBF,0)) {
        int result=fail("stdout_buffering");(void)fflush(stdout);return result;
    }
    int request=argc==2 && !strcmp(argv[1],"--request");
    if(argc!=2 || (!request && strcmp(argv[1],"--check"))) {
        fprintf(stderr,"usage: a6400-film-ui-stop.elf --check | --request\n"
            "Stop dispatch and wait for capture/jobs to exit before --request.\n"
            "Create " FILM_UI_STOP_MARKER "; all capture/inflight/job locks must be absent.\n"
            "This tool does not manage jobs, call Sony, remove hooks or unload the UI.\n");
        return 2;
    }
    const char *reason=request?outer_stopped():0;
    if(reason)return fail(reason);
    if(!film_file_matches(FILM_UI_STOP_MODULE,FILM_UI_STOP_MODULE_BYTES,
                          FILM_UI_STOP_MODULE_FNV))return fail("artifact");
    char start[24];if(!identity(start))return fail("identity");
    struct observed state={0};if(!mapping(&state))return fail("maps");
    char file[512];if(!path(file,sizeof(file),"mem"))return fail("read");
    int fd=open(file,request?O_RDWR:O_RDONLY);
    if(fd<0)return fail("read");
    reason=read_state(fd,&state);
    if(reason){close(fd);return fail(!strcmp(reason,"transition")?"state":reason);}
    reason=still_same(&state,start);
    if(reason){close(fd);return fail(reason);}
    if(state.request==2u){close(fd);puts("FILM_UI_STOPPED");return 0;}
    if(!request) {
        close(fd);printf("FILM_UI_RUNNING request=%u\n",state.request);return 0;
    }
    reason=outer_stopped();
    if(reason){close(fd);return fail(reason);}
    if(state.request==0u) {
        const unsigned stop=1u;
        /* The sole write is the four-byte exported owned control symbol. */
        if(pwrite(fd,&stop,sizeof(stop),state.control)!=sizeof(stop)) {
            close(fd);return fail("write");
        }
    }
    uint32_t began,now;
    if(film_settings_now(&began)){close(fd);return fail("clock");}
    for(;;) {
        reason=still_same(&state,start);
        if(!reason)reason=outer_stopped();
        if(!reason)reason=read_state(fd,&state);
        if(reason && strcmp(reason,"transition")){close(fd);return fail(reason);}
        if(!reason && state.request==2u){close(fd);puts("FILM_UI_STOPPED");return 0;}
        if(!reason && state.request!=1u){close(fd);return fail("control");}
        if(film_settings_now(&now)){close(fd);return fail("clock");}
        if((uint32_t)(now-began)>=FILM_UI_STOP_WAIT_MS) {
            close(fd);puts("FILM_UI_STOP_TIMEOUT request=1");return 12;
        }
        (void)poll(0,0,20);
    }
}
