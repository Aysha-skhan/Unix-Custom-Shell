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

#define MAX_LEN 512
#define MAXARGS 10
#define ARGLEN 30
#define HIST_SIZE 10  // Maximum history size

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_CYAN    "\033[36m"

// Function declarations
int execute(char *arglist[], int background);
char** tokenize(char* cmdline);
char* read_cmd(char*, FILE*);
int parse_redirects(char **args, int *infile, int *outfile);
void handle_sigchld(int sig);
void add_to_history(char *cmd);
char* fetch_from_history(char *cmd);
int execute_pipeline(char *commands[][MAXARGS + 1], int n);

// Global variables for history management
char *history[HIST_SIZE];
int current = 0;

void handle_sigchld(int sig) {
    // Reap child processes to prevent zombie processes
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

void add_to_history(char *cmd) {
    if (history[current] != NULL) {
        free(history[current]);
    }
    history[current] = strdup(cmd);
    current = (current + 1) % HIST_SIZE;
}

char* fetch_from_history(char *cmd) {
    int index;

    if (strcmp(cmd, "!-1") == 0) {
        index = (current + HIST_SIZE - 1) % HIST_SIZE;  // Get last command
    } else {
        index = atoi(cmd + 1) - 1;  // Extract history index
        if (index < 0 || index >= HIST_SIZE) {
            fprintf(stderr, "Invalid history reference.\n");
            return NULL;
        }
    }

    if (history[index] == NULL) {
        fprintf(stderr, "No command stored in this history slot.\n");
        return NULL;
    }
    return strdup(history[index]);
}

int main() {
    struct sigaction sa;
    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);  // Handle background processes

    memset(history, 0, sizeof(history));  // Initialize history

    char *cmdline;
    char **arglist;
    char prompt[MAX_LEN];
    char hostname[HOST_NAME_MAX];
    char cwd[PATH_MAX];
    char *username = getenv("USER");
    if (username == NULL) username = "unknown";

    gethostname(hostname, HOST_NAME_MAX);

    while (1) {
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            snprintf(prompt, sizeof(prompt),
                     COLOR_RED "MyShell " COLOR_RESET
                     "(" COLOR_GREEN "%s" COLOR_RESET
                     "@" COLOR_GREEN "%s" COLOR_RESET
                     ")-[" COLOR_CYAN "%s" COLOR_RESET "] : ",
                     username, hostname, cwd);
        } else {
            perror("getcwd() error");
            return 1;
        }

        cmdline = read_cmd(prompt, stdin);
        if (cmdline == NULL) break;

        if (cmdline[0] == '!') {
            char *newcmd = fetch_from_history(cmdline);
            free(cmdline);
            if (newcmd) {
                cmdline = newcmd;
                printf("Repeating command: %s\n", cmdline);
            } else {
                continue;
            }
        }

        add_to_history(cmdline);

        if ((arglist = tokenize(cmdline)) != NULL) {
            int background = 0;
            int pipe_count = 0;
            char *commands[MAXARGS + 1][MAXARGS + 1] = {NULL};  // To store pipeline segments

            // Split the input into segments for pipeline execution
            int segment = 0, arg = 0;
            for (int i = 0; arglist[i] != NULL; i++) {
                if (strcmp(arglist[i], "|") == 0) {
                    commands[segment][arg] = NULL;  // End current segment
                    segment++;
                    arg = 0;
                    pipe_count++;
                } else {
                    commands[segment][arg++] = arglist[i];
                }
            }
            commands[segment][arg] = NULL;  // End last segment

            if (pipe_count > 0) {
                execute_pipeline(commands, segment + 1);
            } else {
                // Handle background processes
                for (int i = 0; arglist[i] != NULL; i++) {
                    if (strcmp(arglist[i], "&") == 0) {
                        background = 1;
                        free(arglist[i]);
                        arglist[i] = NULL;
                        break;
                    }
                }
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

int execute_pipeline(char *commands[][MAXARGS + 1], int n) {
    int pipefd[2], in_fd = STDIN_FILENO;
    pid_t pid;

    for (int i = 0; i < n; i++) {
        if (i < n - 1) {
            pipe(pipefd);  // Create a pipe for all but the last command
        }

        pid = fork();
        if (pid == 0) {  // Child process
            dup2(in_fd, STDIN_FILENO);  // Redirect input from the previous pipe
            if (i < n - 1) {
                dup2(pipefd[1], STDOUT_FILENO);  // Redirect output to the next pipe
            }

            close(pipefd[0]);
            execvp(commands[i][0], commands[i]);
            perror("execvp failed");
            exit(1);
        } else if (pid > 0) {  // Parent process
            waitpid(pid, NULL, 0);
            close(pipefd[1]);
            in_fd = pipefd[0];  // Set input for next command to the read end of the pipe
        } else {
            perror("fork failed");
            return -1;
        }
    }
    return 0;
}

char* read_cmd(char* prompt, FILE* fp) {
    printf("%s", prompt);
    int c, pos = 0;
    char* cmdline = malloc(MAX_LEN);

    while ((c = getc(fp)) != EOF) {
        if (c == '\n') break;
        cmdline[pos++] = c;
    }
    if (c == EOF && pos == 0) return NULL;
    cmdline[pos] = '\0';
    return cmdline;
}

char** tokenize(char* cmdline) {
    char** arglist = malloc((MAXARGS + 1) * sizeof(char*));
    for (int j = 0; j < MAXARGS + 1; j++) {
        arglist[j] = malloc(ARGLEN);
        bzero(arglist[j], ARGLEN);
    }

    if (cmdline[0] == '\0') return NULL;

    int argnum = 0;
    char* cp = cmdline;
    char* start;
    int len;

    while (*cp != '\0') {
        while (*cp == ' ' || *cp == '\t') cp++;

        start = cp;
        len = 0;
        while (*cp != '\0' && !(*cp == ' ' || *cp == '\t')) {
            cp++;
            len++;
        }

        if (len >= ARGLEN) len = ARGLEN - 1;

        strncpy(arglist[argnum], start, len);
        arglist[argnum++][len] = '\0';
    }
    arglist[argnum] = NULL;
    return arglist;
}
