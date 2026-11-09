#!/usr/bin/env python3
"""Late-load discovery against local-scope fixture DSOs while threads exist."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
FIXTURE = r'''
void ViewRootSelectorToInstance(void) {}
void _ZN18CmnViewSettingNodeC1EPPS_iPK24CmnViewSettingProperties(void) {}
'''
HARNESS = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
struct result {
 uint32_t abi_version,result_bytes;
 uintptr_t object_base,views_base,caution_base,view_handle,caution_handle;
 uint32_t found_mask,anchor_mask;
};
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond=PTHREAD_COND_INITIALIZER;
static int quit;
static void *worker(void *unused) {
 (void)unused;pthread_mutex_lock(&mutex);
 while(!quit)pthread_cond_wait(&cond,&mutex);
 pthread_mutex_unlock(&mutex);return NULL;
}
int main(int argc,char **argv) {
 assert(argc==2);
 int flags=strcmp(argv[1],"global")==0?RTLD_GLOBAL:RTLD_LOCAL;
 void *libraries[3];
 const char *names[]={"./libObj.so","./viewUnified2.so","./CautionConfig.so"};
 for(int i=0;i<3;i++){libraries[i]=dlopen(names[i],RTLD_NOW|RTLD_LOCAL);assert(libraries[i]);}
 /* Dependency symbols remain local: probe must use acquired handles. */
 assert(dlsym(RTLD_DEFAULT,"ViewRootSelectorToInstance")==NULL);
 pthread_t workers[3];
 for(int i=0;i<3;i++)assert(pthread_create(&workers[i],NULL,worker,NULL)==0);
 void *probe=dlopen("./probe.so",RTLD_NOW|flags);assert(probe);
 int (*run)(struct result *,size_t)=dlsym(probe,"nm_discovery_probe");assert(run);
 struct result out={0};
 assert(run(NULL,sizeof out)==-1);
 assert(run(&out,sizeof out-1)==-1);
 for(int i=0;i<3;i++){
  assert(run(&out,sizeof out)==0);
  assert(out.abi_version==1 && out.result_bytes==sizeof out);
  assert(out.found_mask==7 && (out.anchor_mask&2));
  assert(out.object_base && out.views_base && out.caution_base);
  assert(out.view_handle && out.caution_handle);
 }
 pthread_mutex_lock(&mutex);quit=1;pthread_cond_broadcast(&cond);pthread_mutex_unlock(&mutex);
 for(int i=0;i<3;i++)assert(pthread_join(workers[i],NULL)==0);
 puts("LATE_DISCOVERY_PASS");
 return 0;
}
'''


def main():
    cc = os.environ.get('CC', 'cc')
    with tempfile.TemporaryDirectory(prefix='newmenu-discovery-') as directory:
        work = Path(directory)
        (work / 'fixture.c').write_text(FIXTURE)
        (work / 'harness.c').write_text(HARNESS)
        for name in ('libObj.so','viewUnified2.so','CautionConfig.so'):
            subprocess.run([cc,'-shared','-fPIC','fixture.c','-o',name],cwd=work,check=True)
        subprocess.run([cc,'-shared','-fPIC','-fvisibility=hidden','-Wall','-Wextra','-Werror',
                        str(ROOT/'probe.c'),'-ldl','-o','probe.so'],cwd=work,check=True)
        subprocess.run([cc,'-Wall','-Wextra','-Werror','harness.c','-pthread','-ldl',
                        '-o','harness'],cwd=work,check=True)
        for mode in ('local','global'):
            subprocess.run([str(work/'harness'),mode],cwd=work,check=True,timeout=5)
    print('PASS: explicit discovery works after local-scope dependencies and threads exist, under LOCAL and GLOBAL probe loads')


if __name__ == '__main__':
    main()
