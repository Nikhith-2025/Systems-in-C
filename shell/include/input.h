#ifndef INPUT_H
#define INPUT_H

void init_home(void);
const char* get_home_dir(void);
void print_prompt(void);

extern char home[1024]; // Declare home for external access

#endif