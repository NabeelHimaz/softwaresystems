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

int active = 1;

void *sender(void *arg){
    char input[BUFFER_SIZE];

    while(active){
        printf("> ");
        fflush(stdout);
        
        if(fgets(input, BUFFER_SIZE, stdin) == NULL){
            break;
        }
        input[strcspn(input, "\n")] = '\0';
        
        if (strlen(input) == 0){
            continue;
        }

        int rc = udp_socket_write(sd, &server_addr, input, strlen(input) + 1);
        
        if(rc < 0){
            printf("Failed to send message\n");
        }
        
        if(strncmp(input, "disconn$", 8) == 0){
            printf("Disconnecting\n");
            active = 0;
            break;
        }
    }
    return NULL;
}

void *listener(void *arg){
    char response[BUFFER_SIZE];
    struct sockaddr_in responder_addr;
    
    while(active){
        memset(response, 0, BUFFER_SIZE);
        
        int rc = udp_socket_read(sd, &responder_addr, response, BUFFER_SIZE);
        
        if(rc <= 0){
            if (rc < 0 && active) {
                usleep(100000);
            }
            continue;
        }
        
        response[BUFFER_SIZE - 1] = '\0';
        
        if(strncmp(response, "ping$", 5) == 0) {
            char ping_response[BUFFER_SIZE];
            memset(ping_response, 0, BUFFER_SIZE);
            snprintf(ping_response, BUFFER_SIZE, "ret-ping$");
            udp_socket_write(sd, &server_addr, ping_response, strlen(ping_response) + 1);
            continue; 
        }
        
        printf("\n%s", response);
        printf("> ");
        fflush(stdout);
        
        pthread_mutex_lock(&file_mutex);
        
        if(chat_file != NULL){
            fprintf(chat_file, "%s", response);
            fflush(chat_file);
        }
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}

int main(int argc, char *argv[]){   
    int CLIENT_PORT = 0;

    if(argc > 1){
        int port_arg = atoi(argv[1]);
        if(port_arg == 6666){
            CLIENT_PORT = port_arg;
            printf("\nADMIN - Port 6666\n");
        } 
        else{
            CLIENT_PORT = port_arg;
        }
    }
    
    sd = udp_socket_open(CLIENT_PORT);
    
    if(sd < 0){
        fprintf(stderr, "Failed to open socket\n");
        return 1;
    }
    
    struct sockaddr_in local_addr;
    socklen_t addr_len = sizeof(local_addr);
    if(getsockname(sd, (struct sockaddr*)&local_addr, &addr_len) == 0){
        printf("Client running on port: %d\n", ntohs(local_addr.sin_port));
    }

    int rc = set_socket_addr(&server_addr, "127.0.0.1", SERVER_PORT);
    if(rc < 0){
        fprintf(stderr, "Failed to set server address\n");
        close(sd);
        return 1;
    }

    chat_file = fopen("iChat.txt", "a");
    if(chat_file != NULL){
        fprintf(chat_file, "\nNew Chat Session:\n");
        fflush(chat_file);
        printf("Chat log: iChat.txt\n");
    }

    pthread_t sender_id, listener_id;

    if(pthread_create(&listener_id, NULL, listener, NULL) != 0){
        fprintf(stderr, "Failed to create listener thread\n");
        if(chat_file){
            fclose(chat_file);
        }
        close(sd);
        return 1;
    }
    if(pthread_create(&sender_id, NULL, sender, NULL) != 0){
        fprintf(stderr, "Failed to create sender thread\n");
        if(chat_file){ 
            fclose(chat_file);
        }
        close(sd);
        return 1;
    }

    pthread_join(sender_id, NULL);
    
    active = 0;
    sleep(1);
    
    if(chat_file != NULL){
        pthread_mutex_lock(&file_mutex);
        fprintf(chat_file, "Chat Session Ended\n\n");
        fclose(chat_file);
        chat_file = NULL;
        pthread_mutex_unlock(&file_mutex);
    }
    
    close(sd);
    pthread_mutex_destroy(&file_mutex);
    
    printf("\nClient terminated.\n");

    return 0;
}