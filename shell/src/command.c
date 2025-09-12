#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include "../include/command.h"
#include "../include/piping.h"
#include "../include/background.h"
#include <signal.h>
#define MAX_ARGS 64
#define MAX_CMD 256

void tokenise_input(const char *input, char *args[], int *argc, char **in_file, char **out_file, int *append, int *pipe_count, ParsedCommand *cmd)
{
    *argc = 0;
    *in_file = NULL;
    *out_file = NULL;
    *append = 0;
    *pipe_count = 0;
    cmd->tokens = malloc(MAX_ARGS * sizeof(char *));
    if (!cmd->tokens)
    {
        perror("Failed to allocate memory for tokens");
        exit(1);
    }
    cmd->count = 0;

    if (!input) {
        cmd->tokens[0] = NULL;
        return;
    }

    char *input_copy = strdup(input);
    if (!input_copy)
    {
        perror("Failed to allocate memory");
        free(cmd->tokens);
        exit(1);
    }

    char *token = strtok(input_copy, " \t\n");
    while (token != NULL && *argc < MAX_ARGS - 1 && cmd->count < MAX_ARGS - 1)
    {
        if (strcmp(token, "|") == 0)
        {
            (*pipe_count)++;
            cmd->tokens[cmd->count] = strdup(token);
            if (!cmd->tokens[cmd->count]) {
                perror("Failed to allocate memory for token");
                free(input_copy);
                // free the previously used tokens
                for (size_t i = 0; i < cmd->count; i++) {
                    free(cmd->tokens[i]);
                }
                free(cmd->tokens);
                exit(1);
            }
            cmd->count++;
        }
        else if (strcmp(token, "<") == 0)
        {
            token = strtok(NULL, " \t\n");
            if (token)
            {
                free(*in_file);
                *in_file = strdup(token);
                if (!*in_file) {
                    perror("Failed to allocate memory for input file");
                    free(input_copy);
                    exit(1);
                }
                cmd->tokens[cmd->count] = strdup("<");
                if (!cmd->tokens[cmd->count]) {
                    perror("Failed to allocate memory for token");
                    free(input_copy);
                    exit(1);
                }
                cmd->count++;
                if (cmd->count < MAX_ARGS - 1) {
                    cmd->tokens[cmd->count] = strdup(token);
                    if (!cmd->tokens[cmd->count]) {
                        perror("Failed to allocate memory for token");
                        free(input_copy);
                        exit(1);
                    }
                    cmd->count++;
                }
            }
        }
        else if (strcmp(token, ">") == 0)
        {
            token = strtok(NULL, " \t\n");
            if (token)
            {
                free(*out_file);
                *out_file = strdup(token);
                if (!*out_file) {
                    free(input_copy);
                    exit(1);
                }
                *append = 0;
                cmd->tokens[cmd->count] = strdup(">");
                if (!cmd->tokens[cmd->count]) {
                    free(input_copy);
                    exit(1);
                }
                cmd->count++;
                if (cmd->count < MAX_ARGS - 1) {
                    cmd->tokens[cmd->count] = strdup(token);
                    if (!cmd->tokens[cmd->count]) {
                        free(input_copy);
                        exit(1);
                    }
                    cmd->count++;
                }
            }
        }
        else if (strcmp(token, ">>") == 0)
        {
            token = strtok(NULL, " \t\n");
            if (token)
            {
                free(*out_file);
                *out_file = strdup(token);
                if (!*out_file) {
                    free(input_copy);
                    exit(1);
                }
                *append = 1;
                cmd->tokens[cmd->count] = strdup(">>");
                if (!cmd->tokens[cmd->count]) {
                    free(input_copy);
                    exit(1);
                }
                cmd->count++;
                if (cmd->count < MAX_ARGS - 1) {
                    cmd->tokens[cmd->count] = strdup(token);
                    if (!cmd->tokens[cmd->count]) {
                        free(input_copy);
                        exit(1);
                    }
                    cmd->count++;
                }
            }
        }
        else
        {
            args[*argc] = strdup(token);
            if (!args[*argc]) {
                free(input_copy);
                exit(1);
            }
            (*argc)++;
            cmd->tokens[cmd->count] = strdup(token);
            if (!cmd->tokens[cmd->count]) {
                free(input_copy);
                exit(1);
            }
            cmd->count++;
        }
        token = strtok(NULL, " \t\n");
    }
    args[*argc] = NULL;
    if (cmd->count < MAX_ARGS) {
        cmd->tokens[cmd->count] = NULL;
    }

    free(input_copy);
    
    for (size_t i = 0; i < cmd->count - 1; i++) {
        if (strcmp(cmd->tokens[i], "<") == 0 && i + 1 < cmd->count) {
            char* input_filename = cmd->tokens[i + 1];
            if (access(input_filename, F_OK) != 0) {
                // One of the input files doesn't exist - this should cause an error
                // Set the final in_file to this non-existent file so error handling works
                free(*in_file);
                *in_file = strdup(input_filename);
                break;
            }
        }
    }
}


void execute_command(char *args[], char *in_file, char *out_file, int append)
{
    if (!args[0]) {
        fprintf(stderr, "No command to execute\n");
        return;
    }

    // Check if this is a builtin command first
    if (is_builtin(args[0])) {
        // Handle file redirection for builtin commands
        int saved_stdin = -1, saved_stdout = -1;
        
        if (in_file) {
            int fd_in = open(in_file, O_RDONLY);
            if (fd_in < 0) {
                printf("No such file or directory\n");
                return;
            }
            saved_stdin = dup(STDIN_FILENO);
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }

        if (out_file) {
            int flags = O_WRONLY | O_CREAT;
            if (append)
                flags |= O_APPEND;
            else
                flags |= O_TRUNC;
            int fd_out = open(out_file, flags, 0644);
            if (fd_out < 0) {
                printf("Unable to create file for writing\n");
                if (saved_stdin != -1) {
                    dup2(saved_stdin, STDIN_FILENO);
                    close(saved_stdin);
                }
                return;
            }
            saved_stdout = dup(STDOUT_FILENO);
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }

        // Execute the builtin command
        execute_builtin(args);

        // Restore file descriptors
        if (saved_stdin != -1) {
            dup2(saved_stdin, STDIN_FILENO);
            close(saved_stdin);
        }
        if (saved_stdout != -1) {
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdout);
        }
        
        return;
    }

    // Handle external commands with fork/exec
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("Fork failed");
        exit(1);
    }
    if (pid == 0)
    {
        // Child process
        
        // Create new process group for job control
        setpgid(0, 0);
        
        // Give child control of terminal for foreground processes (only if we have terminal control)
        if (isatty(STDIN_FILENO)) {
            tcsetpgrp(STDIN_FILENO, getpgrp());
        }

        // Reset signal handlers to default for child
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        if (in_file)
        {
            int fd_in = open(in_file, O_RDONLY);
            if (fd_in < 0)
            {
                printf("No such file or directory\n");
                exit(1);
            }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }

        if (out_file)
        {
            int flags = O_WRONLY | O_CREAT;
            if (append)
                flags |= O_APPEND;
            else
                flags |= O_TRUNC;
            int fd_out = open(out_file, flags, 0644);
            if (fd_out < 0)
            {
                printf("Unable to create file for writing\n");
                exit(1);
            }
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }

        if (execvp(args[0], args) < 0)
        {
            printf("Command not found!\n");
            exit(1);
        }
    }
    else
    {
        // Parent process
        
        // Put child in its own process group
        setpgid(pid, pid);
        
        // Set as foreground process
        foreground_pid = pid;
        if (isatty(STDIN_FILENO)) {
            tcsetpgrp(STDIN_FILENO, pid);
        }

        int status;
        pid_t result = waitpid(pid, &status, WUNTRACED);

        if (isatty(STDIN_FILENO)) {
            tcsetpgrp(STDIN_FILENO, getpgrp());
        }
        
        if (result > 0) {
            if (WIFSTOPPED(status)) {
                // Process was stopped 
                int job_num = add_job(pid, args, JOB_STATE_STOPPED);
                if (job_num > 0) {
                    printf("\n[%d] Stopped %s\n", job_num, args[0]);
                }
            } else {
                foreground_pid = -1;
            }
        } else {
            foreground_pid = -1;
        }
    }
}
void command(const char *input)
{
    if (!input) {
        return;
    }

    check_background_jobs();

    char *input_copy = strdup(input);
    if (!input_copy)
    {
        perror("Failed to allocate memory");
        exit(1);
    }

    char *saveptr;
    char *cmd_group = strtok_r(input_copy, ";", &saveptr);
    int iteration = 0;
    
    while (cmd_group != NULL)
    {
        if (iteration++ > 50)
        {
            fprintf(stderr, "Too many commands in sequence (possible infinite loop)\n");
            break;
        }
        
        static char prev_cmd[1024] = {0};
        if (strlen(cmd_group) > 0 && strcmp(prev_cmd, cmd_group) == 0 && iteration > 2) {
            fprintf(stderr, "Repeated command detected (possible infinite loop)\n");
            break;
        }
        strncpy(prev_cmd, cmd_group, sizeof(prev_cmd) - 1);
        prev_cmd[sizeof(prev_cmd) - 1] = '\0';

        char *trimmed_cmd = cmd_group;
        while (*trimmed_cmd == ' ' || *trimmed_cmd == '\t')
        {
            trimmed_cmd++;
        }
        
        size_t len = strlen(trimmed_cmd);
        while (len > 0 && (trimmed_cmd[len - 1] == ' ' || trimmed_cmd[len - 1] == '\t'))
        {
            trimmed_cmd[--len] = '\0';
        }

        if (len == 0)
        {
            cmd_group = strtok_r(NULL, ";", &saveptr);
            continue;
        }

        char *args[MAX_ARGS];
        int argc = 0;
        char *in_file = NULL;
        char *out_file = NULL;
        int append = 0;
        int pipe_count = 0;
        ParsedCommand cmd = {0};

        // Parse without background operator 
        char *clean_cmd = remove_background_operator(trimmed_cmd);
        if (!clean_cmd) clean_cmd = strdup(trimmed_cmd);
        
        tokenise_input(clean_cmd, args, &argc, &in_file, &out_file, &append, &pipe_count, &cmd);

        if (pipe_count > 0 && in_file && out_file && strcmp(in_file, out_file) == 0) {
            fprintf(stderr, "Error: Cannot use same file for input and output in pipeline\n");
        }
        else 
        {
            process_command_with_jobs(trimmed_cmd, &cmd, args, argc, in_file, out_file, append, pipe_count);
        }


        for (int i = 0; i < argc; i++)
        {
            if (args[i]) free(args[i]);
        }
        free(in_file);
        free(out_file);
        for (size_t i = 0; i < cmd.count; i++)
        {
            if (cmd.tokens[i]) free(cmd.tokens[i]);
        }
        free(cmd.tokens);
        free(clean_cmd);

        cmd_group = strtok_r(NULL, ";", &saveptr);
    }

    free(input_copy);
}