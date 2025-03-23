#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define IN  0
#define OUT 1
#define ERR 2

#define READEND  0
#define WRITEEND 1

// 用并发编程的方式实现埃拉托斯特尼素数筛
int 
main(){
    int filtered_numbers[36], p[2];
    int i, index = 0;
    int pid;
    // the array fitered_numbers[] store the numbers after each round of filtering
    // the primes are on the top of this array at each round of filtration  
    for (i = 2; i <= 35; i++)
    { // initialize filtered_numbers[]
        filtered_numbers[index] = i;
        index++;
    }
    while (index > 0) // filter at each round, until the last pipe
    {
        pipe(p); // initialize the pipe
        if ((pid = fork()) < 0) 
        {
            fprintf(ERR, "fork error\n");
            exit(1); // exit the current parent and child processes made from fork()
        }
        else if (pid == 0) //child process
        {
            close(p[WRITEEND]);
            int prime = 0;
            int buf = 0;
            index = -1;
            
            while (read(p[READEND], &buf, sizeof(buf)) != 0) //keep reading from readend
            {
                // the first number must be prime
                if (index < 0) 
                {   prime = buf; 
                    index ++;
                }
                else
                {
                    if (buf % prime != 0) { // filter the number buf if it is divided by the prime
                            filtered_numbers[index] = buf;
                            index++;  // if not a prime, index will not increase
                    }
                }
            }
            printf("prime %d\n", prime);
            // fork again until no prime
            close(p[READEND]);
        }
        else // parent process
        {
            close(p[READEND]);
            for (i = 0; i < index; i++) // write the primes at previous round into the writeend of the pipe
            {
                write(p[WRITEEND], &filtered_numbers[i], sizeof(filtered_numbers[i]));
            }
            close(p[WRITEEND]);
            wait((int *)0); // wait until the child process exits
            exit(0); // exit the current parent and child processes made from fork()
        }
    }
    exit(0); // exit the main function
}