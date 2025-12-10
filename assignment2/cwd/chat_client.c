#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include "udp.h"

// Global variables
int sd;
struct sockaddr_in server_addr;
FILE *chat_file = NULL;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

int active = 1;

void *sender(void *arg){
    char input[BUFFER_SIZE];

    while(active){
        printf("> ");
        fflush(stdout);
        
        if (fgets(input, BUFFER_SIZE, stdin) == NULL) {
            break;
        }
        
        // Remove newline
        input[strcspn(input, "\n")] = '\0';
        
        // Skip empty input
        if (strlen(input) == 0) {
            continue;
        }

        // Send to server
        int rc = udp_socket_write(sd, &server_addr, input, strlen(input) + 1);
        
        if (rc < 0) {
            printf("Error: Failed to send message\n");
        }
        
        // Check if disconnect command
        if (strncmp(input, "disconn$", 8) == 0) {
            printf("Disconnecting...\n");
            active = 0;
            break;
        }
    }
    return NULL;
}

void *listener(void *arg){
    char response[BUFFER_SIZE];
    struct sockaddr_in responder_addr;
    
    while (active) {
        // Clear buffer completely before reading
        memset(response, 0, BUFFER_SIZE);
        
        // Read from server
        int rc = udp_socket_read(sd, &responder_addr, response, BUFFER_SIZE);
        
        if (rc <= 0) {
            if (rc < 0 && active) {
                usleep(100000);
            }
            continue;
        }
        
        // Ensure null termination
        response[BUFFER_SIZE - 1] = '\0';
        
        if (strncmp(response, "ping$", 5) == 0) {
            char ping_response[BUFFER_SIZE];
            memset(ping_response, 0, BUFFER_SIZE);
            snprintf(ping_response, BUFFER_SIZE, "ret-ping$");
            udp_socket_write(sd, &server_addr, ping_response, strlen(ping_response) + 1);
            continue;  // Don't display ping
        }
        
        // Display message
        printf("\n%s", response);
        printf("> ");
        fflush(stdout);
        
        // Write to file
        pthread_mutex_lock(&file_mutex);
        
        if (chat_file != NULL) {
            fprintf(chat_file, "%s", response);
            fflush(chat_file);
        }
        
        pthread_mutex_unlock(&file_mutex);
    }
    
    return NULL;
}

int main(int argc, char *argv[])
{   
    int CLIENT_PORT = 0;

    if (argc > 1) {
        int port_arg = atoi(argv[1]);
        if (port_arg == 6666) {
            CLIENT_PORT = port_arg;
            printf("\n*** ADMIN MODE - Port 6666 ***\n");
        } else {
            CLIENT_PORT = port_arg;
        }
    }
    
    // Open socket
    sd = udp_socket_open(CLIENT_PORT);
    
    if (sd < 0) {
        fprintf(stderr, "Error: Failed to open socket\n");
        return 1;
    }
    
    // Get actual port
    struct sockaddr_in local_addr;
    socklen_t addr_len = sizeof(local_addr);
    if (getsockname(sd, (struct sockaddr*)&local_addr, &addr_len) == 0) {
        printf("Client running on port: %d\n", ntohs(local_addr.sin_port));
    }

    // Set server address
    int rc = set_socket_addr(&server_addr, "127.0.0.1", SERVER_PORT);
    if (rc < 0) {
        fprintf(stderr, "Error: Failed to set server address\n");
        close(sd);
        return 1;
    }

    // Open chat log file
    chat_file = fopen("iChat.txt", "a");
    if (chat_file != NULL) {
        fprintf(chat_file, "\n=== New Chat Session ===\n");
        fflush(chat_file);
        printf(" Chat log: iChat.txt\n");
    }

    // Create threads
    pthread_t sender_id, listener_id;

    if (pthread_create(&listener_id, NULL, listener, NULL) != 0) {
        fprintf(stderr, "Error: Failed to create listener thread\n");
        if (chat_file) fclose(chat_file);
        close(sd);
        return 1;
    }
    
    if (pthread_create(&sender_id, NULL, sender, NULL) != 0) {
        fprintf(stderr, "Error: Failed to create sender thread\n");
        if (chat_file) fclose(chat_file);
        close(sd);
        return 1;
    }

    // Wait for sender to finish
    pthread_join(sender_id, NULL);
    
    // Signal listener to stop
    active = 0;
    sleep(1);
    
    // Close chat file
    if (chat_file != NULL) {
        pthread_mutex_lock(&file_mutex);
        fprintf(chat_file, "=== Chat Session Ended ===\n\n");
        fclose(chat_file);
        chat_file = NULL;
        pthread_mutex_unlock(&file_mutex);
    }
    
    // Cleanup
    close(sd);
    pthread_mutex_destroy(&file_mutex);
    
    printf("\nClient terminated.\n");

    return 0;
}