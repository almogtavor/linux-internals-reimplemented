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

// Reap children quickly to avoid zombies
void reap_children(int sig) {
    int errno_backup = errno; // for cases where the code was interrupted by a signal
    // -1 tells waitpid() to wait for any child process, NULL because I'm not interested in the child's exit status
    // and WNOHANG prevents waiting so we only check zombie (exited but not reaped yet) processes
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
    if (errno != ECHILD && errno != EINTR && errno != 0) {
        perror("waitpid in SIGCHLD");
        exit(1);
    }
    errno = errno_backup;
}

// Ignore SIGINT in the shell, but restore it in child processes
// This is because by default, Ctrl+C would kill the shell but I want only child processes to die on Ctrl+C — not the shell.
int prepare(void) {
    // Ignore Ctrl-C in the shell itself
    if (signal(SIGINT, SIG_IGN) == SIG_ERR) {
        perror("signal(SIGINT)");
        return -1;
    }

    // Set a SIGCHLD handler that reaps terminated children
    struct sigaction sa;
    sa.sa_handler = reap_children;
    // SA_RESTART ensures that waitpid() is restarted if interrupted by a signal like SIGCHLD.
    // avoids EINTR errors and reduces the need for extra checks.
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) { // register reap_children() as the handler for SIGCHLD
        perror("sigaction(SIGCHLD)");
        return -1;
    }
    return 0;
}

int finalize(void) {
    return 0;
}

// helper func: find index of symbol in arglist
int find_symbol(char **arglist, int count, const char *symbol) {
    for (int i = 0; i < count; i++) {
        if (strcmp(arglist[i], symbol) == 0)
            return i;
    }
    return -1;
}

// helper func: set up redirection
void redirect(int fd, int target_fd) {
    if (dup2(fd, target_fd) == -1) {
        perror("dup2");
        exit(1);
    }
    close(fd);
}

int handle_input_redirection(char **arglist, int count) {
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
    return 0;
}


int handle_output_redirection(char **arglist, int count) {
    int output_idx = find_symbol(arglist, count, ">");
    if (output_idx != -1) {
        if (output_idx + 1 >= count) {
            fprintf(stderr, "Missing output file\n");
            return 1;
        }

        pid_t pid = fork();
        if (pid == 0) {
            // open the output file for writing, create or truncate it
            int fd = open(arglist[output_idx + 1], O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
            if (fd == -1) {
                perror("open output");
                exit(1);
            }
            // redirect stdout to the file
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
    return 0;
}

int handle_pipes(char **arglist, int count) {
    int pipes_counter = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(arglist[i], "|") == 0)
            pipes_counter++;
    }

    if (pipes_counter > 0) {
        int num_cmds = pipes_counter + 1;
        // Breaks arglist into separate null terminated command arrays by replacing | with NULLx
        char **commands[10]; // 10 commands max
        int cmd_idx = 0;
        
        commands[cmd_idx] = arglist; // First command
        cmd_idx++;
        
        for (int i = 0; i < count; i++) {
            if (strcmp(arglist[i], "|") == 0) {
                arglist[i] = NULL; // Terminate previous command
                commands[cmd_idx] = &arglist[i + 1]; // Next command starts after '|'
                cmd_idx++;
            }
        }
        
        // Allocate and create pipes_counter pipes ([read, write] pairs).
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
                    redirect(pipes[i - 1][READ_END], STDIN_FILENO); // if not first, read from previous pipe
                }
                if (i < pipes_counter) {
                    redirect(pipes[i][WRITE_END], STDOUT_FILENO); // if not last, write to next pipe
                }
        
                // Close all pipe ends in child because we already redirected them
                for (int j = 0; j < pipes_counter; j++) {
                    close(pipes[j][READ_END]);
                    close(pipes[j][WRITE_END]);
                }
        
                // all unnecessary pipe ends are closed, run the command
                execvp(commands[i][0], commands[i]);
                perror("execvp");
                exit(1);
            }
        }
        
        for (int i = 0; i < pipes_counter; i++) {
            close(pipes[i][READ_END]);
            close(pipes[i][WRITE_END]);
        }

        // will block the parent process until any of its children has finished
        // the loop is to wait for all child processes created (in no particular order)
        for (int i = 0; i < num_cmds; i++) {
            wait(NULL);
        }

        return 1;
    }
    return 0;
}

int validate_arglist(char **arglist, int count) {
    // Lone ampersand
    if (count == 1 && strcmp(arglist[0], "&") == 0) {
        fprintf(stderr, "Invalid command\n");
        return 1;
    }

    // Check for malformed pipes
    if (strcmp(arglist[0], "|") == 0 || strcmp(arglist[count - 1], "|") == 0) {
        fprintf(stderr, "Invalid pipe syntax\n");
        return 1;
    }
    for (int i = 1; i < count; i++) {
        if (strcmp(arglist[i], "|") == 0 && strcmp(arglist[i - 1], "|") == 0) {
            fprintf(stderr, "Invalid pipe syntax\n");
            return 1;
        }
    }

    // Check for redirection with no filename
    int input_idx = find_symbol(arglist, count, "<");
    int output_idx = find_symbol(arglist, count, ">");
    if ((input_idx != -1 && input_idx + 1 >= count) ||
        (output_idx != -1 && output_idx + 1 >= count)) {
        fprintf(stderr, "Missing filename\n");
        return 1;
    }
    return 0;
}


int process_arglist(int count, char **arglist) {
    int retVal, background = 0;

    retVal = validate_arglist(arglist, count);
    if (retVal == 1)
        return retVal;

    // Background execution
    // I remove & from the argument list (and not giving it to execvp())
    // Later we would not waitpid() to the child if background
    if (strcmp(arglist[count - 1], "&") == 0) {
        background = 1;
        arglist[count - 1] = NULL;
        count--;
    }

    retVal = handle_input_redirection(arglist, count);
    if (retVal == 1)
        return retVal;

    retVal = handle_output_redirection(arglist, count);
    if (retVal == 1)
        return retVal;

    retVal = handle_pipes(arglist, count);
    if (retVal == 1)
        return retVal;

    pid_t pid = fork();
    if (pid == 0) {
        // Restore the SIGINT for child and set it based on foreground/background
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
