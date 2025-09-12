#ifndef HOP_H
#define HOP_H

#define MAX_DIR_LENGTH 1024

extern char home[1024]; // Declare home as extern for access in hop.c
extern char current_directory[1024];
extern char prev_directory[1024];

void hop(char *input);
void hop_directory(const char *inp);

#endif
