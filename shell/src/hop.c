#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include "../include/hop.h"
#include "../include/input.h" // For get_home_dir()

char current_directory[1024] = "";
char prev_directory[1024] = "";

void hop_directory(const char *inp)
{
    char temp_dir[1024];
    if (getcwd(temp_dir, sizeof(temp_dir)) == NULL)
    {
        perror("getcwd");
        return;
    }

    if (strcmp(inp, "~") == 0)
    {
        const char *shell_home = get_home_dir();
        if (shell_home == NULL || strlen(shell_home) == 0)
        {
            return;
        }
        if (chdir(shell_home) == 0)
        {
            if (getcwd(current_directory, sizeof(current_directory)) != NULL)
            {
                strcpy(prev_directory, temp_dir);
            }
        }
        else
        {
            printf("No such directory!\n");
        }
    }
    else if (strcmp(inp, ".") == 0)
    {
        // Do nothing
    }
    else if (strcmp(inp, "..") == 0)
    {
        if (chdir("..") == 0)
        {
            if (getcwd(current_directory, sizeof(current_directory)) != NULL)
            {
                strcpy(prev_directory, temp_dir);
            }
        }
        else
        {
            printf("No such directory!\n");
        }
    }
    else if (strcmp(inp, "-") == 0)
    {
        if (strlen(prev_directory) != 0)
        {
            if (chdir(prev_directory) == 0)
            {
                if (getcwd(current_directory, sizeof(current_directory)) == NULL)
                {
                    perror("getcwd");
                    return;
                }
                strcpy(prev_directory, temp_dir);
            }
            else
            {
                printf("No such directory!\n");
            }
        }
    }
    else
    {
        if (chdir(inp) == 0)
        {
            if (getcwd(current_directory, sizeof(current_directory)) != NULL)
            {
                strcpy(prev_directory, temp_dir);
            }
        }
        else
        {
            printf("No such directory!\n");
        }
    }
}

void hop(char *input)
{
    if (input == NULL || strlen(input) < 3)
    {
        return;
    }

    if (strncmp(input, "hop", 3) != 0 || (strlen(input) > 3 && input[3] != ' ' && input[3] != '\0'))
    {
        return;
    }

    // Check if length is exactly 3 (just "hop") or if there are no arguments after "hop "
    if (strlen(input) == 3 || (strlen(input) == 4 && input[3] == '\n'))
    {
        // Go to shell's home directory
        hop_directory("~");
        return;
    }

    char *ptr = input + 4;
    while (*ptr && isspace((unsigned char)*ptr))
    {
        ptr++;
    }

    if (!*ptr)
    {
        // Go to shell's home directory  
        hop_directory("~");
        return;
    }

    char *token = strtok(ptr, " ");
    while (token != NULL)
    {
        hop_directory(token);
        token = strtok(NULL, " ");
    }

    if (getcwd(current_directory, sizeof(current_directory)) == NULL)
    {
        perror("getcwd");
    }
}

// Helper function to resolve directory path without changing directories
// Returns 0 on success, -1 on failure
int resolve_directory_path(const char *input_path, char *resolved_path, size_t path_size)
{
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
    {
        return -1;
    }

    if (strcmp(input_path, "~") == 0)
    {
        const char *shell_home = get_home_dir();
        if (shell_home == NULL || strlen(shell_home) == 0)
        {
            return -1;
        }
        strncpy(resolved_path, shell_home, path_size - 1);
        resolved_path[path_size - 1] = '\0';
    }
    else if (strcmp(input_path, ".") == 0)
    {
        strncpy(resolved_path, cwd, path_size - 1);
        resolved_path[path_size - 1] = '\0';
    }
    else if (strcmp(input_path, "..") == 0)
    {
        strncpy(resolved_path, cwd, path_size - 1);
        resolved_path[path_size - 1] = '\0';
        char *last_slash = strrchr(resolved_path, '/');
        if (last_slash && last_slash != resolved_path)
        {
            *last_slash = '\0';
        }
        else
        {
            strcpy(resolved_path, "/");
        }
    }
    else if (strcmp(input_path, "-") == 0)
    {
        if (strlen(prev_directory) == 0)
        {
            return -1;  // No previous directory
        }
        strncpy(resolved_path, prev_directory, path_size - 1);
        resolved_path[path_size - 1] = '\0';
    }
    else
    {
        // Regular path - could be relative or absolute
        if (input_path[0] == '/')
        {
            // Absolute path
            strncpy(resolved_path, input_path, path_size - 1);
            resolved_path[path_size - 1] = '\0';
        }
        else
        {
            // Relative path
            strncpy(resolved_path, cwd, path_size - 1);
            resolved_path[path_size - 1] = '\0';
            if (resolved_path[strlen(resolved_path) - 1] != '/')
            {
                strncat(resolved_path, "/", path_size - strlen(resolved_path) - 1);
            }
            strncat(resolved_path, input_path, path_size - strlen(resolved_path) - 1);
        }
    }

    return 0;
}
