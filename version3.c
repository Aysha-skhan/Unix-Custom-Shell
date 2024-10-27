#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>

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
int execute(char *arglist[], int background);
char** tokenize(char* cmdline);
char* read_cmd(char*, FILE*);
void parse_redirects_and_pipes(char **args, int *infile, int *outfile, int *is_pipe, char ***cmdlist);
void handle_sigchld(int sig);  // Signal handler to avoid zombie processes

int main() {
    // Setup signal handler to handle SIGCHLD to avoid zombie processes
    signal(SIGCHLD, handle_sigchld);

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
            // Check if the command should run in the background
            int background = 0;
            int last_arg = 0;
            while (arglist[last_arg] != NULL) last_arg++;
            if (last_arg > 0 && strcmp(arglist[last_arg - 1], "&") == 0) {
                background = 1;
                free(arglist[--last_arg]);  // Remove '&' from the argument list
                arglist[last_arg] = NULL;
            }

            execute(arglist, background);  // Execute the command

            // Free allocated memory for arguments
            for (int j = 0; j < MAXARGS + 1; j++) free(arglist[j]);
            free(arglist);
            free(cmdline);
        }
    }
    printf("\n");
    return 0;
}

// Signal handler to reap zombie processes
void handle_sigchld(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

// Execute commands with optional background execution
int execute(char *arglist[], int background) {
    pid_t pid = fork();

    if (pid == 0) {  // Child process
        execvp(arglist[0], arglist);
        perror("Command execution failed");
        exit(1);
    } else if (pid > 0) {  // Parent process
        if (!background) {
            waitpid(pid, NULL, 0);  // Wait for foreground process to finish
        } else {
            printf("[PID %d] running in background\n", pid);
        }
    } else {
        perror("fork failed");
    }
    return 0;
}

// Tokenize command line into arguments
char** tokenize(char* cmdline) {
    char** arglist = malloc(sizeof(char*) * (MAXARGS + 1));
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

        if (len >= ARGLEN) len = ARGLEN - 1;

        strncpy(arglist[argnum], start, len);
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
