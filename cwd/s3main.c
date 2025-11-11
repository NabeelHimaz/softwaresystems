#include "s3.h"

int main(int argc, char *argv[])
{
    char line[MAX_LINE];
    char *args[MAX_ARGS];
    int argsc;

    while(1){
        read_command_line(line);
        
        //ignore empty lines
        if (strlen(line) == 0) {
            continue;
        }
        
        // check for batched commands
        if(batched_command(line)){
            execute_batched_commands(line);
            continue;
        }
        
        parse_command(line, args, &argsc);
        
        if (argsc == 0) {
            continue;
        }

        if(command_with_redirection(line)){
            launch_program_with_redirection(args, argsc);
            reap();
        }

        else{
            launch_program(args, argsc);
            reap();
        }
    }

    return 0;
}

