#include "s3.h"

///Simple for now, but will be expanded in a following section
void construct_shell_prompt(char shell_prompt[])
{
    char cwd[MAX_LINE];

    if (getcwd(cwd, sizeof(cwd))) {
        // e.g., "[/path s3]$ "
        snprintf(shell_prompt, MAX_PROMPT_LEN, "[%s s3]$ ", cwd);
    } else {
        // fallback
        strcpy(shell_prompt, "[s3]$ ");
    }
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

int command_with_redirection(char line[])
{
    return (strstr(line, " > ") != NULL) ||
           (strstr(line, " >> ") != NULL) ||
           (strstr(line, " < ") != NULL);

}


// Returns 0 on success, -1 on syntax error (e.g., missing filename)
int parse_redirections(char *args[], int *argsc,
                       char **infile, char **outfile, int *append)
{
    *infile = NULL;
    *outfile = NULL;
    *append = 0;

    int w = 0; // write index for compacting non-redirection args
    for (int r = 0; r < *argsc; r++) {
        if (strcmp(args[r], "<") == 0) {
            // must have a filename next
            if (r + 1 >= *argsc) { fprintf(stderr, "syntax error: missing input file\n"); return -1; }
            *infile = args[r + 1];
            r++; // skip filename
        } else if (strcmp(args[r], ">>") == 0) {
            if (r + 1 >= *argsc) { fprintf(stderr, "syntax error: missing output file\n"); return -1; }
            *outfile = args[r + 1];
            *append = 1;                  // <<<<<< set append when using >>
            r++; // skip filename
        } else if (strcmp(args[r], ">") == 0) {
            if (r + 1 >= *argsc) { fprintf(stderr, "syntax error: missing output file\n"); return -1; }
            *outfile = args[r + 1];
            *append = 0;                  // overwrite for single >
            r++; // skip filename
        } else {
            // keep normal argument
            args[w++] = args[r];
        }
    }
    args[w] = NULL;   // execvp needs NULL-terminated argv
    *argsc = w;       // updated arg count
    return 0;
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

void child_with_input_redirected(const char *infile)
{
    int fd = open(infile, O_RDONLY);
    if (fd < 0) { perror("open infile"); _exit(1); }
    if (dup2(fd, STDIN_FILENO) < 0) { perror("dup2 stdin"); _exit(1); }
    close(fd);
}

void child_with_output_redirected(const char *outfile, int append)
{
    int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
    int fd = open(outfile, flags, 0644);
    if (fd < 0) { perror("open outfile"); _exit(1); }
    if (dup2(fd, STDOUT_FILENO) < 0) { perror("dup2 stdout"); _exit(1); }
    close(fd);
}


int cd_implementation(char *args[], int argsc) {

    char prev_dir[MAX_LINE] = "";
    char cwd[MAX_LINE];


    //gets current working directory and stores it in cwd/ returns an error if it can't access
    if (!getcwd(cwd, sizeof(cwd))) {
        perror("getcwd");
    }

    const char *target = NULL;

    if (argsc == 1){
        target = getenv("HOME");
        if (!target) target = "/" ;
    }   

    else if (argsc == 2) {
        if (strcmp(args[1], "-") == 0) {
            if (prev_dir[0] == '\0') {
                fprintf(stderr, "cd: previous directory not set\n");
                return 1;
            }
            target = prev_dir;
        } else {
            target = args[1];  
        }
    } else {
        fprintf(stderr, "cd: too many arguments\n");
        return 1;
    }

    if (chdir(target) != 0) {
        perror("cd");
        return 1;
    }

    // Success: update prev_dir to the directory we *came from*
    if (cwd[0] != '\0') {
        strncpy(prev_dir, cwd, sizeof(prev_dir));
        prev_dir[sizeof(prev_dir)-1] = '\0';
    }

    // Optional: if "cd -" print new cwd like bash
    if (argsc == 2 && strcmp(args[1], "-") == 0) {
        if (getcwd(cwd, sizeof(cwd))) {
            printf("%s\n", cwd);
            fflush(stdout);
        }
    }

    return 0;
}





void launch_program_with_redirection(char *args[], int argsc)
{
    char *infile = NULL, *outfile = NULL;
    int append = 0;

    if (parse_redirections(args, &argsc, &infile, &outfile, &append) < 0) {
        return; // syntax error already printed
    }

    if (argsc > 0 && strcmp(args[0], "exit") == 0) {
        exit(0);
    }

    pid_t rc = fork();
    if (rc < 0) {
        perror("fork");
        return;
    } else if (rc == 0) {
        // child: apply requested redirections first
        if (infile)  child_with_input_redirected(infile);
        if (outfile) child_with_output_redirected(outfile, append);
        // then exec
        execvp(args[0], args);
        perror("execvp");
        _exit(127);
    } else {
        // parent: do NOT wait here; main() will reap()
        return;
    }
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
        exit(0);  // Exit the shell itself
    }


    if (argsc > 0 && strcmp(args[0], "cd") == 0) {
        cd_implementation(args, argsc);   // run in parent, no fork
        return;
    }

    int rc = fork();

    if (rc < 0) { // fork failed; exit
        fprintf(stderr, "fork failed\n");
        exit(1);
    } else if (rc == 0) { // child (new process)
        
        child(args, argsc);
        exit(1);
    } else { // parent goes down this path (main)
        //int wc = wait();
        return;
    }
}