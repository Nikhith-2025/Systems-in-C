#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include "../include/reveal.h"
#include "../include/hop.h"
#include "../include/input.h"
#include <ctype.h>

static void reveal_with_args(char *ptr);// static allows it to not change
void reveal(char *input)
{
    char *ptr;
    
    // Handle the case where input might contain pipes or other operators
    // In this case,only process up to the first operator
    if (input) {
        char *pipe_pos = strchr(input, '|'); // gives the position of pipes
        char *amp_pos = strchr(input, '&');
        char *semi_pos = strchr(input, ';');
        
        // Find the earliest operator position
        char *earliest_op = input + strlen(input);
        if (pipe_pos && pipe_pos < earliest_op) {
            earliest_op = pipe_pos;
        }
        if (amp_pos && amp_pos < earliest_op) {
            earliest_op = amp_pos;
        }
        if (semi_pos && semi_pos < earliest_op) {
            earliest_op = semi_pos;
        }
        
        // Create a clean input that stops at the first operator
        if (earliest_op < input + strlen(input)) {
            size_t clean_len = earliest_op - input;
            char *clean_input = malloc(clean_len + 1);
            strncpy(clean_input, input, clean_len);
            clean_input[clean_len] = '\0';
            
            // Trim trailing spaces
            while (clean_len > 0 && isspace(clean_input[clean_len - 1])) 
            {
                clean_input[--clean_len] = '\0';
            }
            
            if (strlen(clean_input) > 6 && clean_input[6] == ' ') {
                ptr = clean_input + 7;
            } else if (strlen(clean_input) >= 6) {
                ptr = clean_input + 6;
            } else {
                ptr = clean_input;
            }
            
            char temp_args[MAX_DIR_LENGTH];
            strncpy(temp_args, ptr, MAX_DIR_LENGTH - 1);
            temp_args[MAX_DIR_LENGTH - 1] = '\0';
            free(clean_input);
            
            reveal_with_args(temp_args);
            return;
        }
    }
    
    // this is for reveal without any operators
    if (input && strlen(input) > 6 && input[6] == ' ') {
        ptr = input + 7;  // Skip "reveal "
    } else if (input && strlen(input) >= 6) {
        ptr = input + 6;  
    } else {
        ptr = input ? input : ""; 
    }
    
    reveal_with_args(ptr);
}

static void reveal_with_args(char *ptr)
{
    int show_hidden = 0;        // Flag for -a (hidden files)
    int line_format = 0;        // Flag for -l (l format)
    char target_dir[MAX_DIR_LENGTH] = ""; // Directory to show
    char cwd[MAX_DIR_LENGTH] = "";        // Current working directory to keep track

    if (getcwd(cwd, sizeof(cwd)) == NULL)
    {
        printf("No such directory!\n");
        return;
    }

    if (strlen(current_directory) == 0 || strcmp(cwd, current_directory) != 0)
    {
        strncpy(current_directory, cwd, MAX_DIR_LENGTH - 1);
        current_directory[MAX_DIR_LENGTH - 1] = '\0'; 
    }

    // this function is used to skip spaces
    while (*ptr && isspace((unsigned char)*ptr))
    {
        ptr++;
    }

    // if no argument is given then cwd
    if (!*ptr)
    {
        strncpy(target_dir, cwd, MAX_DIR_LENGTH - 1);
        target_dir[MAX_DIR_LENGTH - 1] = '\0';
    }
    else
    {
        char *arg_start = NULL;
        
        while (*ptr == '-')
        {
            ptr++; 
            
            // Check if only just "-" (previous directory )
            if (*ptr == ' ' || *ptr == '\0')
            {
                arg_start = ptr - 1; 
                break;
            }
            
            while (*ptr && *ptr != ' ' && *ptr != '-')
            {
                if (*ptr == 'a')
                {
                    show_hidden = 1;
                }
                else if (*ptr == 'l')
                {
                    line_format = 1;
                }
                else
                {
                    printf("reveal: Invalid Syntax!\n");
                    return;
                }
                ptr++;
            }
            
            while (*ptr && isspace((unsigned char)*ptr))
            {
                ptr++;
            }
        }

        // If "-" not found argument
        if (arg_start == NULL && *ptr)
        {
            arg_start = ptr;
        }

        if (arg_start != NULL)
        {
            char *arg_end = arg_start;
            while (*arg_end && !isspace((unsigned char)*arg_end))
            {
                arg_end++;
            }
            
            char *temp_ptr = arg_end;
            while (*temp_ptr && isspace((unsigned char)*temp_ptr))
            {
                temp_ptr++;
            }
            if (*temp_ptr) 
            {
                printf("reveal: Invalid Syntax!\n");
                return;
            }
            
            int arg_len = arg_end - arg_start;
            char arg[MAX_DIR_LENGTH] = {0};
            if (arg_len >= MAX_DIR_LENGTH)
            {
                printf("reveal: Invalid Syntax!\n");
                return;
            }
            strncpy(arg, arg_start, arg_len);
            
            if (strcmp(arg, "~") == 0)
            {
                const char *shell_home = get_home_dir();
                if (shell_home) {
                    strncpy(target_dir, shell_home, MAX_DIR_LENGTH - 1);
                    target_dir[MAX_DIR_LENGTH - 1] = '\0';
                } else {
                    strncpy(target_dir, cwd, MAX_DIR_LENGTH - 1);
                    target_dir[MAX_DIR_LENGTH - 1] = '\0';
                }
            }
            else if (strcmp(arg, ".") == 0)
            {
                strncpy(target_dir, cwd, MAX_DIR_LENGTH - 1);
                target_dir[MAX_DIR_LENGTH - 1] = '\0';
            }
            else if (strcmp(arg, "..") == 0)
            {
                strncpy(target_dir, cwd, MAX_DIR_LENGTH - 1);
                target_dir[MAX_DIR_LENGTH - 1] = '\0';
                char *slash = strrchr(target_dir, '/');
                if (slash && slash != target_dir)
                {
                    *slash = '\0';
                }
                else
                {
                    strcpy(target_dir, "/");
                }
            }
            else if (strcmp(arg, "-") == 0)
            {
                if (strlen(prev_directory) == 0)
                {
                    printf("No such directory!\n");
                    return;
                }
                strncpy(target_dir, prev_directory, MAX_DIR_LENGTH - 1);
                target_dir[MAX_DIR_LENGTH - 1] = '\0';
            }
            else
            {
                if (arg[0] == '/')
                {
                    strncpy(target_dir, arg, MAX_DIR_LENGTH - 1);
                    target_dir[MAX_DIR_LENGTH - 1] = '\0';
                }
                else
                {
                    strncpy(target_dir, cwd, MAX_DIR_LENGTH - 1);
                    target_dir[MAX_DIR_LENGTH - 1] = '\0';
                    if (target_dir[strlen(target_dir) - 1] != '/')
                    {
                        strncat(target_dir, "/", MAX_DIR_LENGTH - strlen(target_dir) - 1);
                    }
                    strncat(target_dir, arg, MAX_DIR_LENGTH - strlen(target_dir) - 1);
                }
            }
        }
        else
        {
            strncpy(target_dir, cwd, MAX_DIR_LENGTH - 1);
            target_dir[MAX_DIR_LENGTH - 1] = '\0';
        }
    }


    DIR *dir = opendir(target_dir);
    if (dir == NULL)
    {
        printf("No such directory!\n");
        return;
    }

    char **entries = NULL;
    int entry_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (!show_hidden && entry->d_name[0] == '.')
        {
            continue;
        }
        entries = realloc(entries, (entry_count + 1) * sizeof(char *));
        entries[entry_count] = strdup(entry->d_name);
        entry_count++;
    }
    closedir(dir);


    // sort them
    for (int i = 0; i < entry_count - 1; i++)
    {
        for (int j = i + 1; j < entry_count; j++)
        {
            if (strcasecmp(entries[i], entries[j]) > 0)
            {
                char *temp = entries[i];
                entries[i] = entries[j];
                entries[j] = temp;
            }
        }
    }

    if (entry_count > 0)
    {
        if (line_format)
        {
            for (int i = 0; i < entry_count; i++)
            {
                printf("%s\n", entries[i]);
            }
        }
        else
        {
            for (int i = 0; i < entry_count; i++)
            {
                printf("%s", entries[i]);
                if (i < entry_count - 1)
                {
                    printf(" ");
                }
            }
            printf("\n");
        }
    }

    for (int i = 0; i < entry_count; i++)
    {
        free(entries[i]);
    }
    free(entries);
}
