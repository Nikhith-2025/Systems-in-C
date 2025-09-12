#ifndef SHAM
#define SHAM

#include <stdint.h>

typedef struct {
    uint32_t seq_num;     // Sequence Number (4 bytes)
    uint32_t ack_num;     // Acknowledgment Number (4 bytes)
    uint16_t flags;       // Control flags (SYN, ACK, FIN) (2 bytes)
    uint16_t window_size; // Flow control window size (2 bytes)
} sham;

#endif