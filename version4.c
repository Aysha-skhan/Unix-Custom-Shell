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
int execute_pipeline(char ***cmds, int num_cmds);
char** tokenize(char* cmdline);
char* read_cmd(char*, FILE*);
int parse_redirects(char **args, int *infile, int *outfile);
void handle_sigchld(int sig);
void add_to_history(char *cmd);
char* fetch_from_history(char *cmd);

// Global variables for history management
char *history[HIST_SIZE];
int current = 0;
int history_count = 0;

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
    if (history_count < HIST_SIZE) {
        history_count++;
    }
}

char* fetch_from_history(char *cmd) {
    int index;

    if (strcmp(cmd, "!-1") == 0) {
        // Retrieve the last command in history
        index = (current - 1 + HIST_SIZE) % HIST_SIZE;
        if (history_count == 0 || history[index] == NULL) {
            fprintf(stderr, "No history available.\n");
            return NULL;
        }
    } else {
        index = atoi(cmd + 1) - 1;  // Extract history index
        if (index < 0 || index >= history_count) {
            fprintf(stderr, "Invalid history reference.\n");
            return NULL;
        }
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

        printf("%s", prompt);
        cmdline = read_cmd(NULL, stdin);  // Standard input reading
        if (cmdline == NULL) break;

        if (strlen(cmdline) > 0) {  // Only add non-empty commands to history
            add_to_history(cmdline);
        }

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

        // Check if there's a pipeline
        char *pipe_cmds[MAXARGS];
        int num_cmds = 0;
        char *token = strtok(cmdline, "|");
        while (token && num_cmds < MAXARGS) {
            // Trim spaces around each command in the pipeline
            while (*token == ' ') token++;
            char *end = token + strlen(token) - 1;
            while (end > token && *end == ' ') end--;
            *(end + 1) = '\0';

            pipe_cmds[num_cmds++] = token;
            token = strtok(NULL, "|");
        }

        if (num_cmds > 1) {
            // Handle pipeline commands
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
            // Handle single command
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
            execute(arglist, background);
            for (int j = 0; arglist[j] != NULL; j++) free(arglist[j]);
            free(arglist);
        }
        free(cmdline);
    }
    printf("\n");
    return 0;
}

char* read_cmd(char* prompt, FILE* fp) {
    if (prompt) printf("%s", prompt);
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

int parse_redirects(char **args, int *infile, int *outfile) {
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "<") == 0) {
            *infile = open(args[i + 1], O_RDONLY);
            if (*infile < 0) {
                perror("Failed to open input file");
                return -1;
            }
            args[i] = NULL;
        } else if (strcmp(args[i], ">") == 0) {
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

int execute(char *arglist[], int background) {
    int infile = STDIN_FILENO, outfile = STDOUT_FILENO;
    if (parse_redirects(arglist, &infile, &outfile) < 0) return 1;

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
        execvp(arglist[0], arglist);
        perror("execvp failed");  // Only print error if execvp fails
        exit(1);
    } else if (pid > 0) {  // Parent process
        if (!background) {
            waitpid(pid, NULL, 0);  // Wait only if not background
        } else {
            printf("[Background PID %d]\n", pid);  // Indicate background process
        }
    } else {
        perror("fork failed");
        return 1;
    }
    return 0;
}

int execute_pipeline(char ***cmds, int num_cmds) {
    int i, in_fd = STDIN_FILENO, fd[2];
    pid_t pid;

    for (i = 0; i < num_cmds; i++) {
        if (pipe(fd) == -1) {
            perror("pipe failed");
            return 1;
        }

        if ((pid = fork()) == 0) {  // Child process
            dup2(in_fd, STDIN_FILENO);
            if (i < num_cmds - 1) {
                dup2(fd[1], STDOUT_FILENO);
            }
            close(fd[0]);

            if (cmds[i][0] != NULL) {  // Ensure command exists
                execvp(cmds[i][0], cmds[i]);
                perror("execvp failed");  // Only print error if execvp fails
            }
            exit(1);
        } else if (pid < 0) {
            perror("fork failed");
            return 1;
        }

        close(fd[1]);
        in_fd = fd[0];
    }

    for (i = 0; i < num_cmds; i++) wait(NULL);
    return 0;
}
