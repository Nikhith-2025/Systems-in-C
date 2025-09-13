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

// Static function declaration
static void reveal_with_args(char *ptr);
void reveal(char *input)
{
    char *ptr;
    
    // Handle the case where input might contain pipes or other operators
    // In this case, we should only process up to the first operator
    if (input) {
        char *pipe_pos = strchr(input, '|');
        char *amp_pos = strchr(input, '&');
        char *semi_pos = strchr(input, ';');
        
        // Find the earliest operator position
        char *earliest_op = input + strlen(input); // Default to end
        if (pipe_pos && pipe_pos < earliest_op) earliest_op = pipe_pos;
        if (amp_pos && amp_pos < earliest_op) earliest_op = amp_pos;
        if (semi_pos && semi_pos < earliest_op) earliest_op = semi_pos;
        
        // Create a clean input that stops at the first operator
        if (earliest_op < input + strlen(input)) {
            size_t clean_len = earliest_op - input;
            char *clean_input = malloc(clean_len + 1);
            strncpy(clean_input, input, clean_len);
            clean_input[clean_len] = '\0';
            
            // Trim trailing spaces
            while (clean_len > 0 && isspace(clean_input[clean_len - 1])) {
                clean_input[--clean_len] = '\0';
            }
            
            // Now process the clean input
            if (strlen(clean_input) > 6 && clean_input[6] == ' ') {
                ptr = clean_input + 7;
            } else if (strlen(clean_input) >= 6) {
                ptr = clean_input + 6;
            } else {
                ptr = clean_input;
            }
            
            // We need to copy ptr content before freeing clean_input
            char temp_args[MAX_DIR_LENGTH];
            strncpy(temp_args, ptr, MAX_DIR_LENGTH - 1);
            temp_args[MAX_DIR_LENGTH - 1] = '\0';
            free(clean_input);
            
            // Use temp_args instead of ptr for the rest of the function
            reveal_with_args(temp_args);
            return;
        }
    }
    
    // Normal case without operators
    if (input && strlen(input) > 6 && input[6] == ' ') {
        ptr = input + 7;  // Skip "reveal "
    } else if (input && strlen(input) >= 6) {
        ptr = input + 6;  // Just "reveal", no space or at end
    } else {
        ptr = input ? input : "";  // Fallback
    }
    
    reveal_with_args(ptr);
}

static void reveal_with_args(char *ptr)
{
    int show_hidden = 0;        // Flag for -a (show hidden files)
    int line_format = 0;        // Flag for -l (line-by-line format)
    char target_dir[MAX_DIR_LENGTH] = ""; // Directory to reveal
    char cwd[MAX_DIR_LENGTH] = "";        // Current working directory for reference

    // Get current working directory
    if (getcwd(cwd, sizeof(cwd)) == NULL)
    {
        printf("No such directory!\n");
        return;
    }

    // Synchronize current_directory with actual CWD if needed
    if (strlen(current_directory) == 0 || strcmp(cwd, current_directory) != 0)
    {
        strncpy(current_directory, cwd, MAX_DIR_LENGTH - 1);
        current_directory[MAX_DIR_LENGTH - 1] = '\0'; // Ensure null-termination
    }

    // Skip spaces
    while (*ptr && isspace((unsigned char)*ptr))
    {
        ptr++;
    }

    // No args → use cwd
    if (!*ptr)
    {
        strncpy(target_dir, current_directory, MAX_DIR_LENGTH - 1);
        target_dir[MAX_DIR_LENGTH - 1] = '\0';
    }
    else
    {
        // Parse flags and arguments
        char *arg_start = NULL;
        
        while (*ptr == '-')
        {
            ptr++; // Skip the '-'
            
            // Check if this is just "-" (previous directory argument)
            if (*ptr == ' ' || *ptr == '\0')
            {
                arg_start = ptr - 1; // Point to the '-'
                break;
            }
            
            // Parse flag characters
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
            
            // Skip spaces after flags
            while (*ptr && isspace((unsigned char)*ptr))
            {
                ptr++;
            }
        }

        // If we didn't find a "-" argument, check for regular argument
        if (arg_start == NULL && *ptr)
        {
            arg_start = ptr;
        }

        // Process the argument if found
        if (arg_start != NULL)
        {
            // Find the end of the argument
            char *arg_end = arg_start;
            while (*arg_end && !isspace((unsigned char)*arg_end))
            {
                arg_end++;
            }
            
            // Check if there are more arguments after this one
            char *temp_ptr = arg_end;
            while (*temp_ptr && isspace((unsigned char)*temp_ptr))
            {
                temp_ptr++;
            }
            if (*temp_ptr) // More arguments found
            {
                printf("reveal: Invalid Syntax!\n");
                return;
            }
            
            // Extract the argument
            int arg_len = arg_end - arg_start;
            char arg[MAX_DIR_LENGTH] = {0};
            if (arg_len >= MAX_DIR_LENGTH)
            {
                printf("reveal: Invalid Syntax!\n");
                return;
            }
            strncpy(arg, arg_start, arg_len);
            
            // Use hop's directory resolution function
            if (resolve_directory_path(arg, target_dir, MAX_DIR_LENGTH) != 0)
            {
                printf("No such directory!\n");
                return;
            }
        }
        else
        {
            // No directory argument, use current directory
            strncpy(target_dir, cwd, MAX_DIR_LENGTH - 1);
            target_dir[MAX_DIR_LENGTH - 1] = '\0';
        }
    }

    // Check if directory exists
    DIR *dir = opendir(target_dir);
    if (dir == NULL)
    {
        printf("No such directory!\n");
        return;
    }

    // Note: We don't update current_directory/prev_directory since hop manages them

    // Collect names
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

    // Sort ASCII lexicographic order
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

    // Print
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

    // Cleanup
    for (int i = 0; i < entry_count; i++)
    {
        free(entries[i]);
    }
    free(entries);
}
