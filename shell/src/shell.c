#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <termios.h>
#include "../include/input.h"
#include "../include/parser.h"
#include "../include/hop.h"
#include "../include/reveal.h"
#include "../include/command.h"
#include "../include/log.h"
#include "../include/background.h"
#include "../include/activities.h"
#include "../include/ping.h"


extern pid_t foreground_pid;// this is to tell whether a process is running in background or not
extern void kill_all_jobs();
extern void fg_command(char *args[]);
extern void bg_command(char *args[]);

static pid_t shell_pgid;

// Signal
void sigint_handler(int sig)
{
    (void)sig; // Suppress unused parameter warning
    
    // Handle Ctrl-C: send SIGINT to foreground process group if one exists
    if (foreground_pid > 0) {
        printf("\n"); 
        kill(-foreground_pid, SIGINT);  // Send to process group (negative PID)
        foreground_pid = -1; // Reset foreground PID
    } else {
        // No foreground process, just print new prompt
        printf("\n");
        print_prompt();
        fflush(stdout);
    }
}

void sigtstp_handler(int sig)
{
    (void)sig; // Suppress unused parameter warning
    
    // Handle Ctrl-Z: send SIGTSTP to foreground process group if one exists
    if (foreground_pid > 0) {
        printf("\n"); // Move to new line
        kill(-foreground_pid, SIGTSTP); // Send to process group (negative PID)
        // Don't reset foreground_pid here - let the main waitpid handle it
    } else {
        // No foreground process, just print new prompt
        printf("\n");
        print_prompt();
        fflush(stdout);
    }
}
void setup_signal_handlers()
{
    // Get shell's process group ID
    shell_pgid = getpgrp();
    
    // Install signal handlers for the shell
    struct sigaction sa_int, sa_tstp;
    
    // SIGINT handler
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa_int, NULL);
    
    // SIGTSTP handler  
    sa_tstp.sa_handler = sigtstp_handler;
    sigemptyset(&sa_tstp.sa_mask);
    sa_tstp.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &sa_tstp, NULL);
    
    // Ignore these signals for the shell itself
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGQUIT, SIG_IGN); // Ignore Ctrl+\ for shell
}

int has_background_operator(const char *input)
{
    if (!input) return 0;
    
    // If there is any && in the input, it's NOT a background command
    if (strstr(input, "&&") != NULL) 
    {
        return 0;
    }
    
    return strchr(input, '&') != NULL;
}

int main()
{
    char input[1024];
    
    init_home();
    setup_signal_handlers();
    
    // Make sure shell is in its own process group (but be robust about it)
    if (setpgid(0, 0) < 0) {
        // Don't exit - might be in test environment where this fails
        perror("Warning: couldn't put shell in its own process group");
    }
    
    // Take control of the terminal (but be robust about it)
    if (isatty(STDIN_FILENO)) {
        if (tcsetpgrp(STDIN_FILENO, getpgrp()) < 0) {
            // Don't exit if we can't get terminal control - might be in test environment
            perror("Warning: couldn't take terminal control");
        }
    }
    
    while (1)
    {
        // Check for completed background jobs
        check_background_jobs();

        print_prompt();

        if (!fgets(input, sizeof(input), stdin))
        {
            // Ctrl-D (EOF)
            printf("logout\n");
            fflush(stdout);
            kill_all_jobs();  // this kills all the background process
            exit(0);
        }

        input[strcspn(input, "\n")] = '\0';
        
        char *src = input, *dst = input;
        while (*src) {
            if (*src >= 32 || *src == '\t') 
            {
                *dst++ = *src;
            }
            src++;
        }
        *dst = '\0';

        if (strlen(input) == 0)
        {
            continue;
        }

        // Check for & but not &&
        if (has_background_operator(input))
        {
            logging(input);
            background(input);
            continue;
        }

        logging(input);// add the input to the log.txt
        
        // Parse the input command
        if (!parse_shell_cmd(input))
        {
            printf("Invalid Syntax!\n");
            continue;
        }
        
        if (strcmp(input, "activities") == 0)
        {
            activities();// if it is activities then jump to activities function
            continue;
        }
        
        if (strncmp(input, "ping", 4) == 0)
        {
            char *args[10];
            int argc = 0;
            char *token = strtok(input, " ");
            while (token != NULL && argc < 10)
            {
                args[argc++] = token;
                token = strtok(NULL, " ");
            }

            if (argc != 3)
            {
                printf("Usage: ping <pid> <signal_number>\n");
            }
            else
            {
                ping_process(args[1], args[2]);
            }
            continue;
        }
        
        if (strncmp(input, "fg", 2) == 0)
        {
            char *args[10];
            int argc = 0;
            char *token = strtok(input, " ");
            while (token != NULL && argc < 10)
            {
                args[argc++] = token;
                token = strtok(NULL, " ");
            }
            fg_command(args);
            continue;
        }
        
        // Handle bg command  
        if (strncmp(input, "bg", 2) == 0)
        {
            char *args[10];
            int argc = 0;
            char *token = strtok(input, " ");
            while (token != NULL && argc < 10)
            {
                args[argc++] = token;
                token = strtok(NULL, " ");
            }
            bg_command(args);
            continue;
        }
        
        // handles all the input command execution
        command(input);
    }

    return 0;
}