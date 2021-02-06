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
    int doforkwaitwrite = 1; // choose to do fork() and wait() to write or do dup() to write
    if (doforkwaitwrite) {
        if (fork()==0){ // child process
            write(OUT, "hello ", 6);
        } else { // parent process
            wait((int *) 0); // wait for the child to exit
            write(OUT, "world\n", 6);
            exit(0); // exit the parent and child processes made by fork()
        }
    } else {   
        int fd;
        fd = dup(OUT); // produce the file descriptor duplicating the standard output file
        write(OUT, "hello ", 6);
        write(fd, "world\n", 6);
    }
    exit(0); // exit the main function
}