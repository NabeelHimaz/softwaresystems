#include "s3.h"
#include "s3.h"
#include <ctype.h>

///Simple for now, but will be expanded in a following section
void construct_shell_prompt(char shell_prompt[]){
    char cwd[MAX_LINE];

    if(getcwd(cwd, sizeof(cwd))){
        // e.g., "[/path s3]$ "
        snprintf(shell_prompt, MAX_PROMPT_LEN, "[%s s3]$ ", cwd);
    } 
    else{
        // fallback
        strcpy(shell_prompt, "[s3]$ ");
    }
}

///Prints a shell prompt and reads input from the user
void read_command_line(char line[]){
    char shell_prompt[MAX_PROMPT_LEN];
    construct_shell_prompt(shell_prompt);
    printf("%s", shell_prompt);

    ///See man page of fgets(...)
    if(fgets(line, MAX_LINE, stdin) == NULL){
        perror("fgets failed");
        exit(1);
    }
    ///Remove newline (enter)
    line[strlen(line) - 1] = '\0';
}

void parse_command(char line[], char *args[], int *argsc){
    ///Implements simple tokenization (space delimited)
    ///Note: strtok puts '\0' (null) characters within the existing storage, 
    ///to split it into logical cstrings.
    ///There is no dynamic allocation.

    char *token = strtok(line, " ");
    *argsc = 0;
    while(token != NULL && *argsc < MAX_ARGS - 1)
    {
        args[(*argsc)++] = token;
        token = strtok(NULL, " ");
    }
    args[*argsc] = NULL; ///args must be null terminated
}


// Assuming redirections are in the line:
// Parse through the line 
//Return 0 for success, -1 on error 
int parse_redirections(char *args[], int *argsc, char **infile, char **outfile, int *append){
    *infile = NULL;
    *outfile = NULL;
    *append = 0;

    int w = 0; // write index for compacting non-redirection args
    for(int r = 0; r < *argsc; r++){
        if(strcmp(args[r], "<") == 0){
            // must have a filename next
            if(r + 1 >= *argsc){ 
                fprintf(stderr, "syntax error: missing input file\n"); return -1; 
            }
            *infile = args[r + 1];
            r++; // skip filename
        } 
        else if(strcmp(args[r], ">>") == 0){
            if(r + 1 >= *argsc){ 
                fprintf(stderr, "syntax error: missing output file\n"); return -1; 
            }
            *outfile = args[r + 1];
            *append = 1;                  // append when using >>
            r++; // skip filename
        } 
        else if(strcmp(args[r], ">") == 0){
            if(r + 1 >= *argsc){ 
                fprintf(stderr, "syntax error: missing output file\n"); return -1; 
            }
            *outfile = args[r + 1];
            *append = 0;                  // overwrite for single >
            r++; // skip filename
        } 
        else{
            // keep normal argument
            args[w++] = args[r];
        }
    }
    args[w] = NULL;   // execvp needs NULL-terminated argv
    *argsc = w;       // updated arg count
    return 0;
}



///Launch related functions
void child(char *args[], int argsc){
    ///Use execvp to load the binary specified in args[ARG_PROGNAME].
    execvp(args[ARG_PROGNAME], args);
    /* If execvp returns, it's an error */
    perror("execvp");
    exit(EXIT_FAILURE);
}


int cd_implementation(char *args[], int argsc){

    static char prev_dir[MAX_LINE] = ""; // static so persists between calls
    char cwd[MAX_LINE];

    //gets current working directory and stores it in cwd
    if(!getcwd(cwd, sizeof(cwd))){
        perror("getcwd");
    }

    const char *target = NULL;

    if (argsc == 1){
        target = getenv("HOME");
        if (!target) target = "/" ;
    }   

    else if(argsc == 2){
        if(strcmp(args[1], "-") == 0){
            if(prev_dir[0] == '\0'){
                fprintf(stderr, "cd: previous directory not set\n");
                return 1;
            }
            target = prev_dir;
        } 
        else{
            target = args[1];  
        }
    } 
    else{
        fprintf(stderr, "cd: too many arguments\n");
        return 1;
    }

    if(chdir(target) != 0){
        perror("cd");
        return 1;
    }

    // Success: update prev_dir to the directory we *came from*
    if(cwd[0] != '\0'){
        strncpy(prev_dir, cwd, sizeof(prev_dir));
        prev_dir[sizeof(prev_dir)-1] = '\0';
    }

    // Optional: if "cd -" print new cwd like bash
    if(argsc == 2 && strcmp(args[1], "-") == 0){
        if(getcwd(cwd, sizeof(cwd))){
            printf("%s\n", cwd);
            fflush(stdout);
        }
    }

    return 0;
}


void launch_program(char *args[], int argsc){
    if(argsc <= 0 || args[0] == NULL) return;

    if(strcmp(args[0], "exit") == 0){
        exit(0);  
    }

    if(argsc > 0 && strcmp(args[0], "cd") == 0){
        cd_implementation(args, argsc);   
        return;
    }

    pid_t rc = fork();

    if(rc < 0){ 
        perror("fork");
        return;
    } 
    else if(rc == 0){ 
        /* child */
        child(args, argsc);
        /* child should not return */
        exit(EXIT_FAILURE);
    } 
    else{ 
        /* parent waits for child */
        wait(NULL);
    }
}

void child_with_output_redirected(const char *filename, int append){
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

void child_with_input_redirected(const char *filename){
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

void child_with_redirection(char *args[], int argsc){
    char *input_file = NULL;
    char *output_file = NULL;
    int append_mode = 0;
    
    // Use the unified parse_redirections function
    // It will compact args[] in-place and update argsc
    if(parse_redirections(args, &argsc, &input_file, &output_file, &append_mode) < 0){
        exit(EXIT_FAILURE);
    }
    
    // Apply input redirection if specified
    if(input_file != NULL){
        child_with_input_redirected(input_file);
    }
    
    // Apply output redirection if specified
    if(output_file != NULL){
        child_with_output_redirected(output_file, append_mode);
    }
    
    // Execute (args is already compacted by parse_redirections)
    if(args[0] == NULL){
        exit(EXIT_SUCCESS);
    }
    if(execvp(args[0], args) < 0){
        perror("execvp");
        exit(EXIT_FAILURE);
    }
}

int command_with_redirection(char line[]){
    return(strstr(line, ">") != NULL || strstr(line, "<") != NULL);
}

void launch_program_with_redirection(char *args[], int argsc){
    if(argsc <= 0 || args[0] == NULL) return;
    if(strcmp(args[0], "exit") == 0){
        exit(0);  
    }

    pid_t rc = fork();

    if(rc < 0) { 
        perror("fork");
        return;
    } 
    else if(rc == 0){ 
        child_with_redirection(args, argsc);
        exit(EXIT_FAILURE);
    } 
    else{ 
        wait(NULL);
    }
}


int command_with_pipe(char line[]){
    return (strchr(line, '|') != NULL);
}

int count_pipes(char *args[], int argsc){
    
    int count = 0;
    for (int i=0; i < argsc; i++){
        if(strcmp(args[i], "|") == 0){
            count ++;
        }
    }

    return count;
}

static int stage_is_empty(char *stagev[]){
    return (stagev == NULL || stagev[0] == NULL);
}


void execute_pipeline(char *args[], int argsc){
    int num_pipes = count_pipes(args, argsc);
    int num_commands = num_pipes + 1;

    /* Create pipes (if any). If num_pipes == 0 this loop is skipped. */
    int pipe_fds[2 * (num_pipes > 0 ? num_pipes : 1)];
    if (num_pipes > 0) {
        for(int i = 0; i < num_pipes; i++){
            if(pipe(pipe_fds + (i * 2)) < 0){
                perror("pipe");
                exit(EXIT_FAILURE);
            }
        }
    }

    /* Build per-stage argv arrays. Initialize to NULL. */
    char *commands[num_commands][MAX_ARGS];
    memset(commands, 0, sizeof(commands));
    int cmd_lens[num_commands];
    int cmd_index = 0;
    int arg_index = 0;

    for(int i = 0; i < argsc; i++){
        if(strcmp(args[i], "|") == 0){
            commands[cmd_index][arg_index] = NULL;
            cmd_lens[cmd_index] = arg_index;
            cmd_index++;
            arg_index = 0;
        } 
        else{
            commands[cmd_index][arg_index++] = args[i];
        }
    }

    /* terminate last command */
    commands[cmd_index][arg_index] = NULL;
    cmd_lens[cmd_index] = arg_index;

    /* Fork a process for each command and wire up pipes + redirections.
       We reuse the existing child/redirection helpers (which perform exec)
       rather than calling the higher-level launch_* functions that themselves
       fork — we are already in the per-stage child here. */
    for(int i = 0; i < num_commands; i++){
        pid_t pid = fork();
        if(pid < 0){
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if(pid == 0){  /* child */
            /* If not the first command, read from previous pipe */
            if(i > 0 && num_pipes > 0){
                if (dup2(pipe_fds[(i - 1) * 2], STDIN_FILENO) < 0) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
            }

            /* If not the last command, write to next pipe */
            if(i < num_commands - 1 && num_pipes > 0){
                if(dup2(pipe_fds[i * 2 + 1], STDOUT_FILENO) < 0){
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
            }

            /* Close all pipe fds in child */
            if(num_pipes > 0){
                for (int j = 0; j < num_pipes * 2; j++) close(pipe_fds[j]);
            }

            /* Handle redirections for this stage. parse_redirections will
               compact the argv and return infile/outfile if present. */
            char *infile = NULL;
            char *outfile = NULL;
            int append = 0;
            int this_argc = cmd_lens[i];

            if(this_argc > 0){
                if(parse_redirections(commands[i], &this_argc, &infile, &outfile, &append) < 0){
                    /* syntax error already printed by parse_redirections */
                    exit(EXIT_FAILURE);
                }
            }

            /* If input redirection specified for this stage, apply it. */
            if(infile != NULL){
                child_with_input_redirected(infile);
            }

            /* If output redirection specified for this stage, apply it. */
            if(outfile != NULL){
                child_with_output_redirected(outfile, append);
            }

            /* Nothing to execute? just exit child. */
            if(commands[i][0] == NULL){
                exit(EXIT_SUCCESS);
            }

            /* Exec the command for this stage. */
            execvp(commands[i][0], commands[i]);
            perror("execvp");
            exit(EXIT_FAILURE);
        }
        /* parent continues to create next stage */
    }

    /* Parent closes all pipe file descriptors */
    if(num_pipes > 0){
        for(int i = 0; i < num_pipes * 2; i++) close(pipe_fds[i]);
    }

    /* Wait for all children */
    for(int i = 0; i < num_commands; i++) wait(NULL);
}


int has_batched_command(char line[]){
    return (strchr(line, ';') != NULL);

}

void execute_batched_commands(char line[]){
    char line_copy[MAX_LINE];
    strncpy(line_copy, line, sizeof(line_copy)-1);
    line_copy[sizeof(line_copy)-1] = '\0';

    // separate commands by semicolon 
    char *saveptr = NULL;
    char *command = strtok_r(line_copy, ";", &saveptr);

    while(command != NULL){
        // trim leading and trailing whitespace using isspace()
        while(*command && isspace((unsigned char)*command)) command++;
        size_t len = strlen(command);
        while(len > 0 && isspace((unsigned char)command[len-1])){
            command[--len] = '\0';
        }

        if(len == 0){
            command = strtok_r(NULL, ";", &saveptr);
            continue; // empty command
        }


        // Parse and execute this command
        char *args[MAX_ARGS];
        int argsc;
        char cmd_copy[MAX_LINE];
        strncpy(cmd_copy, command, sizeof(cmd_copy)-1);
        cmd_copy[sizeof(cmd_copy)-1] = '\0';

        parse_command(cmd_copy, args, &argsc);
        if(argsc == 0){
            command = strtok_r(NULL, ";", &saveptr);
            continue;
        }

        if(command_with_pipe(command)){
            execute_pipeline(args, argsc);
        } 
        else if(command_with_redirection(command)){
            launch_program_with_redirection(args, argsc);
        } 
        else{
            launch_program(args, argsc);
        }

        command = strtok_r(NULL, ";", &saveptr);
    }
}