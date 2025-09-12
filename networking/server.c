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
    #include <openssl/md5.h>
    #include <openssl/evp.h> 
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
    FILE *output_file = NULL;
    char *output_filename = NULL;

    void log_event(const char *event)
    {
        if (getenv("RUDP_LOG") && strcmp(getenv("RUDP_LOG"), "1") == 0)
        {
            if (!log_file)
            {
                log_file = fopen("server_log.txt", "w");
                if (!log_file)
                {
                    perror("Failed to open server_log.txt");
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
    void calculate_md5(const char *filename)
    {
        FILE *file = fopen(filename, "rb");
        if (!file)
        {
            perror("Failed to open file for MD5 calculation");
            return;
        }

        EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
        if (!md_ctx)
        {
            perror("Failed to create EVP_MD_CTX");
            fclose(file);
            return;
        }

        if (EVP_DigestInit_ex(md_ctx, EVP_md5(), NULL) != 1)
        {
            perror("EVP_DigestInit_ex failed");
            EVP_MD_CTX_free(md_ctx);
            fclose(file);
            return;
        }

        unsigned char buffer[1024];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
        {
            if (EVP_DigestUpdate(md_ctx, buffer, bytes_read) != 1)
            {
                perror("EVP_DigestUpdate failed");
                EVP_MD_CTX_free(md_ctx);
                fclose(file);
                return;
            }
        }

        unsigned char result[EVP_MAX_MD_SIZE];
        unsigned int result_len;
        if (EVP_DigestFinal_ex(md_ctx, result, &result_len) != 1)
        {
            perror("EVP_DigestFinal_ex failed");
            EVP_MD_CTX_free(md_ctx);
            fclose(file);
            return;
        }

        printf("MD5: ");
        for (unsigned int i = 0; i < result_len; i++)
        {
            printf("%02x", result[i]);
        }
        printf("\n");

        EVP_MD_CTX_free(md_ctx);
        fclose(file);
    }

    int three_way_handshake(int server_socket, struct sockaddr_in *client_addr, socklen_t *addr_len)
    {
        sham packet = {0};
        char log_msg[128];

        ssize_t bytes_received = recvfrom(server_socket, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)client_addr, addr_len);
        if (bytes_received < 0) 
        {
            perror("recvfrom failed");
            return 0;
        }
        memcpy(&packet, buffer, sizeof(sham));
        snprintf(log_msg, sizeof(log_msg), "RCV SYN SEQ=%u", ntohl(packet.seq_num));
        log_event(log_msg);

        if (ntohs(packet.flags) & SYN)
        {
            printf("Received SYN from client\n");

            sham syn_ack_packet = {0};
            syn_ack_packet.seq_num = htonl(0);
            syn_ack_packet.ack_num = htonl(ntohl(packet.seq_num) + 1);
            syn_ack_packet.flags = htons(SYN | ACK);
            syn_ack_packet.window_size = htons(WINDOW_SIZE);

            snprintf(log_msg, sizeof(log_msg), "SND SYN-ACK SEQ=%u ACK=%u", ntohl(syn_ack_packet.seq_num), ntohl(syn_ack_packet.ack_num));
            log_event(log_msg);

            if (sendto(server_socket, &syn_ack_packet, sizeof(syn_ack_packet), 0, (struct sockaddr *)client_addr, *addr_len) < 0)
            {
                perror("sendto failed");
                return 0;
            }
            printf("Sent SYN-ACK to client\n");

            bytes_received = recvfrom(server_socket, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)client_addr, addr_len);
            if (bytes_received < 0)
            {
                perror("recvfrom failed");
                return 0;
            }
            memcpy(&packet, buffer, sizeof(sham));
            if ((ntohs(packet.flags) & ACK) && ntohl(packet.ack_num) == ntohl(syn_ack_packet.seq_num) + 1)
            {
                log_event("RCV ACK FOR SYN");
                printf("Received ACK from client. Three-way handshake completed.\n");
                return 1;
            }
        }
        return 0;
    }

    void send_packet(int server_socket, struct sockaddr_in *client_addr, socklen_t addr_len, packet_t *packet)
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
        if (sendto(server_socket, buffer, sizeof(sham) + packet->data_len, 0, (struct sockaddr *)client_addr, addr_len) < 0)
        {
            perror("sendto failed");
        }
        else
        {
            gettimeofday(&packet->sent_time, NULL);
        }
    }

    void retransmit_packets(int server_socket, struct sockaddr_in *client_addr, socklen_t addr_len)
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
                    send_packet(server_socket, client_addr, addr_len, &sent_packets[i]);
                }
            }
        }
    }

    int main(int argc, char *argv[])
    {
        if (argc < 2 || argc > 4)
        {
            fprintf(stderr, "Usage: %s <port> [--chat] [loss_rate]\n", argv[0]);
            exit(EXIT_FAILURE);
        }

        int port = atoi(argv[1]);
        int chat_mode = 0;
        float loss_rate = 0.0;
        
        if (argc >= 3) {
            if (strcmp(argv[2], "--chat") == 0) {
                chat_mode = 1;
                if (argc == 4) {
                    loss_rate = atof(argv[3]);
                    if (loss_rate < 0.0 || loss_rate > 1.0) {
                        fprintf(stderr, "loss_rate must be between 0.0 and 1.0\n");// loss rate should not be more than 1.0 or less than 0.0
                        exit(EXIT_FAILURE);
                    }
                }
            } else {
                // if the --chat is missing / if it is not in chat_mode
                loss_rate = atof(argv[2]);
                if (loss_rate < 0.0 || loss_rate > 1.0) {
                    fprintf(stderr, "loss_rate must be between 0.0 and 1.0\n");
                    exit(EXIT_FAILURE);
                }
            }
        }
        srand(time(NULL));

        int server_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (server_socket < 0)
        {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in server_side;
        server_side.sin_family = AF_INET;
        server_side.sin_addr.s_addr = INADDR_ANY;
        server_side.sin_port = htons(port);

        if (bind(server_socket, (struct sockaddr *)&server_side, sizeof(server_side)) < 0)
        {
            perror("Bind failed");
            close(server_socket);
            exit(EXIT_FAILURE);
        }
        printf("Bind successful\n");

        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int handshake_result = three_way_handshake(server_socket, &client_addr, &addr_len);
        if (handshake_result == 1)
        {
            printf("Handshake successful. Ready for communication.\n");
        }
        else
        {
            printf("Handshake failed.\n");
            close(server_socket);
            return 1;
        }

        // In file transfer mode, wait for filename from client first
        if (!chat_mode)
        {
            output_filename = "received_file.dat"; // Default fallback
            // Don't create the file yet - wait for filename from client
        }

        char input_buffer[MAX_DATA_SIZE];
        fd_set read_fds;
        int max_fd = server_socket + 1;

        while (1)
        {
            FD_ZERO(&read_fds);
            if (chat_mode && !sent_fin)
            {
                FD_SET(STDIN_FILENO, &read_fds);
            }
            FD_SET(server_socket, &read_fds);

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
                    retransmit_packets(server_socket, &client_addr, addr_len);
                }
                continue;
            }

            if (chat_mode && FD_ISSET(STDIN_FILENO, &read_fds) && !sent_fin)
            {
                if (fgets(input_buffer, MAX_DATA_SIZE, stdin) == NULL)
                {
                    perror("Input error");
                    break;
                }
                input_buffer[strcspn(input_buffer, "\n")] = '\0';
                printf("Input received: '%s' (length: %zu)\n", input_buffer, strlen(input_buffer));
                if (strcmp(input_buffer, "/quit") == 0)
                {
                    packet_t fin_packet = {0};
                    fin_packet.header.seq_num = htonl(last_byte_sent + 1);
                    fin_packet.header.ack_num = htonl(expected_seq);
                    fin_packet.header.flags = htons(FIN);
                    fin_packet.header.window_size = htons(WINDOW_SIZE);
                    fin_packet.data_len = 0;
                    send_packet(server_socket, &client_addr, addr_len, &fin_packet);
                    sent_fin = 1;

                    struct timeval ack_timeout;
                    fd_set ack_fds;
                    FD_ZERO(&ack_fds);
                    FD_SET(server_socket, &ack_fds);
                    ack_timeout.tv_sec = 2;
                    ack_timeout.tv_usec = 0;
                    if (select(max_fd, &ack_fds, NULL, NULL, &ack_timeout) > 0)
                    {
                        ssize_t bytes_received = recvfrom(server_socket, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);
                        if (bytes_received > 0)
                        {
                            sham packet;
                            memcpy(&packet, buffer, sizeof(sham));
                            if (ntohs(packet.flags) & ACK)
                            {
                                char log_msg[128];
                                snprintf(log_msg, sizeof(log_msg), "RCV ACK=%u", ntohl(packet.ack_num));
                                log_event(log_msg);
                                printf("Received ACK for FIN from client\n");

                                // Wait for client's FIN
                                FD_ZERO(&ack_fds);
                                FD_SET(server_socket, &ack_fds);
                                if (select(max_fd, &ack_fds, NULL, NULL, &ack_timeout) > 0)
                                {
                                    bytes_received = recvfrom(server_socket, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);
                                    if (bytes_received > 0)
                                    {
                                        memcpy(&packet, buffer, sizeof(sham));
                                        if (ntohs(packet.flags) & FIN)
                                        {
                                            char log_msg[128];
                                            snprintf(log_msg, sizeof(log_msg), "RCV FIN SEQ=%u", ntohl(packet.seq_num));
                                            log_event(log_msg);
                                            printf("Received FIN from client\n");

                                            sham ack_packet = {0};
                                            ack_packet.seq_num = htonl(last_byte_sent + 2);
                                            ack_packet.ack_num = htonl(ntohl(packet.seq_num) + 1);
                                            ack_packet.flags = htons(ACK);
                                            ack_packet.window_size = htons(WINDOW_SIZE);
                                            snprintf(log_msg, sizeof(log_msg), "SND ACK=%u WIN=%u", ntohl(ack_packet.ack_num), ntohs(ack_packet.window_size));
                                            log_event(log_msg);

                                            if (sendto(server_socket, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&client_addr, addr_len) < 0)
                                            {
                                                perror("sendto failed");
                                                break;
                                            }
                                            printf("Sent ACK to client. Connection closed.\n");
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    else
                    {
                        printf("No ACK received from client. Closing anyway.\n");
                        break;
                    }
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
                    send_packet(server_socket, &client_addr, addr_len, &data_packet);
                    printf("Sent message to client: %s\n", input_buffer);
                    last_byte_sent += data_packet.data_len;
                }
                else
                {
                    printf("Cannot send: window full (%d packets, unacked=%u, window=%u)\n", sent_packet_count, last_byte_sent - last_byte_acked, receiver_window);
                }
            }

            if (FD_ISSET(server_socket, &read_fds))
            {
                ssize_t bytes_received = recvfrom(server_socket, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);
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
                    printf("Received FIN from client\n");

                    // Close file and calculate MD5 if in file transfer mode
                    if (!chat_mode && output_file)
                    {
                        fclose(output_file);
                        output_file = NULL;
                        calculate_md5(output_filename);
                    }

                    sham ack_packet = {0};
                    ack_packet.seq_num = htonl(last_byte_sent + 1);
                    ack_packet.ack_num = htonl(ntohl(packet.seq_num) + 1);
                    ack_packet.flags = htons(ACK);
                    ack_packet.window_size = htons(WINDOW_SIZE);
                    snprintf(log_msg, sizeof(log_msg), "SND ACK=%u WIN=%u", ntohl(ack_packet.ack_num), ntohs(ack_packet.window_size));
                    log_event(log_msg);

                    if (sendto(server_socket, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&client_addr, addr_len) < 0)
                    {
                        perror("sendto failed");
                        break;
                    }
                    printf("Sent ACK to client\n");

                    packet_t fin_packet = {0};
                    fin_packet.header.seq_num = htonl(last_byte_sent + 2);
                    fin_packet.header.ack_num = htonl(expected_seq);
                    fin_packet.header.flags = htons(FIN);
                    fin_packet.header.window_size = htons(WINDOW_SIZE);
                    fin_packet.data_len = 0;
                    send_packet(server_socket, &client_addr, addr_len, &fin_packet);
                    printf("Sent FIN to client\n");

                    struct timeval ack_timeout;
                    fd_set ack_fds;
                    FD_ZERO(&ack_fds);
                    FD_SET(server_socket, &ack_fds);
                    ack_timeout.tv_sec = 2;
                    ack_timeout.tv_usec = 0;
                    if (select(max_fd, &ack_fds, NULL, NULL, &ack_timeout) > 0)
                    {
                        bytes_received = recvfrom(server_socket, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);
                        if (bytes_received > 0)
                        {
                            memcpy(&packet, buffer, sizeof(sham));
                            if (ntohs(packet.flags) & ACK)
                            {
                                snprintf(log_msg, sizeof(log_msg), "RCV ACK=%u", ntohl(packet.ack_num));
                                log_event(log_msg);
                                printf("Received ACK for FIN from client. Connection closed.\n");
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
                                printf("Received message from client: %s\n", buffer + sizeof(sham));
                            }
                            else
                            {
                                // Check if this is a filename packet
                                char *data_ptr = buffer + sizeof(sham);
                                if (strncmp(data_ptr, "FILENAME:", 9) == 0 && !output_file) {
                                    // Extract filename from packet
                                    char *filename = data_ptr + 9; // Skip "FILENAME:" prefix
                                    output_filename = malloc(strlen(filename) + 1);
                                    if (output_filename) {
                                        strcpy(output_filename, filename);
                                        printf("Received filename from client: %s\n", output_filename);
                                        
                                        // Now create the output file with the correct name
                                        output_file = fopen(output_filename, "wb");
                                        if (!output_file) {
                                            perror("Failed to create output file with received filename");
                                            // Fall back to default name
                                            output_filename = "received_file.dat";
                                            output_file = fopen(output_filename, "wb");
                                        }
                                        
                                        char log_msg_filename[256];
                                        snprintf(log_msg_filename, sizeof(log_msg_filename), "OUTPUT FILE CREATED: %s", output_filename);
                                        log_event(log_msg_filename);
                                    }
                                } else if (output_file) {
                                    // Regular file data
                                    fwrite(data_ptr, 1, data_len, output_file);
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

                            if (sendto(server_socket, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&client_addr, addr_len) < 0)
                            {
                                perror("sendto failed");
                                continue;
                            }
                            if (chat_mode)
                            {
                                printf("Sent ACK to client\n");
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
            
                            sham ack_packet = {0};
                            ack_packet.seq_num = htonl(last_byte_sent + 1);
                            ack_packet.ack_num = htonl(expected_seq);
                            ack_packet.flags = htons(ACK);
                            ack_packet.window_size = htons(WINDOW_SIZE);
                            snprintf(log_msg, sizeof(log_msg), "SND ACK=%u WIN=%u", ntohl(ack_packet.ack_num), ntohs(ack_packet.window_size));
                            log_event(log_msg);

                            if (sendto(server_socket, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&client_addr, addr_len) < 0)
                            {
                                perror("sendto failed");
                                continue;
                            }
                        }
                        
                        for (int i = 0; i < SLIDING_WINDOW; i++)
                        {
                            if (recv_buffer[i].data_len && ntohl(recv_buffer[i].header.seq_num) == expected_seq)
                            {
                                if (chat_mode)
                                {
                                    printf("%s\n", recv_buffer[i].data);
                                }
                                else
                                {
                                    // Check if this buffered data is a filename packet
                                    if (strncmp(recv_buffer[i].data, "FILENAME:", 9) == 0 && !output_file) {
                                        // Extract filename from buffered packet
                                        char *filename = recv_buffer[i].data + 9; // Skip "FILENAME:" prefix
                                        output_filename = malloc(strlen(filename) + 1);
                                        if (output_filename) {
                                            strcpy(output_filename, filename);
                                            printf("Received filename from buffered packet: %s\n", output_filename);
                                            
                                            // Create the output file with the correct name
                                            output_file = fopen(output_filename, "wb");
                                            if (!output_file) {
                                                perror("Failed to create output file with received filename");
                                                output_filename = "received_file.dat";
                                                output_file = fopen(output_filename, "wb");
                                            }
                                        }
                                    } else if (output_file) {
                                        // Regular file data
                                        fwrite(recv_buffer[i].data, 1, recv_buffer[i].data_len, output_file);
                                        fflush(output_file);
                                    }
                                }
                                expected_seq += recv_buffer[i].data_len;
                                recv_buffer[i].data_len = 0; 
                            }
                        }
                    }
                }
            }
        }

        if (output_file)
        {
            fclose(output_file);
        }
        if (log_file)
        {
            fclose(log_file);
        }
        close(server_socket);
        return 0;
    }