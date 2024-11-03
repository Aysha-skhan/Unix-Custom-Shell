#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_LEN 512          // Maximum length of the command line input
#define MAXARGS 10           // Maximum number of arguments for a command
#define ARGLEN 30            // Maximum length of each argument
#define HIST_SIZE 10         // Number of commands to retain in history

// ANSI color codes to customize shell prompt appearance
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_CYAN    "\033[36m"

// Function declarations
int execute(char *arglist[], int background);
int execute_pipeline(char ***cmds, int num_cmds);
char** tokenize(char* cmdline);
int parse_redirects(char **args, int *infile, int *outfile);
void handle_sigchld(int sig);
void add_to_history(char *cmd);
char* fetch_from_history(char *cmd);
void setup_signals();
void handle_builtins(char **arglist);

// Global variables for history and background job management
char *history[HIST_SIZE];       // Array to store command history
int current = 0;                // Current position in history
int history_count = 0;          // Number of commands in history
pid_t background_jobs[MAXARGS]; // Array to store background process IDs
int job_count = 0;              // Count of background jobs

// Signal handler to clean up completed background processes
void handle_sigchld(int sig) {
    int saved_errno = errno; // Save errno as it may be modified during handling
    pid_t pid;
    // Loop to reap all finished child processes
    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
        printf("\n[Background process %d completed]\n", pid);
        fflush(stdout);
        // Remove the completed job from the list of background jobs
        for (int i = 0; i < job_count; i++) {
            if (background_jobs[i] == pid) {
                for (int j = i; j < job_count - 1; j++) {
                    background_jobs[j] = background_jobs[j + 1];
                }
                job_count--;
                break;
            }
        }
    }
    errno = saved_errno; // Restore errno to its original state
}

// Adds a command to the history buffer, maintaining a circular buffer
void add_to_history(char *cmd) {
    if (history[current] != NULL) {
        free(history[current]); // Free memory if it already holds a command
    }
    history[current] = strdup(cmd); // Duplicate the command for storage
    current = (current + 1) % HIST_SIZE; // Move to the next slot (circular)
    if (history_count < HIST_SIZE) {
        history_count++;
    }
}

// Fetches a command from history, supporting !N and !-N notation
char* fetch_from_history(char *cmd) {
    int index; // Calculated index in the history buffer
    int n;

    if (cmd[1] == '-') {
        // Reverse lookup for !-N notation
        n = atoi(cmd + 2); // Convert string to integer
        if (n <= 0 || n > history_count) {
            fprintf(stderr, "No such command in history.\n");
            return NULL;
        }
        index = (current - n + HIST_SIZE) % HIST_SIZE; // Get the index
    } else {
        // Forward lookup for !N notation
        n = atoi(cmd + 1);
        if (n <= 0 || n > history_count) {
            fprintf(stderr, "No such command in history.\n");
            return NULL;
        }
        index = (current - history_count + n - 1 + HIST_SIZE) % HIST_SIZE;
    }

    if (history[index] == NULL) {
        fprintf(stderr, "No command at that position in history.\n");
        return NULL;
    }

    return strdup(history[index]); // Return a copy of the command
}

// Sets up signal handling for SIGCHLD to handle background processes
void setup_signals() {
    struct sigaction sa;
    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; // Restart system calls if interrupted by SIGCHLD
    sigaction(SIGCHLD, &sa, NULL);
}

// Executes a command, either in the foreground or background
int execute(char *arglist[], int background) {
    int infile = STDIN_FILENO, outfile = STDOUT_FILENO;
    // Handle any input/output redirection
    if (parse_redirects(arglist, &infile, &outfile) < 0) return 1;

    pid_t pid = fork();
    if (pid == 0) { // Child process
        // Set up input redirection if specified
        if (infile != STDIN_FILENO) {
            dup2(infile, STDIN_FILENO);
            close(infile);
        }
        // Set up output redirection if specified
        if (outfile != STDOUT_FILENO) {
            dup2(outfile, STDOUT_FILENO);
            close(outfile);
        }
        execvp(arglist[0], arglist); // Execute the command
        perror("execvp failed"); // Error if execvp returns
        exit(1);
    } else if (pid > 0) { // Parent process
        if (!background) {
            waitpid(pid, NULL, 0); // Wait for foreground process
        } else {
            background_jobs[job_count++] = pid; // Track background process
            printf("[Background PID %d]\n", pid);
        }
    } else {
        perror("fork failed");
        return 1;
    }
    return 0;
}

// Executes a pipeline of commands (e.g., cmd1 | cmd2 | cmd3)
int execute_pipeline(char ***cmds, int num_cmds) {
    int i, in_fd = STDIN_FILENO, fd[2];
    pid_t pid;

    // Loop through each command in the pipeline
    for (i = 0; i < num_cmds; i++) {
        if (pipe(fd) == -1) {
            perror("pipe failed");
            return 1;
        }

        if ((pid = fork()) == 0) { // Child process
            dup2(in_fd, STDIN_FILENO); // Input from previous command or stdin
            if (i < num_cmds - 1) {
                dup2(fd[1], STDOUT_FILENO); // Output to the pipe
            }
            close(fd[0]); // Close read end of the pipe

            // Execute the command
            if (cmds[i][0] != NULL) {
                execvp(cmds[i][0], cmds[i]);
                perror("execvp failed");
            }
            exit(1); // Exit on execvp failure
        } else if (pid < 0) { // Fork failure
            perror("fork failed");
            return 1;
        }

        close(fd[1]); // Close write end of the pipe
        in_fd = fd[0]; // Set input for the next command
    }

    // Wait for all commands in the pipeline to finish
    for (i = 0; i < num_cmds; i++) wait(NULL);
    return 0;
}

// Tokenizes a command line into arguments, returning an array of arguments
char** tokenize(char* cmdline) {
    char** arglist = malloc((MAXARGS + 1) * sizeof(char*));
    for (int j = 0; j < MAXARGS + 1; j++) {
        arglist[j] = malloc(ARGLEN);
        bzero(arglist[j], ARGLEN); // Initialize memory to zero
    }

    if (cmdline[0] == '\0') return NULL;

    int argnum = 0;
    char* cp = cmdline;
    char* start;
    int len;

    // Split the command line into tokens
    while (*cp != '\0') {
        while (*cp == ' ' || *cp == '\t') cp++; // Skip whitespace

        start = cp;
        len = 0;
        while (*cp != '\0' && !(*cp == ' ' || *cp == '\t')) {
            cp++;
            len++;
        }

        if (len >= ARGLEN) len = ARGLEN - 1;

        strncpy(arglist[argnum], start, len); // Copy token to arglist
        arglist[argnum++][len] = '\0'; // Null-terminate the token
    }
    arglist[argnum] = NULL; // Null-terminate the argument list
    return arglist;
}

// Parses input/output redirection symbols and sets file descriptors
int parse_redirects(char **args, int *infile, int *outfile) {
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "<") == 0) { // Input redirection
            *infile = open(args[i + 1], O_RDONLY);
            if (*infile < 0) {
                perror("Failed to open input file");
                return -1;
            }
            args[i] = NULL;
        } else if (strcmp(args[i], ">") == 0) { // Output redirection
            *outfile = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (*outfile < 0) {
                perror("Failed to open output file");
                return -1;
            }
            args[i] = NULL;
        }
    }
    return 0;
}

// Handles built-in commands like cd, jobs, kill, and exit
void handle_builtins(char **arglist) {
    if (strcmp(arglist[0], "cd") == 0) { // Change directory
        if (arglist[1] == NULL || chdir(arglist[1]) != 0) {
            perror("cd failed");
        }
    } else if (strcmp(arglist[0], "jobs") == 0) { // List background jobs
        for (int i = 0; i < job_count; i++) {
            if (kill(background_jobs[i], 0) == 0) { // Check if job is active
                printf("[%d] %d\n", i + 1, background_jobs[i]);
            } else { // Remove terminated job from list
                for (int j = i; j < job_count - 1; j++) {
                    background_jobs[j] = background_jobs[j + 1];
                }
                job_count--;
                i--;
            }
        }
    } else if (strcmp(arglist[0], "kill") == 0) { // Kill a background job
        if (arglist[1]) {
            int job_index = atoi(arglist[1]) - 1;
            if (job_index >= 0 && job_index < job_count) {
                kill(background_jobs[job_index], SIGKILL);
                printf("Killed job [%d] %d\n", job_index + 1, background_jobs[job_index]);
                for (int j = job_index; j < job_count - 1; j++) {
                    background_jobs[j] = background_jobs[j + 1];
                }
                job_count--;
            }
        } else {
            printf("Usage: kill [job#]\n");
        }
    } else if (strcmp(arglist[0], "help") == 0) { // Display help message
        printf("Available commands:\n");
        printf("  cd [directory]  - Change directory\n");
        printf("  jobs            - List background jobs\n");
        printf("  kill [job#]     - Kill a background job\n");
        printf("  exit            - Exit the shell\n");
        printf("  ![number]       - Execute a command from history\n");
    }
}

// Main function to initialize shell and handle command input
int main() {
    setup_signals(); // Set up signal handling for background processes
    memset(history, 0, sizeof(history)); // Initialize history buffer

    char *cmdline;
    char **arglist;
    char prompt[MAX_LEN];
    char hostname[HOST_NAME_MAX];
    char cwd[PATH_MAX];
    char *username = getenv("USER");
    if (username == NULL) username = "unknown";

    gethostname(hostname, HOST_NAME_MAX); // Get hostname for prompt

    // Main command loop
    while (1) {
        if (getcwd(cwd, sizeof(cwd)) != NULL) { // Get current directory
            snprintf(prompt, sizeof(prompt),
                     COLOR_RED "PucitShell " COLOR_RESET
                     "(" COLOR_GREEN "%s" COLOR_RESET
                     "@" COLOR_GREEN "%s" COLOR_RESET
                     ")-[" COLOR_CYAN "%s" COLOR_RESET "] : ",
                     username, hostname, cwd); // Set prompt
        } else {
            perror("getcwd() error");
            return 1;
        }

        cmdline = readline(prompt); // Read command input
        if (cmdline == NULL) break; // Exit if EOF

        if (strlen(cmdline) > 0) {
            // Handle history commands like !N and !-N
            if (cmdline[0] == '!') {
                char *newcmd = fetch_from_history(cmdline);
                if (newcmd) {
                    printf("Repeating command: %s\n", newcmd);
                    free(cmdline);
                    cmdline = newcmd; // Replace with actual command
                    add_history(cmdline);
                } else {
                    free(cmdline);
                    continue;
                }
            } else {
                add_history(cmdline); // Add to readline history
            }
            add_to_history(cmdline); // Add to custom history
        }

        // Parse the command for pipelines and execute
        char *pipe_cmds[MAXARGS];
        int num_cmds = 0;
        char *token = strtok(cmdline, "|");
        while (token && num_cmds < MAXARGS) {
            while (*token == ' ') token++;
            char *end = token + strlen(token) - 1;
            while (end > token && *end == ' ') end--;
            *(end + 1) = '\0';

            pipe_cmds[num_cmds++] = token;
            token = strtok(NULL, "|");
        }

        // Handle pipeline execution or a single command
        if (num_cmds > 1) {
            char **cmds[MAXARGS];
            for (int i = 0; i < num_cmds; i++) {
                cmds[i] = tokenize(pipe_cmds[i]);
            }
            execute_pipeline(cmds, num_cmds);
            for (int i = 0; i < num_cmds; i++) {
                for (int j = 0; cmds[i][j] != NULL; j++) free(cmds[i][j]);
                free(cmds[i]);
            }
        } else {
            arglist = tokenize(cmdline);
            int background = 0;
            for (int i = 0; arglist[i] != NULL; i++) {
                if (strcmp(arglist[i], "&") == 0) {
                    background = 1;
                    free(arglist[i]);
                    arglist[i] = NULL;
                    break;
                }
            }
            if (strcmp(arglist[0], "exit") == 0) {
                break;
            }
            if (strcmp(arglist[0], "cd") == 0 || strcmp(arglist[0], "jobs") == 0 ||
                strcmp(arglist[0], "kill") == 0 || strcmp(arglist[0], "help") == 0) {
                handle_builtins(arglist);
            } else {
                execute(arglist, background);
            }
            for (int j = 0; arglist[j] != NULL; j++) free(arglist[j]);
            free(arglist);
        }
        free(cmdline);
    }
    printf("\n");
    return 0;
}
