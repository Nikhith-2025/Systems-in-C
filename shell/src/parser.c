#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "../include/parser.h"

typedef struct {
    char** tokens;
    size_t count;
    size_t pos;
} TokenList;

static void tokenize(TokenList* list, char* input);
static int parse_shell_cmd_tokens(TokenList* list);
static int parse_cmd_group(TokenList* list);
static int parse_atomic(TokenList* list);
static int parse_input(TokenList* list);
static int parse_output(TokenList* list);
static int is_name(const char* token);
static void free_tokens(TokenList* list);

bool parse_shell_cmd(char* input) {
    if (!input) return false;
    
    TokenList list = {NULL, 0, 0};
    tokenize(&list, input);
    
    int result = parse_shell_cmd_tokens(&list);
    
    // Must consume all tokens for valid parse
    if (result && list.pos != list.count) {
        result = 0;
    }
    
    free_tokens(&list);
    return result;
}

static void tokenize(TokenList* list, char* input) {
    list->tokens = NULL;
    list->count = 0;
    list->pos = 0;

    if (!input) return;
    
    char* input_copy = strdup(input);
    if (!input_copy) return;

    char* ptr = input_copy;
    // Skip leading whitespace
    while (*ptr && isspace((unsigned char)*ptr)) ptr++;

    while (*ptr) {
        char* start = ptr;
        
        // Handle single-character operators
        if (*ptr == '|' || *ptr == '&' || *ptr == ';' || *ptr == '<') {
            list->tokens = realloc(list->tokens, (list->count + 1) * sizeof(char*));
            if (!list->tokens) { free(input_copy); return; }
            list->tokens[list->count] = strndup(ptr, 1);
            if (!list->tokens[list->count]) { free(input_copy); return; }
            list->count++;
            ptr++;
        }
        // Handle > and >> operators
        else if (*ptr == '>') {
            size_t len = (*(ptr + 1) == '>') ? 2 : 1;
            list->tokens = realloc(list->tokens, (list->count + 1) * sizeof(char*));
            if (!list->tokens) { free(input_copy); return; }
            list->tokens[list->count] = strndup(ptr, len);
            if (!list->tokens[list->count]) { free(input_copy); return; }
            list->count++;
            ptr += len;
        }
        // Handle regular tokens (names)
        else {
            // Find end of token (stop at whitespace or special characters)
            while (*ptr && !isspace((unsigned char)*ptr) && 
                   *ptr != '|' && *ptr != '&' && *ptr != ';' && 
                   *ptr != '<' && *ptr != '>') {
                ptr++;
            }
            
            if (ptr > start) {
                list->tokens = realloc(list->tokens, (list->count + 1) * sizeof(char*));
                if (!list->tokens) { free(input_copy); return; }
                list->tokens[list->count] = strndup(start, ptr - start);
                if (!list->tokens[list->count]) { free(input_copy); return; }
                list->count++;
            }
        }
        
        // Skip whitespace
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
    }
    
    free(input_copy);
}

static void free_tokens(TokenList* list) {
    if (list->tokens) {
        for (size_t i = 0; i < list->count; i++) {
            free(list->tokens[i]);
        }
        free(list->tokens);
        list->tokens = NULL;
    }
    list->count = 0;
    list->pos = 0;
}

static int is_name(const char* token) {
    if (!token || !*token) return 0;
    
    // Check for shell special characters
    for (size_t i = 0; token[i]; i++) {
        if (token[i] == '|' || token[i] == '&' || token[i] == ';' || 
            token[i] == '<' || token[i] == '>') {
            return 0;
        }
    }
    return 1;
}

static int parse_input(TokenList* list) 
{
    if (list->pos >= list->count || strcmp(list->tokens[list->pos], "<") != 0) {
        return 0;  // Not an input redirection
    }
    
    list->pos++;  // Consume '<'
    
    if (list->pos >= list->count || !is_name(list->tokens[list->pos])) {
        return -1;  // Error: incomplete input redirection
    }
    
    list->pos++;  // Consume filename
    return 1;     // Success
}

static int parse_output(TokenList* list) {
    if (list->pos >= list->count || 
        (strcmp(list->tokens[list->pos], ">") != 0 && 
         strcmp(list->tokens[list->pos], ">>") != 0)) {
        return 0;  // Not an output redirection
    }
    
    list->pos++;  // Consume '>' or '>>'
    
    if (list->pos >= list->count || !is_name(list->tokens[list->pos])) {
        return -1;  // Error: incomplete output redirection
    }
    
    list->pos++;  // Consume filename
    return 1;     // Success
}

static int parse_atomic(TokenList* list) {
    // atomic -> name (name | input | output)*
    
    // Must start with a name (command)
    if (list->pos >= list->count || !is_name(list->tokens[list->pos])) {
        return 0;
    }
    
    list->pos++;  // Consume command name
    
    // Parse optional arguments and redirections
    while (list->pos < list->count) {
        int result = 0;
        
        // Try to parse as a name (argument)
        if (is_name(list->tokens[list->pos])) {
            list->pos++;
            result = 1;
        }
        // Try to parse as input redirection
        else {
            int input_result = parse_input(list);
            int output_result = parse_output(list);
            
            if (input_result == -1 || output_result == -1) {
                return 0;  // Error in redirection
            }
            
            if (input_result == 1 || output_result == 1) {
                result = 1;
            }
        }
        
        if (!result) {
            break;  // Stop when we can't parse more
        }
    }
    
    return 1;
}

static int parse_cmd_group(TokenList* list) {
    // cmd_group -> atomic (\| atomic)*
    
    if (!parse_atomic(list)) {
        return 0;
    }
    
    // Handle pipeline: (\| atomic)*
    while (list->pos < list->count && strcmp(list->tokens[list->pos], "|") == 0) {
        list->pos++;  // Consume '|'
        
        if (!parse_atomic(list)) {
            return 0;  // Error: missing command after pipe
        }
    }
    
    return 1;
}

static int parse_shell_cmd_tokens(TokenList* list) {
    // shell_cmd -> cmd_group ((& | ;) cmd_group)* &?
    
    if (list->pos >= list->count) {
        return 1;  // Empty input is valid
    }
    
    // Parse first cmd_group
    if (!parse_cmd_group(list)) {
        return 0;
    }
    
    // Parse ((& | ;) cmd_group)*
    while (list->pos < list->count) {
        if (strcmp(list->tokens[list->pos], ";") == 0) {
            list->pos++;  // Consume ';'
            
            if (!parse_cmd_group(list)) {
                return 0;  // Error: missing command after semicolon
            }
        }
        else if (strcmp(list->tokens[list->pos], "&") == 0) {
            // Check if this is the final optional '&'
            if (list->pos + 1 == list->count) {
                list->pos++;  // Consume final '&'
                return 1;     // Success: trailing & is allowed
            }
            
            // Otherwise, must be followed by another cmd_group
            list->pos++;  // Consume '&'
            
            if (!parse_cmd_group(list)) {
                return 0;  // Error: missing command after ampersand
            }
        }
        else {
            break;  // No more separators
        }
    }
    
    return 1;
}

bool is_valid_name(const char *token) {
    return is_name(token);
}

bool is_valid_input(const char *token) {
    if (!token || strlen(token) < 2 || token[0] != '<') return false;
    return is_name(token + 1);
}

bool is_valid_output(const char *token) 
{
    if (!token || strlen(token) < 2) return false;
    if (token[0] == '>') {
        if (token[1] == '>') {
            if (strlen(token) < 3) return false;
            return is_name(token + 2);
        }
        return is_name(token + 1);
    }
    return false;
}

bool is_valid_atomic(char **tokens, int start, int end) 
{
    return true;
}

bool is_valid_cmd_group(char *cmd_group) 
{
    return true;
}