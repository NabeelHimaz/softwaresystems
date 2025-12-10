#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>  
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "udp.h"


#define HISTORY_SIZE 15

typedef struct {
    char messages[HISTORY_SIZE][BUFFER_SIZE];
    int start;   // Index of oldest message
    int count;   // Number of messages in buffer
    pthread_mutex_t lock;
} message_history_t;

message_history_t message_history = {
    .start = 0,
    .count = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER
};


#define INACTIVITY_THRESHOLD 300  //5mins
#define PING_CHECK_INTERVAL 60    //Check every 60secs
pthread_t ping_monitor_thread;
int server_active = 1;

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
    int awaiting_ping_response; 
    struct client_node *next;
} client_node_t;

typedef struct mute_node {
    struct sockaddr_in muter;
    struct sockaddr_in mutee;
    struct mute_node *next;
} mute_node_t;

client_node_t *client_list_head = NULL;
pthread_rwlock_t client_list_lock = PTHREAD_RWLOCK_INITIALIZER;

mute_node_t *mute_list_head = NULL;
pthread_mutex_t mute_list_lock = PTHREAD_MUTEX_INITIALIZER;

void add_to_history(const char *message) {
    pthread_mutex_lock(&message_history.lock);
    
    int index = (message_history.start + message_history.count) % HISTORY_SIZE;
    strncpy(message_history.messages[index], message, BUFFER_SIZE - 1);
    message_history.messages[index][BUFFER_SIZE - 1] = '\0';
    
    if (message_history.count < HISTORY_SIZE) {
        message_history.count++;
    } else {
        message_history.start = (message_history.start + 1) % HISTORY_SIZE;
    }
    
    pthread_mutex_unlock(&message_history.lock);
}

void send_history_to_client(int sd, struct sockaddr_in *client_addr) {
    pthread_mutex_lock(&message_history.lock);
    
    if (message_history.count > 0) {
        char history_header[BUFFER_SIZE];
        memset(history_header, 0, BUFFER_SIZE);
        snprintf(history_header, BUFFER_SIZE, 
                 "\n=== Last %d messages ===\n", message_history.count);
        udp_socket_write(sd, client_addr, history_header, BUFFER_SIZE);
        
        for (int i = 0; i < message_history.count; i++) {
            int index = (message_history.start + i) % HISTORY_SIZE;
            udp_socket_write(sd, client_addr, message_history.messages[index], BUFFER_SIZE);
            usleep(10000);  //delay
        }
        
        char history_footer[BUFFER_SIZE];
        memset(history_footer, 0, BUFFER_SIZE);
        snprintf(history_footer, BUFFER_SIZE, "=== End of history ===\n\n");
        udp_socket_write(sd, client_addr, history_footer, BUFFER_SIZE);
    }
    
    pthread_mutex_unlock(&message_history.lock);
}

int addr_equal(struct sockaddr_in *a1, struct sockaddr_in *a2){
    return (a1->sin_addr.s_addr == a2->sin_addr.s_addr && a1->sin_port == a2->sin_port);
}

client_node_t* find_client_by_addr(struct sockaddr_in *addr){
    client_node_t *current = client_list_head;
    while(current != NULL){
        if(addr_equal(&current->addr, addr)){
            return current;
        }
        current = current->next;
    }
    return NULL;
}

client_node_t* find_client_by_name(const char *name){
    client_node_t *current = client_list_head;
    while(current != NULL){
        if(strcmp(current->name, name) == 0){
            return current;
        }
        current = current->next;
    }
    return NULL;
}

int is_muted(struct sockaddr_in *muter, struct sockaddr_in *mutee){
    pthread_mutex_lock(&mute_list_lock);
    
    mute_node_t *current = mute_list_head;
    while(current != NULL){
        if(addr_equal(&current->muter, muter) && addr_equal(&current->mutee, mutee)){
            pthread_mutex_unlock(&mute_list_lock);
            return 1;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&mute_list_lock);
    return 0;
}

void add_mute(struct sockaddr_in *muter, struct sockaddr_in *mutee){
    pthread_mutex_lock(&mute_list_lock);
    
    mute_node_t *current = mute_list_head;
    while(current != NULL){
        if(addr_equal(&current->muter, muter) && addr_equal(&current->mutee, mutee)){
            pthread_mutex_unlock(&mute_list_lock);
            return;
        }
        current = current->next;
    }
    
    mute_node_t *new_mute = malloc(sizeof(mute_node_t));
    new_mute->muter = *muter;
    new_mute->mutee = *mutee;
    new_mute->next = mute_list_head;
    mute_list_head = new_mute;
    
    pthread_mutex_unlock(&mute_list_lock);
}

void remove_mute(struct sockaddr_in *muter, struct sockaddr_in *mutee){
    pthread_mutex_lock(&mute_list_lock);
    
    mute_node_t *current = mute_list_head;
    mute_node_t *prev = NULL;
    
    while(current != NULL){
        if(addr_equal(&current->muter, muter) && addr_equal(&current->mutee, mutee)){
            if(prev == NULL){
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

request_t parse_request_type(char *request){
    
    char *dollar = strchr(request, '$');
    
    if(dollar == NULL){
        return UNKNOWN;
    }
  
    int cmd_len = dollar - request;
    
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
    
    char *content = dollar + 1;
    while (*content == ' ') {
        content++;
    }
    
    return content;
}

void *connect_h(void *arg){
    process_data *data = (process_data *)arg;
    char response[BUFFER_SIZE];
    memset(response, 0, BUFFER_SIZE);
    
    pthread_rwlock_wrlock(&client_list_lock);
    
    if(find_client_by_name(data->request_content) != NULL){
        pthread_rwlock_unlock(&client_list_lock);
        snprintf(response, BUFFER_SIZE, "Error: Name '%s' is already taken\n", data->request_content);
        udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
        free(data);
        return NULL;
    }
    
    client_node_t *new_client = malloc(sizeof(client_node_t));
    strncpy(new_client->name, data->request_content, 255);
    new_client->name[255] = '\0';
    new_client->addr = data->client_address;
    new_client->last_active = time(NULL);
    new_client->awaiting_ping_response = 0;  
    new_client->next = client_list_head;
    client_list_head = new_client;
    
    pthread_rwlock_unlock(&client_list_lock);
    
    send_history_to_client(data->sd, &data->client_address);
    
    snprintf(response, BUFFER_SIZE, "Welcome %s! You are now connected to the chat.\n", 
             data->request_content);
    udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
    
    printf("[CONNECT] %s joined from port %d\n", 
           data->request_content, ntohs(data->client_address.sin_port));
    
    free(data);
    return NULL;
}

void *say(void *arg){
    process_data *data = (process_data *)arg;
    char broadcast[BUFFER_SIZE];
    memset(broadcast, 0, BUFFER_SIZE);
    
    pthread_rwlock_rdlock(&client_list_lock);
    
    client_node_t *sender = find_client_by_addr(&data->client_address);
    if (sender == NULL) {
        pthread_rwlock_unlock(&client_list_lock);
        free(data);
        return NULL;
    }
    
    sender->last_active = time(NULL);  // Update activity
    
    snprintf(broadcast, BUFFER_SIZE, "[%s]: %s\n", sender->name, data->request_content);

    add_to_history(broadcast);
    
    client_node_t *current = client_list_head;
    while(current != NULL){
        if(!is_muted(&current->addr, &data->client_address)){
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
    memset(response, 0, BUFFER_SIZE);
    
    pthread_rwlock_rdlock(&client_list_lock);
    
    client_node_t *sender = find_client_by_addr(&data->client_address);
    if (sender == NULL) {
        pthread_rwlock_unlock(&client_list_lock);
        free(data);
        return NULL;
    }
    
    sender->last_active = time(NULL);
    
    char *space = strchr(data->request_content, ' ');
    if (space == NULL) {
        pthread_rwlock_unlock(&client_list_lock);
        snprintf(response, BUFFER_SIZE, "Error: Invalid format. Use: sayto$ <n> <message>\n");
        udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
        free(data);
        return NULL;
    }
    
    char recipient_name[256];
    int name_len = space - data->request_content;
    strncpy(recipient_name, data->request_content, name_len);
    recipient_name[name_len] = '\0';
    
    char *message = space + 1;
    
    client_node_t *recipient = find_client_by_name(recipient_name);
    if (recipient == NULL) {
        pthread_rwlock_unlock(&client_list_lock);
        snprintf(response, BUFFER_SIZE, "Error: User '%s' not found\n", recipient_name);
        udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
        free(data);
        return NULL;
    }
    
    if (is_muted(&recipient->addr, &data->client_address)) {
        pthread_rwlock_unlock(&client_list_lock);
        snprintf(response, BUFFER_SIZE, "Private message sent to %s\n", recipient_name);
        udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
        free(data);
        return NULL;
    }
    
    snprintf(response, BUFFER_SIZE, "[Private from %s]: %s\n", sender->name, message);
    udp_socket_write(data->sd, &recipient->addr, response, BUFFER_SIZE);
    
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
    memset(response, 0, BUFFER_SIZE);
    
    pthread_rwlock_rdlock(&client_list_lock);
    
    client_node_t *muter = find_client_by_addr(&data->client_address);
    if (muter) muter->last_active = time(NULL);
    
    client_node_t *mutee = find_client_by_name(data->request_content);
    
    if (mutee) {
        add_mute(&data->client_address, &mutee->addr);
    }
    
    pthread_rwlock_unlock(&client_list_lock);
    
    snprintf(response, BUFFER_SIZE, "You have muted %s\n", data->request_content);
    udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
    
    printf("[MUTE] %s muted %s\n", muter ? muter->name : "Unknown", data->request_content);
    
    free(data);
    return NULL;
}

void *unmute(void *arg){
    process_data *data = (process_data *)arg;
    char response[BUFFER_SIZE];
    memset(response, 0, BUFFER_SIZE);
    
    pthread_rwlock_rdlock(&client_list_lock);
    
    client_node_t *unmuter = find_client_by_addr(&data->client_address);
    if (unmuter) unmuter->last_active = time(NULL);
    
    client_node_t *unmutee = find_client_by_name(data->request_content);
    
    if (unmutee) {
        remove_mute(&data->client_address, &unmutee->addr);
    }
    
    pthread_rwlock_unlock(&client_list_lock);
    
    snprintf(response, BUFFER_SIZE, "You have unmuted %s\n", data->request_content);
    udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
    
    printf("[UNMUTE] %s unmuted %s\n", unmuter ? unmuter->name : "Unknown", data->request_content);
    
    free(data);
    return NULL;
}

void *rename_h(void *arg){
    process_data *data = (process_data *)arg;
    char response[BUFFER_SIZE];
    memset(response, 0, BUFFER_SIZE);
    
    pthread_rwlock_wrlock(&client_list_lock);
    
    client_node_t *client = find_client_by_addr(&data->client_address);
    if (client == NULL) {
        pthread_rwlock_unlock(&client_list_lock);
        snprintf(response, BUFFER_SIZE, "Error: You are not connected\n");
        udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
        free(data);
        return NULL;
    }
    
    client->last_active = time(NULL);
    
    if (find_client_by_name(data->request_content) != NULL) {
        pthread_rwlock_unlock(&client_list_lock);
        snprintf(response, BUFFER_SIZE, "Error: Name '%s' is already taken\n", data->request_content);
        udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
        free(data);
        return NULL;
    }
    
    char old_name[256];
    strcpy(old_name, client->name);
    strncpy(client->name, data->request_content, 255);
    client->name[255] = '\0';
    
    pthread_rwlock_unlock(&client_list_lock);
    
    snprintf(response, BUFFER_SIZE, "Your name has been changed to '%s'\n", data->request_content);
    udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
    
    printf("[RENAME] %s changed name to %s\n", old_name, data->request_content);
    
    free(data);
    return NULL;
}

void *disconnect(void *arg){
    process_data *data = (process_data *)arg;
    char response[BUFFER_SIZE];
    memset(response, 0, BUFFER_SIZE);
    
    pthread_rwlock_wrlock(&client_list_lock);
    
    client_node_t *current = client_list_head;
    client_node_t *prev = NULL;
    
    while (current != NULL) {
        if (addr_equal(&current->addr, &data->client_address)) {
            if (prev == NULL) {
                client_list_head = current->next;
            } else {
                prev->next = current->next;
            }
            
            snprintf(response, BUFFER_SIZE, "Goodbye %s!\n", current->name);
            udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
            
            printf("[DISCONNECT] %s left\n", current->name);
            free(current);
            pthread_rwlock_unlock(&client_list_lock);
            free(data);
            return NULL;
        }
        prev = current;
        current = current->next;
    }
    
    pthread_rwlock_unlock(&client_list_lock);
    free(data);
    return NULL;
}

void *kick(void *arg){
    process_data *data = (process_data *)arg;
    char response[BUFFER_SIZE];
    memset(response, 0, BUFFER_SIZE);
    
    if (ntohs(data->client_address.sin_port) != 6666) {
        snprintf(response, BUFFER_SIZE, "Error: Only admin can kick\n");
        udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
        free(data);
        return NULL;
    }
    
    pthread_rwlock_wrlock(&client_list_lock);
    
    client_node_t *current = client_list_head;
    client_node_t *prev = NULL;
    
    while (current != NULL) {
        if (strcmp(current->name, data->request_content) == 0) {
            if (prev == NULL) {
                client_list_head = current->next;
            } else {
                prev->next = current->next;
            }
            
            snprintf(response, BUFFER_SIZE, "You have been kicked from the chat\n");
            udp_socket_write(data->sd, &current->addr, response, BUFFER_SIZE);
            
            printf("[KICK] Admin kicked %s\n", current->name);
            free(current);
            pthread_rwlock_unlock(&client_list_lock);
            
            snprintf(response, BUFFER_SIZE, "Kicked %s\n", data->request_content);
            udp_socket_write(data->sd, &data->client_address, response, BUFFER_SIZE);
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

// PE2: Handle ret-ping response
void *handle_ret_ping(void *arg){
    process_data *data = (process_data *)arg;
    
    pthread_rwlock_wrlock(&client_list_lock);
    
    client_node_t *client = find_client_by_addr(&data->client_address);
    if (client != NULL) {
        client->awaiting_ping_response = 0;
        client->last_active = time(NULL);
        printf("[PING] %s responded to ping\n", client->name);
    }
    
    pthread_rwlock_unlock(&client_list_lock);
    
    free(data);
    return NULL;
}

// PE2: Ping monitoring thread
void *ping_monitor(void *arg) {
    int sd = *(int *)arg;
    
    printf("[PING MONITOR] Started\n");
    
    while (server_active) {
        sleep(PING_CHECK_INTERVAL);
        
        if (!server_active) break;
        
        pthread_rwlock_wrlock(&client_list_lock);
        
        time_t now = time(NULL);
        client_node_t *current = client_list_head;
        client_node_t *prev = NULL;
        
        while (current != NULL) {
            client_node_t *next = current->next;
            
            time_t inactive_time = now - current->last_active;
            
            if (current->awaiting_ping_response) {
                printf("[PING MONITOR] %s didn't respond to ping - removing\n", current->name);
                
                if (prev == NULL) {
                    client_list_head = next;
                } else {
                    prev->next = next;
                }
                
                free(current);
                current = next;
                continue;
            }
            
            if (inactive_time > INACTIVITY_THRESHOLD) {
                // Send ping
                printf("[PING MONITOR] %s inactive for %ld seconds - sending ping\n", 
                       current->name, inactive_time);
                
                char ping_msg[BUFFER_SIZE];
                memset(ping_msg, 0, BUFFER_SIZE);
                snprintf(ping_msg, BUFFER_SIZE, "ping$");
                udp_socket_write(sd, &current->addr, ping_msg, BUFFER_SIZE);
                
                current->awaiting_ping_response = 1;
            }
            
            prev = current;
            current = next;
        }
        
        pthread_rwlock_unlock(&client_list_lock);
    }
    
    printf("[PING MONITOR] Stopped\n");
    return NULL;
}

int main(int argc, char *argv[])
{
    int sd = udp_socket_open(SERVER_PORT);
    assert(sd > -1);
    
    pthread_create(&ping_monitor_thread, NULL, ping_monitor, &sd);
    
    while (1) 
    {
        char client_request[BUFFER_SIZE];
        struct sockaddr_in client_address;
        
        int rc = udp_socket_read(sd, &client_address, client_request, BUFFER_SIZE);

        if (rc > 0)
        {
            request_t req_type = parse_request_type(client_request);
            char *content = request_content(client_request);

            process_data *args = malloc(sizeof(process_data));
            args->sd = sd;
            args->client_address = client_address;
            args->request_type = req_type;

            if (content != NULL) {
                strncpy(args->request_content, content, BUFFER_SIZE - 1);
                args->request_content[BUFFER_SIZE - 1] = '\0';
            } else {
                args->request_content[0] = '\0';
            }

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
                case RET_PING:
                    pthread_create(&thread, NULL, handle_ret_ping, args);
                    pthread_detach(thread);
                    break;
                default:
                    printf("Unknown request type from port %d\n", ntohs(client_address.sin_port));
                    free(args);
                    break;
            }
        }
    }

    return 0;
}