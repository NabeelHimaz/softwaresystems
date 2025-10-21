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

    if (strcmp(args[0], "exit") == 0) {
        exit(0);  
    }

    int rc = fork();

    if (rc < 0) { 
        fprintf(stderr, "fork failed\n");
        exit(1);
    } else if (rc == 0) { 
        printf("hello, I am child (pid: %d)\n", getpid());
        child(args, argsc);
        exit(1);
    } else { 
        printf("hello, I am parent of %d (pid: %d)\n", rc, getpid());
        int wc = wait(NULL);
    }
}

int command_with_redirection(char line[]){
    return(strstr(line, ">") != NULL || strstr(line, "<") != NULL);
}

void launch_program_with_redirection(char *args[], int argsc){
    if (strcmp(args[0], "exit") == 0) {
        exit(0);  
    }

    int rc = fork();

    if (rc < 0) { 
        fprintf(stderr, "fork failed\n");
        exit(1);
    } else if (rc == 0) { 
        printf("hello, I am child (pid: %d)\n", getpid());
        child_with_redirection(args, argsc);
        exit(1);
    } else { 
        printf("hello, I am parent of %d (pid: %d)\n", rc, getpid());
        int wc = wait(NULL);
    }
}

void child_with_redirection(char *args[], int argsc){
    char *operator;
    int op_index;
    int redir_type;
    
    while ((redir_type = find_redirection_operator(args, argsc, &operator, &op_index)) != 0) {
        
        if (op_index + 1 >= argsc || args[op_index + 1] == NULL) {
            fprintf(stderr, "Error: missing filename after %s\n", operator);
            exit(EXIT_FAILURE);
        }
        
        char *filename = args[op_index + 1];
        
        if (redir_type == 1) {
            child_with_output_redirected(filename, 0);
        } else if (redir_type == 2) {
            child_with_output_redirected(filename, 1);
        } else if (redir_type == 3) {
            child_with_input_redirected(filename);
        }
        
        args[op_index] = NULL;
        argsc = op_index;
    }
    
    if (execvp(args[0], args) < 0) {
        perror("execvp");
        exit(EXIT_FAILURE);
    }
}

int find_redirection_operator(char *args[], int argsc, char **operator, int *op_index) {
    for (int i = 0; i < argsc; i++) {
        if (strcmp(args[i], ">>") == 0) {
            *operator = args[i];
            *op_index = i;
            return 2; 
        } else if (strcmp(args[i], ">") == 0) {
            *operator = args[i];
            *op_index = i;
            return 1; 
        } else if (strcmp(args[i], "<") == 0) {
            *operator = args[i];
            *op_index = i;
            return 3; 
        }
    }
    return 0;
}

void child_with_output_redirected(char *filename, int append) {
    int fd;
    int flags = O_WRONLY | O_CREAT;
    
    if (append) {
        flags |= O_APPEND;
    } else {
        flags |= O_TRUNC;
    }
    
    fd = open(filename, flags, 0644);
    if (fd < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }
    
    if (dup2(fd, STDOUT_FILENO) < 0) {
        perror("dup2");
        close(fd);
        exit(EXIT_FAILURE);
    }
    
    close(fd);
}

void child_with_input_redirected(char *filename) {
    int fd;
    
    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }
    
    if (dup2(fd, STDIN_FILENO) < 0) {
        perror("dup2");
        close(fd);
        exit(EXIT_FAILURE);
    }
    
    close(fd);
}