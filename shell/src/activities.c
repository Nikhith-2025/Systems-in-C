#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include "../include/background.h"

static int compare_jobs(const void *a, const void *b)
{
    Job *j1 = (Job *)a;
    Job *j2 = (Job *)b;
    return strcmp(j1->command, j2->command);
}

void activities()
{
    int alive_count = 0;
    Job alive_jobs[MAX_JOBS];
    
    // Check status of each job and keep only alive ones in array
    for (int i = 0; i < job_count; i++)
    {
        int status;
        pid_t result = waitpid(jobs[i].pid, &status, WNOHANG | WUNTRACED);
        
        if (result == 0) {
            // Still running, no state change
            alive_jobs[alive_count++] = jobs[i];
        } else if (result > 0) {
            if (WIFSTOPPED(status)) {
                // Process is stopped
                jobs[i].state = JOB_STATE_STOPPED;
                alive_jobs[alive_count++] = jobs[i];
            } else if (WIFCONTINUED(status)) {
                // Process continues
                jobs[i].state = JOB_STATE_RUNNING;
                alive_jobs[alive_count++] = jobs[i];
            }
            
        } else {
            
            alive_jobs[alive_count++] = jobs[i];
        }
    }
    
    for (int i = 0; i < alive_count; i++)
    {
        jobs[i] = alive_jobs[i];
    }
    job_count = alive_count;
    
    // Sort them
    qsort(alive_jobs, alive_count, sizeof(Job), compare_jobs);
    
    for (int i = 0; i < alive_count; i++)
    {
        const char *state = (alive_jobs[i].state == JOB_STATE_STOPPED) ? "Stopped" : "Running";
        printf("[%d] : %s - %s\n", alive_jobs[i].pid, alive_jobs[i].command, state);
    }
}
