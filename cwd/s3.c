#include "s3.h"

///Simple for now, but will be expanded in a following section
void construct_shell_prompt(char shell_prompt[])
{
    strcpy(shell_prompt, "[s3]$ ");
}

///Prints a shell prompt and reads input from the user
void read_command_line(char line[])
{
    char shell_prompt[MAX_PROMPT_LEN];
    construct_shell_prompt(shell_prompt);
    printf("%s", shell_prompt);

    ///See man page of fgets(...)
    if (fgets(line, MAX_LINE, stdin) == NULL)
    {
        perror("fgets failed");
        exit(1);
    }
    ///Remove newline (enter)
    line[strlen(line) - 1] = '\0';
}

void parse_command(char line[], char *args[], int *argsc)
{
    ///Implements simple tokenization (space delimited)
    ///Note: strtok puts '\0' (null) characters within the existing storage, 
    ///to split it into logical cstrings.
    ///There is no dynamic allocation.

    ///See the man page of strtok(...)
    char *token = strtok(line, " ");
    *argsc = 0;
    while (token != NULL && *argsc < MAX_ARGS - 1)
    {
        args[(*argsc)++] = token;
        token = strtok(NULL, " ");
    }
    
    args[*argsc] = NULL; ///args must be null terminated
}

///Launch related functions
void child(char *args[], int argsc)
{
    ///Implement this function:

    ///Use execvp to load the binary 
    ///of the command specified in args[ARG_PROGNAME].
    ///For reference, see the code in lecture 3.

    execvp(args[ARG_PROGNAME], args);
}

void launch_program(char *args[], int argsc)
{
    ///Implement this function:

    ///fork() a child process.
    ///In the child part of the code,
    ///call child(args, argv)
    ///For reference, see the code in lecture 2.

    ///Handle the 'exit' command;
    ///so that the shell, not the child process,
    ///exits.

    if(strcmp(args[0], "exit") == 0){
        exit(0);  
    }

    int rc = fork();

    if(rc < 0){ 
        fprintf(stderr, "fork failed\n");
        exit(1);
    } 
    else if(rc == 0){ 
        child(args, argsc);
        exit(1);
    } 
    else{ 
        int wc = wait(NULL);
    }
}

int command_with_redirection(char line[]){
    return(strstr(line, ">") != NULL || strstr(line, "<") != NULL);
}

void launch_program_with_redirection(char *args[], int argsc){
    if(strcmp(args[0], "exit") == 0) {
        exit(0);  
    }

    int rc = fork();

    if(rc < 0) { 
        fprintf(stderr, "fork failed\n");
        exit(1);
    } 
    else if(rc == 0){ 
        child_with_redirection(args, argsc);
        exit(1);
    } 
    else{ 
        int wc = wait(NULL);
    }
}

void child_with_redirection(char *args[], int argsc){
    // variables to track redirection
    char *input_file = NULL;
    char *output_file = NULL;
    int append_mode = 0;
    char *cmd_args[MAX_ARGS];
    int cmd_argc = 0;
    
    // collect redirection info
    for(int i = 0; i < argsc; i++){
        if(strcmp(args[i], "<") == 0){
            if (i + 1 >= argsc || args[i + 1] == NULL){
                fprintf(stderr, "Error: missing filename after '<'\n");
                exit(EXIT_FAILURE);
            }
            input_file = args[i + 1]; //stores file name
            i++;  // skip filename
            
        } 
        else if(strcmp(args[i], ">>") == 0){
            if(i + 1 >= argsc || args[i + 1] == NULL){
                fprintf(stderr, "Error: missing filename after '>>'\n");
                exit(EXIT_FAILURE);
            }
            output_file = args[i + 1];
            append_mode = 1;
            i++;
            
        } 
        else if(strcmp(args[i], ">") == 0){
            if(i + 1 >= argsc || args[i + 1] == NULL){
                fprintf(stderr, "Error: missing filename after '>'\n");
                exit(EXIT_FAILURE);
            }
            output_file = args[i + 1];
            append_mode = 0;
            i++;
            
        } 
        else{
            cmd_args[cmd_argc++] = args[i]; //store args 
        }
    }
    

    //apply all redirections
    // NULL-terminate the cleaned args
    cmd_args[cmd_argc] = NULL;
    
    // Apply input redirection if specified
    if(input_file != NULL){
        child_with_input_redirected(input_file);
    }
    
    // Apply output redirection if specified
    if(output_file != NULL){
        child_with_output_redirected(output_file, append_mode);
    }
    
    // Execute
    if(execvp(cmd_args[0], cmd_args) < 0){
        perror("execvp");
        exit(EXIT_FAILURE);
    }
}

void child_with_output_redirected(char *filename, int append){
    int fd;
    int flags = O_WRONLY | O_CREAT;
    
    if(append){
        flags |= O_APPEND;
    } 
    else{
        flags |= O_TRUNC;
    }
    
    fd = open(filename, flags, 0644);
    if(fd < 0){
        perror("open");
        exit(EXIT_FAILURE);
    }
    
    if(dup2(fd, STDOUT_FILENO) < 0){
        perror("dup2");
        close(fd);
        exit(EXIT_FAILURE);
    }
    
    close(fd);
}

void child_with_input_redirected(char *filename){
    int fd;
    
    fd = open(filename, O_RDONLY); //open the file
    if(fd < 0){
        perror("open");
        exit(EXIT_FAILURE);
    }
    
    if(dup2(fd, STDIN_FILENO) < 0){ //redirects fd as the input intead of the keyboard
        perror("dup2");
        close(fd);
        exit(EXIT_FAILURE);
    }
    
    close(fd);
}

int batched_command(char line[]){
    return (strchr(line, ';') != NULL);
}

void execute_batched_commands(char line[]){
    char line_copy[MAX_LINE];
    strcpy(line_copy, line);
    
    // seperate commands by semicolon
    char *command = strtok(line_copy, ";");
    
    while(command != NULL){
        //ignore whitespace*
        while(*command == ' '){
            command++;
        }
        
        // Skip empty commands
        if(strlen(command) == 0){
            command = strtok(NULL, ";");
            continue;
        }
        
        // Parse and execute this command
        char *args[MAX_ARGS];
        int argsc;
        
        char cmd_copy[MAX_LINE];
        strcpy(cmd_copy, command);
        
        // Check for pipes 
        parse_command(cmd_copy, args, &argsc);
        
        if (argsc == 0) {
            command = strtok(NULL, ";");
            continue;
        }
        
        //Implement pipes




        //check for redirection
        else if (command_with_redirection(command)) {
            launch_program_with_redirection(args, argsc);
            reap();
        }
        else {
            launch_program(args, argsc);
            reap();
        }
        
        command = strtok(NULL, ";");
    }
}