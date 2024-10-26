#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>

#define MAX_LEN 512           // Maximum length of command line input
#define MAXARGS 10            // Maximum number of arguments
#define ARGLEN 30             // Maximum length of each argument

// ANSI color codes for prompt
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"

// Function declarations
int execute(char *arglist[]);
char** tokenize(char* cmdline);
char* read_cmd(char*, FILE*);
void parse_redirects_and_pipes(char **args, int *infile, int *outfile, int *is_pipe, char ***cmdlist);

int main() {
    char *cmdline;
    char **arglist;
    char prompt[MAX_LEN];
    char hostname[HOST_NAME_MAX];
    char cwd[PATH_MAX];
    char* username = getenv("USER");
    if (username == NULL) username = "unknown"; // Fallback if username not set

    gethostname(hostname, HOST_NAME_MAX);  // Get the system's hostname

    while (1) {
        // Display the shell prompt with username, hostname, and current directory
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            snprintf(prompt, sizeof(prompt), 
                COLOR_RED "PUCITshell " COLOR_RESET  
                "(" COLOR_GREEN "%s" COLOR_RESET     
                "@" COLOR_GREEN "%s" COLOR_RESET      
                ")-[" COLOR_CYAN "%s" COLOR_RESET "] : ", 
                username, hostname, cwd);
        } else {
            perror("getcwd() error");
            return 1;
        }

        // Read command input
        cmdline = read_cmd(prompt, stdin);
        if (cmdline == NULL) break;
        if ((arglist = tokenize(cmdline)) != NULL) {
            execute(arglist);  // Execute the command
            // Free allocated memory for arguments
            for (int j = 0; j < MAXARGS + 1; j++) free(arglist[j]);
            free(arglist);
            free(cmdline);
        }
    }
    printf("\n");
    return 0;
}

// Function to execute commands and handle redirection and piping
int execute(char *arglist[]) {
    int infile = 0, outfile = 1, is_pipe = 0;
    char **cmdlist[MAXARGS];
    parse_redirects_and_pipes(arglist, &infile, &outfile, &is_pipe, cmdlist);

    if (is_pipe) {  // Handle command piping
        int fd[2];
        int status;
        pid_t pid;

        for (int i = 0; cmdlist[i] != NULL; i++) {
            pipe(fd);  // Create pipe
            if ((pid = fork()) == 0) {
                if (i > 0) dup2(infile, 0); // Read from previous pipe
                if (cmdlist[i + 1] != NULL) dup2(fd[1], 1); // Write to current pipe
                close(fd[0]); // Close read end
                execvp(cmdlist[i][0], cmdlist[i]);
                perror("Command execution failed");
                exit(1);
            }
            close(fd[1]);
            infile = fd[0]; // For the next command
            wait(&status);
        }
    } else {  // Handle simple commands with possible redirection
        pid_t pid = fork();
        if (pid == 0) {
            if (infile != 0) {
                dup2(infile, STDIN_FILENO);  // Redirect input
                close(infile);
            }
            if (outfile != 1) {
                dup2(outfile, STDOUT_FILENO);  // Redirect output
                close(outfile);
            }
            execvp(cmdlist[0][0], cmdlist[0]);  // Execute command
            perror("Command execution failed");
            exit(1);
        }
        wait(NULL);  // Wait for child process to finish
    }

    // Close files if they were redirected
    if (infile != 0) close(infile);
    if (outfile != 1) close(outfile);
    return 0;
}

// Parse command line arguments for input/output redirection and piping
void parse_redirects_and_pipes(char **args, int *infile, int *outfile, int *is_pipe, char ***cmdlist) {
    int i, cmd_idx = 0, arg_idx = 0;
    cmdlist[cmd_idx] = malloc(MAXARGS * sizeof(char*));  // Allocate space for the command list

    for (i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "<") == 0) {
            *infile = open(args[++i], O_RDONLY);  // Open input file
            if (*infile < 0) {
                perror("Cannot open input file");
                exit(1);
            }
        } else if (strcmp(args[i], ">") == 0) {
            *outfile = open(args[++i], O_WRONLY | O_CREAT | O_TRUNC, 0644);  // Open output file
            if (*outfile < 0) {
                perror("Cannot open output file");
                exit(1);
            }
        } else if (strcmp(args[i], "|") == 0) {
            cmdlist[cmd_idx][arg_idx] = NULL;  // End current command for piping
            cmd_idx++;
            cmdlist[cmd_idx] = malloc(MAXARGS * sizeof(char*));  // Allocate next command
            arg_idx = 0;
            *is_pipe = 1;
        } else {
            cmdlist[cmd_idx][arg_idx++] = args[i];  // Add argument to current command
        }
    }
    cmdlist[cmd_idx][arg_idx] = NULL;  // End the last command
    cmdlist[cmd_idx + 1] = NULL;       // Null-terminate command list
}

// Tokenize command line into arguments
char** tokenize(char* cmdline) {
    char** arglist = malloc(sizeof(char*) * (MAXARGS + 1));  // Allocate argument list
    for (int j = 0; j < MAXARGS + 1; j++) {
        arglist[j] = malloc(sizeof(char) * ARGLEN);
        bzero(arglist[j], ARGLEN);
    }

    if (cmdline[0] == '\0') return NULL;

    int argnum = 0;
    char* cp = cmdline;
    char* start;
    int len;

    while (*cp != '\0') {
        while (*cp == ' ' || *cp == '\t') cp++;  // Skip whitespace

        start = cp;
        len = 0;
        while (*cp != '\0' && !(*cp == ' ' || *cp == '\t')) {
            cp++;
            len++;
        }

        if (len >= ARGLEN) len = ARGLEN - 1;  // Limit argument length

        strncpy(arglist[argnum], start, len);  // Copy argument
        arglist[argnum][len] = '\0';
        argnum++;
    }
    arglist[argnum] = NULL;  // Null-terminate argument list
    return arglist;
}

// Read command from input and print prompt
char* read_cmd(char* prompt, FILE* fp) {
    printf("%s", prompt);
    int c;
    int pos = 0;
    char* cmdline = malloc(sizeof(char) * MAX_LEN);

    while ((c = getc(fp)) != EOF) {
        if (c == '\n') break;  // End on newline
        cmdline[pos++] = c;
    }
    if (c == EOF && pos == 0) return NULL;  // Handle EOF
    cmdline[pos] = '\0';
    return cmdline;
}
