#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <ctype.h>
#include "../include/hop.h"
#include "../include/reveal.h"
#include "../include/command.h"
#define MAX_ARGS 64
#define MAX_JOBS 64

typedef enum {
    JOB_STATE_RUNNING,
    JOB_STATE_STOPPED
} JobState;

typedef struct
{
    pid_t pid;
    char *command;
    char *full_command;
    int job_number;
    JobState state;
} Job;

Job jobs[MAX_JOBS];
int job_count = 0;
pid_t foreground_pid = -1;  
int next_job_num = 1;      

// Forward declarations
int find_job_by_pid(pid_t pid);
int find_job_by_number(int job_num);
void remove_job(int idx);
int get_most_recent_job();

int add_job(pid_t pid, char *args[], JobState state)
{
    if (job_count >= MAX_JOBS) return -1;
    
    jobs[job_count].pid = pid;
    jobs[job_count].state = state;
    jobs[job_count].job_number = next_job_num++;
    
 
    jobs[job_count].command = strdup(args[0] ? args[0] : "(unknown)");
    if (!jobs[job_count].command) {
        perror("malloc failed");
        return -1;
    }
    
    size_t len = 0;
    for (int i = 0; args[i]; i++) {
        len += strlen(args[i]) + 1;
    }
    jobs[job_count].full_command = malloc(len ? len : 1);
    if (!jobs[job_count].full_command) {
        perror("malloc failed");
        free(jobs[job_count].command);
        return -1;
    }
    jobs[job_count].full_command[0] = '\0';
    for (int i = 0; args[i]; i++) {
        strcat(jobs[job_count].full_command, args[i]);
        if (args[i + 1]) strcat(jobs[job_count].full_command, " ");
    }
    
    int job_num = jobs[job_count].job_number;
    job_count++;
    return job_num;
}

void check_background_jobs()
{
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0)
    {
        int idx = find_job_by_pid(pid);
        if (idx < 0) continue;
        
        if (WIFSTOPPED(status)) {
            jobs[idx].state = JOB_STATE_STOPPED;
            printf("\n[%d] Stopped %s\n", jobs[idx].job_number, jobs[idx].command);
        } else if (WIFCONTINUED(status)) {
            jobs[idx].state = JOB_STATE_RUNNING;
        } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                printf("%s with pid %d exited normally\n", jobs[idx].command, pid);
            } else {
                printf("%s with pid %d exited abnormally\n", jobs[idx].command, pid);
            }
            remove_job(idx);
        }
    }
}

int find_job_by_pid(pid_t pid)
{
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].pid == pid) return i;
    }
    return -1;
}

int find_job_by_number(int job_num)
{
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].job_number == job_num) return i;
    }
    return -1;
}

void remove_job(int idx)
{
    if (idx < 0 || idx >= job_count) return;
    free(jobs[idx].command);
    free(jobs[idx].full_command);
    for (int i = idx; i < job_count - 1; i++) {
        jobs[i] = jobs[i + 1];
    }
    job_count--;
}

int get_most_recent_job()
{
    if (job_count == 0) return -1;
    int max_job = jobs[0].job_number;
    int idx = 0;
    for (int i = 1; i < job_count; i++) {
        if (jobs[i].job_number > max_job) {
            max_job = jobs[i].job_number;
            idx = i;
        }
    }
    return idx;
}

int is_builtin(char *cmd)
{
    return cmd && (strcmp(cmd, "hop") == 0 || strcmp(cmd, "reveal") == 0 || 
                   strcmp(cmd, "echo") == 0 || strcmp(cmd, "log") == 0 || 
                   strcmp(cmd, "activities") == 0 || strcmp(cmd, "ping") == 0 ||
                   strcmp(cmd, "fg") == 0 || strcmp(cmd, "bg") == 0);
}

void execute_builtin(char *args[])
{
    char full_cmd[1024] = "";
    for (int i = 0; args[i]; i++)
    {
        strcat(full_cmd, args[i]);
        if (args[i + 1])
            strcat(full_cmd, " ");
    }

    if (strcmp(args[0], "hop") == 0)
    {
        hop(full_cmd);
    }
    else if (strcmp(args[0], "reveal") == 0)
    {
        reveal(full_cmd);
    }
    else if (strcmp(args[0], "echo") == 0)
    {
        for (int i = 1; args[i]; i++) {
            printf("%s", args[i]);
            if (args[i + 1]) printf(" ");
        }
        printf("\n");
    }
    else if (strcmp(args[0], "log") == 0)
    {
        extern char* logging_command(int argc, char* args[]);
        int argc = 0;
        while (args[argc]) argc++;
        char* exec_cmd = logging_command(argc, args);
        if (exec_cmd != NULL) {
            // For log execute, we need to execute the returned command
            if (argc == 3 && strcmp(args[1], "execute") == 0) {
                // Use a static flag to prevent infinite recursion
                static int executing_log_command = 0;
                if (executing_log_command) {
                    printf("Error: recursive log execute detected\n");
                    free(exec_cmd);
                    return;
                }
                
                executing_log_command = 1;
                
                // Parse the command to execute
                char *cmd_args[64];
                int cmd_argc = 0;
                char *exec_cmd_copy = strdup(exec_cmd);
                if (exec_cmd_copy) {
                    char *token = strtok(exec_cmd_copy, " \t");
                    
                    while (token != NULL && cmd_argc < 63) {
                        cmd_args[cmd_argc++] = token;
                        token = strtok(NULL, " \t");
                    }
                    cmd_args[cmd_argc] = NULL;
                    
                    if (cmd_argc > 0) {
                        if (is_builtin(cmd_args[0])) {
                            execute_builtin(cmd_args);
                        } else {
                            pid_t pid = fork();
                            if (pid == 0) {
                                // Child process
                                execvp(cmd_args[0], cmd_args);
                                fprintf(stderr, "Command not found!\n");
                                exit(1);
                            } else if (pid > 0) {
                                int status;
                                waitpid(pid, &status, 0);
                            } else {
                                perror("fork failed");
                            }
                        }
                    }
                    
                    free(exec_cmd_copy);
                }
                
                executing_log_command = 0;
            }
            free(exec_cmd);
        }
    }
    else if (strcmp(args[0], "activities") == 0)
    {
        extern void activities();
        activities();
    }
    else if (strcmp(args[0], "ping") == 0)
    {
        // Handle ping command
        int argc = 0;
        while (args[argc]) argc++;
        if (argc == 3) {
            extern void ping_process(char*, char*);
            ping_process(args[1], args[2]);
        } else {
            printf("Usage: ping <pid> <signal_number>\n");
        }
    }
    else if (strcmp(args[0], "fg") == 0)
    {
        extern void fg_command(char**);
        fg_command(args);
    }
    else if (strcmp(args[0], "bg") == 0)
    {
        extern void bg_command(char**);
        bg_command(args);
    }
}

void execute(char *args[], int is_background)
{
    if (!args[0])
        return;

    if (is_builtin(args[0]))
    {
        if (is_background)
        {
            pid_t pid = fork();
            if (pid == 0)
            { // Child
                // Create new process group
                setpgid(0, 0);
                
                // Redirect stdin to /dev/null for background builtins
                int fd = open("/dev/null", O_RDONLY);
                dup2(fd, STDIN_FILENO);
                close(fd);
                
                // Reset signal handlers
                signal(SIGINT, SIG_DFL);
                signal(SIGTSTP, SIG_DFL);
                
                execute_builtin(args);
                exit(0);
            }
            else if (pid > 0)
            { // Parent
                setpgid(pid, pid); // Set child's process group
                int job_num = add_job(pid, args, JOB_STATE_RUNNING);
                if (job_num > 0) {
                    printf("[%d] %d\n", job_num, pid);
                }
            }
            else
            {
                perror("fork failed");
            }
        }
        else
        {
            execute_builtin(args);
        }
    }
    else
    {
        pid_t pid = fork();
        if (pid == 0)
        { // Child
            setpgid(0, 0);
            
            if (is_background)
            {
                int fd = open("/dev/null", O_RDONLY);
                dup2(fd, STDIN_FILENO);
                close(fd);
            } else {
                if (isatty(STDIN_FILENO)) {
                    tcsetpgrp(STDIN_FILENO, getpgrp());
                }
            }
            
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            
            if (execvp(args[0], args) == -1) {
                printf("Command not found!\n");
                exit(1);
            }
        }
        else if (pid > 0)
        { // Parent
            setpgid(pid, pid);
            
            if (is_background)
            {
                int job_num = add_job(pid, args, JOB_STATE_RUNNING);
                if (job_num > 0) {
                    printf("[%d] %d\n", job_num, pid);
                }
            }
            else
            {
                foreground_pid = pid;  // Set current foreground process
                if (isatty(STDIN_FILENO)) {
                    tcsetpgrp(STDIN_FILENO, pid); // Give child terminal control
                }
                
                int status;
                pid_t result = waitpid(pid, &status, WUNTRACED);  // Also catch stopped processes
                
                // Restore shell terminal control
                if (isatty(STDIN_FILENO)) {
                    tcsetpgrp(STDIN_FILENO, getpgrp());
                }
                
                if (result > 0) {
                    // Check if process was stopped by Ctrl-Z
                    if (WIFSTOPPED(status)) {
                        int job_num = add_job(pid, args, JOB_STATE_STOPPED);
                        if (job_num > 0) {
                            printf("\n[%d] Stopped %s\n", job_num, args[0]);
                        }
                    }
                }
                
                foreground_pid = -1;    // Clear foreground process
            }
        }
        else
        {
            perror("fork failed");
        }
    }
}

void background(char *input)
{
    char *commands[MAX_ARGS];
    int cmd_count = 0;

    // Split by '&' to get separate commands
    char *saveptr;
    char *cmd = strtok_r(input, "&", &saveptr);
    while (cmd && cmd_count < MAX_ARGS - 1) {
        while (*cmd == ' ' || *cmd == '\t') cmd++; // trim leading space
        if (*cmd != '\0')
            commands[cmd_count++] = cmd;
        cmd = strtok_r(NULL, "&", &saveptr);
    }
    commands[cmd_count] = NULL;

    for (int i = 0; i < cmd_count; i++) {
        // Tokenize each command into args
        char *args[MAX_ARGS];
        int j = 0;
        char *tok = strtok(commands[i], " \t");
        while (tok && j < MAX_ARGS - 1) {
            args[j++] = tok;
            tok = strtok(NULL, " \t");
        }
        args[j] = NULL;

        if (!args[0]) continue;

        // Since we're in the background() function, this command should be backgrounded
        // unless it's the last command and there were multiple commands separated by &
        int is_background = 1;
        execute(args, is_background);
    }
}

// fg command: bring job to foreground
void fg_command(char *args[])
{
    int job_idx = -1;
    
    if (args[1]) {
        // Job number provided
        int job_num = atoi(args[1]);
        job_idx = find_job_by_number(job_num);
    } else {
        // Use most recent job
        job_idx = get_most_recent_job();
    }
    
    if (job_idx < 0) {
        printf("No such job\n");
        return;
    }
    
    Job *job = &jobs[job_idx];
    printf("%s\n", job->full_command);
    
    // Set as foreground process
    foreground_pid = job->pid;
    tcsetpgrp(STDIN_FILENO, job->pid);
    
    // If job is stopped, send SIGCONT
    if (job->state == JOB_STATE_STOPPED) {
        kill(-job->pid, SIGCONT); // Send to process group
    }
    
    job->state = JOB_STATE_RUNNING;
    
    // Wait for job to complete or stop
    int status;
    pid_t result = waitpid(job->pid, &status, WUNTRACED);
    
    // Restore shell terminal control
    tcsetpgrp(STDIN_FILENO, getpgrp());
    foreground_pid = -1;
    
    if (result > 0) {
        if (WIFSTOPPED(status)) {
            job->state = JOB_STATE_STOPPED;
            printf("\n[%d] Stopped %s\n", job->job_number, job->command);
        } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                printf("%s with pid %d exited normally\n", job->command, job->pid);
            } else {
                printf("%s with pid %d exited abnormally\n", job->command, job->pid);
            }
            remove_job(job_idx);
        }
    }
}

void bg_command(char *args[])
{
    int job_idx = -1;
    
    if (args[1]) {
        // Job number provided
        int job_num = atoi(args[1]);
        job_idx = find_job_by_number(job_num);
    } else {
        // Use most recent job
        job_idx = get_most_recent_job();
    }
    
    if (job_idx < 0) {
        printf("No such job\n");
        return;
    }
    
    Job *job = &jobs[job_idx];
    
    if (job->state == JOB_STATE_RUNNING) {
        printf("Job already running\n");
        return;
    }
    
    // Send SIGCONT to resume the job
    kill(-job->pid, SIGCONT); // Send to process group
    job->state = JOB_STATE_RUNNING;
    
    printf("[%d] %s &\n", job->job_number, job->command);
}

void kill_all_jobs()
{
    for (int i = 0; i < job_count; i++) {
        kill(jobs[i].pid, SIGKILL);
    }
}

// Remove background operator from command string
char* remove_background_operator(const char *input)
{
    if (!input) return NULL;
    
    char *result = strdup(input);
    if (!result) return NULL;
    
    // Find standalone & (not &&)
    char *amp = strchr(result, '&');
    while (amp) {
        if (amp > result && *(amp - 1) == '&') {
            // This is part of &&, skip
            amp = strchr(amp + 1, '&');
            continue;
        }
        if (*(amp + 1) == '&') {
            // This is part of &&, skip
            amp = strchr(amp + 2, '&');
            continue;
        }
        // Found standalone &, remove it and everything after
        *amp = '\0';
        // Trim trailing whitespace
        while (amp > result && (*(amp-1) == ' ' || *(amp-1) == '\t')) {
            *(--amp) = '\0';
        }
        break;
    }
    
    return result;
}

// Process command with job control integration
void process_command_with_jobs(const char *input, ParsedCommand *cmd, char *args[], int argc,
                              char *in_file, char *out_file, int append, int pipe_count)
{
    (void)in_file; (void)out_file; (void)append; // Suppress unused parameter warnings
    
    // For now, just execute the command normally
    if (pipe_count > 0) {
        char *input_copy = strdup(input ? input : "");
        execute_pipeline(cmd, input_copy, pipe_count);
        free(input_copy);
    } else {
        execute_command(args, in_file, out_file, append);
    }
}