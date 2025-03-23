#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define IN  0
#define OUT 1
#define ERR 2
#define MAXIN 1024
#define MAXWORD 64

// 实现 UNIX xargs program
int 
main(int argc, char *argv[])
{
    char inputline[MAXIN];
    char* params_original[MAXARG];
    int n, params_index_original = 0;
    int i;

    // argv[1] is the command cmd to be executed
    // argv[1],...,argv[argc-1] are the original parameters of the command
    // exec's first parameter is still argv[1], it is the name of the command
    char* cmd = argv[1];
    for (i = 1; i < argc; i++) params_original[params_index_original++] = argv[i];

    while ((n = read(IN, inputline, MAXIN)) > 0) 
    // n is the number of bytes read from input and saved to inputlines
    // read until the end of the IN file
    // if input is from console, read() can only read one line each time
    // at each line, append each argument to the original params and exec the cmd  
    {
        if (fork() == 0) // child process
        {
            char* arg; // arg is used to store each of the arguments of params as a string
            arg = (char*) malloc(sizeof(inputline));
            char* params[MAXARG];  // params is used to concatenate all original params and additional ones inside a line
            for (i = 0; i < params_index_original; i++) params[i]=params_original[i];
            int arg_index = 0, params_index = params_index_original;
            for (i = 0; i < n; i++)
            {
                if (inputline[i] == ' ' || inputline[i] == '\n') 
                                        // identify the end of an argument in param, append it to params
                {
                    arg[arg_index] = 0; // end of arg
                    params[params_index++] = arg; // save the current arg to additional params
                    arg_index = 0; // reset arg index
                    arg = (char*) malloc(sizeof(inputline));
                }
                //else if (inputlines[i] == '\n') // identify the end of an input line, run cmd
                //{
                //    arg[arg_index] = 0; // end of arg
                //    params[params_index++] = arg; // save the current arg to params
                    //params[params_index] = 0;
                    //exec(cmd, params); 
                        // the original parameters of cmd must be executed for each additional argument in the line
                        // execute cmd with params
                    //for (i = params_index_original; i < params_index; i++) params[i]=(char*) malloc(sizeof(MAXWORD));
                    //params_index = params_index_original; // reset the params index
                //    arg_index = 0; // reset arg index
                //    arg = (char*) malloc(sizeof(inputlines));
                //}
                else
                    arg[arg_index++] = inputline[i]; // read each character in inputline and save to arg
            }
            arg[arg_index] = 0;
            params[params_index] = 0;
            exec(cmd, params);
                        // the original parameters of cmd must be executed for each additional argument in the inputline
                        // execute cmd with params
        }
        else wait((int*)0); // parent process
    }
    exit(0);
}