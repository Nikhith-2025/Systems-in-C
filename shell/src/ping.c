#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "../include/ping.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>

void ping_process(char *pid_str, char *sig_str)
{
    if (!pid_str || !sig_str)
    {
        printf("Usage: ping <pid> <signal_number>\n");
        return;
    }

    char *endptr;
    long sig_long = strtol(sig_str, &endptr, 10);
    if (*endptr != '\0' || endptr == sig_str) {
        printf("Invalid syntax!\n");
        return;
    }

    pid_t pid = (pid_t)atoi(pid_str);
    int sig = (int)(sig_long % 32);

    if (pid <= 0)
    {
        printf("Invalid PID\n");
        return;
    }

    if (kill(pid, sig) == -1)
    {
        if (errno == ESRCH)
        {
            printf("No such process found\n");
        }
        else
        {
            perror("kill failed");
        }
    }
    else
    {
        printf("Sent signal %d to process with pid %d\n", sig, pid);
    }
}
