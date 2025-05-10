// #define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define READ_END 0
#define WRITE_END 1

// Ignore SIGINT in the shell, but restore it in child processes
// This is because by default, Ctrl+C would kill the shell but I want only child processes to die on Ctrl+C — not the shell.
int prepare(void) {
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    return sigaction(SIGINT, &sa, NULL);
}

int finalize(void) {
    return 0;
}

// Helper: find index of symbol in arglist
int find_symbol(char **arglist, int count, const char *symbol) {
    for (int i = 0; i < count; i++) {
        if (strcmp(arglist[i], symbol) == 0)
            return i;
    }
    return -1;
}

// Helper: set up redirection
void redirect(int fd, int target_fd) {
    if (dup2(fd, target_fd) == -1) {
        perror("dup2");
        exit(1);
    }
    close(fd);
}

int process_arglist(int count, char **arglist) {
    int background = 0;

    // Background execution
    // I remove & from the argument list (and not giving it to execvp())
    // Later we would not waitpid() to the child if background
    if (strcmp(arglist[count - 1], "&") == 0) {
        background = 1;
        arglist[count - 1] = NULL;
        count--;
    }

    // Handle input redirection
    int input_idx = find_symbol(arglist, count, "<");
    if (input_idx != -1) {
        // I expect a filename after the < so if < is the last argument it’s invalid
        if (input_idx + 1 >= count) {
            fprintf(stderr, "Missing input file\n");
            return 1;
        }

        pid_t pid = fork();
        if (pid == 0) {
            // Open the input file (the one after <) for reading.
            int fd = open(arglist[input_idx + 1], O_RDONLY);
            if (fd == -1) {
                perror("open input");
                exit(1);
            }
            // now when the program tries to read from standard input it's actually reading from the file instead
            redirect(fd, STDIN_FILENO); 
            // Set < to NULL so execvp() only sees the actual command (not < or file)
            arglist[input_idx] = NULL;
            execvp(arglist[0], arglist);
            // If execvp() is successful, it won't return at all, otherwise there's an error
            perror("execvp");
            exit(1);
        } else if (pid > 0) {
            waitpid(pid, NULL, 0);
        } else {
            perror("fork");
            return 1;
        }
        return 1;
    }

    // Handle output redirection
    int output_idx = find_symbol(arglist, count, ">");
    if (output_idx != -1) {
        if (output_idx + 1 >= count) {
            fprintf(stderr, "Missing output file\n");
            return 1;
        }

        pid_t pid = fork();
        if (pid == 0) {
            int fd = open(arglist[output_idx + 1], O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
            if (fd == -1) {
                perror("open output");
                exit(1);
            }

            redirect(fd, STDOUT_FILENO);
            arglist[output_idx] = NULL;
            execvp(arglist[0], arglist);
            perror("execvp");
            exit(1);
        } else if (pid > 0) {
            waitpid(pid, NULL, 0);
        } else {
            perror("fork");
            return 1;
        }
        return 1;
    }

    // Handle pipes
    int pipes_counter = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(arglist[i], "|") == 0)
            pipes_counter++;
    }

    if (pipes_counter > 0) {
        int num_cmds = pipes_counter + 1;
        // Breaks arglist into separate null terminated command arrays by replacing | with NULLx
        char **commands[10];  // 10 commands max
        int cmd_idx = 0;
        
        commands[cmd_idx] = arglist;  // First command
        cmd_idx++;
        
        for (int i = 0; i < count; i++) {
            if (strcmp(arglist[i], "|") == 0) {
                arglist[i] = NULL;  // Terminate previous command
                commands[cmd_idx] = &arglist[i + 1];  // Next command starts after '|'
                cmd_idx++;
            }
        }
        
        // Allocate and create pipe_count pipes ([read, write] pairs).
        int pipes[pipes_counter][2];
        for (int i = 0; i < pipes_counter; i++) {
            // initialize a new pipe and store its file descriptors in the ith row
            // creates a new pipe and returns two file descriptors, one referring to the read end of the
            // pipe, the other referring to the write end
            if (pipe(pipes[i]) == -1) {
                perror("pipe");
                return 1;
            }
        }

        for (int i = 0; i < num_cmds; i++) {
            pid_t pid = fork();
            if (pid == 0) {
                signal(SIGINT, SIG_DFL); // restore Ctrl+C for children
        
                if (i > 0) {
                    redirect(pipes[i - 1][READ_END], STDIN_FILENO);  // if not first, read from previous pipe
                }
                if (i < pipes_counter) {
                    redirect(pipes[i][WRITE_END], STDOUT_FILENO);    // if not last, write to next pipe
                }
        
                // Close all pipe ends in child
                for (int j = 0; j < pipes_counter; j++) {
                    close(pipes[j][READ_END]);
                    close(pipes[j][WRITE_END]);
                }
        
                execvp(commands[i][0], commands[i]);
                perror("execvp");
                exit(1);
            }
        }
        
        for (int i = 0; i < pipes_counter; i++) {
            close(pipes[i][READ_END]);
            close(pipes[i][WRITE_END]);
        }

        for (int i = 0; i < num_cmds; i++) {
            wait(NULL);
        }

        return 1;
    }

    // Simple command (foreground or background)
    pid_t pid = fork();
    if (pid == 0) {
        // Restore SIGINT for child
        signal(SIGINT, background ? SIG_IGN : SIG_DFL);
        execvp(arglist[0], arglist);
        perror("execvp");
        exit(1);
    } else if (pid > 0) {
        if (!background) {
            waitpid(pid, NULL, 0);
        }
    } else {
        perror("fork");
        return 1;
    }

    return 1;
}
