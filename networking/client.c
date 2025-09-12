#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include "sham.h"

#define MAX_DATA_SIZE 1024
#define MAX_PACKET_SIZE (sizeof(sham) + MAX_DATA_SIZE)
#define SLIDING_WINDOW 10
#define WINDOW_SIZE 10240 // 10 packets * 1024 bytes
#define RTO_MS 500        // 500ms timeout
#define SYN 0x1
#define ACK 0x2
#define FIN 0x4

typedef struct
{
    sham header;
    char data[MAX_DATA_SIZE];
    size_t data_len;
    struct timeval sent_time;
    int acked;
} packet_t;

char buffer[MAX_PACKET_SIZE];
FILE *log_file = NULL;
packet_t sent_packets[SLIDING_WINDOW];
packet_t recv_buffer[SLIDING_WINDOW];
uint32_t last_byte_acked = 0;
uint32_t last_byte_sent = 0;
uint32_t expected_seq = 1;
uint16_t receiver_window = WINDOW_SIZE;
int sent_packet_count = 0;
int sent_fin = 0;

// File transfer variables
FILE *input_file = NULL;
FILE *output_file = NULL;
int file_transfer_complete = 0;
void log_event(const char *event)
{
    if (getenv("RUDP_LOG") && strcmp(getenv("RUDP_LOG"), "1") == 0)
    {
        if (!log_file)
        {
            log_file = fopen("client_log.txt", "w");
            if (!log_file)
            {
                perror("Failed to open client_log.txt");
                return;
            }
        }
        char time_buffer[30];
        struct timeval tv;
        time_t curtime;
        gettimeofday(&tv, NULL);
        curtime = tv.tv_sec;
        strftime(time_buffer, 30, "%Y-%m-%d %H:%M:%S", localtime(&curtime));
        fprintf(log_file, "[%s.%06ld] [LOG] %s\n", time_buffer, tv.tv_usec, event);
        fflush(log_file);
    }
}
int three_way_handshake(int client_socket, struct sockaddr_in *server_addr)
{
    sham packet = {0};
    packet.seq_num = htonl(0);
    packet.ack_num = htonl(0);
    packet.flags = htons(SYN);
    packet.window_size = htons(WINDOW_SIZE);

    char log_msg[128];
    snprintf(log_msg, sizeof(log_msg), "SND SYN SEQ=%u", ntohl(packet.seq_num));
    log_event(log_msg);

    if (sendto(client_socket, &packet, sizeof(packet), 0, (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0)
    {
        perror("sendto failed");
        return 0;
    }

    ssize_t bytes_received = recvfrom(client_socket, buffer, MAX_PACKET_SIZE, 0, NULL, NULL);
    if (bytes_received < 0)
    {
        perror("recvfrom failed");
        return 0;
    }
    memcpy(&packet, buffer, sizeof(sham));
    snprintf(log_msg, sizeof(log_msg), "RCV SYN-ACK SEQ=%u ACK=%u", ntohl(packet.seq_num), ntohl(packet.ack_num));
    log_event(log_msg);

    if (ntohs(packet.flags) == (SYN | ACK))
    {
        sham ack_packet = {0};
        ack_packet.seq_num = htonl(1);
        ack_packet.ack_num = htonl(ntohl(packet.seq_num) + 1);
        ack_packet.flags = htons(ACK);
        ack_packet.window_size = htons(WINDOW_SIZE);

        snprintf(log_msg, sizeof(log_msg), "SND ACK=%u WIN=%u", ntohl(ack_packet.ack_num), ntohs(ack_packet.window_size));
        log_event(log_msg);

        if (sendto(client_socket, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0)
        {
            perror("sendto failed");
            return 0;
        }
        return 1;
    }
    return 0;
}

void send_packet(int client_socket, struct sockaddr_in *server_addr, socklen_t addr_len, packet_t *packet)
{
    memcpy(buffer, &packet->header, sizeof(sham));
    memcpy(buffer + sizeof(sham), packet->data, packet->data_len);
    char log_msg[128];
    if (ntohs(packet->header.flags) & FIN)
    {
        snprintf(log_msg, sizeof(log_msg), "SND FIN SEQ=%u", ntohl(packet->header.seq_num));
    }
    else
    {
        snprintf(log_msg, sizeof(log_msg), "SND DATA SEQ=%u LEN=%zu", ntohl(packet->header.seq_num), packet->data_len);
    }
    log_event(log_msg);
    if (sendto(client_socket, buffer, sizeof(sham) + packet->data_len, 0, (struct sockaddr *)server_addr, addr_len) < 0)
    {
        perror("sendto failed");
    }
    else
    {
        gettimeofday(&packet->sent_time, NULL);
    }
}

void retransmit_packets(int client_socket, struct sockaddr_in *server_addr, socklen_t addr_len)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    for (int i = 0; i < sent_packet_count; i++)
    {
        if (!sent_packets[i].acked)
        {
            long elapsed_ms = (now.tv_sec - sent_packets[i].sent_time.tv_sec) * 1000 +
                              (now.tv_usec - sent_packets[i].sent_time.tv_usec) / 1000;
            if (elapsed_ms >= RTO_MS)
            {
                char log_msg[128];
                snprintf(log_msg, sizeof(log_msg), "TIMEOUT SEQ=%u", ntohl(sent_packets[i].header.seq_num));
                log_event(log_msg);
                snprintf(log_msg, sizeof(log_msg), "RETX DATA SEQ=%u LEN=%zu", ntohl(sent_packets[i].header.seq_num), sent_packets[i].data_len);
                log_event(log_msg);
                send_packet(client_socket, server_addr, addr_len, &sent_packets[i]);
            }
        }
    }
}

void send_file_data(int client_socket, struct sockaddr_in *server_addr, socklen_t addr_len)
{
    char file_buffer[MAX_DATA_SIZE];

    while (!file_transfer_complete && sent_packet_count < SLIDING_WINDOW)
    {
        size_t bytes_read = fread(file_buffer, 1, MAX_DATA_SIZE, input_file);
        if (bytes_read == 0)
        {
            file_transfer_complete = 1;
            break;
        }

        if (last_byte_sent - last_byte_acked + bytes_read <= receiver_window)
        {
            packet_t data_packet = {0};
            data_packet.header.seq_num = htonl(last_byte_sent + 1);
            data_packet.header.ack_num = htonl(expected_seq);
            data_packet.header.flags = 0;
            data_packet.header.window_size = htons(WINDOW_SIZE);
            memcpy(data_packet.data, file_buffer, bytes_read);
            data_packet.data_len = bytes_read;
            data_packet.acked = 0;
            sent_packets[sent_packet_count++] = data_packet;
            send_packet(client_socket, server_addr, addr_len, &data_packet);
            last_byte_sent += bytes_read;
        }
        else
        {
            // Rewind file pointer to retry later
            fseek(input_file, -bytes_read, SEEK_CUR);
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <server_ip> <server_port> <input_file> <output_file_name> [loss_rate]\n", argv[0]);
        fprintf(stderr, "   or: %s <server_ip> <server_port> --chat [loss_rate]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int chat_mode = 0;
    float loss_rate = 0.0;
    char *input_filename = NULL;
    char *output_filename = NULL;

    // Parse command line arguments
    if (argc >= 4 && strcmp(argv[3], "--chat") == 0)
    {
        chat_mode = 1;
        if (argc == 5)
        {
            loss_rate = atof(argv[4]);
        }
    }
    else if (argc >= 5)
    {
        // File transfer mode
        input_filename = argv[3];
        output_filename = argv[4];
        if (argc == 6)
        {
            loss_rate = atof(argv[5]);
        }
    }
    else
    {
        fprintf(stderr, "Invalid arguments\n");
        exit(EXIT_FAILURE);
    }

    if (loss_rate < 0.0 || loss_rate > 1.0)
    {
        fprintf(stderr, "loss_rate must be between 0.0 and 1.0\n");
        exit(EXIT_FAILURE);
    }
    srand(time(NULL));

    // Open files for file transfer mode
    if (!chat_mode)
    {
        input_file = fopen(input_filename, "rb");
        if (!input_file)
        {
            perror("Failed to open input file");
            exit(EXIT_FAILURE);
        }
        output_file = fopen(output_filename, "wb");
        if (!output_file)
        {
            perror("Failed to create output file");
            fclose(input_file);
            exit(EXIT_FAILURE);
        }
    }

    int client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_socket < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0)
    {
        fprintf(stderr, "Invalid server IP address\n");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    int handshake_result = three_way_handshake(client_socket, &server_addr);
    if (handshake_result != 1)
    {
        close(client_socket);
        return 1;
    }

    // Send filename to server in file transfer mode
    if (!chat_mode && output_filename) {
        packet_t filename_packet = {0};
        filename_packet.header.seq_num = htonl(last_byte_sent + 1);
        filename_packet.header.ack_num = htonl(expected_seq);
        filename_packet.header.flags = 0;
        filename_packet.header.window_size = htons(WINDOW_SIZE);
        
        // Add a special marker to indicate this is a filename packet
        snprintf(filename_packet.data, MAX_DATA_SIZE, "FILENAME:%s", output_filename);
        filename_packet.data_len = strlen(filename_packet.data) + 1;
        filename_packet.acked = 0;
        
        char log_msg[128];
        snprintf(log_msg, sizeof(log_msg), "SENDING FILENAME: %s", output_filename);
        log_event(log_msg);
        
        send_packet(client_socket, &server_addr, sizeof(server_addr), &filename_packet);
        last_byte_sent += filename_packet.data_len;
        sent_packets[sent_packet_count++] = filename_packet;
        
        // Wait for ACK of filename before proceeding
        int filename_acked = 0;
        struct timeval start_time;
        gettimeofday(&start_time, NULL);
        
        while (!filename_acked) {
            fd_set temp_fds;
            FD_ZERO(&temp_fds);
            FD_SET(client_socket, &temp_fds);
            struct timeval timeout = {1, 0}; // 1 second timeout
            
            int activity = select(client_socket + 1, &temp_fds, NULL, NULL, &timeout);
            if (activity > 0 && FD_ISSET(client_socket, &temp_fds)) {
                ssize_t bytes_received = recvfrom(client_socket, buffer, MAX_PACKET_SIZE, 0, NULL, NULL);
                if (bytes_received > 0) {
                    sham ack_packet;
                    memcpy(&ack_packet, buffer, sizeof(sham));
                    if ((ntohs(ack_packet.flags) & ACK) && ntohl(ack_packet.ack_num) > ntohl(filename_packet.header.seq_num)) {
                        filename_acked = 1;
                        // Update our tracking variables
                        sent_packets[0].acked = 1;
                        last_byte_acked = ntohl(ack_packet.ack_num) - 1;
                        sent_packet_count = 0; // Clear the filename packet from sent queue
                        snprintf(log_msg, sizeof(log_msg), "FILENAME ACK RECEIVED");
                        log_event(log_msg);
                    }
                }
            } else {
                // Timeout, retransmit filename
                snprintf(log_msg, sizeof(log_msg), "RETRANSMITTING FILENAME");
                log_event(log_msg);
                send_packet(client_socket, &server_addr, sizeof(server_addr), &filename_packet);
            }
        }
    }

    char input_buffer[MAX_DATA_SIZE];
    fd_set read_fds;
    int max_fd = client_socket + 1;

    while (1)
    {
        FD_ZERO(&read_fds);
        if (chat_mode && !sent_fin)
        {
            FD_SET(STDIN_FILENO, &read_fds);
        }
        FD_SET(client_socket, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = RTO_MS * 1000;

        int activity = select(max_fd, &read_fds, NULL, NULL, &timeout);
        if (activity < 0)
        {
            perror("select failed");
            break;
        }
        else if (activity == 0)
        {
            if (!sent_fin)
            {
                retransmit_packets(client_socket, &server_addr, sizeof(server_addr));
                if (!chat_mode && !file_transfer_complete)
                {
                    send_file_data(client_socket, &server_addr, sizeof(server_addr));
                }
            }
            continue;
        }

        // Handle file transfer data sending
        if (!chat_mode && !file_transfer_complete && !sent_fin)
        {
            send_file_data(client_socket, &server_addr, sizeof(server_addr));
        }


        if (chat_mode && FD_ISSET(STDIN_FILENO, &read_fds) && !sent_fin)
        {
            if (fgets(input_buffer, MAX_DATA_SIZE, stdin) == NULL)
            {
                break;
            }
            input_buffer[strcspn(input_buffer, "\n")] = '\0';
            if (strcmp(input_buffer, "/quit") == 0)
            {
                packet_t fin_packet = {0};
                fin_packet.header.seq_num = htonl(last_byte_sent + 1);
                fin_packet.header.ack_num = htonl(expected_seq);
                fin_packet.header.flags = htons(FIN);
                fin_packet.header.window_size = htons(WINDOW_SIZE);
                fin_packet.data_len = 0;
                send_packet(client_socket, &server_addr, sizeof(server_addr), &fin_packet);
                sent_fin = 1;
            }
            else if (sent_packet_count < SLIDING_WINDOW && last_byte_sent - last_byte_acked + strlen(input_buffer) + 1 <= receiver_window)
            {
                packet_t data_packet = {0};
                data_packet.header.seq_num = htonl(last_byte_sent + 1);
                data_packet.header.ack_num = htonl(expected_seq);
                data_packet.header.flags = 0;
                data_packet.header.window_size = htons(WINDOW_SIZE);
                strncpy(data_packet.data, input_buffer, MAX_DATA_SIZE);
                data_packet.data_len = strlen(input_buffer) + 1;
                data_packet.acked = 0;
                sent_packets[sent_packet_count++] = data_packet;
                send_packet(client_socket, &server_addr, sizeof(server_addr), &data_packet);
                last_byte_sent += data_packet.data_len;
            }
        }
        if (FD_ISSET(client_socket, &read_fds))
        {
            ssize_t bytes_received = recvfrom(client_socket, buffer, MAX_PACKET_SIZE, 0, NULL, NULL);
            if (bytes_received < 0)
            {
                perror("recvfrom failed");
                continue;
            }
            if (loss_rate > 0.0 && (rand() / (double)RAND_MAX) < loss_rate && !(ntohs(((sham *)buffer)->flags) & (SYN | FIN)))
            {
                char log_msg[128];
                snprintf(log_msg, sizeof(log_msg), "DROP DATA SEQ=%u", ntohl(((sham *)buffer)->seq_num));
                log_event(log_msg);
                continue;
            }
            sham packet;
            memcpy(&packet, buffer, sizeof(sham));
            char log_msg[128];
            if (ntohs(packet.flags) & FIN)
            {
                snprintf(log_msg, sizeof(log_msg), "RCV FIN SEQ=%u", ntohl(packet.seq_num));
                log_event(log_msg);

                sham ack_packet = {0};
                ack_packet.seq_num = htonl(last_byte_sent + 1);
                ack_packet.ack_num = htonl(ntohl(packet.seq_num) + 1);
                ack_packet.flags = htons(ACK);
                ack_packet.window_size = htons(WINDOW_SIZE);
                snprintf(log_msg, sizeof(log_msg), "SND ACK FOR FIN");
                log_event(log_msg);

                if (sendto(client_socket, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
                {
                    perror("sendto failed");
                    break;
                }

                packet_t fin_packet = {0};
                fin_packet.header.seq_num = htonl(last_byte_sent + 2);
                fin_packet.header.ack_num = htonl(expected_seq);
                fin_packet.header.flags = htons(FIN);
                fin_packet.header.window_size = htons(WINDOW_SIZE);
                fin_packet.data_len = 0;
                send_packet(client_socket, &server_addr, sizeof(server_addr), &fin_packet);

                // Wait for final ACK
                struct timeval ack_timeout;
                fd_set ack_fds;
                FD_ZERO(&ack_fds);
                FD_SET(client_socket, &ack_fds);
                ack_timeout.tv_sec = 2;
                ack_timeout.tv_usec = 0;
                if (select(max_fd, &ack_fds, NULL, NULL, &ack_timeout) > 0)
                {
                    bytes_received = recvfrom(client_socket, buffer, MAX_PACKET_SIZE, 0, NULL, NULL);
                    if (bytes_received > 0)
                    {
                        memcpy(&packet, buffer, sizeof(sham));
                        if (ntohs(packet.flags) & ACK)
                        {
                            snprintf(log_msg, sizeof(log_msg), "RCV ACK=%u", ntohl(packet.ack_num));
                            log_event(log_msg);
                        }
                    }
                }
                break;
            }
            else if (ntohs(packet.flags) & ACK)
            {
                snprintf(log_msg, sizeof(log_msg), "RCV ACK=%u", ntohl(packet.ack_num));
                log_event(log_msg);
                for (int i = 0; i < sent_packet_count; i++)
                {
                    if (!sent_packets[i].acked && ntohl(sent_packets[i].header.seq_num) < ntohl(packet.ack_num))
                    {
                        sent_packets[i].acked = 1;
                        last_byte_acked = ntohl(packet.ack_num) - 1;
                    }
                }
                // Shift window
                int new_count = 0;
                for (int i = 0; i < sent_packet_count; i++)
                {
                    if (!sent_packets[i].acked)
                    {
                        sent_packets[new_count++] = sent_packets[i];
                    }
                }
                sent_packet_count = new_count;
                receiver_window = ntohs(packet.window_size);
                snprintf(log_msg, sizeof(log_msg), "FLOW WIN UPDATE=%u", receiver_window);
                log_event(log_msg);
                
                // Check file transfer completion after ACK processing
                if (!chat_mode) {
                    
                    // After ACK processing, check if we should send FIN
                    if (file_transfer_complete && sent_packet_count == 0 && !sent_fin) {
                        snprintf(log_msg, sizeof(log_msg), "FILE TRANSFER COMPLETE, SENDING FIN");
                        log_event(log_msg);
                        
                        packet_t fin_packet = {0};
                        fin_packet.header.seq_num = htonl(last_byte_sent + 1);
                        fin_packet.header.ack_num = htonl(expected_seq);
                        fin_packet.header.flags = htons(FIN);
                        fin_packet.header.window_size = htons(WINDOW_SIZE);
                        fin_packet.data_len = 0;
                        send_packet(client_socket, &server_addr, sizeof(server_addr), &fin_packet);
                        sent_fin = 1;
                    }
                }
            }
            else
            {
                size_t data_len = bytes_received - sizeof(sham);
                if (data_len > 0 && data_len <= MAX_DATA_SIZE)
                {
                    if (ntohl(packet.seq_num) == expected_seq)
                    {
                        snprintf(log_msg, sizeof(log_msg), "RCV DATA SEQ=%u LEN=%zu", ntohl(packet.seq_num), data_len);
                        log_event(log_msg);

                        if (chat_mode)
                        {
                            printf("Received: %s\n", buffer + sizeof(sham));
                        }
                        else
                        {
                            // Write to output file in file transfer mode
                            if (output_file)
                            {
                                fwrite(buffer + sizeof(sham), 1, data_len, output_file);
                                fflush(output_file);
                            }
                        }
                        expected_seq += data_len;

                        sham ack_packet = {0};
                        ack_packet.seq_num = htonl(last_byte_sent + 1);
                        ack_packet.ack_num = htonl(expected_seq);
                        ack_packet.flags = htons(ACK);
                        ack_packet.window_size = htons(WINDOW_SIZE);
                        snprintf(log_msg, sizeof(log_msg), "SND ACK=%u WIN=%u", ntohl(ack_packet.ack_num), ntohs(ack_packet.window_size));
                        log_event(log_msg);

                        if (sendto(client_socket, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
                        {
                            perror("sendto failed");
                            continue;
                        }
                    }
                    else if (ntohl(packet.seq_num) > expected_seq)
                    {
                        snprintf(log_msg, sizeof(log_msg), "RCV DATA SEQ=%u LEN=%zu (out-of-order)", ntohl(packet.seq_num), data_len);
                        log_event(log_msg);
                        // Buffer out-of-order packet
                        int i;
                        for (i = 0; i < SLIDING_WINDOW; i++)
                        {
                            if (!recv_buffer[i].data_len)
                            {
                                recv_buffer[i].header = packet;
                                memcpy(recv_buffer[i].data, buffer + sizeof(sham), data_len);
                                recv_buffer[i].data_len = data_len;
                                break;
                            }
                        }
                        // Send ACK for expected_seq
                        sham ack_packet = {0};
                        ack_packet.seq_num = htonl(last_byte_sent + 1);
                        ack_packet.ack_num = htonl(expected_seq);
                        ack_packet.flags = htons(ACK);
                        ack_packet.window_size = htons(WINDOW_SIZE);
                        snprintf(log_msg, sizeof(log_msg), "SND ACK=%u WIN=%u", ntohl(ack_packet.ack_num), ntohs(ack_packet.window_size));
                        log_event(log_msg);

                        if (sendto(client_socket, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
                        {
                            perror("sendto failed");
                            continue;
                        }
                    }
                    // Process buffered packets
                    for (int i = 0; i < SLIDING_WINDOW; i++)
                    {
                        if (recv_buffer[i].data_len && ntohl(recv_buffer[i].header.seq_num) == expected_seq)
                        {
                            if (chat_mode)
                            {
                                printf("Received: %s\n", recv_buffer[i].data);
                            }
                            else
                            {
                                // Write buffered data to output file
                                if (output_file)
                                {
                                    fwrite(recv_buffer[i].data, 1, recv_buffer[i].data_len, output_file);
                                    fflush(output_file);
                                }
                            }
                            expected_seq += recv_buffer[i].data_len;
                            recv_buffer[i].data_len = 0; // Clear
                        }
                    }
                }
            }
        }
    }

    if (input_file)
        fclose(input_file);
    if (output_file)
        fclose(output_file);
    if (log_file)
        fclose(log_file);
    close(client_socket);
    return 0;
}