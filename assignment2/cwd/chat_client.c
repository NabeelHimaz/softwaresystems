#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "udp.h"

int sd;
struct sockaddr_in server_addr;
pthread_mutex_t socket_mutex = PTHREAD_MUTEX_INITIALIZER;
// Storage for request and response messages
char client_request[BUFFER_SIZE], server_response[BUFFER_SIZE];

int active = 1;

void *sender(void *arg){


    char input[BUFFER_SIZE];

    while(active){
        
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

    // Variable to store the server's IP address and port
    // (i.e. the server we are trying to contact).
    // Generally, it is possible for the responder to be
    // different from the server requested.
    // Although, in our case the responder will
    // always be the same as the server.
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
    
    sd = udp_socket_open(CLIENT_PORT);

    int rc = set_socket_addr(&server_addr, "127.0.0.1", SERVER_PORT);
    
    pthread_t sender_id, listener_id;

    pthread_create(&listener_id, NULL, listener, NULL);
    
    pthread_create(&sender_id, NULL, sender, NULL);

    pthread_join(sender_id, NULL);
    
    active = 0;
    sleep(1);
    close(sd);
    pthread_mutex_destroy(&socket_mutex);

    return 0;
}