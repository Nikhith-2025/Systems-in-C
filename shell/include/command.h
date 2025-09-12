#ifndef COMMAND_H
#define COMMAND_H

#include "piping.h"

// Command parsing and execution functions
void tokenise_input(const char *input, char *args[], int *argc, char **in_file, char **out_file, int *append, int *pipe_count, ParsedCommand *cmd);
void execute_command(char *args[], char *in_file, char *out_file, int append);
void command(const char *input);

#endif // COMMAND_H