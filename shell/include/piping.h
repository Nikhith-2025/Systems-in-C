#ifndef PIPING_H
#define PIPING_H

#include <sys/types.h>

// Structure to hold parsed command information
typedef struct {
    char **tokens;
    size_t count;
} ParsedCommand;

// Pipeline execution functions
void execute_pipeline(ParsedCommand *cmd, char *input, int pipe_count);
int execute_external(char *tokens[], size_t token_count, char *input_file, char *output_file, int output_append);

// Cleanup function
void cleanup(ParsedCommand *cmd, char *input, char **cmd_tokens, char *input_file, char *output_file);

#endif // PIPING_H