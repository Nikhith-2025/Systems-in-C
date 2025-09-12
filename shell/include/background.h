#ifndef BACKGROUND_H
#define BACKGROUND_H

#include <sys/types.h>
#include "../include/piping.h"

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

// External declarations for global variables
extern Job jobs[MAX_JOBS];
extern int job_count;
extern pid_t foreground_pid;

// Function declarations
int add_job(pid_t pid, char *args[], JobState state);
void check_background_jobs();
int find_job_by_pid(pid_t pid);
int find_job_by_number(int job_num);
void remove_job(int idx);
int get_most_recent_job();
int is_builtin(char *cmd);
void execute_builtin(char *args[]);
void execute(char *args[], int is_background);
void fg_command(char *args[]);
void bg_command(char *args[]);
void kill_all_jobs();
void execute_simple_background(char *args[], const char *original_command);
void execute_pipeline_background(ParsedCommand *cmd, char *input, int pipe_count, const char *original_command);
int has_background_operator(const char *input);
char* remove_background_operator(const char *input);
void handle_command_with_background(const char *input, ParsedCommand *cmd, char *args[], int argc, 
                                  char *in_file, char *out_file, int append, int pipe_count);
void process_command_with_jobs(const char *input, ParsedCommand *cmd, char *args[], int argc,
                              char *in_file, char *out_file, int append, int pipe_count);
void show_jobs();
void kill_job(int job_num);
void kill_process(const char *target, int signal_num);
void background(char *input);

#endif