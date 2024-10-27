#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>

#define MAX_LEN 512
#define MAXARGS 10
#define ARGLEN 30

// ANSI color codes for prompt
#define COLOR_RESET "\033[0m"
#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_BLUE "\033[34m"
#define COLOR_CYAN "\033[36m"

// Function declarations
int execute(char ***cmdlist, int infile, int outfile, int background);
char **tokenize(char *cmdline);
char *read_cmd(char *, FILE *);
void parse_command(char *arglist[], char ***cmdlist, int *background, int *infile, int *outfile);
void handle_sigchld(int sig);

int main() {
    signal(SIGCHLD, handle_sigchld);  // Handle SIGCHLD to prevent zombies

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

        arglist = tokenize(cmdline);
        if (arglist != NULL) {
            int background = 0;
            int infile = 0, outfile = 1;
            char ***cmdlist = malloc((MAXARGS + 1) * sizeof(char **));
            parse_command(arglist, cmdlist, &background, &infile, &outfile);
            execute(cmdlist, infile, outfile, background);

            // Close redirection files
            if (infile != 0) close(infile);
            if (outfile != 1) close(outfile);

            // Free allocated memory
            for (int i = 0; cmdlist[i] != NULL; i++) {
                for (int j = 0; cmdlist[i][j] != NULL; j++) free(cmdlist[i][j]);
                free(cmdlist[i]);
            }
            free(cmdlist);
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

// Parse command with redirection and pipes
void parse_command(char *arglist[], char ***cmdlist, int *background, int *infile, int *outfile) {
    int cmd_idx = 0, arg_idx = 0;
    cmdlist[cmd_idx] = malloc((MAXARGS + 1) * sizeof(char *));

    for (int i = 0; arglist[i] != NULL; i++) {
        if (strcmp(arglist[i], "|") == 0) {
            cmdlist[cmd_idx][arg_idx] = NULL;
            cmd_idx++;
            cmdlist[cmd_idx] = malloc((MAXARGS + 1) * sizeof(char *));
            arg_idx = 0;
        } else if (strcmp(arglist[i], "<") == 0) {
            *infile = open(arglist[++i], O_RDONLY);
            if (*infile < 0) {
                perror("Cannot open input file");
                exit(1);
            }
        } else if (strcmp(arglist[i], ">") == 0) {
            *outfile = open(arglist[++i], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (*outfile < 0) {
                perror("Cannot open output file");
                exit(1);
            }
        } else if (strcmp(arglist[i], "&") == 0) {
            *background = 1;
        } else {
            cmdlist[cmd_idx][arg_idx++] = strdup(arglist[i]);
        }
    }
    cmdlist[cmd_idx][arg_idx] = NULL;
    cmdlist[cmd_idx + 1] = NULL;
}

// Execute commands with pipes and redirection
int execute(char ***cmdlist, int infile, int outfile, int background) {
    int fd[2], in = infile, status;
    pid_t pid;

    for (int i = 0; cmdlist[i] != NULL; i++) {
        pipe(fd);

        if ((pid = fork()) == 0) {
            if (in != 0) {
                dup2(in, STDIN_FILENO);
                close(in);
            }
            if (cmdlist[i + 1] != NULL) {
                dup2(fd[1], STDOUT_FILENO);
                close(fd[1]);
            } else if (outfile != 1) {
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

    if (!background) {
        while (wait(&status) > 0);
    } else {
        printf("[Background] Process started with PID %d\n", pid);
    }
    return 0;
}

// Tokenize the command line into arguments
char **tokenize(char *cmdline) {
    char **arglist = malloc((MAXARGS + 1) * sizeof(char *));
    int argnum = 0;
    char *token = strtok(cmdline, " \t\n");

    while (token != NULL && argnum < MAXARGS) {
        arglist[argnum++] = strdup(token);
        token = strtok(NULL, " \t\n");
    }
    arglist[argnum] = NULL;
    return arglist;
}

// Read command from input and print prompt
char *read_cmd(char *prompt, FILE *fp) {
    printf("%s", prompt);
    char *cmdline = malloc(MAX_LEN);
    if (fgets(cmdline, MAX_LEN, fp) == NULL) return NULL;
    return cmdline;
}
