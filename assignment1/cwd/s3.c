#include "s3.h"
#include "s3.h"
#include <ctype.h>

void construct_shell_prompt(char shell_prompt[]){
    char cwd[MAX_LINE];

    if(getcwd(cwd, sizeof(cwd))){
        
        snprintf(shell_prompt, MAX_PROMPT_LEN, "[%s s3]$ ", cwd);
    } 
    else{

        strcpy(shell_prompt, "[s3]$ ");
    }
}

void read_command_line(char line[]){
    char shell_prompt[MAX_PROMPT_LEN];
    construct_shell_prompt(shell_prompt);
    printf("%s", shell_prompt);

    if(fgets(line, MAX_LINE, stdin) == NULL){

        if(feof(stdin)){
            exit(0);
        }
        perror("fgets failed");
        exit(1);
    }
    line[strlen(line) - 1] = '\0';
}

void parse_command(char line[], char *args[], int *argsc){
    char *token = strtok(line, " ");
    *argsc = 0;
    while(token != NULL && *argsc < MAX_ARGS - 1){
        args[(*argsc)++] = token;
        token = strtok(NULL, " ");
    }
    args[*argsc] = NULL;
}

int parse_redirections(char *args[], int *argsc, char **infile, char **outfile, int *append){
    *infile = NULL;
    *outfile = NULL;
    *append = 0;

    int w = 0; 
    for (int r = 0; r < *argsc; r++) {
        if(strcmp(args[r], "<") == 0){
            if(r + 1 >= *argsc){ 
                fprintf(stderr, "syntax error: missing input file\n"); 
                return -1; 
            }
            *infile = args[r + 1];
            r++;
        } 
        else if(strcmp(args[r], ">>") == 0){
            if(r + 1 >= *argsc){ 
                fprintf(stderr, "syntax error: missing output file\n"); 
                return -1; 
            }
            *outfile = args[r + 1];
            *append = 1;       
            r++; 
        } 
        else if(strcmp(args[r], ">") == 0){
            if(r + 1 >= *argsc){ 
                fprintf(stderr, "syntax error: missing output file\n"); 
                return -1; 
            }
            *outfile = args[r + 1];
            *append = 0;            
            r++;
        } 
        else{
            args[w++] = args[r];
        }
    }
    args[w] = NULL;  
    *argsc = w;      
    return 0;
}

void child(char *args[], int argsc)
{
    execvp(args[ARG_PROGNAME], args);
    perror("execvp");
    exit(EXIT_FAILURE);
}


int cd_implementation(char *args[], int argsc) {

    static char prev_dir[MAX_LINE] = ""; 
    char cwd[MAX_LINE];

    if(!getcwd(cwd, sizeof(cwd))){
        perror("getcwd");
    }

    const char *target = NULL;

    if(argsc == 1){
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

    if(cwd[0] != '\0'){
        strncpy(prev_dir, cwd, sizeof(prev_dir));
        prev_dir[sizeof(prev_dir)-1] = '\0';
    }

    if(argsc == 2 && strcmp(args[1], "-") == 0){
        if (getcwd(cwd, sizeof(cwd))) {
            printf("%s\n", cwd);
            fflush(stdout);
        }
    }

    return 0;
}


void launch_program(char *args[], int argsc)
{
    if(argsc <= 0 || args[0] == NULL) return;

    if(strcmp(args[0], "exit") == 0){
        exit(0);  
    }

    if (argsc > 0 && strcmp(args[0], "cd") == 0){
        cd_implementation(args, argsc);   
        return;
    }

    pid_t rc = fork();

    if(rc < 0){ 
        perror("fork");
        return;
    } 
    else if(rc == 0){ 
        child(args, argsc);
        exit(EXIT_FAILURE);
    } 
    else{ 
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
    
    fd = open(filename, O_RDONLY); 
    if(fd < 0){
        perror("open");
        exit(EXIT_FAILURE);
    }
    
    if(dup2(fd, STDIN_FILENO) < 0){ 
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
    char *cmd_args[MAX_ARGS];
    int cmd_argc = 0;

    for(int i = 0; i < argsc; i++){
        if(strcmp(args[i], "<") == 0){
            if (i + 1 >= argsc || args[i + 1] == NULL){
                fprintf(stderr, "Error: missing filename after '<'\n");
                exit(EXIT_FAILURE);
            }
            input_file = args[i + 1]; 
            i++;  
            
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
    
    cmd_args[cmd_argc] = NULL;
    
    if(input_file != NULL){
        child_with_input_redirected(input_file);
    }

    if(output_file != NULL){
        child_with_output_redirected(output_file, append_mode);
    }
    
    if(cmd_args[0] == NULL){
        exit(EXIT_SUCCESS);
    }
    if(execvp(cmd_args[0], cmd_args) < 0){
        perror("execvp");
        exit(EXIT_FAILURE);
    }
}


int command_with_redirection(char line[]){
    return(strstr(line, ">") != NULL || strstr(line, "<") != NULL);
}

void launch_program_with_redirection(char *args[], int argsc){
    if(argsc <= 0 || args[0] == NULL) return;
    if(strcmp(args[0], "exit") == 0) {
        exit(0);  
    }

    pid_t rc = fork();

    if(rc < 0){ 
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
    for(int i=0; i < argsc; i++){
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

    int pipe_fds[2 * (num_pipes > 0 ? num_pipes : 1)];
    if(num_pipes > 0){
        for(int i = 0; i < num_pipes; i++){
            if(pipe(pipe_fds + (i * 2)) < 0){
                perror("pipe");
                exit(EXIT_FAILURE);
            }
        }
    }

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

    commands[cmd_index][arg_index] = NULL;
    cmd_lens[cmd_index] = arg_index;
    for(int i = 0; i < num_commands; i++){
        pid_t pid = fork();
        if(pid < 0){
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if(pid == 0){  
            if(i > 0 && num_pipes > 0){
                if(dup2(pipe_fds[(i - 1) * 2], STDIN_FILENO) < 0){
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
            }

            if(i < num_commands - 1 && num_pipes > 0){
                if(dup2(pipe_fds[i * 2 + 1], STDOUT_FILENO) < 0){
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
            }

            if(num_pipes > 0){
                for (int j = 0; j < num_pipes * 2; j++) close(pipe_fds[j]);
            }

            char *infile = NULL;
            char *outfile = NULL;
            int append = 0;
            int this_argc = cmd_lens[i];

            if(this_argc > 0){
                if(parse_redirections(commands[i], &this_argc, &infile, &outfile, &append) < 0){
                    exit(EXIT_FAILURE);
                }
            }

            if (infile != NULL) {
                child_with_input_redirected(infile);
            }

            if(outfile != NULL){
                child_with_output_redirected(outfile, append);
            }

            if(commands[i][0] == NULL){
                exit(EXIT_SUCCESS);
            }

            execvp(commands[i][0], commands[i]);
            perror("execvp");
            exit(EXIT_FAILURE);
        }
    }

    if(num_pipes > 0){
        for (int i = 0; i < num_pipes * 2; i++) close(pipe_fds[i]);
    }

    for(int i = 0; i < num_commands; i++) wait(NULL);
}

int has_batched_command(char line[]){
    int depth = 0;
    for (int i = 0; line[i] != '\0'; i++) {
        if(line[i] == '('){
            depth++;
        } 
        else if(line[i] == ')'){
            depth--;
        } 
        else if(line[i] == ';' && depth == 0){
            return 1;  
        }
    }
    return 0;  
}

void execute_batched_commands(char line[]){
    char line_copy[MAX_LINE];
    strncpy(line_copy, line, sizeof(line_copy)-1);
    line_copy[sizeof(line_copy)-1] = '\0';

    int original_len = strlen(line_copy);
    
    int start = 0;
    int depth = 0;
    
    for(int i = 0; i < original_len; i++){
        if(line_copy[i] == '('){
            depth++;
        } 
        else if(line_copy[i] == ')'){
            depth--;
        } 
        else if(line_copy[i] == ';' && depth == 0){
            line_copy[i] = '\0'; 
            char *command = line_copy + start;
            
            // Trim whitespace
            while(*command && isspace((unsigned char)*command)) command++;
            size_t len = strlen(command);
            while(len > 0 && isspace((unsigned char)command[len-1])){
                command[--len] = '\0';
            }
            
            if(len > 0){
                if(has_subshell(command)){
                    int pos = 0;
                    while (command[pos] && isspace((unsigned char)command[pos])) pos++;
                    
                    if(command[pos] == '('){
                        char *content = extract_subshell_content(command, &pos);
                        if (content) {
                            execute_subshell(content);
                            free(content);
                            start = i + 1;
                            continue;
                        }
                    }
                }
                
                // Parse and execute this command
                char *args[MAX_ARGS];
                int argsc;
                char cmd_copy[MAX_LINE];
                strncpy(cmd_copy, command, sizeof(cmd_copy)-1);
                cmd_copy[sizeof(cmd_copy)-1] = '\0';
                
                parse_command(cmd_copy, args, &argsc);
                if(argsc > 0){
                    if(command_with_pipe(command)){
                        execute_pipeline(args, argsc);
                    } 
                    else if(command_with_redirection(command)){
                        launch_program_with_redirection(args, argsc);
                    } 
                    else{
                        launch_program(args, argsc);
                    }
                }
            }
            
            start = i + 1;
        }
    }
    
    // Handle the last command (after the last semicolon or if there's no semicolon)
    if(start < original_len){
        char *command = line_copy + start;
        
        // Trim whitespace
        while(*command && isspace((unsigned char)*command)) command++;
        size_t len = strlen(command);
        while(len > 0 && isspace((unsigned char)command[len-1])){
            command[--len] = '\0';
        }
        
        if (len > 0) {
            
            if(has_subshell(command)){
                int pos = 0;
                while (command[pos] && isspace((unsigned char)command[pos])) pos++;
                
                if(command[pos] == '('){
                    char *content = extract_subshell_content(command, &pos);
                    if(content){
                        execute_subshell(content);
                        free(content);
                        return;
                    }
                }
            }
            
            // Parse and execute this command
            char *args[MAX_ARGS];
            int argsc;
            char cmd_copy[MAX_LINE];
            strncpy(cmd_copy, command, sizeof(cmd_copy)-1);
            cmd_copy[sizeof(cmd_copy)-1] = '\0';
            
            parse_command(cmd_copy, args, &argsc);
            if (argsc > 0) {
                if(command_with_pipe(command)){
                    execute_pipeline(args, argsc);
                } 
                else if (command_with_redirection(command)){
                    launch_program_with_redirection(args, argsc);
                } 
                else{
                    launch_program(args, argsc);
                }
            }
        }
    }
}

//PE1

int has_subshell(char line[]){
    return (strchr(line, '(') != NULL && strchr(line, ')') != NULL);
}

char* extract_subshell_content(char *line, int *start_pos){
    int i = *start_pos;
    
    // Skip whitespace to find opening parenthesis
    while (line[i] && isspace((unsigned char)line[i])) i++;
    
    if(line[i] != '('){
        return NULL;  // No subshell at this position
    }
    
    i++; // Skip opening parenthesis
    int depth = 1;
    int content_start = i;
    
    while(line[i] && depth > 0){
        if(line[i] == '('){
            depth++;
        } 
        else if(line[i] == ')'){
            depth--;
        }
        if (depth > 0) i++;
    }
    
    if(depth != 0){
        fprintf(stderr, "Error: Unmatched parentheses in subshell\n");
        return NULL;
    }
    
    int content_len = i - content_start;
    char *content = malloc(content_len + 1);
    if(!content){
        perror("malloc");
        return NULL;
    }
    
    strncpy(content, line + content_start, content_len);
    content[content_len] = '\0';
    
    *start_pos = i + 1; 
    
    return content;
}

void execute_subshell(char *subshell_content){
    pid_t pid = fork();
    
    if (pid < 0){
        perror("fork");
        return;
    }
    
    if (pid == 0){
        execlp("./s3", "s3", "-c", subshell_content, (char*)NULL);
        execlp("s3", "s3", "-c", subshell_content, (char*)NULL);
        perror("execlp failed to launch subshell");
        exit(EXIT_FAILURE);
    } 
    else{
        wait(NULL);
    }
}