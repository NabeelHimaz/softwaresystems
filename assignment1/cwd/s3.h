#ifndef _S3_H_
#define _S3_H_ 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <limits.h>

#define MAX_LINE 1024
#define MAX_ARGS 128
#define MAX_PROMPT_LEN 256


enum ArgIndex{
    ARG_PROGNAME,
    ARG_1,
    ARG_2,
    ARG_3,
};

///Shell I/O and related functions (add more as appropriate)
void read_command_line(char line[]);
void construct_shell_prompt(char shell_prompt[]);
void parse_command(char line[], char *args[], int *argsc);


///Child functions (add more as appropriate)
void child(char *args[], int argsc);
void child_with_input_redirected(const char *infile);
void child_with_output_redirected(const char *outfile, int append);

///Program launching functions (add more as appropriate)
void launch_program(char *args[], int argsc);
void launch_program_with_redirection(char *args[], int argsc);
void execute_pipeline(char *args[], int argsc);
void execute_batched_commands(char line[]);

//implements cd 
int cd_implementation(char *args[], int argsc);

//helper for pipes
int count_pipes(char *args[], int argsc);

/// Checks if there's a command with a redirection/ pipes/ batched commans
int command_with_redirection(char line[]);  
int command_with_pipe(char line[]);
int has_batched_command(char line[]);

/// Subshell support 
int has_subshell(char line[]);
char* extract_subshell_content(char *line, int *start_pos);
void execute_subshell(char *subshell_content);

#endif