#include <stdio.h>
#include <stdlib.h>
#include "udp.h"
#include <pthread.h>
#include <string.h>

pthread_mutex_t listener_mutex = PTHREAD_MUTEX_INITIALIZER;
typedef enum{
    CONNECT,
    SAY,
    SAYTO,
    MUTE,
    UNMUTE,
    RENAME,
    DISCONNECT,
    KICK,
    PING,
    RET_PING,
    UNKNOWN
} request_t;

typedef struct{
    int sd;
    struct sockaddr_in client_address;
    request_t request_type;
    char request_content[BUFFER_SIZE];
} process_data;


// Helper function to parse request type
request_t parse_request_type(char *request){
    
    char *dollar = strchr(request, '$');
    
   
    if(dollar == NULL){
        return UNKNOWN;
    }
  
    int cmd_len = dollar - request;
    
    // Compare command strings
    if(strncmp(request, "conn", cmd_len) == 0){
        return CONNECT;
    }
    else if(strncmp(request, "say", cmd_len) == 0){
        return SAY;
    }
    else if(strncmp(request, "sayto", cmd_len) == 0){
        return SAYTO;
    }
    else if(strncmp(request, "mute", cmd_len) == 0){
        return MUTE;
    }
    else if(strncmp(request, "unmute", cmd_len) == 0){
        return UNMUTE;
    }
    else if(strncmp(request, "rename", cmd_len) == 0){
        return RENAME;
    }
    else if(strncmp(request, "disconn", cmd_len) == 0){
        return DISCONNECT;
    }
    else if(strncmp(request, "kick", cmd_len) == 0){
        return KICK;
    }
    
    return UNKNOWN;
}

char* request_content(char *request){
    char *dollar = strchr(request, '$');
    if(dollar == NULL){
        return NULL;
    }
    return dollar + 1;  // Skip the '$' character
}






int main(int argc, char *argv[])
{

    // This function opens a UDP socket,
    // binding it to all IP interfaces of this machine,
    // and port number SERVER_PORT
    // (See details of the function in udp.h)
    int sd = udp_socket_open(SERVER_PORT);

    assert(sd > -1);

    // Server main loop
    while (1) 
    {
        // Storage for request and response messages
        char client_request[BUFFER_SIZE], server_response[BUFFER_SIZE];

        // Demo code (remove later)
        printf("Server is listening on port %d\n", SERVER_PORT);

        // Variable to store incoming client's IP address and port
        struct sockaddr_in client_address;
    
        // This function reads incoming client request from
        // the socket at sd.
        // (See details of the function in udp.h)
        int rc = udp_socket_read(sd, &client_address, client_request, BUFFER_SIZE);

        // Successfully received an incoming request
        if (rc > 0)
        {
            
            request_t req_type = request_type(client_request);
            char *content = request_content(client_request);
            
            //intialise struct
            process_data *args = malloc(sizeof(process_data));
            args->sd = sd;
            args->client_address = client_address;
            args->request_type = req_type;

            strncpy(args->request_content, content, BUFFER_SIZE - 1);
            args->request_content[BUFFER_SIZE - 1] = '\0';
                

            pthread_t thread;
            
            switch (req_type){
                case CONNECT:
                    pthread_create(&thread, NULL, handle_connect, args);
                    pthread_detach(thread);
                    break;
                case SAY:
                    pthread_create(&thread, NULL, handle_say, args);
                    pthread_detach(thread);
                    break;
                case SAYTO:
                    pthread_create(&thread, NULL, handle_sayto, args);
                    pthread_detach(thread);
                    break;
                case DISCONNECT:
                    pthread_create(&thread, NULL, handle_disconnect, args);
                    pthread_detach(thread);
                    break;
                default:
                    printf("Unknown or unimplemented request type\n");
                    free(args);
            }


            // This function writes back to the incoming client,
            // whose address is now available in client_address, 
            // through the socket at sd.
            // (See details of the function in udp.h)
            rc = udp_socket_write(sd, &client_address, server_response, BUFFER_SIZE);

            // Demo code (remove later)
            printf("Request served...\n");
            printf("Client Port: %d\n", ntohs(client_address.sin_port));
        }
    }

    return 0;
}