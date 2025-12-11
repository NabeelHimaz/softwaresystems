#include "s3.h"
#include <ctype.h>

void process_command_line(char *line){
    char *args[MAX_ARGS];
    int argsc;
    
    if(has_batched_command(line)){
        execute_batched_commands(line);
    }
    else if(command_with_pipe(line)){
        parse_command(line, args, &argsc);
        execute_pipeline(args, argsc);
    }
    else if(command_with_redirection(line)){
        parse_command(line, args, &argsc);
        launch_program_with_redirection(args, argsc);
    }
    else{
        parse_command(line, args, &argsc);
        launch_program(args, argsc);
    }
}

void process_line_with_subshells(char *line){
    int pos = 0;
    
    while(line[pos] && isspace((unsigned char)line[pos])) pos++;
    
    if(line[pos] == '('){
        char *content = extract_subshell_content(line, &pos);
        if(content){
            execute_subshell(content);
            free(content);

            while (line[pos] && isspace((unsigned char)line[pos])) pos++;
            if(line[pos] != '\0'){
                fprintf(stderr, "cannot handle this command\n");
            }
            return;
        }
    }
    process_command_line(line);
}

int main(int argc, char *argv[]){
    char line[MAX_LINE];

    if(argc >= 3 && strcmp(argv[1], "-c") == 0){
        strncpy(line, argv[2], MAX_LINE - 1);
        line[MAX_LINE - 1] = '\0';
        process_command_line(line);
        return 0;
    }

    while(1){
        read_command_line(line);
        
        if(has_subshell(line)){
            process_line_with_subshells(line);
            continue;
        }
        process_command_line(line);
    }
    return 0;
}