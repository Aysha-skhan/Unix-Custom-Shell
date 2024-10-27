#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <pwd.h>

#define MAX_LEN 512
#define MAXARGS 10

// ANSI color codes for prompt
#define COLOR_RESET "\033[0m"
#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_CYAN "\033[36m"

// Function declarations
void handle_sigchld(int sig);
char **parse_input(char *input);
void execute_pipes(char ***cmdlist, int infile, int outfile, int background);
char ***parse_commands(char *input, int *background);
void display_prompt();

// Signal handler to reap zombie processes
void handle_sigchld(int sig) {
    int saved_errno = errno;
    pid_t pid;
    int status;

    // Reap all zombie processes
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("[Process %d exited with status %d]\n", pid, WEXITSTATUS(status));
        fflush(stdout);  // Ensure prompt is displayed immediately
    }
    errno = saved_errno;
}

// Tokenize the command into arguments
char **parse_input(char *input) {
    char **command = malloc(MAXARGS * sizeof(char *));
    char *token;
    int index = 0;

    token = strtok(input, " \t\n");
    while (token != NULL && index < MAXARGS - 1) {
        command[index++] = strdup(token);
        token = strtok(NULL, " \t\n");
    }
    command[index] = NULL;
    return command;
}

// Parse commands with pipes and detect background processes
char ***parse_commands(char *input, int *background) {
    char ***cmdlist = malloc(MAXARGS * sizeof(char **));
    char *cmd;
    int cmd_idx = 0;

    cmd = strtok(input, "|");
    while (cmd != NULL) {
        char **args = parse_input(cmd);
        cmdlist[cmd_idx++] = args;
        cmd = strtok(NULL, "|");
    }
    cmdlist[cmd_idx] = NULL;

    // Check if the last command is a background process (`&`)
    for (int i = 0; cmdlist[cmd_idx - 1][i] != NULL; i++) {
        if (strcmp(cmdlist[cmd_idx - 1][i], "&") == 0) {
            *background = 1;
            free(cmdlist[cmd_idx - 1][i]);  // Remove '&' from command
            cmdlist[cmd_idx - 1][i] = NULL;
        }
    }

    return cmdlist;
}

// Execute commands with pipes, redirection, and background process handling
void execute_pipes(char ***cmdlist, int infile, int outfile, int background) {
    int fd[2], in = infile, i = 0;
    pid_t pid;

    while (cmdlist[i] != NULL) {
        pipe(fd);  // Create a pipe

        if ((pid = fork()) == 0) {  // Child process
            if (in != 0) {
                dup2(in, STDIN_FILENO);  // Redirect input
                close(in);
            }
            if (cmdlist[i + 1] != NULL) {
                dup2(fd[1], STDOUT_FILENO);  // Redirect output to the pipe
            } else if (outfile != 1) {
                dup2(outfile, STDOUT_FILENO);  // Redirect output to the file
                close(outfile);
            }
            close(fd[0]);  // Close the unused end of the pipe
            execvp(cmdlist[i][0], cmdlist[i]);  // Execute the command
            perror("execvp failed");  // If execvp fails
            exit(1);
        } else if (pid > 0) {  // Parent process
            close(fd[1]);  // Close write end of the pipe
            in = fd[0];  // Save read end for the next command

            if (!background) {
                waitpid(pid, NULL, 0);  // Wait for the child process if not in background
            }
        } else {
            perror("fork failed");
            exit(1);
        }
        i++;
    }
}

// Display the custom prompt
void display_prompt() {
    char hostname[HOST_NAME_MAX];
    char cwd[PATH_MAX];
    char *username = getenv("USER");
    if (username == NULL) username = "unknown";

    gethostname(hostname, sizeof(hostname));  // Get the hostname
    if (getcwd(cwd, sizeof(cwd)) != NULL) {  // Get the current directory
        printf(COLOR_RED "PUCITshell " COLOR_RESET
               "(" COLOR_GREEN "%s" COLOR_RESET
               "@" COLOR_GREEN "%s" COLOR_RESET
               ")-[" COLOR_CYAN "%s" COLOR_RESET "] : ", 
               username, hostname, cwd);
    } else {
        perror("getcwd error");
    }
    fflush(stdout);  // Ensure the prompt is displayed immediately
}

int main() {
    struct sigaction sa;
    sa.sa_handler = &handle_sigchld;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);  // Set up SIGCHLD handler

    char input[MAX_LEN];
    char ***cmdlist;
    int background;

    while (1) {
        display_prompt();  // Show the custom prompt

        if (!fgets(input, MAX_LEN, stdin)) break;  // Read input
        input[strlen(input) - 1] = '\0';  // Remove newline character

        background = 0;  // Reset background flag
        cmdlist = parse_commands(input, &background);  // Parse commands
        execute_pipes(cmdlist, 0, 1, background);  // Execute commands

        // Free allocated memory
        for (int i = 0; cmdlist[i] != NULL; i++) {
            for (int j = 0; cmdlist[i][j] != NULL; j++) {
                free(cmdlist[i][j]);
            }
            free(cmdlist[i]);
        }
        free(cmdlist);
    }
    printf("\n");
    return 0;
}
