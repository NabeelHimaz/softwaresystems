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

        pthread_mutex_lock(&socket_mutex);
        // This function writes to the server (sends request)
        // through the socket at sd.
        // (See details of the function in udp.h)
        int rc = udp_socket_write(sd, &server_addr, input, BUFFER_SIZE);
        pthread_mutex_unlock(&socket_mutex);

        if(rc > 0){

        }
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

        pthread_mutex_lock(&socket_mutex);
        // This function reads the response from the server
        // through the socket at sd.
        // In our case, responder_addr will simply be
        // the same as server_addr.
        // (See details of the function in udp.h)
        int rc = udp_socket_read(sd, &responder_addr, response, BUFFER_SIZE);
        pthread_mutex_unlock(&socket_mutex);
        
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
        CLIENT_PORT = atoi(argv[1]);
        if (CLIENT_PORT == 6666) {
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

    pthread_t sender_id, listener_id;

    pthread_create(&listener_id, //thread id stored here
        NULL, //defualt attributes
        listener, //function to run
        NULL); //passing no data
        
    
    pthread_create(&sender_id, NULL, sender, NULL);

    return 0;
}