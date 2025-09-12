#include "../include/log.h"
#include "../include/input.h" 
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define HISTORY_FILE "log.txt"
#define MAX_HISTORY 15
#define MAX_LINE 4096

static void get_history_path(char *path, size_t size)
{
    snprintf(path, size, "%s/src/%s", get_home_dir(), HISTORY_FILE);
}

static int read_history(char lines[MAX_HISTORY][MAX_LINE], char **last_cmd)
{
    char history_path[MAX_LINE];
    get_history_path(history_path, sizeof(history_path));
    int count = 0;
    int fd = open(history_path, O_RDONLY);
    if (fd >= 0)
    {
        char buffer[MAX_LINE];
        int pos = 0;
        while (count < MAX_HISTORY)
        {
            ssize_t bytes = read(fd, &buffer[pos], 1);
            if (bytes <= 0)
                break;
            if (buffer[pos] == '\n' || pos == MAX_LINE - 1)
            {
                buffer[pos] = '\0';
                strncpy(lines[count], buffer, MAX_LINE - 1);
                lines[count][MAX_LINE - 1] = '\0';
                count++;
                pos = 0;
            }
            else
            {
                pos++;
            }
        }
        if (count > 0)
            *last_cmd = lines[count - 1];
        close(fd);
    }
    return count;
}

static void write_history(char lines[MAX_HISTORY][MAX_LINE], int count)
{
    char history_path[MAX_LINE];
    get_history_path(history_path, sizeof(history_path));
    int fd = open(history_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        for (int i = 0; i < count; i++)
        {
            write(fd, lines[i], strlen(lines[i]));
            write(fd, "\n", 1);
        }
        close(fd);
    }
}

// logging stores the input into log.txt
void logging(const char *command)
{
    // dont store the input having command in it
    if (strncmp(command, "log", 3) == 0)
    {
        return;
    }

    char lines[MAX_HISTORY][MAX_LINE];
    char *last_cmd = NULL;
    int count = read_history(lines, &last_cmd);

    // dont store the duplicates
    if (last_cmd && strcmp(last_cmd, command) == 0)
    {
        return;
    }

    if (count >= MAX_HISTORY)
    {
        for (int i = 1; i < MAX_HISTORY; i++)
        {
            strncpy(lines[i - 1], lines[i], MAX_LINE - 1);
            lines[i - 1][MAX_LINE - 1] = '\0';
        }
        count--;
    }

    strncpy(lines[count], command, MAX_LINE - 1);
    lines[count][MAX_LINE - 1] = '\0';
    count++;

    write_history(lines, count);
}

char *logging_command(int argc, char *argv[])
{
    char history_path[MAX_LINE];
    get_history_path(history_path, sizeof(history_path));

    if (argc == 1)
    {
        // the new command should come  at the last and while using execute we need to retrive from bottom
        char lines[MAX_HISTORY][MAX_LINE];
        char *last_cmd = NULL;
        int count = read_history(lines, &last_cmd);
        for (int i = 0; i < count; i++)
        {
            printf("%s\n", lines[i]);
        }
        return NULL;
    }
    else if (argc == 2 && strcmp(argv[1], "purge") == 0)
    {
        int fd = open(history_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0)
            close(fd);
        return NULL;
    }
    else if (argc == 3 && strcmp(argv[1], "execute") == 0)
    {
        int index = atoi(argv[2]);
        if (index < 1 || index > MAX_HISTORY)
        {
            fprintf(stderr, "Invalid index\n");
            return NULL;
        }
        char lines[MAX_HISTORY][MAX_LINE];
        char *last_cmd = NULL;
        int count = read_history(lines, &last_cmd);
        if (index > count)
        {
            fprintf(stderr, "Invalid index\n");
            return NULL;
        }
        return strdup(lines[count - index]);
    }
    else
    {
        fprintf(stderr, "Invalid Syntax!\n");
        return NULL;
    }
}