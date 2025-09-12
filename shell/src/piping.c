#include "../include/piping.h"
#include "../include/background.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <stdlib.h>

void cleanup(ParsedCommand *cmd, char *input, char **cmd_tokens, char *input_file, char *output_file)
{
    if (cmd_tokens)
        free(cmd_tokens);
    if (input_file)
        free(input_file);
    if (output_file)
        free(output_file);
    for (size_t j = 0; j < cmd->count; j++)
    {
        if (cmd->tokens[j])
            free(cmd->tokens[j]);
    }
    if (cmd->tokens)
        free(cmd->tokens);
    if (input)
        free(input);
}

int execute_external(char *tokens[], size_t token_count, char *input_file, char *output_file, int output_append)
{
    if (!tokens[0] || token_count == 0)
    {
        fprintf(stderr, "No command to execute\n");
        return 1;
    }

    // Check if this is a builtin command
    if (is_builtin(tokens[0]))
    {
        
        // Handle file redirection for builtins
        int saved_stdin = -1, saved_stdout = -1;
        
        if (input_file)
        {
            int fd_in = open(input_file, O_RDONLY);
            if (fd_in < 0)
            {
                fprintf(stderr, "No such file or directory: %s\n", input_file);
                return 1;
            }
            saved_stdin = dup(STDIN_FILENO);
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }

        if (output_file)
        {
            int flags = O_WRONLY | O_CREAT;
            if (output_append)
                flags |= O_APPEND;
            else
                flags |= O_TRUNC;
            int fd_out = open(output_file, flags, 0644);
            if (fd_out < 0)
            {
                fprintf(stderr, "Cannot open or create output file: %s\n", output_file);
                if (saved_stdin != -1) {
                    dup2(saved_stdin, STDIN_FILENO);
                    close(saved_stdin);
                }
                return 1;
            }
            saved_stdout = dup(STDOUT_FILENO);
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }
        
        // Execute the builtin
        execute_builtin(tokens);
        
        if (saved_stdin != -1) {
            dup2(saved_stdin, STDIN_FILENO);
            close(saved_stdin);
        }
        if (saved_stdout != -1) {
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdout);
        }
        
        return 0;
    }

    if (input_file)
    {
        int fd_in = open(input_file, O_RDONLY);
        if (fd_in < 0)
        {
            fprintf(stderr, "No such file or directory: %s\n", input_file);
            return 1;
        }
        dup2(fd_in, STDIN_FILENO);
        close(fd_in);
    }

    if (output_file)
    {
        int flags = O_WRONLY | O_CREAT;
        if (output_append)
            flags |= O_APPEND;
        else
            flags |= O_TRUNC;
        int fd_out = open(output_file, flags, 0644);
        if (fd_out < 0)
        {
            fprintf(stderr, "Cannot open or create output file: %s\n", output_file);
            return 1;
        }
        dup2(fd_out, STDOUT_FILENO);
        close(fd_out);
    }

    if (execvp(tokens[0], tokens) < 0)
    {
        printf("Command not found!\n");
        return 1;
    }
    return 0;
}

void execute_pipeline(ParsedCommand *cmd, char *input, int pipe_count)
{
    if (pipe_count <= 0) {
        fprintf(stderr, "Invalid pipe count\n");
        return;
    }

    int pipes[pipe_count][2];
    pid_t pids[pipe_count + 1];
    size_t cmd_start = 0;

    for (int i = 0; i < pipe_count; i++) {
        if (pipe(pipes[i]) == -1) {
            fprintf(stderr, "Pipe creation failed\n");
           
            for (int j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return;
        }
    }

    for (int i = 0; i <= pipe_count; i++)
    {
        char *input_file = NULL;
        char *output_file = NULL;
        int output_append = 0;
        int input_error = 0;
        size_t cmd_token_count = 0;

        char **cmd_tokens = malloc((cmd->count + 1) * sizeof(char *));
        if (!cmd_tokens)
        {
            fprintf(stderr, "Memory allocation failed\n");
            for (int k = 0; k < pipe_count; k++) {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }
            return;
        }

        size_t j = cmd_start;
        while (j < cmd->count && strcmp(cmd->tokens[j], "|") != 0 && 
               strcmp(cmd->tokens[j], ";") != 0 && strcmp(cmd->tokens[j], "&") != 0)
        {
            if (strcmp(cmd->tokens[j], "<") == 0)
            {
                j++;
                if (j >= cmd->count || strcmp(cmd->tokens[j], "|") == 0)
                {
                    fprintf(stderr, "Invalid Syntax!\n");
                    input_error = 1;
                    break;
                }
                if (access(cmd->tokens[j], F_OK) != 0)
                {
                    input_error = 1;
                }
                if (input_file)
                    free(input_file);
                input_file = strdup(cmd->tokens[j]);
                if (!input_file)
                {
                    fprintf(stderr, "Memory allocation failed\n");
                    input_error = 1;
                    break;
                }
                j++;
                continue;
            }
            if (strcmp(cmd->tokens[j], ">") == 0 || strcmp(cmd->tokens[j], ">>") == 0)
            {
                output_append = strcmp(cmd->tokens[j], ">>") == 0;
                j++;
                if (j >= cmd->count || strcmp(cmd->tokens[j], "|") == 0)
                {
                    fprintf(stderr, "Invalid Syntax!\n");
                    input_error = 1;
                    break;
                }
                if (output_file)
                    free(output_file);
                output_file = strdup(cmd->tokens[j]);
                if (!output_file)
                {
                    fprintf(stderr, "Memory allocation failed\n");
                    input_error = 1;
                    break;
                }
                j++;
                continue;
            }
           
            cmd_tokens[cmd_token_count] = strdup(cmd->tokens[j]);
            if (!cmd_tokens[cmd_token_count]) {
                fprintf(stderr, "Memory allocation failed\n");
                input_error = 1;
                break;
            }
            cmd_token_count++;
            j++;
        }
        cmd_tokens[cmd_token_count] = NULL;

        if (input_error || cmd_token_count == 0)
        {
            if (input_error)
                fprintf(stderr, "No such file or directory\n");
            else
                fprintf(stderr, "Unknown command\n");
            
            // Free allocated cmd_tokens
            for (size_t k = 0; k < cmd_token_count; k++) {
                if (cmd_tokens[k]) free(cmd_tokens[k]);
            }
            free(cmd_tokens);
            if (input_file) free(input_file);
            if (output_file) free(output_file);
            
            for (int k = 0; k < pipe_count; k++) {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }
            return;
        }

        if (is_builtin(cmd_tokens[0]))
        {
            pids[i] = fork();
            if (pids[i] == -1)
            {
                fprintf(stderr, "Fork failed\n");
                for (size_t k = 0; k < cmd_token_count; k++) {
                    if (cmd_tokens[k]) free(cmd_tokens[k]);
                }
                free(cmd_tokens);
                if (input_file) free(input_file);
                if (output_file) free(output_file);
                
                // Close all pipes
                for (int k = 0; k < pipe_count; k++) {
                    close(pipes[k][0]);
                    close(pipes[k][1]);
                }
                return;
            }
            if (pids[i] == 0) // Child process for builtin
            {
                // Set up input redirection
                if (i > 0) {
                    dup2(pipes[i - 1][0], STDIN_FILENO);
                }

                // Set up output redirection
                if (i < pipe_count) {
                    dup2(pipes[i][1], STDOUT_FILENO);
                }

                // Close all pipe file descriptors in child
                for (int k = 0; k < pipe_count; k++) {
                    close(pipes[k][0]);
                    close(pipes[k][1]);
                }

                // Handle file redirection for builtins
                if (input_file)
                {
                    int fd_in = open(input_file, O_RDONLY);
                    if (fd_in < 0)
                    {
                        fprintf(stderr, "No such file or directory: %s\n", input_file);
                        exit(1);
                    }
                    dup2(fd_in, STDIN_FILENO);
                    close(fd_in);
                }

                if (output_file)
                {
                    int flags = O_WRONLY | O_CREAT;
                    if (output_append)
                        flags |= O_APPEND;
                    else
                        flags |= O_TRUNC;
                    int fd_out = open(output_file, flags, 0644);
                    if (fd_out < 0)
                    {
                        fprintf(stderr, "Cannot open or create output file: %s\n", output_file);
                        exit(1);
                    }
                    dup2(fd_out, STDOUT_FILENO);
                    close(fd_out);
                }
                
                execute_builtin(cmd_tokens);
                
                
                for (size_t k = 0; k < cmd_token_count; k++) {
                    if (cmd_tokens[k]) free(cmd_tokens[k]);
                }
                free(cmd_tokens);
                if (input_file) free(input_file);
                if (output_file) free(output_file);
                
                exit(0);
            }
        }
        else
        {
        
            pids[i] = fork();
            if (pids[i] == -1)
            {
                fprintf(stderr, "Fork failed\n");
                
                for (size_t k = 0; k < cmd_token_count; k++) {
                    if (cmd_tokens[k]) free(cmd_tokens[k]);
                }
                free(cmd_tokens);
                if (input_file) free(input_file);
                if (output_file) free(output_file);
                
                // Close all pipes
                for (int k = 0; k < pipe_count; k++) {
                    close(pipes[k][0]);
                    close(pipes[k][1]);
                }
                return;
            }

            if (pids[i] == 0) 
            {
                // Set up input redirection
                if (i > 0) {
                    dup2(pipes[i - 1][0], STDIN_FILENO);
                }

                
                if (i < pipe_count) {
                    dup2(pipes[i][1], STDOUT_FILENO);
                }

                
                for (int k = 0; k < pipe_count; k++) {
                    close(pipes[k][0]);
                    close(pipes[k][1]);
                }

                int status = execute_external(cmd_tokens, cmd_token_count, input_file, output_file, output_append);
                
                
                for (size_t k = 0; k < cmd_token_count; k++) {
                    if (cmd_tokens[k]) free(cmd_tokens[k]);
                }
                free(cmd_tokens);
                if (input_file) free(input_file);
                if (output_file) free(output_file);
                
                exit(status);
            }
        }

    
        for (size_t k = 0; k < cmd_token_count; k++) {
            if (cmd_tokens[k]) free(cmd_tokens[k]);
        }
        free(cmd_tokens);
        if (input_file) free(input_file);
        if (output_file) free(output_file);
        
        cmd_start = j + 1;
    }

    // Close all pipes 
    for (int i = 0; i < pipe_count; i++)
    {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // Wait for all children
    for (int i = 0; i <= pipe_count; i++)
    {
        int status;
        waitpid(pids[i], &status, 0);
    }

   
}