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
    // build two pipes, write end for parent/child
    int p_parent[2];
    int p_child[2];

    char buf[10];
    int pid;
    
    pipe(p_parent);
    pipe(p_child);

    if ((pid = fork()) < 0)
    {
        fprintf(ERR, "fork error\n");
        exit(1);
    }
    else if (pid == 0) // child process
    {
        close(p_parent[WRITEEND]);
        close(p_child[READEND]);
        read(p_parent[READEND], buf, 4);
        printf("%d: received %s\n", getpid(), buf);
        write(p_child[WRITEEND], "pong", 4);
        close(p_child[WRITEEND]);
    }
    else // parent process
    {
        close(p_parent[READEND]);
        close(p_child[WRITEEND]);
        write(p_parent[WRITEEND], "ping", 4);
        close(p_parent[WRITEEND]);
        read(p_child[READEND], buf, 4);
        printf("%d: received %s\n", getpid(), buf);
        wait((int *) 0); // wait for the child process to exit
        exit(0); // exit the parent and child process produced by fork()
    }
    exit(0);
}