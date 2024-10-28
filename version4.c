#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <errno.h>
#include <signal.h>

#define MAX_LEN 512
#define MAXARGS 10
#define ARGLEN 30
#define HIST_SIZE 10  // Maximum size of the history

// ANSI color codes for prompt
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_CYAN    "\033[36m"

// Function declarations
int execute(char *arglist[], int background);
char** tokenize(char* cmdline);
char* read_cmd(char*, FILE*, char**);
void parse_redirects_and_pipes(char **args, int *infile, int *outfile, int *is_pipe, char ***cmdlist);
void handle_sigchld(int sig);
void add_to_history(char *cmd);
char* fetch_from_history(char *cmd);

// Global variables for history management
char *history[HIST_SIZE];
int current = 0;

void handle_sigchld(int sig) {
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}

void add_to_history(char *cmd) {
    if (history[current] != NULL) {
        free(history[current]);  // Free the slot if it's filled
    }
    history[current] = strdup(cmd);  // Copy command to history
    current = (current + 1) % HIST_SIZE;  // Update current index
}

char* fetch_from_history(char *cmd) {
    int index;
    if (cmd[1] == '-') {
        // Handle negative indices for recent commands
        index = (current + HIST_SIZE - 1) % HIST_SIZE;
    } else {
        index = atoi(cmd + 1) - 1;  // Convert number and adjust for 0-based index
        index = index % HIST_SIZE;  // Ensure the index wraps around
    }
    return history[index];
}

int main() {
    signal(SIGCHLD, handle_sigchld);
    memset(history, 0, sizeof(history));  // Initialize history array

    char *cmdline;
    char **arglist;
    char prompt[MAX_LEN];
    char hostname[HOST_NAME_MAX];
    char cwd[PATH_MAX];
    char* username = getenv("USER");
    if (username == NULL) username = "unknown";

    gethostname(hostname, HOST_NAME_MAX);

    while (1) {
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

        cmdline = read_cmd(prompt, stdin, history);  // Modified to pass history
        if (cmdline == NULL) break;

        if (cmdline[0] == '!') {
            cmdline = fetch_from_history(cmdline);
            printf("Repeating command: %s\n", cmdline);
        }

        add_to_history(cmdline);

        if ((arglist = tokenize(cmdline)) != NULL) {
            int background = 0;
            for (int i = 0; arglist[i] != NULL; i++) {
                if (strcmp(arglist[i], "&") == 0) {
                    background = 1;
                    free(arglist[i]);
                    arglist[i] = NULL;
                    break;
                }
            }
            execute(arglist, background);
            for (int j = 0; j < MAXARGS + 1; j++) free(arglist[j]);
            free(arglist);
            free(cmdline);
        }
    }
    printf("\n");
    return 0;
}

char* read_cmd(char* prompt, FILE* fp, char** history) {
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

int execute(char *arglist[], int background) {
    int infile = 0, outfile = 1, is_pipe = 0;
    char **cmdlist[MAXARGS];
    parse_redirects_and_pipes(arglist, &infile, &outfile, &is_pipe, cmdlist);

    if (is_pipe) {  // Handle command piping
        int fd[2];
        int in = infile;
        pid_t pid;

        for (int i = 0; cmdlist[i] != NULL; i++) {
            pipe(fd);
            if ((pid = fork()) == 0) {  // Child process
                if (in != STDIN_FILENO) {
                    dup2(in, STDIN_FILENO);
                    close(in);
                }
                if (cmdlist[i + 1] != NULL) {
                    dup2(fd[1], STDOUT_FILENO);
                } else if (outfile != STDOUT_FILENO) {
                    dup2(outfile, STDOUT_FILENO);
                    close(outfile);
                }
                close(fd[0]);
                execvp(cmdlist[i][0], cmdlist[i]);
                perror("Command execution failed");
                exit(1);
            }
            close(fd[1]);
            in = fd[0];
        }
        while (wait(NULL) > 0);  // Wait for all child processes
    } else {  // Handle simple commands with redirection
        pid_t pid = fork();
        if (pid == 0) {  // Child process
            if (infile != STDIN_FILENO) {
                dup2(infile, STDIN_FILENO);
                close(infile);
            }
            if (outfile != STDOUT_FILENO) {
                dup2(outfile, STDOUT_FILENO);
                close(outfile);
            }
            execvp(cmdlist[0][0], cmdlist[0]);
            perror("Command execution failed");
            exit(1);
        } else if (pid > 0) {  // Parent process
            if (background) {
                printf("[Background process started with PID %d]\n", pid);
            } else {
                waitpid(pid, NULL, 0);  // Wait for child process to finish
            }
        } else {
            perror("Fork failed");
            return 1;
        }
    }

    if (infile != STDIN_FILENO) close(infile);
    if (outfile != STDOUT_FILENO) close(outfile);
    return 0;
}

void parse_redirects_and_pipes(char **args, int *infile, int *outfile, int *is_pipe, char ***cmdlist) {
    int i, cmd_idx = 0, arg_idx = 0;
    cmdlist[cmd_idx] = malloc(MAXARGS * sizeof(char*));

    for (i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "<") == 0) {
            *infile = open(args[++i], O_RDONLY);
            if (*infile < 0) {
                perror("Cannot open input file");
                exit(1);
            }
        } else if (strcmp(args[i], ">") == 0) {
            *outfile = open(args[++i], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (*outfile < 0) {
                perror("Cannot open output file");
                exit(1);
            }
        } else if (strcmp(args[i], "|") == 0) {
            cmdlist[cmd_idx][arg_idx] = NULL;
            cmd_idx++;
            cmdlist[cmd_idx] = malloc(MAXARGS * sizeof(char*));
            arg_idx = 0;
            *is_pipe = 1;
        } else {
            cmdlist[cmd_idx][arg_idx++] = args[i];
        }
    }
    cmdlist[cmd_idx][arg_idx] = NULL;
    cmdlist[cmd_idx + 1] = NULL;
}
