#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>  
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "udp.h"

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

typedef struct client_node {
    char name[256];
    struct sockaddr_in addr;
    time_t last_active;
    struct client_node *next;
} client_node_t;

typedef struct mute_node {
    struct sockaddr_in muter;  // Who muted
    struct sockaddr_in mutee;  // Who is muted
    struct mute_node *next;
} mute_node_t;

client_node_t *client_list_head = NULL;
pthread_rwlock_t client_list_lock = PTHREAD_RWLOCK_INITIALIZER;

mute_node_t *mute_list_head = NULL;
pthread_mutex_t mute_list_lock = PTHREAD_MUTEX_INITIALIZER;


int addr_equal(struct sockaddr_in *a1, struct sockaddr_in *a2) {
    return (a1->sin_addr.s_addr == a2->sin_addr.s_addr && 
            a1->sin_port == a2->sin_port);
}

client_node_t* find_client_by_addr(struct sockaddr_in *addr) {
    // Caller should hold read or write lock
    client_node_t *current = client_list_head;
    while (current != NULL) {
        if (addr_equal(&current->addr, addr)) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

client_node_t* find_client_by_name(const char *name) {
    // Caller should hold read or write lock
    client_node_t *current = client_list_head;
    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

int is_muted(struct sockaddr_in *muter, struct sockaddr_in *mutee) {
    pthread_mutex_lock(&mute_list_lock);
    
    mute_node_t *current = mute_list_head;
    while (current != NULL) {
        if (addr_equal(&current->muter, muter) && 
            addr_equal(&current->mutee, mutee)) {
            pthread_mutex_unlock(&mute_list_lock);
            return 1;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&mute_list_lock);
    return 0;
}

void add_mute(struct sockaddr_in *muter, struct sockaddr_in *mutee) {
    pthread_mutex_lock(&mute_list_lock);
    
    // Check if already muted
    mute_node_t *current = mute_list_head;
    while (current != NULL) {
        if (addr_equal(&current->muter, muter) && 
            addr_equal(&current->mutee, mutee)) {
            pthread_mutex_unlock(&mute_list_lock);
            return;  // Already muted
        }
        current = current->next;
    }
    
    // Add new mute
    mute_node_t *new_mute = malloc(sizeof(mute_node_t));
    new_mute->muter = *muter;
    new_mute->mutee = *mutee;
    new_mute->next = mute_list_head;
    mute_list_head = new_mute;
    
    pthread_mutex_unlock(&mute_list_lock);
}

void remove_mute(struct sockaddr_in *muter, struct sockaddr_in *mutee) {
    pthread_mutex_lock(&mute_list_lock);
    
    mute_node_t *current = mute_list_head;
    mute_node_t *prev = NULL;
    
    while (current != NULL) {
        if (addr_equal(&current->muter, muter) && 
            addr_equal(&current->mutee, mutee)) {
            // Found it, remove it
            if (prev == NULL) {
                mute_list_head = current->next;
            } else {
                prev->next = current->next;
            }
            free(current);
            pthread_mutex_unlock(&mute_list_lock);
            return;
        }
        prev = current;
        current = current->next;
    }
    
    pthread_mutex_unlock(&mute_list_lock);
}

void update_last_active(struct sockaddr_in *addr) {
    pthread_rwlock_wrlock(&client_list_lock);
    
    client_node_t *client = find_client_by_addr(addr);
    if (client != NULL) {
        client->last_active = time(NULL);
    }
    
    pthread_rwlock_unlock(&client_list_lock);
}

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

void *connect_h(void *arg){
    process_data *data = (process_data *)arg;
    char response[BUFFER_SIZE];
    
    pthread_rwlock_wrlock(&client_list_lock);
    
    // Add new client
    client_node_t *new_client = malloc(sizeof(client_node_t));
    strncpy(new_client->name, data->request_content, 255);
    new_client->name[255] = '\0';
    new_client->addr = data->client_address;
    new_client->last_active = time(NULL);
    new_client->next = client_list_head;
    client_list_head = new_client;
    
    pthread_rwlock_unlock(&client_list_lock);
    
    // Send confirmation
    snprintf(response, BUFFER_SIZE, 
             "Welcome %s! You are now connected to the chat.\n", 
             data->request_content);
    udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
    
    printf("[CONNECT] %s joined from port %d\n", 
           data->request_content, ntohs(data->client_address.sin_port));
    
    free(data);
    return NULL;
}

void *disconnect(void *arg){
    process_data *data = (process_data *)arg;
    char response[BUFFER_SIZE];
    
    pthread_rwlock_wrlock(&client_list_lock);
    
    // Find and remove client
    client_node_t *current = client_list_head;
    client_node_t *prev = NULL;
    
    while (current != NULL) {
        if (addr_equal(&current->addr, &data->client_address)) {
            // Found the client
            if (prev == NULL) {
                client_list_head = current->next;
            } else {
                prev->next = current->next;
            }
            
            snprintf(response, BUFFER_SIZE, 
                     "Goodbye %s! You have been disconnected.\n", current->name);
            
            printf("[DISCONNECT] %s left\n", current->name);
            
            free(current);
            pthread_rwlock_unlock(&client_list_lock);
            
            udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
            free(data);
            return NULL;
        }
        prev = current;
        current = current->next;
    }
    
    pthread_rwlock_unlock(&client_list_lock);
    
    snprintf(response, BUFFER_SIZE, "Error: You are not connected\n");
    udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
    
    free(data);
    return NULL;
}

void *say(void *arg){
    process_data *data = (process_data *)arg;
    char response[BUFFER_SIZE];
    char broadcast[BUFFER_SIZE];
    
    pthread_rwlock_rdlock(&client_list_lock);
    
    // Find sender
    client_node_t *sender = find_client_by_addr(&data->client_address);
    
    // Update sender's last active time
    sender->last_active = time(NULL);
    
    // Create broadcast message
    snprintf(broadcast, BUFFER_SIZE, "[%s]: %s\n", sender->name, data->request_content);
    
    // Send to all clients except those who muted the sender
    client_node_t *current = client_list_head;
    while (current != NULL) {
        if (!is_muted(&current->addr, &data->client_address)) {
            udp_socket_write(data->sd, &current->addr, broadcast, BUFFER_SIZE);
        }
        current = current->next;
    }
    
    pthread_rwlock_unlock(&client_list_lock);
    
    printf("[BROADCAST] %s: %s\n", sender->name, data->request_content);
    
    free(data);
    return NULL;
}

void *sayto(void *arg){
    process_data *data = (process_data *)arg;
    char response[BUFFER_SIZE];
    
    pthread_rwlock_rdlock(&client_list_lock);
    
    // Find sender
    client_node_t *sender = find_client_by_addr(&data->client_address);
    
    // Update sender's last active time
    sender->last_active = time(NULL);
    
    // Parse recipient name and message
    char *space = strchr(data->request_content, ' ');
    
    char recipient_name[256];
    int name_len = space - data->request_content;
    strncpy(recipient_name, data->request_content, name_len);
    recipient_name[name_len] = '\0';
    
    char *message = space + 1;
    
    // Find recipient
    client_node_t *recipient = find_client_by_name(recipient_name);
    
    // Check if recipient has muted sender
    if (is_muted(&recipient->addr, &data->client_address)) {
        pthread_rwlock_unlock(&client_list_lock);
        // Don't tell sender they are muted, just confirm delivery
        snprintf(response, BUFFER_SIZE, "Private message sent to %s\n", recipient_name);
        udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
        free(data);
        return NULL;
    }
    
    // Send private message
    snprintf(response, BUFFER_SIZE, "[Private from %s]: %s\n", sender->name, message);
    udp_socket_write(data->sd, &recipient->addr, response, BUFFER_SIZE);
    
    // Confirm to sender
    snprintf(response, BUFFER_SIZE, "Private message sent to %s\n", recipient_name);
    udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
    
    pthread_rwlock_unlock(&client_list_lock);
    
    printf("[PRIVATE] %s -> %s: %s\n", sender->name, recipient_name, message);
    
    free(data);
    return NULL;
}

void *mute(void *arg){
    process_data *data = (process_data *)arg;
    char response[BUFFER_SIZE];
    
    pthread_rwlock_rdlock(&client_list_lock);
    
    // Find muter
    client_node_t *muter = find_client_by_addr(&data->client_address);
    
    // Find client to mute
    client_node_t *mutee = find_client_by_name(data->request_content);
    
    // Add mute
    add_mute(&data->client_address, &mutee->addr);
    
    pthread_rwlock_unlock(&client_list_lock);
    
    snprintf(response, BUFFER_SIZE, "You have muted %s\n", data->request_content);
    udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
    
    printf("[MUTE] %s muted %s\n", muter->name, data->request_content);
    
    free(data);
    return NULL;
}

void *unmute(void *arg){
    process_data *data = (process_data *)arg;
    char response[BUFFER_SIZE];
    
    pthread_rwlock_rdlock(&client_list_lock);
    
    // Find unmuter
    client_node_t *unmuter = find_client_by_addr(&data->client_address);
    
    // Find client to unmute
    client_node_t *unmutee = find_client_by_name(data->request_content);
    
    // Remove mute
    remove_mute(&data->client_address, &unmutee->addr);
    
    pthread_rwlock_unlock(&client_list_lock);
    
    snprintf(response, BUFFER_SIZE, "You have unmuted %s\n", data->request_content);
    udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
    
    printf("[UNMUTE] %s unmuted %s\n", unmuter->name, data->request_content);
    
    free(data);
    return NULL;
}

void *kick(void *arg){
    process_data *data = (process_data *)arg;
    char response[BUFFER_SIZE];
    char broadcast[BUFFER_SIZE];
    
    // Check if requester is admin (port 6666)
    if (ntohs(data->client_address.sin_port) != 6666) {
        snprintf(response, BUFFER_SIZE, "Need Admin privilages\n");
        udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
        free(data);
        return NULL;
    }
    
    pthread_rwlock_wrlock(&client_list_lock);
    
    // Find client to kick
    client_node_t *current = client_list_head;
    client_node_t *prev = NULL;
    
    while (current != NULL) {
        if (strcmp(current->name, data->request_content) == 0) {
            // Found the client to kick
            
            // Notify the kicked client
            snprintf(response, BUFFER_SIZE, 
                     "You have been kicked from the chat by admin\n");
            udp_socket_write(data->sd, &current->addr, response, BUFFER_SIZE);
            
            // Broadcast to all other clients
            snprintf(broadcast, BUFFER_SIZE, 
                     "[SYSTEM]: %s has been kicked from the chat\n", current->name);
            
            client_node_t *temp = client_list_head;
            while (temp != NULL) {
                if (!addr_equal(&temp->addr, &current->addr)) {
                    udp_socket_write(data->sd, &temp->addr, broadcast, BUFFER_SIZE);
                }
                temp = temp->next;
            }
            
            // Remove from list
            if (prev == NULL) {
                client_list_head = current->next;
            } else {
                prev->next = current->next;
            }
            
            printf("[KICK] Admin kicked %s\n", current->name);
            
            // Confirm to admin
            snprintf(response, BUFFER_SIZE, "User %s has been kicked\n", current->name);
            udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
            
            free(current);
            pthread_rwlock_unlock(&client_list_lock);
            free(data);
            return NULL;
        }
        prev = current;
        current = current->next;
    }
    
    pthread_rwlock_unlock(&client_list_lock);
    
    snprintf(response, BUFFER_SIZE, "Error: User '%s' not found\n", data->request_content);
    udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
    
    free(data);
    return NULL;
}

void *rename_h(void *arg){
    process_data *data = (process_data *)arg;
    char response[BUFFER_SIZE];
    
    pthread_rwlock_wrlock(&client_list_lock);
    
    // Find client
    client_node_t *client = find_client_by_addr(&data->client_address);
    
    // Check if new name is already taken
    if (find_client_by_name(data->request_content) != NULL) {
        pthread_rwlock_unlock(&client_list_lock);
        snprintf(response, BUFFER_SIZE, "Error: Name '%s' is already taken\n", 
                 data->request_content);
        udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
        free(data);
        return NULL;
    }
    
    char old_name[256];
    strcpy(old_name, client->name);
    
    // Update name
    strncpy(client->name, data->request_content, 255);
    client->name[255] = '\0';
    
    pthread_rwlock_unlock(&client_list_lock);
    
    snprintf(response, BUFFER_SIZE, "Your name has been changed to '%s'\n", 
             data->request_content);
    udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
    
    printf("[RENAME] %s changed name to %s\n", old_name, data->request_content);
    
    free(data);
    return NULL;
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
            
            request_t req_type = parse_request_type(client_request);
            char *content = request_content(client_request);

            printf("request recieved \n");
            
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
                    pthread_create(&thread, NULL, connect_h, args);
                    pthread_detach(thread);
                    break;
                case SAY:
                    pthread_create(&thread, NULL, say, args);
                    pthread_detach(thread);
                    break;
                case SAYTO:
                    pthread_create(&thread, NULL, sayto, args);
                    pthread_detach(thread);
                    break;
                case MUTE:
                    pthread_create(&thread, NULL, mute, args);
                    pthread_detach(thread);
                    break;
                case UNMUTE:
                    pthread_create(&thread, NULL, unmute, args);
                    pthread_detach(thread);
                    break;
                case RENAME:
                    pthread_create(&thread, NULL, rename_h, args);
                    pthread_detach(thread);
                    break;
                case DISCONNECT:
                    pthread_create(&thread, NULL, disconnect, args);
                    pthread_detach(thread);
                    break;
                case KICK:
                    pthread_create(&thread, NULL, kick, args);
                    pthread_detach(thread);
                    break;
                default:
                    printf("Unknown request \n");
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