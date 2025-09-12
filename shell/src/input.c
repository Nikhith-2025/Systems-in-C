#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <string.h>
#include "../include/input.h"

char home[1024];

// Called once when the shell starts to set the home directory
void init_home()
{
    if (getcwd(home, sizeof(home)) == NULL)
    {
        perror("getcwd");
        exit(1);
    }
}

const char* get_home_dir()
{
    return home;
}

void print_prompt()
{
    struct passwd *pw = getpwuid(getuid());
    char *username;

    if (pw != NULL)
    {
        username = pw->pw_name;
    }
    else
    {
        username = "user";
    }

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "host");
    }

    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
    {
        perror("getcwd");
        exit(1);
    }

    char display_path[1024];
    if (strncmp(cwd, home, strlen(home)) == 0)
    {
        snprintf(display_path, sizeof(display_path), "~%s", cwd + strlen(home));
    }
    else
    {
        snprintf(display_path, sizeof(display_path), "%s", cwd);
    }
    // Print the final prompt
    printf("<%s@%s:%s> ", username, hostname, display_path);
    fflush(stdout);
}
