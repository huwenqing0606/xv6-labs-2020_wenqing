#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define IN  0
#define OUT 1
#define ERR 2

int
main(int argc, char* argv[])
{
    if (argc != 2)
    {
	fprintf(ERR, "Wenqing says: Syntax error! Use: sleep <number>\n");
        exit(1);
    }
    int number = atoi(argv[1]);
    sleep(number);
    exit(0);
}