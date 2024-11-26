#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>    
#include <pwd.h>      

#define MAX_LEN 512     // Maximum length of a command input
#define MAXARGS 10      // Maximum number of arguments allowed
#define ARGLEN 30       // Maximum length of each argument

// ANSI color codes for terminal output
#define COLOR_RESET   "\033[0m"        // Resets to default color
#define COLOR_RED     "\033[31m"       // Red color for errors or important text
#define COLOR_GREEN   "\033[32m"       // Green color for username
#define COLOR_BLUE    "\033[34m"       // Blue color for hostname
#define COLOR_CYAN    "\033[36m"       // Cyan color for the current working directory (PWD)

// Function declarations
int execute(char* arglist[]);            // Function to fork and execute the command
char** tokenize(char* cmdline);          // Function to tokenize the command line input into arguments
char* read_cmd(char*, FILE*);            // Function to read the command input from the user

int main() {
   char *cmdline;                        // Pointer to hold the command input
   char** arglist;                       // Pointer to hold the list of tokenized arguments
   char prompt[MAX_LEN];                 // Array to store the dynamic prompt string
   char hostname[HOST_NAME_MAX];         // Array to store the machine (host) name
   char cwd[PATH_MAX];                   // Array to store the current working directory (cwd)
   char* username = getenv("USER");      // Retrieve the username from the environment variables

   // Ensure that username is not NULL, set it to "unknown" if not found
   if (username == NULL) {
       username = "unknown";
   }

   gethostname(hostname, HOST_NAME_MAX); // Get the hostname of the machine

   // Main loop that runs continuously until the user exits (e.g., with Ctrl+D)
   while(1) {
      // Get the current working directory, and format the prompt string
      if (getcwd(cwd, sizeof(cwd)) != NULL) {
         // Format the prompt with colors using ANSI escape codes
         snprintf(prompt, sizeof(prompt), 
            COLOR_RED "PUCITshell " COLOR_RESET  // PUCITshell in red
            "(" COLOR_GREEN "%s" COLOR_RESET     // Username in green
            "@" COLOR_GREEN "%s" COLOR_RESET      // Hostname in blue
            ")-[" COLOR_CYAN "%s" COLOR_RESET "] :", 
            username, hostname, cwd);
      } else {
         // If there’s an error getting the current directory, print an error message
         perror("getcwd() error");
         return 1; // Exit with error code
      }

      // Read the command from the user input
      cmdline = read_cmd(prompt, stdin);
      
      // If Ctrl+D (EOF) is pressed, cmdline will be NULL and the shell will exit
      if (cmdline == NULL) {
         break;
      }
      
      // Tokenize the command line input into arguments, and execute the command
      if ((arglist = tokenize(cmdline)) != NULL) {
            execute(arglist);  // Call the execute function to fork and run the command
            
            // Free the dynamically allocated memory for arguments
            for (int j = 0; j < MAXARGS+1; j++)
               free(arglist[j]);
            free(arglist);
            free(cmdline);  // Free the memory allocated for the command line
      }
   }

   printf("\n");
   return 0;  // Return success
}

// Function to execute the command by forking and using execvp
int execute(char* arglist[]) {
   int status;
   int cpid = fork();  // Fork a new process
   switch (cpid) {
      case -1:
         // If fork fails, print an error message and exit with error code
         perror("fork failed");
         exit(1);
      case 0:
         // Child process: execute the command using execvp
         execvp(arglist[0], arglist);
         // If execvp fails (command not found), print an error and exit
         perror("Command not found...");
         exit(1);
      default:
         // Parent process: wait for the child process to finish and get its exit status
         waitpid(cpid, &status, 0);
         printf("child exited with status %d \n", status >> 8);  // Print exit status of child
         return 0;
   }
}

// Function to tokenize the input command line into arguments
char** tokenize(char* cmdline) {
   // Allocate memory for argument list, with room for MAXARGS arguments
   char** arglist = (char**)malloc(sizeof(char*) * (MAXARGS + 1));
   for (int j = 0; j < MAXARGS + 1; j++) {
       arglist[j] = (char*)malloc(sizeof(char) * ARGLEN); // Allocate memory for each argument
       bzero(arglist[j], ARGLEN); // Initialize the argument memory with zeros
   }

   // If the user enters an empty command (just presses Enter), return NULL
   if (cmdline[0] == '\0')
      return NULL;

   int argnum = 0;  // Variable to count how many arguments have been parsed
   char* cp = cmdline;  // Pointer to track the current position in the command line string
   char* start;         // Pointer to mark the start of each argument
   int len;             // Length of each argument

   // Loop through the command line string to tokenize it
   while (*cp != '\0') {
      // Skip any leading spaces or tabs
      while (*cp == ' ' || *cp == '\t')
          cp++;
      
      start = cp;  // Mark the start of the argument
      len = 1;
      
      // Find the end of the argument (i.e., next space or tab)
      while (*++cp != '\0' && !(*cp == ' ' || *cp == '\t'))
         len++;
      
      // Copy the argument into the argument list
      strncpy(arglist[argnum], start, len);
      arglist[argnum][len] = '\0';  // Null-terminate the string
      argnum++;  // Increment the argument count
   }

   // The last element in the argument list should be NULL, marking the end of arguments
   arglist[argnum] = NULL;
   return arglist;  // Return the list of arguments
}

// Function to read the command input from the user
char* read_cmd(char* prompt, FILE* fp) {
   printf("%s", prompt);  // Print the shell prompt
   int c;  // Variable to hold each input character
   int pos = 0;  // Position in the command line
   char* cmdline = (char*)malloc(sizeof(char) * MAX_LEN);  // Allocate memory for the command line

   // Read characters from user input until Enter (newline) or EOF (Ctrl+D)
   while ((c = getc(fp)) != EOF) {
       if (c == '\n')  // If Enter is pressed, stop reading input
          break;
       cmdline[pos++] = c;  // Add the character to the command line
   }

   // If EOF is encountered and no input was entered, return NULL (exit the shell)
   if (c == EOF && pos == 0)
      return NULL;

   cmdline[pos] = '\0';  // Null-terminate the command string
   return cmdline;  // Return the command line
}
