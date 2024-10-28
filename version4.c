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
char* read_cmd(char*, FILE*);
void parse_redirects_and_pipes(char **args, int *infile, int *outfile, int *is_pipe, char ****cmdlist);
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
        free(history[current]);
    }
    history[current] = strdup(cmd);
    current = (current + 1) % HIST_SIZE;
}

char* fetch_from_history(char *cmd) {
    int index;
    if (cmd[1] == '-') {
        index = (current + HIST_SIZE - 1) % HIST_SIZE;
    } else {
        index = atoi(cmd + 1) - 1;
        index = index % HIST_SIZE;
    }
    if (history[index] == NULL) {
        fprintf(stderr, "No command stored in this history slot.\n");
        return NULL;
    }
    return strdup(history[index]);
}

int main() {
    signal(SIGCHLD, handle_sigchld);  // Handle background processes
    memset(history, 0, sizeof(history));

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
        }
        free(cmdline);
    }
    printf("\n");
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

void parse_redirects_and_pipes(char **args, int *infile, int *outfile, int *is_pipe, char ***cmdlist[]) {
    int cmd_idx = 0;
    *cmdlist = malloc(MAXARGS * sizeof(char**));
    (*cmdlist)[cmd_idx] = malloc(MAXARGS * sizeof(char*));

    int arg_idx = 0;
    for (int i = 0; args[i] != NULL; i++) {
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
            (*cmdlist)[cmd_idx][arg_idx] = NULL;
            cmd_idx++;
            (*cmdlist)[cmd_idx] = malloc(MAXARGS * sizeof(char*));
            arg_idx = 0;
            *is_pipe = 1;
        } else {
            (*cmdlist)[cmd_idx][arg_idx++] = args[i];
        }
    }
    (*cmdlist)[cmd_idx][arg_idx] = NULL;
}

int execute(char *arglist[], int background) {
    int infile = 0, outfile = 1, is_pipe = 0;
    char ***cmdlist;
    parse_redirects_and_pipes(arglist, &infile, &outfile, &is_pipe, &cmdlist);

    if (is_pipe) {
        int pipefds[2], in = 0;

        for (int i = 0; cmdlist[i] != NULL; i++) {
            pipe(pipefds);
            if (fork() == 0) {
                if (in != 0) {
                    dup2(in, 0);
                    close(in);
                }
                if (cmdlist[i + 1] != NULL) {
                    dup2(pipefds[1], 1);
                } else if (outfile != 1) {
                    dup2(outfile, 1);
                }
                close(pipefds[0]);
                execvp(cmdlist[i][0], cmdlist[i]);
                perror("execvp");
                exit(1);
            } else {
                close(pipefds[1]);
                if (in != 0) close(in);
                in = pipefds[0];
            }
        }

        if (!background) {
            while (wait(NULL) > 0);
        }
    } else {
        pid_t pid = fork();
        if (pid == 0) {
            if (infile != 0) {
                dup2(infile, 0);
                close(infile);
            }
            if (outfile != 1) {
                dup2(outfile, 1);
                close(outfile);
            }
            execvp(arglist[0], arglist);
            perror("execvp");
            exit(1);
        } else if (pid > 0) {
            if (!background) waitpid(pid, NULL, 0);
        } else {
            perror("fork");
            return 1;
        }
    }

    for (int i = 0; cmdlist[i] != NULL; i++) {
        free(cmdlist[i]);
    }
    free(cmdlist);

    return 0;
}
