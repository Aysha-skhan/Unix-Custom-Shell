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

#define MAX_LEN 512
#define MAXARGS 10
#define ARGLEN 30
#define HIST_SIZE 10

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_CYAN    "\033[36m"

// Function prototypes
int execute(char *arglist[], int background);
int execute_pipeline(char ***cmds, int num_cmds);
char** tokenize(char* cmdline);
int parse_redirects(char **args, int *infile, int *outfile);
void handle_sigchld(int sig);
void add_to_history(char *cmd);
char* fetch_from_history(char *cmd);
void setup_signals();
void handle_builtins(char **arglist);

// Global variables for history and job management
char *history[HIST_SIZE];
int current = 0;
int history_count = 0;
pid_t background_jobs[MAXARGS];
int job_count = 0;

// Signal handler for reaping background processes
void handle_sigchld(int sig) {
    int saved_errno = errno;
    pid_t pid;
    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
        printf("\n[Background process %d completed]\n", pid);
        fflush(stdout);
        // Remove the completed job from the list
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
    errno = saved_errno;
}

// Function to add a command to history with history count tracking
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

// Function to fetch a command from history, supporting !N and !-N
char* fetch_from_history(char *cmd) {
    int index;
    int n;

    if (cmd[1] == '-') {
        n = atoi(cmd + 2); // Get the number after !-
        if (n <= 0 || n > history_count) {
            fprintf(stderr, "No such command in history.\n");
            return NULL;
        }
        // Calculate the index in reverse order
        index = (current - n + HIST_SIZE) % HIST_SIZE;
    } else {
        n = atoi(cmd + 1); // Get the number after !
        if (n <= 0 || n > history_count) {
            fprintf(stderr, "No such command in history.\n");
            return NULL;
        }
        // Calculate the index based on the absolute position
        index = (current - history_count + n - 1 + HIST_SIZE) % HIST_SIZE;
    }

    if (history[index] == NULL) {
        fprintf(stderr, "No command at that position in history.\n");
        return NULL;
    }

    return strdup(history[index]);
}

// Setup signal handling for SIGCHLD
void setup_signals() {
    struct sigaction sa;
    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);
}

// Execute function with background process handling
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
        perror("execvp failed");
        exit(1);
    } else if (pid > 0) {  // Parent process
        if (!background) {
            waitpid(pid, NULL, 0);
        } else {
            background_jobs[job_count++] = pid;
            printf("[Background PID %d]\n", pid);
        }
    } else {
        perror("fork failed");
        return 1;
    }
    return 0;
}

// Execute a pipeline of commands
int execute_pipeline(char ***cmds, int num_cmds) {
    int i, in_fd = STDIN_FILENO, fd[2];
    pid_t pid;

    for (i = 0; i < num_cmds; i++) {
        if (pipe(fd) == -1) {
            perror("pipe failed");
            return 1;
        }

        if ((pid = fork()) == 0) {
            dup2(in_fd, STDIN_FILENO);
            if (i < num_cmds - 1) {
                dup2(fd[1], STDOUT_FILENO);
            }
            close(fd[0]);

            if (cmds[i][0] != NULL) {
                execvp(cmds[i][0], cmds[i]);
                perror("execvp failed");
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

// Tokenize function to split command into arguments
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

// Parse redirects for input/output redirection
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

// Handle built-in commands like cd, exit, jobs, kill
void handle_builtins(char **arglist) {
    if (strcmp(arglist[0], "cd") == 0) {
        if (arglist[1] == NULL || chdir(arglist[1]) != 0) {
            perror("cd failed");
        }
    } else if (strcmp(arglist[0], "jobs") == 0) {
        for (int i = 0; i < job_count; i++) {
            if (kill(background_jobs[i], 0) == 0) {
                printf("[%d] %d\n", i + 1, background_jobs[i]);
            } else {  // Remove terminated job
                for (int j = i; j < job_count - 1; j++) {
                    background_jobs[j] = background_jobs[j + 1];
                }
                job_count--;
                i--;
            }
        }
    } else if (strcmp(arglist[0], "kill") == 0) {
        if (arglist[1]) {
            int job_index = atoi(arglist[1]) - 1;
            if (job_index >= 0 && job_index < job_count) {
                kill(background_jobs[job_index], SIGKILL);
                printf("Killed job [%d] %d\n", job_index + 1, background_jobs[job_index]);
                // Remove killed job from list
                for (int j = job_index; j < job_count - 1; j++) {
                    background_jobs[j] = background_jobs[j + 1];
                }
                job_count--;
            }
        } else {
            printf("Usage: kill [job#]\n");
        }
    } else if (strcmp(arglist[0], "help") == 0) {
        printf("Available commands:\n");
        printf("  cd [directory]  - Change directory\n");
        printf("  jobs            - List background jobs\n");
        printf("  kill [job#]     - Kill a background job\n");
        printf("  exit            - Exit the shell\n");
        printf("  ![number]       - Execute a command from history\n");
    }
}

int main() {
    setup_signals();
    memset(history, 0, sizeof(history));

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

        cmdline = readline(prompt);
        if (cmdline == NULL) break;

        if (strlen(cmdline) > 0) {
            add_history(cmdline);
            add_to_history(cmdline);
        }

        if (cmdline[0] == '!') {
            char *newcmd = fetch_from_history(cmdline);
            if (newcmd) {
                printf("Repeating command: %s\n", newcmd);
                free(cmdline);
                cmdline = newcmd;
            } else {
                free(cmdline);
                continue;
            }
        }

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
