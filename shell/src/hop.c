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
        // Do nothing for . as it is meant to not shift the directory
    }
    else if (strcmp(inp, "..") == 0)
    {
        if (chdir("..") == 0)// if changing to parent directory is possible
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

    if (strlen(input) <= 4)
    {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) == NULL)
        {
            perror("getcwd");
            return;
        }
        extern char home[1024];
        strncpy(home, cwd, sizeof(home) - 1);
        home[sizeof(home) - 1] = '\0';
        strcpy(current_directory, cwd);
        return;
    }

    char *ptr = input + 4;
    while (*ptr && isspace((unsigned char)*ptr))
    {
        ptr++;
    }

    if (!*ptr)
    {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) == NULL)
        {
            perror("getcwd");
            return;
        }
        extern char home[1024];
        strncpy(home, cwd, sizeof(home) - 1);
        home[sizeof(home) - 1] = '\0';
        strcpy(current_directory, cwd);
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