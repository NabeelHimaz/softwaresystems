#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include "udp.h"


int sd;
struct sockaddr_in server_addr;
FILE *chat_file = NULL;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
// Storage for request and response messages
char client_request[BUFFER_SIZE], server_response[BUFFER_SIZE];

int active = 1;

void *sender(void *arg){

    char input[BUFFER_SIZE];

    while(active){
        printf("> ");
        fflush(stdout);
        
        if (fgets(input, BUFFER_SIZE, stdin) == NULL) {
            break;
        }
        input[strcspn(input, "\n")] = '\0';
        strcpy(client_request, input);



        // This function writes to the server (sends request)
        // through the socket at sd.
        // (See details of the function in udp.h)
        int rc = udp_socket_write(sd, &server_addr, input, BUFFER_SIZE);


    }
    return NULL;
}

void *listener(void *arg){

    char response[BUFFER_SIZE];

    struct sockaddr_in responder_addr;
    
    while (active) {


        // This function reads the response from the server
        // through the socket at sd.
        // In our case, responder_addr will simply be
        // the same as server_addr.
        // (See details of the function in udp.h)
        int rc = udp_socket_read(sd, &responder_addr, response, BUFFER_SIZE);

        
        if (rc > 0) {
            printf("\n%s", response);
        }
        strcpy(server_response, response);

        pthread_mutex_lock(&file_mutex);
            
        if (chat_file != NULL) {
            // Get timestamp
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            char timestamp[64];
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
            
            // Write timestamp and message to file
            fprintf(chat_file, "[%s] %s", timestamp, response);
            fflush(chat_file);  // Ensure it's written immediately
        }
        
        pthread_mutex_unlock(&file_mutex);
        
        // Also print a notification to terminal
        printf("\n[New message received - check iChat.txt]\n> ");
        fflush(stdout);
    }
    
    return NULL;
}

// client code
int main(int argc, char *argv[])
{   
    int CLIENT_PORT = 0;

    if (argc > 1) {
        int CLIENT_PORT_ADMIN = atoi(argv[1]);
        if (CLIENT_PORT_ADMIN == 6666) {
            CLIENT_PORT = CLIENT_PORT_ADMIN;
            printf("ADMIN client on port 6666\n");
        }
    }
    // This function opens a UDP socket,
    // binding it to all IP interfaces of this machine,
    // and port number CLIENT_PORT.
    // (See details of the function in udp.h)
    int sd = udp_socket_open(CLIENT_PORT); //0 binds to any available port

    // Initializing the server's address.
    // We are currently running the server on localhost (127.0.0.1).
    // You can change this to a different IP address
    // when running the server on a different machine.
    // (See details of the function in udp.h)
    int rc = set_socket_addr(&server_addr, "127.0.0.1", SERVER_PORT);

    chat_file = fopen("iChat.txt", "a");
    
        // Write a session start marker
    time_t now = time(NULL);
    fprintf(chat_file, "\n=== New Chat Session Started at %s ===\n", ctime(&now));
    fflush(chat_file);
    printf("Chat output file opened: iChat.txt\n");
    

    pthread_t sender_id, listener_id;

    pthread_create(&listener_id, //thread id stored here
        NULL, //defualt attributes
        listener, //function to run
        NULL); //passing no data
        
    
    pthread_create(&sender_id, NULL, sender, NULL);
    
    pthread_join(sender_id, NULL);
    active = 0;
    
    // Give listener thread a moment to finish
    sleep(1);
    
    // Close chat file
    if (chat_file != NULL) {
        pthread_mutex_lock(&file_mutex);
        time_t now = time(NULL);
        fprintf(chat_file, "=== Chat Session Ended at %s ===\n\n", ctime(&now));
        fclose(chat_file);
        chat_file = NULL;
        pthread_mutex_unlock(&file_mutex);
        printf("Chat file closed\n");
    }
    
    // Cleanup
    close(sd);
    pthread_mutex_destroy(&file_mutex);
    
    printf("Client terminated.\n");

    return 0;
}