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
    int p[2];
    char *argv[2];
    argv[0] = "wc";
    argv[1] = 0;

    pipe(p); // initialize the pipe
    if (fork()==0){ // child process
        close(IN);
        dup(p[READEND]); // set the pipe readend pointing to file IN
        close(p[READEND]);
        close(p[WRITEEND]);        
        exec("/bin/wc", argv);
    } else { // parent process
        close(p[READEND]);
        write(p[WRITEEND], "hello world\n", 12);
        close(p[WRITEEND]);
        wait((int *) 0); // wait for the child process to exit 
        exit(0); // exit the parent and child processes made by fork()
    }
    exit(0);
}