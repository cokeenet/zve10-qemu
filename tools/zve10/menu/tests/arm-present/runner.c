#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <stdlib.h>
int main(void) {
 int c=open("/dev/kmsg",O_WRONLY);
 if(c>=0){dup2(c,1);dup2(c,2);if(c>2)close(c);}
 setvbuf(stdout,NULL,_IONBF,0);
 puts("ARM_RUNNER_BEGIN");
 const char *tests[]={"/tests/adapter_test","/tests/trampoline_test"};
 for(int i=0;i<2;i++){
  pid_t p=fork();
  if(p==0){char *args[]={(char*)tests[i],NULL};char *env[]={"LD_LIBRARY_PATH=/lib",NULL};execve(tests[i],args,env);perror("execve");_exit(127);}
  int st=0;if(p<0){perror("fork");continue;}waitpid(p,&st,0);
  printf("ARM_RUNNER_RESULT %s status=%d\n",tests[i],st);
 }
 puts("ARM_RUNNER_DONE");
 for(;;)pause();
}
