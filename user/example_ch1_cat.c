#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define IN  0
#define OUT 1
#define ERR 2

#define READEND  0
#define WRITEEND 1

void
main(){
    char buf[512];
    int n;
    for (;;){
        n = read(IN, buf, sizeof(buf));
        if (n==0){ // read() returns 0 when reaching the end of the file 
            fprintf(ERR, "Wenqing says: finish!\n");
            exit(0);
        }
        if (n<0){
            fprintf(ERR, "read error\n");
            exit(1);
        }
        if (write(OUT, buf, n)!=n){ // write() returns the bytes that are being written
            fprintf(OUT, "write error\n");
            exit(1);
        }
    }
}
