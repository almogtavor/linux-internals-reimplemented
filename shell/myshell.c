// #define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

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

            redirect(fd, STDIN_FILENO);
            arglist[input_idx] = NULL;
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

    // Handle output redirection
    int output_idx = find_symbol(arglist, count, ">");
    if (output_idx != -1) {
        if (output_idx + 1 >= count) {
            fprintf(stderr, "Missing output file\n");
            return 1;
        }

        pid_t pid = fork();
        if (pid == 0) {
            int fd = open(arglist[output_idx + 1], O_WRONLY | O_CREAT | O_TRUNC, 0600);
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
    int pipe_count = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(arglist[i], "|") == 0)
            pipe_count++;
    }

    if (pipe_count > 0) {
        int num_cmds = pipe_count + 1;
        char **commands[10];  // max 10 commands
        int cmd_idx = 0;
        commands[cmd_idx++] = arglist;

        for (int i = 0; i < count; i++) {
            if (strcmp(arglist[i], "|") == 0) {
                arglist[i] = NULL;
                commands[cmd_idx++] = &arglist[i + 1];
            }
        }

        int pipes[pipe_count][2];
        for (int i = 0; i < pipe_count; i++) {
            if (pipe(pipes[i]) == -1) {
                perror("pipe");
                return 1;
            }
        }

        for (int i = 0; i < num_cmds; i++) {
            pid_t pid = fork();
            if (pid == 0) {
                // SIGINT default in child
                signal(SIGINT, SIG_DFL);

                if (i > 0) {
                    redirect(pipes[i - 1][READ_END], STDIN_FILENO);
                }
                if (i < pipe_count) {
                    redirect(pipes[i][WRITE_END], STDOUT_FILENO);
                }

                for (int j = 0; j < pipe_count; j++) {
                    close(pipes[j][READ_END]);
                    close(pipes[j][WRITE_END]);
                }

                execvp(commands[i][0], commands[i]);
                perror("execvp");
                exit(1);
            }
        }

        for (int i = 0; i < pipe_count; i++) {
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
