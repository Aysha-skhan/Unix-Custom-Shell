# Custom Shell Program - PucitShell

## Project Status

`PucitShell`, is fully functional with core features such as command execution, background job handling, history management, and pipeline support. Most of the core functionalities are implemented as required, and the shell operates as expected for common usage. 

## Implemented Features

1. **Command Execution**:
   - Executes single commands or applications from the shell with argument support.
   - Supports running commands in the background using `&` at the end.
   - Can use arrow keys (advanced) to move among previous commands.

2. **History Management**:
   - Maintains a history of the last 10 commands using a circular buffer.
   - Supports executing past commands with `!N` for specific command numbers or `!-N` for commands in reverse order (last Nth command).

3. **Built-in Commands**:
   - `cd` to change directories.
   - `jobs` to list background jobs.
   - `kill` to terminate specific background jobs.
   - `exit` to close the shell.
   - `help` to display available built-in commands.

4. **Pipeline Support**:
   - Allows chaining commands using pipes (`|`) to pass output from one command as input to another.
   - Executes all commands in the pipeline sequentially with proper input/output redirection between commands.

5. **Custom Prompt**:
   - Displays a prompt with user information, hostname, and the current directory in color-coded format for enhanced readability.

6. **Signal Handling**:
   - Utilizes `SIGCHLD` to clean up completed background processes automatically.

## Additional Features

- Added color-coded prompt display for an enhanced user experience.
- Integrated `readline` and `history` libraries for better command line editing and recall functionalities.
- Improved error handling and messaging to inform users of invalid inputs or failed operations.

## Acknowledgements

This project was completed as part of a shell programming assignment. Special thanks to:

- **Prof. Arif Butt** for providing the initial codebase and framework, which served as a foundation for this project.
- The **Teaching Assistants (TAs)** for their guidance and valuable feedback throughout the development process.
  
Additionally, various online resources were referenced to understand complex functionalities, including:

- `man` pages for Linux system calls (`fork`, `execvp`, `pipe`, `dup2`, etc.).
- Stack Overflow and Unix Stack Exchange for handling specific implementation details related to shell programming.
- AI tools like Chatgpt4, Blackbox AI and Code Copilot have been used to help in fixing bugs.

**Known Bugs**:
1. There is occasional behavior where repeating commands using `!-N` or `!N` may result in unexpected output or failures if invalid history references are provided. This will need careful handling for edge cases.
2. Background job list (`jobs`) may sometimes contain jobs that are already completed but haven't been removed due to asynchronous process handling limitations.
  
## How to Run

1. Compile the code:
   ```bash
   gcc shell.c -o shell -lreadline
   ```

2. Run the shell:
   ```bash
   ./shell
   ```

## Usage

After launching `PucitShell`, you can use it as a regular shell environment. Here are some example commands:

- **Single command**: `ls -l`
- **Background job**: `sleep 10 &`
- **Pipeline**: `cat file.txt | grep "search_term" | sort`
- **Change directory**: `cd /path/to/directory`
- **Run previous command**: `!1` (first command in history) or `!-1` (last command in history)
- **Exit the shell**: `exit`

## Future Enhancements

1. Add improved error handling and validation for edge cases in history management.
2. Expand support for more complex built-in commands and additional shell features.
3. Improve handling of asynchronous job removal in the `jobs` list.

