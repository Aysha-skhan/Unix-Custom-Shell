#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>

#define MAX_LEN 512
#define MAXARGS 10
#define ARGLEN 30

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
        if ((arglist = tokenize(cmdline)) != NULL) {
            execute(arglist);
            for (int j = 0; j < MAXARGS + 1; j++) free(arglist[j]);
            free(arglist);
            free(cmdline);
        }
    }
    printf("\n");
    return 0;
}

int execute(char *arglist[]) {
    int infile = 0, outfile = 1, is_pipe = 0;
    char **cmdlist[MAXARGS];
    parse_redirects_and_pipes(arglist, &infile, &outfile, &is_pipe, cmdlist);

    // Check if infile and outfile are the same
    if (infile == outfile) {
        fprintf(stderr, "Input file cannot be the same as output file\n");
        return -1; // Early return on error
    }

    if (is_pipe) {
        int fd[2];
        int status;
        pid_t pid;

        for (int i = 0; cmdlist[i] != NULL; i++) {
            pipe(fd);
            if ((pid = fork()) == 0) {
                if (i > 0) dup2(infile, 0); // Read from the previous pipe
                if (cmdlist[i + 1] != NULL) dup2(fd[1], 1); // Write to the current pipe
                close(fd[0]);
                execvp(cmdlist[i][0], cmdlist[i]);
                perror("Command execution failed");
                exit(1);
            }
            close(fd[1]);
            infile = fd[0]; // For the next command
            wait(&status);
        }
    } else {
        pid_t pid = fork();
        if (pid == 0) {
            if (infile != 0) dup2(infile, 0); // Redirect input
            if (outfile != 1) dup2(outfile, 1); // Redirect output
            execvp(arglist[0], arglist);
            perror("Command execution failed");
            exit(1);
        }
        wait(NULL);
    }

    if (infile != 0) close(infile);
    if (outfile != 1) close(outfile);
    return 0;
}

void parse_redirects_and_pipes(char **args, int *infile, int *outfile, int *is_pipe, char ***cmdlist) {
    int i, cmd_idx = 0, arg_idx = 0;
    cmdlist[cmd_idx] = malloc(MAXARGS * sizeof(char*));

    for (i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "<") == 0) {
            // Open input file and update infile descriptor
            *infile = open(args[++i], O_RDONLY);
            if (*infile < 0) {
                perror("Cannot open input file");
                return;
            }
        } else if (strcmp(args[i], ">") == 0) {
            // Open output file and update outfile descriptor
            *outfile = open(args[++i], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (*outfile < 0) {
                perror("Cannot open output file");
                return;
            }
        } else if (strcmp(args[i], "|") == 0) {
            // Handle pipes by moving to the next command in cmdlist
            cmdlist[cmd_idx][arg_idx] = NULL; // Null-terminate the current command
            cmd_idx++;
            cmdlist[cmd_idx] = malloc(MAXARGS * sizeof(char*));
            arg_idx = 0;
            *is_pipe = 1;
        } else {
            // Add argument to the current command
            cmdlist[cmd_idx][arg_idx++] = args[i];
        }
    }
    // Null-terminate the last command
    cmdlist[cmd_idx][arg_idx] = NULL;
    cmdlist[cmd_idx + 1] = NULL; // Ensure the last command is null-terminated
}

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
        while (*cp == ' ' || *cp == '\t') cp++;
        
        start = cp;
        len = 0;
        while (*cp != '\0' && !(*cp == ' ' || *cp == '\t')) {
            cp++;
            len++;
        }

        // Ensure we don't exceed the argument length
        if (len >= ARGLEN) len = ARGLEN - 1;

        strncpy(arglist[argnum], start, len);
        arglist[argnum][len] = '\0';
        argnum++;
    }
    arglist[argnum] = NULL;
    return arglist;
}

char* read_cmd(char* prompt, FILE* fp) {
    printf("%s", prompt);
    int c;
    int pos = 0;
    char* cmdline = malloc(sizeof(char) * MAX_LEN);

    while ((c = getc(fp)) != EOF) {
        if (c == '\n') break;
        cmdline[pos++] = c;
    }
    if (c == EOF && pos == 0) return NULL;
    cmdline[pos] = '\0';
    return cmdline;
}

