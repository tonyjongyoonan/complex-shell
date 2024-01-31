#include "parser.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include "jobs.h"

#define MAX_INPUT_SIZE 4096

int curr_pid;

void handler(int sig) {
    
    if (write(STDERR_FILENO, "\n", 1) == -1) {
        perror("write error");
        exit(EXIT_FAILURE);
    }
    if (write(STDERR_FILENO, PROMPT, strlen(PROMPT)) == -1) {
        perror("write error");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {
    // takes in file argument
    bool interactive = true;

    if (argc != 1) {
        perror("too many arguments");
        exit(EXIT_FAILURE);
    }

    if (isatty(STDIN_FILENO) == 0) {
        interactive = false;     
    }

    while (1) {
        if (signal(SIGINT, handler) == SIG_ERR) {
            perror("signal error");
            exit(EXIT_FAILURE);
        }

        if (signal(SIGTSTP, handler) == SIG_ERR) {
            perror("signal error");
            exit(EXIT_FAILURE);
        }

        int status;       
        char* cmd_line = (char*) malloc(sizeof(char) * MAX_INPUT_SIZE);
        int numBytes = -1;
        // take in an input
        if (interactive) {
            if (write(STDERR_FILENO,PROMPT,strlen(PROMPT)) == -1) {
                perror("write fail");
                exit(EXIT_FAILURE);
            }
            numBytes = read(STDIN_FILENO, cmd_line, MAX_INPUT_SIZE);
            cmd_line[numBytes] = '\0';
            if (numBytes < 0) {
                perror("read error");
                exit(EXIT_FAILURE);
            } else if (numBytes == 0) {
                free(cmd_line);
                return 0;
            }
        } else {
            size_t buffer_size = 4096;
            numBytes = getline(&cmd_line, &buffer_size, stdin);
            cmd_line[numBytes] = '\0';
            if (numBytes == -1) {
                return 0;
            } else if (numBytes == 0) {
                return 0;
            }
        }

        update_statuses();

        // parse command from input
        struct parsed_command *cmd;
        int i = parse_command(cmd_line, &cmd);
        if (i < 0) {
            perror("parser invalid system error call");
            free(cmd_line);
            free(cmd);
            exit(EXIT_FAILURE);
            continue;
        } else if (i > 0) {
            fprintf(stderr, "parser invalid syntax error: %d\n", i);
            free(cmd_line);
            continue;
        }
        
        
        if (cmd->num_commands == 0) {
            free(cmd_line);
            free(cmd);
            continue;
        } else {
            if (strcmp(cmd -> commands[0][0],"jobs") == 0) {
                // print();

                update_statuses();
                print_jobs();
                free(cmd_line);
                free(cmd);
                continue;
            } else if (strcmp(cmd -> commands[0][0],"bg") == 0) {

                int job_id = -1;
                if (cmd -> commands[0][1] != NULL) {
                    job_id = atoi(cmd -> commands[0][1]);
                }

                struct job* j = get_job(job_id);

                if (j!= NULL) {
                    int pid = j -> pgid;

                    if (j -> status == 0) {
                        killpg(pid, SIGCONT);
                        update_status(pid, 1);
                    }
                }

                free(cmd_line);
                free(cmd);
                continue;
            } else if (strcmp(cmd -> commands[0][0],"fg") == 0) {
                
                int job_id = -1;
                if (cmd -> commands[0][1] != NULL) {
                    job_id = atoi(cmd -> commands[0][1]);
                }

                struct job* j = get_job(job_id);

                if (j!= NULL) {
                    int pid = j -> pgid;

                    if (j -> status == 0) {
                        killpg(pid, SIGCONT);
                        update_status(pid, 2);
                    } else {
                        write(STDERR_FILENO, j->cmd_raw, strlen(j->cmd_raw));
                        write(STDERR_FILENO,"\n", strlen("\n"));
                    }

                    if (tcsetpgrp(STDIN_FILENO, pid) == -1) {
                        perror("tcsetpgrp error");
                    }

                    waitpid(-pid, &status, WUNTRACED);

                    if (WIFSTOPPED(status)) {
                        write(STDERR_FILENO, "\n", 1);
                        update_status(pid, 0);
                    } else if (WIFSIGNALED(status) || WIFEXITED(status)) {
                        process_terminated(pid);
                    }

                    signal(SIGTTOU, SIG_IGN);
                    tcsetpgrp(STDIN_FILENO, getpgid(0));
                }

                update_statuses();

                free(cmd_line);
                free(cmd);
                continue;
            }

        }


        int pipe_fds[cmd->num_commands - 1][2];
        int *pids = malloc(sizeof(int) * cmd->num_commands);
        for (int i = 0; i < cmd->num_commands; ++i) {
            // int pipe_fds[i]: pipe connecting command[i] to command[i+1]
            // int pipe_fds[i][0]: read pipe
            // int pipe_fds[i][1]: write pipe
            
            if (i != cmd->num_commands - 1) {
                if (pipe(pipe_fds[i]) == -1) {
                    perror("pipe error");
                    exit(EXIT_FAILURE);
                }
            }

            pids[i] = fork();
            if (pids[i] == -1) {
                perror("fork error");
                exit(EXIT_FAILURE);
            }

            // child process
            if (pids[i] == 0) {
                // redirects read from stdin to pipe if necessary
                if (i == 0) {
                    if (cmd->stdin_file != NULL) {
                        // redirect stdin to input
                        int fd = open(cmd->stdin_file, O_RDONLY);
                        if (fd < 0) {
                            perror("open error");
                            exit(EXIT_FAILURE);
                        }
                        if (dup2(fd, STDIN_FILENO) < 0) {
                            perror("dup2 error");
                            exit(EXIT_FAILURE);
                        }
                        close(fd);
                    } // otherwise, do not redirect, we read from stdin
                } else { // not first element, read from previous read pipe
                    if (dup2(pipe_fds[i-1][0], STDIN_FILENO) < 0) {
                        perror("dup2 error");
                        exit(EXIT_FAILURE);
                    }
                }

                // redirects write from stdout to pipe
                if (i == cmd->num_commands - 1) { // last element
                    if (cmd->stdout_file != NULL) { // redirect to given stdout_file
                        int fd = open(cmd->stdout_file, O_CREAT | O_WRONLY | O_TRUNC, 0666);
                        if (fd < 0) {
                            perror("open error");
                            exit(EXIT_FAILURE);
                        }
                        if (dup2(fd, STDOUT_FILENO) < 0) {
                            perror("dup2 error");
                            exit(EXIT_FAILURE);
                        }
                        close(fd);
                    }
                } else { // not last element, redirect to stdout
                    if (dup2(pipe_fds[i][1], STDOUT_FILENO) < 0) {
                        perror("dup2 error");
                        exit(EXIT_FAILURE);
                    }
                }

                if (i != cmd->num_commands - 1) {
                    close(pipe_fds[i][1]);
                }
                if (i > 0) {
                    close(pipe_fds[i - 1][0]);
                }
                // executing command will be redirected from and to pipe
                execvp(cmd->commands[i][0], cmd->commands[i]);

            } else {
                curr_pid = pids[0];

                // parent process
                // setpgid(pid, pgid) sets pid's process' pgid to pgid
                if (setpgid(pids[i], pids[0]) != 0) {
                    perror("setpgid error");
                }

                if (i != cmd->num_commands - 1) {
                    close(pipe_fds[i][1]);
                }
                if (i > 0) {
                    close(pipe_fds[i - 1][0]);
                }
            }
        }

        // set foreground process group to be the first subcommand
        if (!cmd->is_background) {
            if (interactive && tcsetpgrp(STDIN_FILENO, pids[0]) == -1) {
                perror("tcsetpgrp error");
            }
            // wait for foreground process to complete
            waitpid(-pids[0], &status, WUNTRACED);

            if (WIFSTOPPED(status)) {
                write(STDERR_FILENO, "\n", 1);
                create_job(cmd_line, pids, cmd->num_commands, 0);
            } else {
                while (waitpid(-pids[0], &status, 0) > 0);
                free(cmd_line);
                free(pids);
            }

        } else {
            tcsetpgrp(STDIN_FILENO, getpgid(0));
            create_job(cmd_line, pids, cmd -> num_commands, 1);

        }
        signal(SIGTTOU, SIG_IGN);
        tcsetpgrp(STDIN_FILENO, getpgid(0));

        update_statuses();

        // free(pids);
        free(cmd);
    }

    return 0;
}
