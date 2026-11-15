/* Serialize completed MPR inventories and menu-gated RAM/SD jobs. Stable
 * metadata is only a candidate heuristic; the pinned job verifies full JPEGs.
 * No direct Sony calls, UI modification, ptrace, or Linux SD mount is used here.
 */
#define _GNU_SOURCE
#include "film_capture_daemon_build.h"
#include <stdio.h>
#include "film_runtime_log.h"
static struct film_log capture_log;
#define printf(...) film_log_printf(&capture_log,__VA_ARGS__)
#define puts(s) film_log_puts(&capture_log,(s))
#define putchar(c) film_log_putchar(&capture_log,(c))
#define main legacy_probe_main
#include "native_capture_probe.c"
#undef main

#ifndef FILM_CAPTURE_ACTIVE_MS
#define FILM_CAPTURE_ACTIVE_MS 600000u
#endif
#ifndef FILM_CAPTURE_PERSISTENT
#define FILM_CAPTURE_PERSISTENT 0
#endif
#ifndef FILM_CAPTURE_CLI_TIMEOUT_MS
#define FILM_CAPTURE_CLI_TIMEOUT_MS 2000u
#endif
#ifndef FILM_CAPTURE_JOB_TIMEOUT_MS
#define FILM_CAPTURE_JOB_TIMEOUT_MS 600000u
#endif
#ifndef FILM_CAPTURE_BASELINE_TIMEOUT_MS
#define FILM_CAPTURE_BASELINE_TIMEOUT_MS 60000u
#endif
#define JOB_LOG_BYTES 65536u

enum state { OLD, WAITING, PENDING, APPLIED, OFF, FAILED, INFLIGHT };
struct queued {
    struct entry entry;
    uint32_t stable;
    enum state state;
    int present;
    char target[32];
};
static struct queued queue[MAX_FILES];
static unsigned queue_count,applied_jobs,off_jobs,failed_jobs,next_output;
static volatile sig_atomic_t daemon_signal;
static int own_manifest,own_log,own_inflight,retain_lock;
static pid_t job_pid;
static const char *phase="startup";
static int capture_log_bad(void) {
    return capture_log.io_error || capture_log.bad_record || !capture_log.trusted;
}
static char job_log[JOB_LOG_BYTES+1u];

/* Do not set the legacy scanner's stopping flag. A stop must wait until its
 * current complete inventory, or its native job, has actually returned. */
static void daemon_stop(int sig) {daemon_signal=sig;}
static int absent_file(const char *path) {
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd>=0){close(fd);return 0;}
    return errno==ENOENT;
}
static int pin(const char *path,unsigned bytes,uint32_t expected) {
    unsigned char block[1024];unsigned total=0;uint32_t hash=2166136261u;
    int fd=open(path,O_RDONLY|O_NONBLOCK|O_NOFOLLOW);if(fd<0)return 0;
    ssize_t n;
    while((n=read(fd,block,sizeof(block)))>0) {
        if(total>bytes || (unsigned)n>bytes-total){close(fd);return 0;}
        for(ssize_t i=0;i<n;++i)hash=(hash^block[i])*16777619u;
        total+=(unsigned)n;
    }
    close(fd);return !n && total==bytes && hash==expected;
}
static int pins(void) {
    return monitor_matches() &&
        pin(FILM_CAPTURE_SETTINGS,FILM_CAPTURE_SETTINGS_BYTES,FILM_CAPTURE_SETTINGS_FNV) &&
        pin(FILM_CAPTURE_SCRIPT,FILM_CAPTURE_SCRIPT_BYTES,FILM_CAPTURE_SCRIPT_FNV);
}
static const char *state_name(enum state value) {
    static const char *names[]={"baseline","waiting","pending","applied","off","failed","inflight"};
    return names[value];
}
static int source_path(const struct entry *entry,char *out,unsigned bytes) {
    const char *s=entry->path;
    if(strlen(s)!=30u || memcmp(s,"ms:/DCIM/",9) || s[17]!='/' || strcmp(s+26,".JPG"))return -1;
    for(unsigned i=9;i<26u;++i) {
        if(i==17u)continue;
        char c=s[i];
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return -1;
    }
    int n=snprintf(out,bytes,"A:%s",s+3);
    return n>0 && (unsigned)n<bytes?0:-1;
}
static int save_manifest(void) {
    int flags=O_WRONLY|O_CREAT|O_NOFOLLOW|(own_manifest?O_TRUNC:O_EXCL);
    int fd=open(FILM_CAPTURE_MANIFEST,flags,0600);if(fd<0)return -1;
    own_manifest=1;FILE *file=fdopen(fd,"w");if(!file){close(fd);return -1;}
    fprintf(file,"FILM_CAPTURE_MANIFEST version=1\npid=%u\nphase=%s\njob_pid=%u\nuncertain=%d\ncount=%u\n",
            (unsigned)getpid(),phase,(unsigned)job_pid,retain_lock,queue_count);
    for(unsigned i=0;i<queue_count;++i) {
        struct queued *q=&queue[i];char source[40];
        if(source_path(&q->entry,source,sizeof(source))){fclose(file);return -1;}
        fprintf(file,"item state=%s source=%s bytes=%u stamp=%s target=%s stable_ms=%u present=%d\n",
                state_name(q->state),source,q->entry.size,q->entry.time,
                q->target[0]?q->target:"none",(unsigned)q->stable,q->present);
    }
    int error=ferror(file);return fclose(file) || error?-1:0;
}
static int save_inflight(const struct queued *q,const char *settings) {
    int flags=O_WRONLY|O_CREAT|O_NOFOLLOW|(own_inflight?O_TRUNC:O_EXCL);
    int fd=open(FILM_CAPTURE_INFLIGHT,flags,0600);if(fd<0)return -1;
    own_inflight=1;FILE *file=fdopen(fd,"w");if(!file){close(fd);return -1;}
    char source[40];if(source_path(&q->entry,source,sizeof(source))){fclose(file);return -1;}
    fprintf(file,"FILM_CAPTURE_INFLIGHT version=1\ndaemon_pid=%u\njob_pid=%u\nsource=%s\nbytes=%u\ntarget=%s\nsettings=%s\n",
            (unsigned)getpid(),(unsigned)job_pid,source,q->entry.size,q->target,settings);
    int error=ferror(file);return fclose(file) || error?-1:0;
}
static void reap_readonly(pid_t pid) {
    int status;kill(pid,SIGTERM);
    for(unsigned i=0;i<50u;++i) {
        pid_t got=waitpid(pid,&status,WNOHANG);
        if(got==pid || (got<0 && errno==ECHILD))return;
        pause_ms(10);
    }
    kill(pid,SIGKILL);while(waitpid(pid,&status,0)<0 && errno==EINTR){}
}
/* Preserve bounded child evidence without allowing terminal controls or a
 * malformed CLI response to become a daemon log record. This never opens the
 * gate: unavailable settings still remain pending for a later fresh read. */
static int settings_unavailable(const char *reason,int error,pid_t pid,int status,
                                int done,uint32_t began,const char *out,unsigned used) {
    int source=0,at=0,has_source=0;
    if(used && sscanf(out,"FILM_UNAVAILABLE source=%d%n",&source,&at)==1 &&
       at>0 && (unsigned)at+1u==used && out[at]=='\n')has_source=1;
    const char *source_reason="none";
    if(has_source)switch(source) {
        case -17:source_reason="ui_artifact";break;
        case -16:source_reason="clock";break;
        case -15:source_reason="process_mem_read";break;
        case -14:source_reason="process_mem_open";break;
        case -13:source_reason="module_maps";break;
        case -12:source_reason="process_maps_read";break;
        case -11:source_reason="process_identity";break;
        case -10:source_reason="process_comm_read";break;
        case -1:source_reason="snapshot_or_job_invalid";break;
        case 1:source_reason="snapshot_inactive_or_stale";break;
        case 2:source_reason="snapshot_changed";break;
        case 10:source_reason="module_not_loaded";break;
        default:source_reason="unknown";break;
    }
    printf("FILM_CAPTURE_SETTINGS_DIAG reason=%s errno=%d uid=%u euid=%u child_pid=%u wait_done=%d exit=%d signal=%d elapsed_ms=%u stdout_bytes=%u has_source=%d cli_source=%d cli_reason=%s stdout_hex=",
           reason,error,(unsigned)getuid(),(unsigned)geteuid(),pid>0?(unsigned)pid:0u,
           done,done && WIFEXITED(status)?WEXITSTATUS(status):-1,
           done && WIFSIGNALED(status)?WTERMSIG(status):0,
           (unsigned)(now_ms()-began),used,has_source,source,source_reason);
    for(unsigned i=0;i<used;++i)printf("%02x",(unsigned)(unsigned char)out[i]);
    putchar('\n');return 11;
}
/* CLI is read-only, with exact bounded stdout and a separately pinned binary.
 * Its output carries enabled settings regardless of whether the menu is open. */
static int live_settings(char *out,unsigned capacity) {
    uint32_t began=now_ms();out[0]=0;errno=0;
    if(!pin(FILM_CAPTURE_SETTINGS,FILM_CAPTURE_SETTINGS_BYTES,FILM_CAPTURE_SETTINGS_FNV))
        return settings_unavailable("cli_pin",errno,-1,0,0,began,out,0);
    int pipefd[2];if(pipe(pipefd))return settings_unavailable("pipe",errno,-1,0,0,began,out,0);
    pid_t pid=fork();
    if(pid<0){int error=errno;close(pipefd[0]);close(pipefd[1]);
        return settings_unavailable("fork",error,-1,0,0,began,out,0);}
    if(!pid) {
        int null=open("/dev/null",O_WRONLY);
        if(null<0 || dup2(null,2)<0 || dup2(pipefd[1],1)<0)_exit(126);
        if(null!=2)close(null);
        close(pipefd[0]);close(pipefd[1]);
        execl(FILM_CAPTURE_SETTINGS,FILM_CAPTURE_SETTINGS,(char *)0);_exit(127);
    }
    close(pipefd[1]);
    if(fcntl(pipefd[0],F_SETFL,O_NONBLOCK)<0){int error=errno;close(pipefd[0]);reap_readonly(pid);
        return settings_unavailable("nonblocking",error,pid,0,0,began,out,0);}
    unsigned used=0;int status=0,done=0,eof=0,error=0;const char *failure=0;uint32_t start=now_ms();
    while(!(done && eof)) {
        if((uint32_t)(now_ms()-start)>=FILM_CAPTURE_CLI_TIMEOUT_MS){failure="cli_timeout";break;}
        char block[128];ssize_t n=read(pipefd[0],block,sizeof(block));
        if(n>0) {
            if(used+(unsigned)n>=capacity){
                unsigned keep=capacity-used-1u;
                memcpy(out+used,block,keep);used+=keep;failure="stdout_capacity";break;
            }
            memcpy(out+used,block,(unsigned)n);used+=(unsigned)n;
        } else if(!n)eof=1;
        else if(errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR){failure="stdout_read";error=errno;break;}
        if(!done) {
            pid_t got=waitpid(pid,&status,WNOHANG);
            if(got==pid)done=1;
            else if(got<0 && errno!=EINTR){failure="child_wait";error=errno;break;}
        }
        if(!(done && eof))pause_ms(5);
    }
    close(pipefd[0]);if(!done)reap_readonly(pid);
    out[used]=0;
    if(failure)return settings_unavailable(failure,error,pid,status,done,began,out,used);
    if(!WIFEXITED(status))return settings_unavailable("child_signal",0,pid,status,done,began,out,used);
    if(!used)return settings_unavailable("stdout_empty",0,pid,status,done,began,out,used);
    if(out[used-1]!='\n')return settings_unavailable("stdout_unterminated",0,pid,status,done,began,out,used);
    if(WEXITSTATUS(status)==10 && used==sizeof("FILM_OFF\n")-1u && !memcmp(out,"FILM_OFF\n",used))return 10;
    if(WEXITSTATUS(status))return settings_unavailable("cli_exit",0,pid,status,done,began,out,used);
    char preset[32];unsigned grain,halation;int at=0;
    if(sscanf(out,"FILM_JOB preset=%31[a-z0-9_] grain=%u halation=%u%n",preset,&grain,&halation,&at)!=3 ||
       at<0 || (unsigned)at+1u!=used || out[at]!='\n' || grain>3u || halation>1u)
        return settings_unavailable("job_format",0,pid,status,done,began,out,used);
    if(strcmp(preset,"portra_400") && strcmp(preset,"cinestill_800t") && strcmp(preset,"fuji_provia") &&
       strcmp(preset,"kodak_gold") && strcmp(preset,"tri_x_400"))
        return settings_unavailable("job_preset",0,pid,status,done,began,out,used);
    char canonical[128];
    snprintf(canonical,sizeof(canonical),"FILM_JOB preset=%s grain=%u halation=%u\n",preset,grain,halation);
    if(strlen(canonical)!=used || memcmp(canonical,out,used))
        return settings_unavailable("job_noncanonical",0,pid,status,done,began,out,used);
    out[used-1u]=0;return 0;
}
static int read_job_receipt(const struct queued *q,int *retained) {
    int fd=open(FILM_CAPTURE_JOB_LOG,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return -1;
    unsigned used=0;ssize_t n;
    while((n=read(fd,job_log+used,JOB_LOG_BYTES-used))>0) {
        used+=(unsigned)n;
        if(used==JOB_LOG_BYTES){close(fd);return -1;}
    }
    close(fd);if(n<0)return -1;job_log[used]=0;
    *retained=strstr(job_log,"RAM_JOB_RETAINED ")!=NULL || strstr(job_log,"uncertain=1")!=NULL;
    char source[40],prefix[160];if(source_path(&q->entry,source,sizeof(source)))return -1;
    snprintf(prefix,sizeof(prefix),"RAM_JOB_APPLIED source=%s target=%s ",source,q->target);
    unsigned reports=0;char *save=NULL;
    for(char *line=strtok_r(job_log,"\n",&save);line;line=strtok_r(NULL,"\n",&save)) {
        if(strstr(line,"RAM_JOB_APPLIED ")==line) {
            size_t size=strlen(line);static const char suffix[]=" original_verified=1";
            if(strstr(line,prefix)!=line || size<sizeof(suffix)-1u ||
               strcmp(line+size-(sizeof(suffix)-1u),suffix))return -1;
            ++reports;
        }
    }
    return reports==1u?1:reports? -1:0;
}
/* Start the script only after its PID and candidate have been recorded. The
 * child waits on a one-byte gate, so a failed receipt write cannot start I/O. */
static int run_job(struct queued *q,const char *settings) {
    if(capture_log_bad() || !pins() || !absent_file(FILM_CAPTURE_JOB_LOCK) || !absent_file(FILM_CAPTURE_INFLIGHT))return 21;
    int flags=O_WRONLY|O_CREAT|O_NOFOLLOW|(own_log?O_TRUNC:O_EXCL);
    int log=open(FILM_CAPTURE_JOB_LOG,flags,0600);if(log<0)return 21;
    own_log=1;
    q->state=INFLIGHT;phase="job_prepare";
    if(save_inflight(q,settings) || save_manifest()){close(log);return 21;}
    int gate[2];if(pipe(gate)){close(log);return 21;}
    job_pid=fork();
    if(job_pid<0){close(log);close(gate[0]);close(gate[1]);job_pid=0;return 21;}
    if(!job_pid) {
        close(gate[1]);char token;
        ssize_t n;do n=read(gate[0],&token,1);while(n<0 && errno==EINTR);
        close(gate[0]);if(n!=1 || token!='G')_exit(125);
        if(dup2(log,1)<0 || dup2(log,2)<0)_exit(126);
        close(log);char source[40],bytes[16];
        if(source_path(&q->entry,source,sizeof(source)))_exit(124);
        snprintf(bytes,sizeof(bytes),"%u",q->entry.size);
        execl("/bin/sh","sh",FILM_CAPTURE_SCRIPT,"apply",source,bytes,q->target,(char *)0);_exit(127);
    }
    close(log);close(gate[0]);phase="job";
    if(save_inflight(q,settings) || save_manifest()) {
        close(gate[1]);reap_readonly(job_pid);job_pid=0;return 21;
    }
    char token='G';ssize_t released=write(gate[1],&token,1);close(gate[1]);
    if(released!=1){reap_readonly(job_pid);job_pid=0;return 21;}
    uint32_t start=now_ms();int status;
    for(;;) {
        pid_t got=waitpid(job_pid,&status,WNOHANG);
        if(got==job_pid)break;
        if(got<0 && errno!=EINTR)return 21;
        if((uint32_t)(now_ms()-start)>=FILM_CAPTURE_JOB_TIMEOUT_MS) {
            /* Never kill a possibly outstanding native operation. Leave the
             * child PID, manifest, inflight and locks for explicit recovery. */
            printf("FILM_CAPTURE_JOB_UNCONFIRMED reason=deadline pid=%u\n",(unsigned)job_pid);
            return 21;
        }
        pause_ms(10);
    }
    job_pid=0;
    int retained=0,receipt=read_job_receipt(q,&retained);
    if(!WIFEXITED(status) || WEXITSTATUS(status)==21 || retained ||
       !absent_file(FILM_CAPTURE_JOB_LOCK) || receipt<0)return 21;
    int rc=WEXITSTATUS(status);
    if(!rc && receipt!=1)return 21;
    if(rc && receipt)return 21;
    q->state=rc?FAILED:APPLIED;
    if(rc)++failed_jobs;else ++applied_jobs;
    phase="scanning";
    if(save_manifest() || unlink(FILM_CAPTURE_INFLIGHT))return 21;
    own_inflight=0;
    printf("FILM_CAPTURE_JOB_DONE result=%d path=%s target=%s applied=%u failed=%u\n",
           rc,q->entry.path,q->target,applied_jobs,failed_jobs);
    return 0;
}
static int update_queue(const struct catalogue *current) {
    uint32_t now=now_ms();
    for(unsigned i=0;i<queue_count;++i)queue[i].present=0;
    for(unsigned i=0;i<current->count;++i) {
        const struct entry *e=&current->items[i];char source[40];
        if(source_path(e,source,sizeof(source)))return -1;
        unsigned j=0;while(j<queue_count && strcmp(queue[j].entry.path,e->path))++j;
        if(j==queue_count) {
            if(queue_count==MAX_FILES)return -1;
            queue[j].entry=*e;queue[j].stable=now;queue[j].state=WAITING;++queue_count;
        }
        struct queued *q=&queue[j];q->present=1;
        if(q->state!=WAITING && q->state!=PENDING)continue;
        if(!q->stable || q->entry.size!=e->size || strcmp(q->entry.time,e->time)) {
            q->entry=*e;q->stable=now;q->state=WAITING;
        }
        if(e->size && (uint32_t)(now-q->stable)>=CAPTURE_STABLE_MS && q->state==WAITING) {
            q->state=PENDING;
            printf("FILM_CAPTURE_PENDING source=%s bytes=%u heuristic_only=1\n",source,e->size);
        }
    }
    for(unsigned i=0;i<queue_count;++i)if(!queue[i].present &&
        (queue[i].state==WAITING || queue[i].state==PENDING)) {
        queue[i].stable=0;queue[i].state=WAITING;
    }
    return 0;
}

int main(int argc,char **argv) {
    if(argc!=2 || strcmp(argv[1],"watch"))return 2;
    /* Startup stdout can already contain bootstrap/guard evidence. Own a
     * separate fresh file; never adopt or truncate that preceding stream. */
    if(film_log_open(&capture_log,FILM_CAPTURE_RUNTIME_LOG,1))return 22;
    setvbuf(stdout,NULL,_IONBF,0);
    if(!pins()){puts("FILM_CAPTURE_FAIL reason=pin");return 3;}
    if(stop_requested()!=0 || !absent_file(FILM_CAPTURE_INFLIGHT) ||
       !absent_file(FILM_CAPTURE_JOB_LOCK)) {puts("FILM_CAPTURE_FAIL reason=stale_stop_or_job");return 5;}
    int fd=open(CAPTURE_LOCK,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if(fd<0){puts("FILM_CAPTURE_FAIL reason=lock");return 4;}
    char pid[40];int n=snprintf(pid,sizeof(pid),"pid=%u\n",(unsigned)getpid());
    int lock_bad=write(fd,pid,(size_t)n)!=n;close(fd);
    if(lock_bad){unlink(CAPTURE_LOCK);return 5;}
    signal(SIGHUP,SIG_IGN);signal(SIGPIPE,SIG_IGN);
    signal(SIGINT,daemon_stop);signal(SIGTERM,daemon_stop);
    struct transport transport={.pid=-1,.in=-1,.out=-1};struct catalogue current;
    int result=1;const char *reason="error";uint32_t start=now_ms();
    next_output=(start^((uint32_t)getpid()*2654435761u))%10000000u;
    if(save_manifest()){reason="manifest";goto done;}
    printf("FILM_CAPTURE_READY active_ms=%u persistent=%d queue_capacity=%u\n",
           FILM_CAPTURE_ACTIVE_MS,FILM_CAPTURE_PERSISTENT,MAX_FILES);
    if(capture_log_bad()){result=21;retain_lock=1;reason="runtime_log";goto done;}
    if(transport_start(&transport)){result=5;reason="monitor_start";goto done;}
    uint32_t baseline_started=now_ms();int baseline;
    for(;;) {
        if(capture_log_bad()){result=21;retain_lock=1;reason="runtime_log";goto done;}
        int stop=stop_requested();
        if(stop<0){reason="stop_marker";goto done;}
        if(daemon_signal){result=20;reason="signal";goto done;}
        if(stop){result=0;reason="stop_file";goto done;}
        baseline=inventory(&transport,&current);
        if(capture_log_bad()){result=21;retain_lock=1;reason="runtime_log";goto done;}
        if(baseline!=-2)break;
        /* A complete root FFirst explicitly reported unmounted media. No
         * partial inventory is accepted as an empty baseline, and unknown
         * failures are never retried. Cold boot may precede SD readiness. */
        if((uint32_t)(now_ms()-baseline_started)>=FILM_CAPTURE_BASELINE_TIMEOUT_MS) {
            reason="baseline_media_timeout";goto done;
        }
        phase="baseline_media_wait";
        if(save_manifest()){reason="manifest";goto done;}
        printf("FILM_CAPTURE_BASELINE_MEDIA_WAIT elapsed_ms=%u limit_ms=%u\n",
               (unsigned)(now_ms()-baseline_started),FILM_CAPTURE_BASELINE_TIMEOUT_MS);
        pause_ms(CAPTURE_INTERVAL_MS);
    }
    if(baseline) {
        result=21;retain_lock=1;reason="baseline_unconfirmed";goto done;
    }
    if(current.count>=MAX_FILES){reason="baseline_capacity";goto done;}
    for(unsigned i=0;i<current.count;++i) {
        char source[40];if(source_path(&current.items[i],source,sizeof(source))){reason="baseline_path";goto done;}
        queue[i].entry=current.items[i];queue[i].state=OLD;queue[i].present=1;
    }
    queue_count=current.count;phase="waiting";
    if(save_manifest()){reason="manifest";goto done;}
    printf("FILM_CAPTURE_BASELINE count=%u\n",queue_count);
    uint32_t waiting=now_ms();
    while(!daemon_signal && !stop_requested() &&
          (uint32_t)(now_ms()-waiting)<CAPTURE_WAIT_MS)pause_ms(10);
    start=now_ms();
    while(1) {
        if(capture_log_bad()){result=21;retain_lock=1;reason="runtime_log";goto done;}
        int stop=stop_requested();
        if(stop<0){reason="stop_marker";break;}
        if(daemon_signal){result=20;reason="signal";break;}
        if(stop){result=0;reason="stop_file";break;}
        if(!FILM_CAPTURE_PERSISTENT && (uint32_t)(now_ms()-start)>=FILM_CAPTURE_ACTIVE_MS) {
            result=0;reason="expired";break;
        }
        if(transport.pid<0 && transport_start(&transport)){reason="monitor_restart";break;}
        phase="scanning";int scan=inventory(&transport,&current);
        if(capture_log_bad()){result=21;retain_lock=1;reason="runtime_log";goto done;}
        if(scan==-2) {
            for(unsigned i=0;i<queue_count;++i)if(queue[i].state==WAITING || queue[i].state==PENDING) {
                queue[i].stable=0;queue[i].state=WAITING;
            }
            phase="media_unavailable";
            if(save_manifest()){reason="manifest";break;}
            pause_ms(CAPTURE_INTERVAL_MS);continue;
        }
        if(scan){result=21;retain_lock=1;reason="scan_unconfirmed";break;}
        if(update_queue(&current)){reason="queue_capacity_or_path";break;}
        if(save_manifest()){reason="manifest";break;}
        unsigned pending=0;
        while(pending<queue_count && queue[pending].state!=PENDING)++pending;
        if(pending<queue_count) {
            /* inventory returned a fresh completed native prompt. Reap MPR
             * before the settings CLI or any IFSYS read/write can begin. */
            transport_stop(&transport);
            if(daemon_signal || stop_requested()!=0)continue;
            struct queued *q=&queue[pending];char settings[128];
            int gate=live_settings(settings,sizeof(settings));
            if(daemon_signal || stop_requested()!=0)continue;
            if(gate==10) {
                /* This fresh OFF observation applies to the entire pending
                 * snapshot, not only its first entry. Later ON must not turn
                 * the rest of this already observed OFF batch into jobs. */
                for(unsigned i=0;i<queue_count;++i)if(queue[i].present && queue[i].state==PENDING) {
                    queue[i].state=OFF;++off_jobs;
                    printf("FILM_CAPTURE_SKIP_OFF path=%s\n",queue[i].entry.path);
                }
            } else if(gate)puts("FILM_CAPTURE_SETTINGS_UNAVAILABLE");
            else {
                snprintf(q->target,sizeof(q->target),"A:/A6400/TEST/F%07u.JPG",next_output);
                next_output=(next_output+1u)%10000000u;
                printf("FILM_CAPTURE_DISPATCH path=%s target=%s %s\n",q->entry.path,q->target,settings);
                if(run_job(q,settings)) {
                    result=21;retain_lock=1;reason="job_uncertain";break;
                }
            }
            if(save_manifest()){reason="manifest";break;}
        }
        pause_ms(CAPTURE_INTERVAL_MS);
    }
done:
    transport_stop(&transport);
    phase=retain_lock?"uncertain":reason;
    if(own_manifest && save_manifest()){result=21;retain_lock=1;}
    if(!retain_lock && unlink(CAPTURE_LOCK)){result=21;retain_lock=1;}
    printf("FILM_CAPTURE_STOP result=%d reason=%s applied=%u off=%u failed=%u retained=%d pid=%u\n",
           result,reason,applied_jobs,off_jobs,failed_jobs,retain_lock,(unsigned)getpid());
    return film_log_close(&capture_log)?21:result;
}
