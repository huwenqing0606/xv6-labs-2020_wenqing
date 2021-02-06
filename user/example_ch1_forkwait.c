#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define IN  0
#define OUT 1
#define ERR 2

#define READEND  0
#define WRITEEND 1

int 
main(){
	int pid = fork(); // start the fork() process, parent and child both started 
	if(pid>0){  // parent process
		printf("parent: child=%d\n", pid); // before wait(), parent and child processes are executing simutaneously
		pid = wait((int *) 0); // wait for the child to exit, return the child's pid 
		printf("parent: child %d is done\n", pid); // after wait(), child process exits first
		exit(0); // exit the parent and child processes made by fork()
	} else if(pid==0){ // child process
		printf("child: exiting\n");
	} else {
		printf("fork error\n");
		exit(1); // exit the parent and child processes made by fork()
	}
	exit(0);
}
